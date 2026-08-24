#pragma once

#include <stddef.h>
#include <stdint.h>

#include "event_protocol.h"
#include "event_records.h"
#include "event_store.h"
#include "event_identity.h"

namespace HubEventLedger {

constexpr size_t HUB_EVENT_CAPACITY = 8;
constexpr uint32_t ORDINAL_RESERVATION_BLOCK_SIZE = 32U;

enum class Status : uint8_t {
    READY,
    INVALID_CONFIGURATION,
    STORAGE_UNAVAILABLE,
    STORAGE_FAILURE,
    INDETERMINATE_SLOT,
    RECORD_CONFLICT,
    GENERATION_AMBIGUOUS,
    DUPLICATE_IDENTITY,
    DUPLICATE_ORDINAL,
    ORDINAL_UNAVAILABLE,
    ORDINAL_EXHAUSTED
};

enum class AdmissionStatus : uint8_t {
    DURABLE_NEW_ADMISSION,
    EXACT_DUPLICATE,
    CAPACITY,
    IDENTITY_CONTENT_MISMATCH,
    UNSUPPORTED_EVENT,
    MALFORMED_EVENT,
    ROUTING_REJECTED,
    STORAGE_FAILURE,
    DEGRADED
};

enum class ConsumeStatus : uint8_t {
    CONSUMED,
    ALREADY_CONSUMED,
    NOT_FOUND,
    STORAGE_FAILURE,
    DEGRADED
};

struct AdmissionResult {
    AdmissionStatus status;
    uint8_t slot;
    uint32_t admissionOrdinal;
};

struct ConsumeResult {
    ConsumeStatus status;
    uint8_t slot;
};

class Storage {
public:
    virtual ~Storage() {}
    virtual EventIdentity::StorageResult readHubMetadata(
        EventStorage::CopySlot copy, uint8_t* output,
        size_t capacity, size_t& length) = 0;
    virtual EventIdentity::StorageResult writeHubMetadata(
        EventStorage::CopySlot copy, const uint8_t* input, size_t length) = 0;
    virtual EventIdentity::StorageResult readHubEventCopy(
        uint8_t logicalSlot, EventStorage::CopySlot copy, uint8_t* output,
        size_t capacity, size_t& length) = 0;
    virtual EventIdentity::StorageResult writeHubEventCopy(
        uint8_t logicalSlot, EventStorage::CopySlot copy,
        const uint8_t* input, size_t length) = 0;
    virtual EventIdentity::StorageResult commit() = 0;
};

class Ledger {
public:
    Status recover(Storage& storage, uint8_t hubDeviceId, uint8_t nodeDeviceId) {
        reset();
        storage_ = &storage;
        hubDeviceId_ = hubDeviceId;
        nodeDeviceId_ = nodeDeviceId;
        if (hubDeviceId == 0 || nodeDeviceId == 0 || hubDeviceId == nodeDeviceId) {
            return failLedger(Status::INVALID_CONFIGURATION);
        }

        bool allSlotCopiesMissing = true;
        for (uint8_t slot = 0; slot < HUB_EVENT_CAPACITY; ++slot) {
            EventStorage::FixedCopy<EventRecords::HUB_RECORD_SIZE> a = {};
            EventStorage::FixedCopy<EventRecords::HUB_RECORD_SIZE> b = {};
            if (!readRecordCopy(slot, EventStorage::CopySlot::A, a) ||
                !readRecordCopy(slot, EventStorage::CopySlot::B, b)) {
                return status_;
            }
            if (a.status != EventStorage::FixedReadStatus::MISSING ||
                b.status != EventStorage::FixedReadStatus::MISSING) {
                allSlotCopiesMissing = false;
            }
            EventRecords::HubRecord selected = {};
            EventStorage::CopySlot selectedCopy = EventStorage::CopySlot::A;
            const EventStorage::SelectionResult selection = EventStorage::selectCopies(
                a, b, EventRecords::decodeHubRecordForSelection,
                selected, selectedCopy);
            if (!acceptRecordSelection(selection)) return status_;
            if (selection != EventStorage::SelectionResult::BOTH_INVALID) {
                slots_[slot].present = true;
                slots_[slot].record = selected;
                slots_[slot].activeCopy = selectedCopy;
            }
        }

        EventStorage::FixedCopy<EventRecords::HUB_METADATA_SIZE> metaA = {};
        EventStorage::FixedCopy<EventRecords::HUB_METADATA_SIZE> metaB = {};
        if (!readMetadataCopy(EventStorage::CopySlot::A, metaA) ||
            !readMetadataCopy(EventStorage::CopySlot::B, metaB)) {
            // Existing slot custody remains recoverable even when ordinal storage is down.
            if (!allSlotCopiesMissing && validateRecoveredSlots()) {
                ledgerHealthy_ = true;
                status_ = Status::ORDINAL_UNAVAILABLE;
            }
            return status_;
        }
        const bool allMetadataMissing =
            metaA.status == EventStorage::FixedReadStatus::MISSING &&
            metaB.status == EventStorage::FixedReadStatus::MISSING;

        if (allSlotCopiesMissing && allMetadataMissing) {
            if (!initializeFresh()) return status_;
        } else {
            for (uint8_t slot = 0; slot < HUB_EVENT_CAPACITY; ++slot) {
                if (!slots_[slot].present) return failLedger(Status::INDETERMINATE_SLOT);
            }
            if (!validateRecoveredSlots()) return status_;
            recoverMetadata(metaA, metaB);
        }
        ledgerHealthy_ = true;
        if (ordinalReady_) status_ = Status::READY;
        return status_;
    }

