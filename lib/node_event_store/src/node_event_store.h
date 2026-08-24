#pragma once

#include <stddef.h>
#include <stdint.h>

#include "event_identity.h"
#include "event_protocol.h"
#include "event_records.h"
#include "event_store.h"

namespace NodeEventStore {

constexpr size_t NODE_EVENT_CAPACITY = 8;

enum class Status : uint8_t {
    READY,
    INVALID_SOURCE_DEVICE_ID,
    STORAGE_UNAVAILABLE,
    STORAGE_FAILURE,
    READ_BACK_MISSING,
    READ_BACK_MISMATCH,
    READ_BACK_INVALID,
    INDETERMINATE_SLOT,
    RECORD_CONFLICT,
    GENERATION_AMBIGUOUS,
    DUPLICATE_IDENTITY,
    IDENTITY_FAILURE
};

enum class EnqueueStatus : uint8_t {
    ENQUEUED,
    QUEUE_FULL,
    INVALID_EVENT,
    STORAGE_FAILURE,
    IDENTITY_FAILURE,
    DEGRADED
};

enum class MutationStatus : uint8_t {
    OK,
    INVALID_SLOT,
    INVALID_TRANSITION,
    STORAGE_FAILURE,
    DEGRADED
};

enum class AttemptStatus : uint8_t {
    ARMED,
    EXHAUSTED,
    INVALID_SLOT,
    NOT_QUEUED,
    STORAGE_FAILURE,
    DEGRADED
};

struct EventInput {
    uint8_t family;
    uint8_t flags;
    uint32_t lifetimeBudgetSeconds;
    uint8_t bodyLength;
    uint8_t body[EventProtocol::MAX_BODY_SIZE];
};

struct EnqueueResult {
    EnqueueStatus status;
    EventProtocol::Identity identity;
    uint8_t slot;
};

class Storage : public EventIdentity::MetadataStorage {
public:
    virtual EventIdentity::StorageResult readEventCopy(
        uint8_t logicalSlot,
        EventStorage::CopySlot copy,
        uint8_t* output,
        size_t capacity,
        size_t& length
    ) = 0;

    virtual EventIdentity::StorageResult writeEventCopy(
        uint8_t logicalSlot,
        EventStorage::CopySlot copy,
        const uint8_t* input,
        size_t length
    ) = 0;
};

class Store {
public:
    Status recover(
        Storage& storage,
        EventIdentity::EntropySource& entropy,
        uint8_t configuredSourceDeviceId
    ) {
        reset();
        storage_ = &storage;
        sourceDeviceId_ = configuredSourceDeviceId;
        if (configuredSourceDeviceId == 0) {
            return fail(Status::INVALID_SOURCE_DEVICE_ID);
        }

        bool allCopiesMissing = true;
        bool anyNonempty = false;
        for (uint8_t slot = 0; slot < NODE_EVENT_CAPACITY; ++slot) {
            EventStorage::FixedCopy<EventRecords::NODE_RECORD_SIZE> copyA = {};
            EventStorage::FixedCopy<EventRecords::NODE_RECORD_SIZE> copyB = {};
            if (!readRecordCopy(slot, EventStorage::CopySlot::A, copyA) ||
                !readRecordCopy(slot, EventStorage::CopySlot::B, copyB)) {
                return status_;
            }
            if (copyA.status != EventStorage::FixedReadStatus::MISSING ||
                copyB.status != EventStorage::FixedReadStatus::MISSING) {
                allCopiesMissing = false;
            }

            EventRecords::NodeRecord selected = {};
            EventStorage::CopySlot selectedCopy = EventStorage::CopySlot::A;
            const EventStorage::SelectionResult selection = EventStorage::selectCopies(
                copyA, copyB, EventRecords::decodeNodeRecordForSelection,
                selected, selectedCopy);
            if (!acceptSelection(selection, slot)) return status_;
            if (selection == EventStorage::SelectionResult::BOTH_INVALID) {
                slots_[slot].present = false;
                continue;
            }
            slots_[slot].present = true;
            slots_[slot].record = selected;
            slots_[slot].activeCopy = selectedCopy;
            if (selected.state != EventRecords::NodeState::FREE) anyNonempty = true;
        }

        if (allCopiesMissing) {
            for (uint8_t slot = 0; slot < NODE_EVENT_CAPACITY; ++slot) {
                EventRecords::NodeRecord freeRecord = {};
                freeRecord.generation = 1;
                freeRecord.state = EventRecords::NodeState::FREE;
                if (!commitInitialFree(slot, freeRecord)) return status_;
            }
        } else {
            for (uint8_t slot = 0; slot < NODE_EVENT_CAPACITY; ++slot) {
                if (!slots_[slot].present) return fail(Status::INDETERMINATE_SLOT);
            }
        }

        const EventIdentity::CustodyState custody = anyNonempty
            ? EventIdentity::CustodyState::NONEMPTY
            : EventIdentity::CustodyState::EMPTY;
        const EventIdentity::Status identityStatus = allocator_.recover(
            storage, entropy, configuredSourceDeviceId, custody);
        if (identityStatus != EventIdentity::Status::READY) {
            identityStatus_ = identityStatus;
            return fail(Status::IDENTITY_FAILURE);
        }
        if (!rebuildIndex()) return status_;
        healthy_ = true;
        status_ = Status::READY;
        return status_;
    }

