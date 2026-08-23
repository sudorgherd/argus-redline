#pragma once

#include <stddef.h>
#include <stdint.h>

#include "event_protocol.h"
#include "event_records.h"
#include "event_store.h"

namespace EventIdentity {

constexpr uint32_t RESERVATION_BLOCK_SIZE = 32U;

enum class CustodyState : uint8_t {
    EMPTY,
    NONEMPTY,
    UNPROVABLE
};

enum class StorageResult : uint8_t {
    OK,
    MISSING,
    UNAVAILABLE,
    ERROR
};

enum class Status : uint8_t {
    READY,
    INVALID_SOURCE_DEVICE_ID,
    STORAGE_UNAVAILABLE,
    STORAGE_FAILURE,
    READ_BACK_MISSING,
    READ_BACK_MISMATCH,
    READ_BACK_INVALID,
    ENTROPY_FAILURE,
    IDENTITY_CONTEXT_MISMATCH,
    IDENTITY_RECOVERY_REQUIRED,
    METADATA_CONFLICT,
    GENERATION_AMBIGUOUS,
    EPOCH_EXHAUSTED
};

struct AllocationResult {
    Status status;
    EventProtocol::Identity identity;
};

class MetadataStorage {
public:
    virtual ~MetadataStorage() {}

    virtual StorageResult read(
        EventStorage::CopySlot slot,
        uint8_t* output,
        size_t capacity,
        size_t& length
    ) = 0;

    virtual StorageResult write(
        EventStorage::CopySlot slot,
        const uint8_t* input,
        size_t length
    ) = 0;

    virtual StorageResult commit() = 0;
};

class EntropySource {
public:
    virtual ~EntropySource() {}
    virtual bool nextUint32(uint32_t& value) = 0;
};

class Allocator {
public:
    Status recover(
        MetadataStorage& storage,
        EntropySource& entropy,
        uint8_t configuredSourceDeviceId,
        CustodyState custodyState
    ) {
        reset();
        storage_ = &storage;
        entropy_ = &entropy;
        sourceDeviceId_ = configuredSourceDeviceId;

        if (configuredSourceDeviceId == 0) {
            return fail(Status::INVALID_SOURCE_DEVICE_ID);
        }
        if (custodyState == CustodyState::UNPROVABLE) {
            return fail(Status::IDENTITY_RECOVERY_REQUIRED);
        }

        EventStorage::FixedCopy<EventRecords::NODE_METADATA_SIZE> copyA = {};
        EventStorage::FixedCopy<EventRecords::NODE_METADATA_SIZE> copyB = {};
        const Status readA = readCopy(EventStorage::CopySlot::A, copyA);
        const Status readB = readCopy(EventStorage::CopySlot::B, copyB);
        if (readA == Status::STORAGE_UNAVAILABLE ||
            readB == Status::STORAGE_UNAVAILABLE) {
            return fail(Status::STORAGE_UNAVAILABLE);
        }

        EventRecords::NodeMetadata selected = {};
        EventStorage::CopySlot selectedSlot = EventStorage::CopySlot::A;
        const EventStorage::SelectionResult selection = EventStorage::selectCopies(
            copyA,
            copyB,
            EventRecords::decodeNodeMetadataForSelection,
            selected,
            selectedSlot
        );

        if (selection == EventStorage::SelectionResult::EQUAL_DISAGREEMENT) {
            return fail(Status::METADATA_CONFLICT);
        }
        if (selection == EventStorage::SelectionResult::GENERATION_AMBIGUOUS) {
            return fail(Status::GENERATION_AMBIGUOUS);
        }
        if (selection == EventStorage::SelectionResult::BOTH_INVALID) {
            if (custodyState == CustodyState::NONEMPTY) {
                return fail(Status::IDENTITY_RECOVERY_REQUIRED);
            }
            return initializeFresh();
        }

        metadata_ = selected;
        activeSlot_ = selectedSlot;
        hasAuthoritativeMetadata_ = true;

        if (!validHighWater(selected.nextUnreservedEventId)) {
            if (custodyState == CustodyState::NONEMPTY) {
                return fail(Status::IDENTITY_RECOVERY_REQUIRED);
            }
            return initializeFresh();
        }

        if (custodyState == CustodyState::NONEMPTY) {
            if (selected.custodySourceDeviceId != configuredSourceDeviceId) {
                return fail(Status::IDENTITY_CONTEXT_MISMATCH);
            }
            return becomeReady();
        }

        if (selected.custodySourceDeviceId != configuredSourceDeviceId) {
            return initializeFresh();
        }
        return becomeReady();
    }