    AdmissionResult admit(const EventProtocol::Event& event) {
        AdmissionResult result = {AdmissionStatus::DEGRADED, 0xFF, 0};
        if (!ledgerHealthy_) return result;
        const AdmissionStatus validation = validateAdmission(event);
        if (validation != AdmissionStatus::DURABLE_NEW_ADMISSION) {
            result.status = validation;
            return result;
        }

        for (uint8_t slot = 0; slot < HUB_EVENT_CAPACITY; ++slot) {
            if (!isOwned(slots_[slot].record) || !sameIdentity(slots_[slot].record, event)) continue;
            result.slot = slot;
            result.admissionOrdinal = slots_[slot].record.admissionOrdinal;
            result.status = sameCanonical(slots_[slot].record, event)
                ? AdmissionStatus::EXACT_DUPLICATE
                : AdmissionStatus::IDENTITY_CONTENT_MISMATCH;
            return result;
        }

        uint8_t target = firstEmpty();
        if (target == 0xFF) target = oldestReclaimable(event);
        if (target == 0xFF) {
            result.status = AdmissionStatus::CAPACITY;
            return result;
        }
        if (!ordinalReady_) {
            result.status = AdmissionStatus::STORAGE_FAILURE;
            return result;
        }
        uint32_t ordinal = 0;
        if (!allocateOrdinal(ordinal)) {
            result.status = AdmissionStatus::STORAGE_FAILURE;
            return result;
        }

        EventRecords::HubRecord next = {};
        next.generation = slots_[target].record.generation + 1U;
        next.state = EventRecords::HubState::ACTIVE;
        next.sourceDeviceId = event.source;
        next.family = event.family;
        next.flags = event.flags;
        next.bodyLength = event.bodyLength;
        next.eventEpoch = event.epoch;
        next.eventId = event.id;
        next.lifetimeBudgetSeconds = event.lifetimeBudgetSeconds;
        next.admissionOrdinal = ordinal;
        for (uint8_t i = 0; i < event.bodyLength; ++i) next.body[i] = event.body[i];
        if (!commitRecord(target, next)) {
            result.status = AdmissionStatus::STORAGE_FAILURE;
            return result;
        }
        result.status = AdmissionStatus::DURABLE_NEW_ADMISSION;
        result.slot = target;
        result.admissionOrdinal = ordinal;
        return result;
    }