    EnqueueResult enqueue(const EventInput& input) {
        EnqueueResult result = {};
        result.status = EnqueueStatus::DEGRADED;
        result.slot = 0xFF;
        if (!healthy_) return result;
        if (!validInput(input)) {
            result.status = EnqueueStatus::INVALID_EVENT;
            return result;
        }
        uint8_t freeSlot = 0xFF;
        for (uint8_t slot = 0; slot < NODE_EVENT_CAPACITY; ++slot) {
            if (slots_[slot].record.state == EventRecords::NodeState::FREE) {
                freeSlot = slot;
                break;
            }
        }
        if (freeSlot == 0xFF) {
            result.status = EnqueueStatus::QUEUE_FULL;
            return result;
        }

        const EventIdentity::AllocationResult allocation = allocator_.allocate();
        if (allocation.status != EventIdentity::Status::READY) {
            identityStatus_ = allocation.status;
            healthy_ = false;
            status_ = Status::IDENTITY_FAILURE;
            result.status = EnqueueStatus::IDENTITY_FAILURE;
            return result;
        }

        EventRecords::NodeRecord record = {};
        record.generation = slots_[freeSlot].record.generation + 1U;
        record.state = EventRecords::NodeState::QUEUED;
        record.flags = input.flags;
        record.family = input.family;
        record.bodyLength = input.bodyLength;
        record.eventEpoch = allocation.identity.epoch;
        record.eventId = allocation.identity.id;
        record.lifetimeBudgetSeconds = input.lifetimeBudgetSeconds;
        record.remainingActiveSeconds = input.lifetimeBudgetSeconds;
        record.attemptsUsed = 0;
        for (uint8_t i = 0; i < input.bodyLength; ++i) record.body[i] = input.body[i];

        if (!commitMutation(freeSlot, record)) {
            result.status = EnqueueStatus::STORAGE_FAILURE;
            return result;
        }
        if (!rebuildIndex()) {
            result.status = EnqueueStatus::STORAGE_FAILURE;
            return result;
        }
        result.status = EnqueueStatus::ENQUEUED;
        result.identity = allocation.identity;
        result.slot = freeSlot;
        return result;
    }

    MutationStatus markFailed(uint8_t slot) {
        return transition(slot, EventRecords::NodeState::QUEUED,
            EventRecords::NodeState::FAILED);
    }
    MutationStatus markExpired(uint8_t slot) {
        return transition(slot, EventRecords::NodeState::QUEUED,
            EventRecords::NodeState::EXPIRED);
    }
    MutationStatus reclaim(uint8_t slot) {
        if (!healthy_) return MutationStatus::DEGRADED;
        if (slot >= NODE_EVENT_CAPACITY) return MutationStatus::INVALID_SLOT;
        const EventRecords::NodeState state = slots_[slot].record.state;
        if (state != EventRecords::NodeState::FAILED &&
            state != EventRecords::NodeState::EXPIRED) {
            return MutationStatus::INVALID_TRANSITION;
        }
        return makeFree(slot);
    }
    MutationStatus releaseQueued(uint8_t slot) {
        if (!healthy_) return MutationStatus::DEGRADED;
        if (slot >= NODE_EVENT_CAPACITY) return MutationStatus::INVALID_SLOT;
        if (slots_[slot].record.state != EventRecords::NodeState::QUEUED) {
            return MutationStatus::INVALID_TRANSITION;
        }
        return makeFree(slot);
    }