    AllocationResult allocate() {
        AllocationResult result = {};
        result.status = status_;
        if (!ready_) {
            return result;
        }

        if (nextVolatileId_ == volatileEndExclusive_) {
            const Status reservation = reserveBlock();
            if (reservation != Status::READY) {
                result.status = reservation;
                return result;
            }
        }

        result.identity.sourceDeviceId = sourceDeviceId_;
        result.identity.epoch = metadata_.eventEpoch;
        result.identity.id = nextVolatileId_;
        ++nextVolatileId_;
        result.status = Status::READY;
        return result;
    }

    bool ready() const { return ready_; }
    Status status() const { return status_; }
    const EventRecords::NodeMetadata& metadata() const { return metadata_; }
    EventStorage::CopySlot activeSlot() const { return activeSlot_; }

private:
    Status readCopy(
        EventStorage::CopySlot slot,
        EventStorage::FixedCopy<EventRecords::NODE_METADATA_SIZE>& copy
    ) {
        size_t length = 0;
        const StorageResult result = storage_->read(
            slot, copy.bytes, sizeof(copy.bytes), length);
        if (result == StorageResult::UNAVAILABLE) {
            copy.status = EventStorage::FixedReadStatus::UNAVAILABLE;
            return Status::STORAGE_UNAVAILABLE;
        }
        if (result != StorageResult::OK ||
            length != EventRecords::NODE_METADATA_SIZE) {
            copy.status = result == StorageResult::MISSING
                ? EventStorage::FixedReadStatus::MISSING
                : EventStorage::FixedReadStatus::OK;
            if (result != StorageResult::MISSING) {
                for (size_t i = 0; i < sizeof(copy.bytes); ++i) {
                    copy.bytes[i] = 0;
                }
            }
            return Status::READY;
        }
        copy.status = EventStorage::FixedReadStatus::OK;
        return Status::READY;
    }

    Status initializeFresh() {
        uint32_t epoch = 0;
        if (!entropy_->nextUint32(epoch) || epoch == 0) {
            return fail(Status::ENTROPY_FAILURE);
        }
        EventRecords::NodeMetadata fresh = {};
        fresh.generation = hasAuthoritativeMetadata_
            ? metadata_.generation + 1U
            : 1U;
        fresh.custodySourceDeviceId = sourceDeviceId_;
        fresh.eventEpoch = epoch;
        fresh.nextUnreservedEventId = 1U;
        return commitMetadata(fresh);
    }

    Status becomeReady() {
        nextVolatileId_ = metadata_.nextUnreservedEventId;
        volatileEndExclusive_ = metadata_.nextUnreservedEventId;
        ready_ = true;
        status_ = Status::READY;
        return status_;
    }

    Status reserveBlock() {
        EventRecords::NodeMetadata next = metadata_;
        next.generation = metadata_.generation + 1U;

        if (metadata_.nextUnreservedEventId >
            UINT32_MAX - RESERVATION_BLOCK_SIZE) {
            if (metadata_.eventEpoch == UINT32_MAX) {
                return fail(Status::EPOCH_EXHAUSTED);
            }
            next.eventEpoch = metadata_.eventEpoch + 1U;
            next.nextUnreservedEventId = 1U + RESERVATION_BLOCK_SIZE;
            const Status committed = commitMetadata(next);
            if (committed != Status::READY) return committed;
            nextVolatileId_ = 1U;
            volatileEndExclusive_ = 1U + RESERVATION_BLOCK_SIZE;
            return Status::READY;
        }

        const uint32_t blockStart = metadata_.nextUnreservedEventId;
        next.nextUnreservedEventId = blockStart + RESERVATION_BLOCK_SIZE;
        const Status committed = commitMetadata(next);
        if (committed != Status::READY) return committed;
        nextVolatileId_ = blockStart;
        volatileEndExclusive_ = blockStart + RESERVATION_BLOCK_SIZE;
        return Status::READY;
    }