    ConsumeResult consume(const EventProtocol::Identity& identity) {
        ConsumeResult result = {ConsumeStatus::NOT_FOUND, 0xFF};
        if (!ledgerHealthy_) {
            result.status = ConsumeStatus::DEGRADED;
            return result;
        }
        for (uint8_t slot = 0; slot < HUB_EVENT_CAPACITY; ++slot) {
            EventRecords::HubRecord& record = slots_[slot].record;
            if (!isOwned(record) || record.sourceDeviceId != identity.sourceDeviceId ||
                record.eventEpoch != identity.epoch || record.eventId != identity.id) continue;
            result.slot = slot;
            if (record.state == EventRecords::HubState::CONSUMED) {
                result.status = ConsumeStatus::ALREADY_CONSUMED;
                return result;
            }
            EventRecords::HubRecord next = record;
            next.generation += 1U;
            next.state = EventRecords::HubState::CONSUMED;
            result.status = commitRecord(slot, next)
                ? ConsumeStatus::CONSUMED : ConsumeStatus::STORAGE_FAILURE;
            return result;
        }
        return result;
    }

    bool oldestActive(uint8_t& slot, EventRecords::HubRecord& record) const {
        if (!ledgerHealthy_) return false;
        uint8_t found = 0xFF;
        for (uint8_t i = 0; i < HUB_EVENT_CAPACITY; ++i) {
            if (slots_[i].record.state != EventRecords::HubState::ACTIVE) continue;
            if (found == 0xFF || slots_[i].record.admissionOrdinal <
                slots_[found].record.admissionOrdinal) found = i;
        }
        if (found == 0xFF) return false;
        slot = found;
        record = slots_[found].record;
        return true;
    }

    bool find(const EventProtocol::Identity& identity, uint8_t& slot,
              EventRecords::HubRecord& record) const {
        if (!ledgerHealthy_) return false;
        for (uint8_t i = 0; i < HUB_EVENT_CAPACITY; ++i) {
            if (isOwned(slots_[i].record) &&
                slots_[i].record.sourceDeviceId == identity.sourceDeviceId &&
                slots_[i].record.eventEpoch == identity.epoch &&
                slots_[i].record.eventId == identity.id) {
                slot = i;
                record = slots_[i].record;
                return true;
            }
        }
        return false;
    }

    bool healthy() const { return ledgerHealthy_; }
    bool ordinalReady() const { return ordinalReady_; }
    Status status() const { return status_; }
    size_t activeCount() const { return countState(EventRecords::HubState::ACTIVE); }
    size_t consumedCount() const { return countState(EventRecords::HubState::CONSUMED); }
    const EventRecords::HubMetadata& metadata() const { return metadata_; }
    const EventRecords::HubRecord* record(uint8_t slot) const {
        return slot < HUB_EVENT_CAPACITY && slots_[slot].present ? &slots_[slot].record : nullptr;
    }

private:
    struct SlotState {
        bool present;
        EventStorage::CopySlot activeCopy;
        EventRecords::HubRecord record;
    };

    static bool isOwned(const EventRecords::HubRecord& record) {
        return record.state == EventRecords::HubState::ACTIVE ||
            record.state == EventRecords::HubState::CONSUMED;
    }
    static bool sameIdentity(const EventRecords::HubRecord& record,
                             const EventProtocol::Event& event) {
        return record.sourceDeviceId == event.source &&
            record.eventEpoch == event.epoch && record.eventId == event.id;
    }
    static bool sameCanonical(const EventRecords::HubRecord& record,
                              const EventProtocol::Event& event) {
        if (!sameIdentity(record, event) || record.family != event.family ||
            record.flags != event.flags ||
            record.lifetimeBudgetSeconds != event.lifetimeBudgetSeconds ||
            record.bodyLength != event.bodyLength) return false;
        return EventStorage::bytesEqual(record.body, event.body, event.bodyLength);
    }
    static EventStorage::CopySlot opposite(EventStorage::CopySlot copy) {
        return copy == EventStorage::CopySlot::A
            ? EventStorage::CopySlot::B : EventStorage::CopySlot::A;
    }
    static bool validHighWater(uint32_t value) {
        return value != 0U && ((value - 1U) % ORDINAL_RESERVATION_BLOCK_SIZE) == 0U;
    }