    MutationStatus checkpointRemaining(
        uint8_t slot,
        uint32_t remainingActiveSeconds
    ) {
        if (!healthy_) return MutationStatus::DEGRADED;
        if (slot >= NODE_EVENT_CAPACITY) return MutationStatus::INVALID_SLOT;
        const EventRecords::NodeRecord& current = slots_[slot].record;
        if (current.state != EventRecords::NodeState::QUEUED ||
            remainingActiveSeconds == 0 ||
            remainingActiveSeconds >= current.remainingActiveSeconds) {
            return MutationStatus::INVALID_TRANSITION;
        }
        EventRecords::NodeRecord next = current;
        next.generation += 1U;
        next.remainingActiveSeconds = remainingActiveSeconds;
        if (!commitMutation(slot, next)) return MutationStatus::STORAGE_FAILURE;
        return MutationStatus::OK;
    }

    AttemptStatus armAttempt(uint8_t slot) {
        if (!healthy_) return AttemptStatus::DEGRADED;
        if (slot >= NODE_EVENT_CAPACITY) return AttemptStatus::INVALID_SLOT;
        const EventRecords::NodeRecord& current = slots_[slot].record;
        if (current.state != EventRecords::NodeState::QUEUED) {
            return AttemptStatus::NOT_QUEUED;
        }
        if (current.attemptsUsed >= EventRecords::MAX_ATTEMPTS) {
            return AttemptStatus::EXHAUSTED;
        }
        EventRecords::NodeRecord next = current;
        next.generation += 1U;
        next.attemptsUsed += 1U;
        if (!commitMutation(slot, next)) return AttemptStatus::STORAGE_FAILURE;
        return AttemptStatus::ARMED;
    }

    bool healthy() const { return healthy_; }
    Status status() const { return status_; }
    EventIdentity::Status identityStatus() const { return identityStatus_; }
    size_t queuedCount() const { return queueCount_; }
    size_t ownedCount() const {
        size_t count = 0;
        for (size_t i = 0; i < NODE_EVENT_CAPACITY; ++i) {
            if (slots_[i].present && slots_[i].record.state != EventRecords::NodeState::FREE) ++count;
        }
        return count;
    }
    const EventRecords::NodeRecord* queuedAt(size_t index, uint8_t* slot = nullptr) const {
        if (index >= queueCount_) return nullptr;
        if (slot != nullptr) *slot = queue_[index];
        return &slots_[queue_[index]].record;
    }
    const EventRecords::NodeRecord* recordAt(uint8_t slot) const {
        return slot < NODE_EVENT_CAPACITY && slots_[slot].present
            ? &slots_[slot].record : nullptr;
    }

private:
    struct SlotState {
        bool present;
        EventRecords::NodeRecord record;
        EventStorage::CopySlot activeCopy;
    };

    bool readRecordCopy(
        uint8_t slot,
        EventStorage::CopySlot copySlot,
        EventStorage::FixedCopy<EventRecords::NODE_RECORD_SIZE>& copy
    ) {
        size_t length = 0;
        const EventIdentity::StorageResult result = storage_->readEventCopy(
            slot, copySlot, copy.bytes, sizeof(copy.bytes), length);
        if (result == EventIdentity::StorageResult::UNAVAILABLE) {
            copy.status = EventStorage::FixedReadStatus::UNAVAILABLE;
            fail(Status::STORAGE_UNAVAILABLE);
            return false;
        }
        if (result == EventIdentity::StorageResult::MISSING) {
            copy.status = EventStorage::FixedReadStatus::MISSING;
            return true;
        }
        if (result != EventIdentity::StorageResult::OK ||
            length != EventRecords::NODE_RECORD_SIZE) {
            copy.status = EventStorage::FixedReadStatus::OK;
            for (size_t i = 0; i < sizeof(copy.bytes); ++i) copy.bytes[i] = 0;
            return true;
        }
        copy.status = EventStorage::FixedReadStatus::OK;
        return true;
    }

    bool acceptSelection(EventStorage::SelectionResult selection, uint8_t) {
        if (selection == EventStorage::SelectionResult::EQUAL_DISAGREEMENT) {
            fail(Status::RECORD_CONFLICT);
            return false;
        }
        if (selection == EventStorage::SelectionResult::GENERATION_AMBIGUOUS) {
            fail(Status::GENERATION_AMBIGUOUS);
            return false;
        }
        return true;
    }