    Status commitMetadata(const EventRecords::NodeMetadata& next) {
        uint8_t encoded[EventRecords::NODE_METADATA_SIZE] = {};
        if (EventRecords::encodeNodeMetadata(
                next, encoded, sizeof(encoded)) != EventRecords::CodecResult::OK) {
            return fail(Status::STORAGE_FAILURE);
        }
        const EventStorage::CopySlot target = hasAuthoritativeMetadata_
            ? opposite(activeSlot_)
            : EventStorage::CopySlot::A;
        const StorageResult writeResult = storage_->write(
            target, encoded, sizeof(encoded));
        if (writeResult == StorageResult::UNAVAILABLE) {
            return fail(Status::STORAGE_UNAVAILABLE);
        }
        if (writeResult != StorageResult::OK) {
            return fail(Status::STORAGE_FAILURE);
        }
        const StorageResult commitResult = storage_->commit();
        if (commitResult == StorageResult::UNAVAILABLE) {
            return fail(Status::STORAGE_UNAVAILABLE);
        }
        if (commitResult != StorageResult::OK) {
            return fail(Status::STORAGE_FAILURE);
        }

        EventStorage::FixedCopy<EventRecords::NODE_METADATA_SIZE> readBack = {};
        size_t readBackLength = 0;
        const StorageResult readResult = storage_->read(
            target, readBack.bytes, sizeof(readBack.bytes), readBackLength);
        if (readResult == StorageResult::UNAVAILABLE) {
            return fail(Status::STORAGE_UNAVAILABLE);
        }
        if (readResult == StorageResult::MISSING) {
            return fail(Status::READ_BACK_MISSING);
        }
        if (readResult != StorageResult::OK ||
            readBackLength != EventRecords::NODE_METADATA_SIZE) {
            return fail(Status::READ_BACK_INVALID);
        }
        readBack.status = EventStorage::FixedReadStatus::OK;
        EventRecords::NodeMetadata verified = {};
        if (EventRecords::decodeNodeMetadata(
                readBack.bytes, sizeof(readBack.bytes), verified) !=
                EventRecords::CodecResult::OK) {
            return fail(Status::READ_BACK_INVALID);
        }
        const EventStorage::ReadBackResult classification =
            EventStorage::classifyReadBack(encoded, readBack);
        if (classification != EventStorage::ReadBackResult::MATCH) {
            return fail(Status::READ_BACK_MISMATCH);
        }

        metadata_ = verified;
        activeSlot_ = target;
        hasAuthoritativeMetadata_ = true;
        return becomeReady();
    }

    static EventStorage::CopySlot opposite(EventStorage::CopySlot slot) {
        return slot == EventStorage::CopySlot::A
            ? EventStorage::CopySlot::B
            : EventStorage::CopySlot::A;
    }

    static bool validHighWater(uint32_t value) {
        return value != 0U && ((value - 1U) % RESERVATION_BLOCK_SIZE) == 0U;
    }

    Status fail(Status status) {
        ready_ = false;
        status_ = status;
        return status;
    }

    void reset() {
        storage_ = nullptr;
        entropy_ = nullptr;
        metadata_ = {};
        activeSlot_ = EventStorage::CopySlot::A;
        sourceDeviceId_ = 0;
        nextVolatileId_ = 0;
        volatileEndExclusive_ = 0;
        hasAuthoritativeMetadata_ = false;
        ready_ = false;
        status_ = Status::IDENTITY_RECOVERY_REQUIRED;
    }

    MetadataStorage* storage_ = nullptr;
    EntropySource* entropy_ = nullptr;
    EventRecords::NodeMetadata metadata_ = {};
    EventStorage::CopySlot activeSlot_ = EventStorage::CopySlot::A;
    uint8_t sourceDeviceId_ = 0;
    uint32_t nextVolatileId_ = 0;
    uint32_t volatileEndExclusive_ = 0;
    bool hasAuthoritativeMetadata_ = false;
    bool ready_ = false;
    Status status_ = Status::IDENTITY_RECOVERY_REQUIRED;
};

}  // namespace EventIdentity