    AdmissionStatus validateAdmission(const EventProtocol::Event& event) const {
        if (!EventProtocol::hasValidEndpoints(event.source, event.destination) ||
            event.source != nodeDeviceId_ || event.destination != hubDeviceId_ ||
            event.epoch == 0 || event.id == 0) return AdmissionStatus::ROUTING_REJECTED;
        if (!EventProtocol::isRegisteredFamily(event.family)) {
            return AdmissionStatus::UNSUPPORTED_EVENT;
        }
        if ((event.flags & static_cast<uint8_t>(~EventProtocol::ALLOWED_FLAGS)) != 0 ||
            event.lifetimeBudgetSeconds < EventProtocol::MIN_LIFETIME_SECONDS ||
            event.lifetimeBudgetSeconds > EventProtocol::MAX_LIFETIME_SECONDS ||
            event.bodyLength > EventProtocol::MAX_BODY_SIZE ||
            !EventProtocol::isValidFamilyBody(event.family, event.body, event.bodyLength)) {
            return AdmissionStatus::MALFORMED_EVENT;
        }
        for (size_t i = event.bodyLength; i < EventProtocol::MAX_BODY_SIZE; ++i) {
            if (event.body[i] != 0) return AdmissionStatus::MALFORMED_EVENT;
        }
        return AdmissionStatus::DURABLE_NEW_ADMISSION;
    }

    uint8_t firstEmpty() const {
        for (uint8_t i = 0; i < HUB_EVENT_CAPACITY; ++i) {
            if (slots_[i].record.state == EventRecords::HubState::EMPTY) return i;
        }
        return 0xFF;
    }

    static bool incomingLater(const EventProtocol::Event& event,
                              const EventRecords::HubRecord& tombstone) {
        return event.source == tombstone.sourceDeviceId &&
            (event.epoch > tombstone.eventEpoch ||
             (event.epoch == tombstone.eventEpoch && event.id > tombstone.eventId));
    }

    uint8_t oldestReclaimable(const EventProtocol::Event& event) const {
        uint8_t found = 0xFF;
        for (uint8_t i = 0; i < HUB_EVENT_CAPACITY; ++i) {
            const EventRecords::HubRecord& record = slots_[i].record;
            if (record.state != EventRecords::HubState::CONSUMED ||
                !incomingLater(event, record)) continue;
            if (found == 0xFF || record.admissionOrdinal <
                slots_[found].record.admissionOrdinal) found = i;
        }
        return found;
    }

    bool initializeFresh() {
        EventRecords::HubMetadata fresh = {};
        fresh.generation = 1;
        fresh.nextUnreservedAdmissionOrdinal = 1;
        if (!commitMetadata(fresh, EventStorage::CopySlot::A)) return false;
        for (uint8_t slot = 0; slot < HUB_EVENT_CAPACITY; ++slot) {
            EventRecords::HubRecord empty = {};
            empty.generation = 1;
            empty.state = EventRecords::HubState::EMPTY;
            slots_[slot].activeCopy = EventStorage::CopySlot::A;
            if (!commitInitialRecord(slot, empty)) return false;
        }
        return true;
    }

    void recoverMetadata(
        const EventStorage::FixedCopy<EventRecords::HUB_METADATA_SIZE>& a,
        const EventStorage::FixedCopy<EventRecords::HUB_METADATA_SIZE>& b) {
        EventRecords::HubMetadata selected = {};
        EventStorage::CopySlot copy = EventStorage::CopySlot::A;
        const EventStorage::SelectionResult selection = EventStorage::selectCopies(
            a, b, EventRecords::decodeHubMetadataForSelection, selected, copy);
        if (selection == EventStorage::SelectionResult::EQUAL_DISAGREEMENT) {
            status_ = Status::RECORD_CONFLICT;
            return;
        }
        if (selection == EventStorage::SelectionResult::GENERATION_AMBIGUOUS) {
            status_ = Status::GENERATION_AMBIGUOUS;
            return;
        }
        if (selection == EventStorage::SelectionResult::BOTH_INVALID ||
            !validHighWater(selected.nextUnreservedAdmissionOrdinal)) {
            status_ = Status::ORDINAL_UNAVAILABLE;
            return;
        }
        metadata_ = selected;
        metadataCopy_ = copy;
        hasMetadata_ = true;
        nextVolatileOrdinal_ = selected.nextUnreservedAdmissionOrdinal;
        volatileEndExclusive_ = selected.nextUnreservedAdmissionOrdinal;
        ordinalReady_ = true;
        status_ = Status::READY;
    }