    bool commitInitialFree(uint8_t slot, const EventRecords::NodeRecord& record) {
        uint8_t encoded[EventRecords::NODE_RECORD_SIZE] = {};
        if (EventRecords::encodeNodeRecord(record, encoded, sizeof(encoded)) !=
            EventRecords::CodecResult::OK) return failBool(Status::STORAGE_FAILURE);
        if (!writeVerify(slot, EventStorage::CopySlot::A, encoded)) return false;
        slots_[slot].present = true;
        slots_[slot].record = record;
        slots_[slot].activeCopy = EventStorage::CopySlot::A;
        return true;
    }

    bool commitMutation(uint8_t slot, const EventRecords::NodeRecord& record) {
        uint8_t encoded[EventRecords::NODE_RECORD_SIZE] = {};
        if (EventRecords::encodeNodeRecord(record, encoded, sizeof(encoded)) !=
            EventRecords::CodecResult::OK) return failBool(Status::STORAGE_FAILURE);
        const EventStorage::CopySlot target = opposite(slots_[slot].activeCopy);
        if (!writeVerify(slot, target, encoded)) return false;

        EventStorage::FixedCopy<EventRecords::NODE_RECORD_SIZE> oldCopy = {};
        EventStorage::FixedCopy<EventRecords::NODE_RECORD_SIZE> newCopy = {};
        if (!readRecordCopy(slot, slots_[slot].activeCopy, oldCopy) ||
            !readRecordCopy(slot, target, newCopy)) return false;
        EventRecords::NodeRecord selected = {};
        EventStorage::CopySlot selectedSlot = EventStorage::CopySlot::A;
        const EventStorage::SelectionResult selection = EventStorage::selectCopies(
            slots_[slot].activeCopy == EventStorage::CopySlot::A ? oldCopy : newCopy,
            slots_[slot].activeCopy == EventStorage::CopySlot::A ? newCopy : oldCopy,
            EventRecords::decodeNodeRecordForSelection, selected, selectedSlot);
        if (!acceptSelection(selection, slot) || selectedSlot != target ||
            selected.generation != record.generation) {
            return failBool(Status::STORAGE_FAILURE);
        }
        slots_[slot].record = record;
        slots_[slot].activeCopy = target;
        slots_[slot].present = true;
        return true;
    }

    bool writeVerify(uint8_t slot, EventStorage::CopySlot target, const uint8_t* encoded) {
        EventIdentity::StorageResult result = storage_->writeEventCopy(
            slot, target, encoded, EventRecords::NODE_RECORD_SIZE);
        if (result != EventIdentity::StorageResult::OK) {
            return failBool(result == EventIdentity::StorageResult::UNAVAILABLE
                ? Status::STORAGE_UNAVAILABLE : Status::STORAGE_FAILURE);
        }
        result = storage_->commit();
        if (result != EventIdentity::StorageResult::OK) {
            return failBool(result == EventIdentity::StorageResult::UNAVAILABLE
                ? Status::STORAGE_UNAVAILABLE : Status::STORAGE_FAILURE);
        }
        EventStorage::FixedCopy<EventRecords::NODE_RECORD_SIZE> readBack = {};
        size_t length = 0;
        result = storage_->readEventCopy(
            slot, target, readBack.bytes, sizeof(readBack.bytes), length);
        if (result == EventIdentity::StorageResult::MISSING) return failBool(Status::READ_BACK_MISSING);
        if (result == EventIdentity::StorageResult::UNAVAILABLE) return failBool(Status::STORAGE_UNAVAILABLE);
        if (result != EventIdentity::StorageResult::OK || length != sizeof(readBack.bytes)) {
            return failBool(Status::READ_BACK_INVALID);
        }
        readBack.status = EventStorage::FixedReadStatus::OK;
        EventRecords::NodeRecord decoded = {};
        if (EventRecords::decodeNodeRecord(readBack.bytes, sizeof(readBack.bytes), decoded) !=
            EventRecords::CodecResult::OK) return failBool(Status::READ_BACK_INVALID);
        if (!EventStorage::bytesEqual(
                encoded, readBack.bytes, EventRecords::NODE_RECORD_SIZE)) {
            return failBool(Status::READ_BACK_MISMATCH);
        }
        return true;
    }

    MutationStatus transition(uint8_t slot, EventRecords::NodeState from,
                              EventRecords::NodeState to) {
        if (!healthy_) return MutationStatus::DEGRADED;
        if (slot >= NODE_EVENT_CAPACITY) return MutationStatus::INVALID_SLOT;
        if (slots_[slot].record.state != from) return MutationStatus::INVALID_TRANSITION;
        EventRecords::NodeRecord next = slots_[slot].record;
        next.generation += 1U;
        next.state = to;
        if (!commitMutation(slot, next)) return MutationStatus::STORAGE_FAILURE;
        if (!rebuildIndex()) return MutationStatus::STORAGE_FAILURE;
        return MutationStatus::OK;
    }