    bool allocateOrdinal(uint32_t& ordinal) {
        ordinal = 0;
        if (!ordinalReady_) return false;
        if (nextVolatileOrdinal_ == volatileEndExclusive_) {
            const uint32_t start = metadata_.nextUnreservedAdmissionOrdinal;
            if (start > UINT32_MAX - ORDINAL_RESERVATION_BLOCK_SIZE) {
                ordinalReady_ = false;
                status_ = Status::ORDINAL_EXHAUSTED;
                return false;
            }
            EventRecords::HubMetadata next = metadata_;
            next.generation += 1U;
            next.nextUnreservedAdmissionOrdinal = start + ORDINAL_RESERVATION_BLOCK_SIZE;
            if (!commitMetadata(next, opposite(metadataCopy_))) return false;
            nextVolatileOrdinal_ = start;
            volatileEndExclusive_ = start + ORDINAL_RESERVATION_BLOCK_SIZE;
        }
        ordinal = nextVolatileOrdinal_++;
        return true;
    }

    bool commitMetadata(const EventRecords::HubMetadata& next,
                        EventStorage::CopySlot target) {
        uint8_t encoded[EventRecords::HUB_METADATA_SIZE] = {};
        if (EventRecords::encodeHubMetadata(next, encoded, sizeof(encoded)) !=
            EventRecords::CodecResult::OK) return failOrdinal(Status::STORAGE_FAILURE);
        EventIdentity::StorageResult result = storage_->writeHubMetadata(
            target, encoded, sizeof(encoded));
        if (result != EventIdentity::StorageResult::OK) return failOrdinal(storageStatus(result));
        result = storage_->commit();
        if (result != EventIdentity::StorageResult::OK) return failOrdinal(storageStatus(result));
        EventStorage::FixedCopy<EventRecords::HUB_METADATA_SIZE> readBack = {};
        size_t length = 0;
        result = storage_->readHubMetadata(
            target, readBack.bytes, sizeof(readBack.bytes), length);
        if (result != EventIdentity::StorageResult::OK || length != sizeof(readBack.bytes)) {
            return failOrdinal(storageStatus(result));
        }
        EventRecords::HubMetadata decoded = {};
        if (EventRecords::decodeHubMetadata(readBack.bytes, sizeof(readBack.bytes), decoded) !=
            EventRecords::CodecResult::OK ||
            !EventStorage::bytesEqual(encoded, readBack.bytes, sizeof(encoded))) {
            return failOrdinal(Status::STORAGE_FAILURE);
        }
        EventStorage::FixedCopy<EventRecords::HUB_METADATA_SIZE> copyA = {};
        EventStorage::FixedCopy<EventRecords::HUB_METADATA_SIZE> copyB = {};
        if (!readMetadataCopy(EventStorage::CopySlot::A, copyA) ||
            !readMetadataCopy(EventStorage::CopySlot::B, copyB)) {
            return failOrdinal(Status::STORAGE_UNAVAILABLE);
        }
        EventRecords::HubMetadata authoritative = {};
        EventStorage::CopySlot authoritativeCopy = EventStorage::CopySlot::A;
        const EventStorage::SelectionResult selection = EventStorage::selectCopies(
            copyA, copyB, EventRecords::decodeHubMetadataForSelection,
            authoritative, authoritativeCopy);
        if (authoritativeCopy != target || authoritative.generation != next.generation ||
            selection == EventStorage::SelectionResult::BOTH_INVALID ||
            selection == EventStorage::SelectionResult::EQUAL_DISAGREEMENT ||
            selection == EventStorage::SelectionResult::GENERATION_AMBIGUOUS) {
            return failOrdinal(selection == EventStorage::SelectionResult::GENERATION_AMBIGUOUS
                ? Status::GENERATION_AMBIGUOUS : Status::STORAGE_FAILURE);
        }
        metadata_ = authoritative;
        metadataCopy_ = target;
        hasMetadata_ = true;
        ordinalReady_ = true;
        return true;
    }

    bool commitInitialRecord(uint8_t slot, const EventRecords::HubRecord& next) {
        slots_[slot].present = true;
        slots_[slot].record = next;
        slots_[slot].activeCopy = EventStorage::CopySlot::B;
        if (!commitRecord(slot, next, EventStorage::CopySlot::A)) return false;
        return true;
    }

    bool commitRecord(uint8_t slot, const EventRecords::HubRecord& next) {
        return commitRecord(slot, next, opposite(slots_[slot].activeCopy));
    }

    bool commitRecord(uint8_t slot, const EventRecords::HubRecord& next,
                      EventStorage::CopySlot target) {
        uint8_t encoded[EventRecords::HUB_RECORD_SIZE] = {};
        if (EventRecords::encodeHubRecord(next, encoded, sizeof(encoded)) !=
            EventRecords::CodecResult::OK) return failLedgerBool(Status::STORAGE_FAILURE);
        EventIdentity::StorageResult result = storage_->writeHubEventCopy(
            slot, target, encoded, sizeof(encoded));
        if (result != EventIdentity::StorageResult::OK) return failLedgerBool(storageStatus(result));
        result = storage_->commit();
        if (result != EventIdentity::StorageResult::OK) return failLedgerBool(storageStatus(result));
        EventStorage::FixedCopy<EventRecords::HUB_RECORD_SIZE> readBack = {};
        size_t length = 0;
        result = storage_->readHubEventCopy(
            slot, target, readBack.bytes, sizeof(readBack.bytes), length);
        if (result != EventIdentity::StorageResult::OK || length != sizeof(readBack.bytes)) {
            return failLedgerBool(storageStatus(result));
        }
        EventRecords::HubRecord decoded = {};
        if (EventRecords::decodeHubRecord(readBack.bytes, sizeof(readBack.bytes), decoded) !=
            EventRecords::CodecResult::OK ||
            !EventStorage::bytesEqual(encoded, readBack.bytes, sizeof(encoded))) {
            return failLedgerBool(Status::STORAGE_FAILURE);
        }
        EventStorage::FixedCopy<EventRecords::HUB_RECORD_SIZE> copyA = {};
        EventStorage::FixedCopy<EventRecords::HUB_RECORD_SIZE> copyB = {};
        if (!readRecordCopy(slot, EventStorage::CopySlot::A, copyA) ||
            !readRecordCopy(slot, EventStorage::CopySlot::B, copyB)) return false;
        EventRecords::HubRecord authoritative = {};
        EventStorage::CopySlot authoritativeCopy = EventStorage::CopySlot::A;
        const EventStorage::SelectionResult selection = EventStorage::selectCopies(
            copyA, copyB, EventRecords::decodeHubRecordForSelection,
            authoritative, authoritativeCopy);
        if (authoritativeCopy != target || authoritative.generation != next.generation ||
            selection == EventStorage::SelectionResult::BOTH_INVALID ||
            selection == EventStorage::SelectionResult::EQUAL_DISAGREEMENT ||
            selection == EventStorage::SelectionResult::GENERATION_AMBIGUOUS) {
            return failLedgerBool(selection == EventStorage::SelectionResult::GENERATION_AMBIGUOUS
                ? Status::GENERATION_AMBIGUOUS : Status::STORAGE_FAILURE);
        }
        slots_[slot].present = true;
        slots_[slot].record = authoritative;
        slots_[slot].activeCopy = target;
        return true;
    }

    bool readMetadataCopy(EventStorage::CopySlot slot,
                          EventStorage::FixedCopy<EventRecords::HUB_METADATA_SIZE>& copy) {
        size_t length = 0;
        const EventIdentity::StorageResult result = storage_->readHubMetadata(
            slot, copy.bytes, sizeof(copy.bytes), length);
        return classifyRead(result, length, sizeof(copy.bytes), copy.status, false);
    }

    bool readRecordCopy(uint8_t slot, EventStorage::CopySlot copySlot,
                        EventStorage::FixedCopy<EventRecords::HUB_RECORD_SIZE>& copy) {
        size_t length = 0;
        const EventIdentity::StorageResult result = storage_->readHubEventCopy(
            slot, copySlot, copy.bytes, sizeof(copy.bytes), length);
        return classifyRead(result, length, sizeof(copy.bytes), copy.status, true);
    }