    MutationStatus makeFree(uint8_t slot) {
        EventRecords::NodeRecord next = {};
        next.generation = slots_[slot].record.generation + 1U;
        next.state = EventRecords::NodeState::FREE;
        if (!commitMutation(slot, next)) return MutationStatus::STORAGE_FAILURE;
        if (!rebuildIndex()) return MutationStatus::STORAGE_FAILURE;
        return MutationStatus::OK;
    }

    bool validInput(const EventInput& input) const {
        if ((input.flags & static_cast<uint8_t>(~EventProtocol::ALLOWED_FLAGS)) != 0 ||
            input.lifetimeBudgetSeconds < EventProtocol::MIN_LIFETIME_SECONDS ||
            input.lifetimeBudgetSeconds > EventProtocol::MAX_LIFETIME_SECONDS ||
            input.bodyLength > EventProtocol::MAX_BODY_SIZE ||
            !EventProtocol::isRegisteredFamily(input.family) ||
            !EventProtocol::isValidFamilyBody(input.family, input.body, input.bodyLength)) {
            return false;
        }
        for (size_t i = input.bodyLength; i < EventProtocol::MAX_BODY_SIZE; ++i) {
            if (input.body[i] != 0) return false;
        }
        return true;
    }

    bool rebuildIndex() {
        queueCount_ = 0;
        for (uint8_t slot = 0; slot < NODE_EVENT_CAPACITY; ++slot) {
            if (!slots_[slot].present || slots_[slot].record.state == EventRecords::NodeState::FREE) continue;
            for (uint8_t other = 0; other < slot; ++other) {
                if (!slots_[other].present || slots_[other].record.state == EventRecords::NodeState::FREE) continue;
                if (slots_[other].record.eventEpoch == slots_[slot].record.eventEpoch &&
                    slots_[other].record.eventId == slots_[slot].record.eventId) {
                    return failBool(Status::DUPLICATE_IDENTITY);
                }
            }
            if (slots_[slot].record.state == EventRecords::NodeState::QUEUED) queue_[queueCount_++] = slot;
        }
        for (size_t i = 1; i < queueCount_; ++i) {
            const uint8_t value = queue_[i];
            size_t position = i;
            while (position > 0 && identityLess(slots_[value].record,
                                                slots_[queue_[position - 1]].record)) {
                queue_[position] = queue_[position - 1];
                --position;
            }
            queue_[position] = value;
        }
        return true;
    }

    static bool identityLess(const EventRecords::NodeRecord& left,
                             const EventRecords::NodeRecord& right) {
        return left.eventEpoch < right.eventEpoch ||
            (left.eventEpoch == right.eventEpoch && left.eventId < right.eventId);
    }
    static EventStorage::CopySlot opposite(EventStorage::CopySlot copy) {
        return copy == EventStorage::CopySlot::A
            ? EventStorage::CopySlot::B : EventStorage::CopySlot::A;
    }
    Status fail(Status status) { healthy_ = false; status_ = status; return status; }
    bool failBool(Status status) { fail(status); return false; }
    void reset() {
        storage_ = nullptr;
        allocator_ = EventIdentity::Allocator();
        for (size_t i = 0; i < NODE_EVENT_CAPACITY; ++i) slots_[i] = {};
        for (size_t i = 0; i < NODE_EVENT_CAPACITY; ++i) queue_[i] = 0;
        sourceDeviceId_ = 0;
        queueCount_ = 0;
        healthy_ = false;
        status_ = Status::INDETERMINATE_SLOT;
        identityStatus_ = EventIdentity::Status::IDENTITY_RECOVERY_REQUIRED;
    }

    Storage* storage_ = nullptr;
    EventIdentity::Allocator allocator_;
    SlotState slots_[NODE_EVENT_CAPACITY] = {};
    uint8_t queue_[NODE_EVENT_CAPACITY] = {};
    uint8_t sourceDeviceId_ = 0;
    size_t queueCount_ = 0;
    bool healthy_ = false;
    Status status_ = Status::INDETERMINATE_SLOT;
    EventIdentity::Status identityStatus_ = EventIdentity::Status::IDENTITY_RECOVERY_REQUIRED;
};

}  // namespace NodeEventStore