    bool classifyRead(EventIdentity::StorageResult result, size_t length,
                      size_t expected, EventStorage::FixedReadStatus& status,
                      bool ledgerRead) {
        if (result == EventIdentity::StorageResult::MISSING) {
            status = EventStorage::FixedReadStatus::MISSING;
            return true;
        }
        if (result == EventIdentity::StorageResult::UNAVAILABLE) {
            status = EventStorage::FixedReadStatus::UNAVAILABLE;
            if (ledgerRead) failLedger(Status::STORAGE_UNAVAILABLE);
            else status_ = Status::ORDINAL_UNAVAILABLE;
            return false;
        }
        status = EventStorage::FixedReadStatus::OK;
        if (result != EventIdentity::StorageResult::OK || length != expected) {
            // Existing but malformed data remains an invalid copy for A/B selection.
            return true;
        }
        return true;
    }

    bool acceptRecordSelection(EventStorage::SelectionResult result) {
        if (result == EventStorage::SelectionResult::EQUAL_DISAGREEMENT)
            return failLedgerBool(Status::RECORD_CONFLICT);
        if (result == EventStorage::SelectionResult::GENERATION_AMBIGUOUS)
            return failLedgerBool(Status::GENERATION_AMBIGUOUS);
        return true;
    }

    bool validateRecoveredSlots() {
        for (uint8_t i = 0; i < HUB_EVENT_CAPACITY; ++i) {
            if (!slots_[i].present) return failLedgerBool(Status::INDETERMINATE_SLOT);
            if (!isOwned(slots_[i].record)) continue;
            for (uint8_t j = 0; j < i; ++j) {
                if (!isOwned(slots_[j].record)) continue;
                if (slots_[i].record.sourceDeviceId == slots_[j].record.sourceDeviceId &&
                    slots_[i].record.eventEpoch == slots_[j].record.eventEpoch &&
                    slots_[i].record.eventId == slots_[j].record.eventId)
                    return failLedgerBool(Status::DUPLICATE_IDENTITY);
                if (slots_[i].record.admissionOrdinal == slots_[j].record.admissionOrdinal)
                    return failLedgerBool(Status::DUPLICATE_ORDINAL);
            }
        }
        return true;
    }

    size_t countState(EventRecords::HubState state) const {
        size_t count = 0;
        if (!ledgerHealthy_) return 0;
        for (size_t i = 0; i < HUB_EVENT_CAPACITY; ++i)
            if (slots_[i].record.state == state) ++count;
        return count;
    }

    static Status storageStatus(EventIdentity::StorageResult result) {
        return result == EventIdentity::StorageResult::UNAVAILABLE
            ? Status::STORAGE_UNAVAILABLE : Status::STORAGE_FAILURE;
    }
    Status failLedger(Status status) {
        ledgerHealthy_ = false;
        status_ = status;
        return status;
    }
    bool failLedgerBool(Status status) { failLedger(status); return false; }
    bool failOrdinal(Status status) {
        ordinalReady_ = false;
        status_ = status;
        return false;
    }
    void reset() {
        storage_ = nullptr;
        for (size_t i = 0; i < HUB_EVENT_CAPACITY; ++i) slots_[i] = {};
        metadata_ = {};
        metadataCopy_ = EventStorage::CopySlot::A;
        nextVolatileOrdinal_ = 0;
        volatileEndExclusive_ = 0;
        hubDeviceId_ = 0;
        nodeDeviceId_ = 0;
        hasMetadata_ = false;
        ledgerHealthy_ = false;
        ordinalReady_ = false;
        status_ = Status::INDETERMINATE_SLOT;
    }

    Storage* storage_ = nullptr;
    SlotState slots_[HUB_EVENT_CAPACITY] = {};
    EventRecords::HubMetadata metadata_ = {};
    EventStorage::CopySlot metadataCopy_ = EventStorage::CopySlot::A;
    uint32_t nextVolatileOrdinal_ = 0;
    uint32_t volatileEndExclusive_ = 0;
    uint8_t hubDeviceId_ = 0;
    uint8_t nodeDeviceId_ = 0;
    bool hasMetadata_ = false;
    bool ledgerHealthy_ = false;
    bool ordinalReady_ = false;
    Status status_ = Status::INDETERMINATE_SLOT;
};

}  // namespace HubEventLedger
