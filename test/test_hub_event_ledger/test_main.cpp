#include <unity.h>

#include <stdint.h>

#include "hub_event_ledger.h"

using namespace EventStorage;
using namespace EventRecords;
using namespace EventIdentity;
using namespace HubEventLedger;

namespace {

enum class Fault : uint8_t { NONE, WRITE, COMMIT, READ_MISSING, READ_UNAVAILABLE, MISMATCH };

class FakeStorage final : public Storage {
public:
    FixedCopy<HUB_METADATA_SIZE> metadata[2] = {};
    FixedCopy<HUB_RECORD_SIZE> records[HUB_EVENT_CAPACITY][2] = {};
    Fault fault = Fault::NONE;
    bool afterWrite = false;
    bool metadataWrite = false;
    uint8_t writtenSlot = 0;
    CopySlot writtenCopy = CopySlot::A;
    unsigned metadataWrites = 0;
    unsigned recordWrites = 0;
    unsigned commits = 0;

    FakeStorage() {
        metadata[0].status = metadata[1].status = FixedReadStatus::MISSING;
        for (size_t i = 0; i < HUB_EVENT_CAPACITY; ++i)
            records[i][0].status = records[i][1].status = FixedReadStatus::MISSING;
    }

    StorageResult readHubMetadata(CopySlot copy, uint8_t* out,
                                  size_t capacity, size_t& length) override {
        return readFixed(metadata[index(copy)], out, capacity, length,
                         HUB_METADATA_SIZE, true, 0, copy);
    }
    StorageResult writeHubMetadata(CopySlot copy, const uint8_t* in,
                                   size_t length) override {
        ++metadataWrites; metadataWrite = true; writtenCopy = copy; afterWrite = true;
        if (fault == Fault::WRITE || length != HUB_METADATA_SIZE) return StorageResult::ERROR;
        copyBytes(metadata[index(copy)], in, length); return StorageResult::OK;
    }
    StorageResult readHubEventCopy(uint8_t slot, CopySlot copy, uint8_t* out,
                                  size_t capacity, size_t& length) override {
        if (slot >= HUB_EVENT_CAPACITY) return StorageResult::ERROR;
        return readFixed(records[slot][index(copy)], out, capacity, length,
                         HUB_RECORD_SIZE, false, slot, copy);
    }
    StorageResult writeHubEventCopy(uint8_t slot, CopySlot copy,
                                   const uint8_t* in, size_t length) override {
        ++recordWrites; metadataWrite = false; writtenSlot = slot;
        writtenCopy = copy; afterWrite = true;
        if (fault == Fault::WRITE || slot >= HUB_EVENT_CAPACITY || length != HUB_RECORD_SIZE)
            return StorageResult::ERROR;
        copyBytes(records[slot][index(copy)], in, length); return StorageResult::OK;
    }
    StorageResult commit() override {
        ++commits;
        return fault == Fault::COMMIT ? StorageResult::ERROR : StorageResult::OK;
    }

    void clearFault() { fault = Fault::NONE; afterWrite = false; }

private:
    static size_t index(CopySlot copy) { return copy == CopySlot::A ? 0U : 1U; }
    template <size_t N>
    static void copyBytes(FixedCopy<N>& destination, const uint8_t* source, size_t length) {
        destination.status = FixedReadStatus::OK;
        for (size_t i = 0; i < length; ++i) destination.bytes[i] = source[i];
    }
    template <size_t N>
    StorageResult readFixed(FixedCopy<N>& source, uint8_t* output, size_t capacity,
                            size_t& length, size_t exact, bool isMetadata,
                            uint8_t slot, CopySlot copy) {
        length = 0;
        const bool readBack = afterWrite && metadataWrite == isMetadata &&
            (!isMetadata ? slot == writtenSlot : true) && copy == writtenCopy;
        if (readBack && fault == Fault::READ_MISSING) return StorageResult::MISSING;
        if (readBack && fault == Fault::READ_UNAVAILABLE) return StorageResult::UNAVAILABLE;
        if (source.status == FixedReadStatus::MISSING) return StorageResult::MISSING;
        if (source.status == FixedReadStatus::UNAVAILABLE) return StorageResult::UNAVAILABLE;
        if (capacity < exact) return StorageResult::ERROR;
        for (size_t i = 0; i < exact; ++i) output[i] = source.bytes[i];
        if (readBack && fault == Fault::MISMATCH) output[8] ^= 1U;
        length = exact;
        return StorageResult::OK;
    }
};

EventProtocol::Event makeEvent(uint32_t id, uint32_t epoch = 7,
                               uint8_t source = 2, uint8_t family = 0x40) {
    EventProtocol::Event event = {};
    event.source = source; event.destination = 1; event.sequence = 9;
    event.family = family; event.epoch = epoch; event.id = id;
    event.lifetimeBudgetSeconds = 600; event.bodyLength = 1;
    event.body[0] = family == 0x44 ? 0x01 : 0x02;
    if (family == 0x41) {
        event.bodyLength = 8; event.body[0] = 1; event.body[2] = 2;
        event.body[7] = 1;
    }
    return event;
}

struct Fixture {
    FakeStorage storage;
    Ledger ledger;
    void fresh() {
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HubEventLedger::Status::READY),
            static_cast<uint8_t>(ledger.recover(storage, 1, 2)));
    }
    AdmissionResult admit(uint32_t id, uint32_t epoch = 7) {
        return ledger.admit(makeEvent(id, epoch));
    }
};

void testFreshInitializationAndCanonicalEmpty() {
    Fixture f; f.fresh();
    TEST_ASSERT_TRUE(f.ledger.healthy()); TEST_ASSERT_TRUE(f.ledger.ordinalReady());
    TEST_ASSERT_EQUAL_UINT32(1, f.ledger.metadata().nextUnreservedAdmissionOrdinal);
    TEST_ASSERT_EQUAL_UINT(8, f.storage.recordWrites);
    for (uint8_t i = 0; i < HUB_EVENT_CAPACITY; ++i) {
        const HubRecord* record = f.ledger.record(i);
        TEST_ASSERT_NOT_NULL(record);
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HubState::EMPTY),
                               static_cast<uint8_t>(record->state));
        TEST_ASSERT_EQUAL_UINT32(1, record->generation);
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::OK),
            static_cast<uint8_t>(validateHubRecord(*record)));
    }
}

void testCorruptExistingIsNotFresh() {
    Fixture f;
    f.storage.records[0][0].status = FixedReadStatus::OK;
    f.storage.records[0][0].bytes[0] = 0xAA;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HubEventLedger::Status::INDETERMINATE_SLOT),
        static_cast<uint8_t>(f.ledger.recover(f.storage, 1, 2)));
    TEST_ASSERT_EQUAL_UINT(0, f.storage.recordWrites);
}

void testFirstAdmissionReservesBeforeDurableActive() {
    Fixture f; f.fresh();
    const unsigned beforeMetadata = f.storage.metadataWrites;
    AdmissionResult result = f.admit(1);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AdmissionStatus::DURABLE_NEW_ADMISSION),
                           static_cast<uint8_t>(result.status));
    TEST_ASSERT_EQUAL_UINT32(1, result.admissionOrdinal);
    TEST_ASSERT_EQUAL_UINT32(33, f.ledger.metadata().nextUnreservedAdmissionOrdinal);
    TEST_ASSERT_EQUAL_UINT(beforeMetadata + 1, f.storage.metadataWrites);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HubState::ACTIVE),
                           static_cast<uint8_t>(f.ledger.record(0)->state));
}

void testAllFamiliesAndExactCanonicalContent() {
    Fixture f; f.fresh();
    const uint8_t families[] = {0x40, 0x41, 0x44};
    for (uint8_t i = 0; i < 3; ++i) {
        EventProtocol::Event event = makeEvent(i + 1, 7, 2, families[i]);
        AdmissionResult result = f.ledger.admit(event);
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AdmissionStatus::DURABLE_NEW_ADMISSION),
                               static_cast<uint8_t>(result.status));
        TEST_ASSERT_EQUAL_UINT8(families[i], f.ledger.record(result.slot)->family);
        TEST_ASSERT_EQUAL_UINT32(event.lifetimeBudgetSeconds,
                                f.ledger.record(result.slot)->lifetimeBudgetSeconds);
    }
}

void testValidationBeforeMutation() {
    Fixture f; f.fresh(); const unsigned writes = f.storage.recordWrites;
    EventProtocol::Event event = makeEvent(1); event.family = 0x42;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AdmissionStatus::UNSUPPORTED_EVENT),
                           static_cast<uint8_t>(f.ledger.admit(event).status));
    event = makeEvent(1); event.flags = 0x80;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AdmissionStatus::MALFORMED_EVENT),
                           static_cast<uint8_t>(f.ledger.admit(event).status));
    event = makeEvent(1); event.destination = 3;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AdmissionStatus::ROUTING_REJECTED),
                           static_cast<uint8_t>(f.ledger.admit(event).status));
    TEST_ASSERT_EQUAL_UINT(writes, f.storage.recordWrites);
    TEST_ASSERT_EQUAL_UINT32(1, f.ledger.metadata().nextUnreservedAdmissionOrdinal);
}

void testActiveDuplicateAndEveryCanonicalMismatch() {
    Fixture f; f.fresh(); EventProtocol::Event original = makeEvent(1);
    AdmissionResult first = f.ledger.admit(original);
    const unsigned records = f.storage.recordWrites, metadata = f.storage.metadataWrites;
    EventProtocol::Event duplicate = original; duplicate.sequence = 77;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AdmissionStatus::EXACT_DUPLICATE),
                           static_cast<uint8_t>(f.ledger.admit(duplicate).status));
    EventProtocol::Event variants[4] = {original, original, original, original};
    variants[0].flags = 1; variants[1].lifetimeBudgetSeconds = 601;
    variants[2].family = 0x44; variants[2].body[0] = 1;
    variants[3].body[0] = 3;
    for (const auto& value : variants)
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AdmissionStatus::IDENTITY_CONTENT_MISMATCH),
                               static_cast<uint8_t>(f.ledger.admit(value).status));
    TEST_ASSERT_EQUAL_UINT(records, f.storage.recordWrites);
    TEST_ASSERT_EQUAL_UINT(metadata, f.storage.metadataWrites);
    TEST_ASSERT_EQUAL_UINT32(first.admissionOrdinal, f.ledger.record(first.slot)->admissionOrdinal);
}

void testCapacityNeverEvictsActiveOrAllocatesOrdinal() {
    Fixture f; f.fresh();
    for (uint32_t id = 1; id <= 8; ++id)
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AdmissionStatus::DURABLE_NEW_ADMISSION),
                               static_cast<uint8_t>(f.admit(id).status));
    const unsigned metadata = f.storage.metadataWrites, records = f.storage.recordWrites;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AdmissionStatus::CAPACITY),
                           static_cast<uint8_t>(f.admit(9).status));
    TEST_ASSERT_EQUAL_UINT(metadata, f.storage.metadataWrites);
    TEST_ASSERT_EQUAL_UINT(records, f.storage.recordWrites);
    TEST_ASSERT_EQUAL_UINT(8, f.ledger.activeCount());
}

void testDurableConsumeIdempotencyAndOldestActive() {
    Fixture f; f.fresh();
    AdmissionResult a = f.admit(1), b = f.admit(2);
    EventProtocol::Identity identity = {2, 7, 1};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ConsumeStatus::CONSUMED),
                           static_cast<uint8_t>(f.ledger.consume(identity).status));
    const unsigned writes = f.storage.recordWrites;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ConsumeStatus::ALREADY_CONSUMED),
                           static_cast<uint8_t>(f.ledger.consume(identity).status));
    TEST_ASSERT_EQUAL_UINT(writes, f.storage.recordWrites);
    TEST_ASSERT_EQUAL_UINT32(a.admissionOrdinal, f.ledger.record(a.slot)->admissionOrdinal);
    uint8_t slot = 0xFF; HubRecord record = {};
    TEST_ASSERT_TRUE(f.ledger.oldestActive(slot, record));
    TEST_ASSERT_EQUAL_UINT8(b.slot, slot);
    identity.id = 99;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ConsumeStatus::NOT_FOUND),
                           static_cast<uint8_t>(f.ledger.consume(identity).status));
}

void testConsumedDuplicateSurvivesRebootWithoutReactivation() {
    Fixture f; f.fresh(); EventProtocol::Event event = makeEvent(1);
    AdmissionResult first = f.ledger.admit(event);
    EventProtocol::Identity identity = {2, 7, 1};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ConsumeStatus::CONSUMED),
                           static_cast<uint8_t>(f.ledger.consume(identity).status));
    Ledger rebooted;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HubEventLedger::Status::READY),
                           static_cast<uint8_t>(rebooted.recover(f.storage, 1, 2)));
    const unsigned writes = f.storage.recordWrites, metadata = f.storage.metadataWrites;
    AdmissionResult duplicate = rebooted.admit(event);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AdmissionStatus::EXACT_DUPLICATE),
                           static_cast<uint8_t>(duplicate.status));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HubState::CONSUMED),
                           static_cast<uint8_t>(rebooted.record(first.slot)->state));
    TEST_ASSERT_EQUAL_UINT(writes, f.storage.recordWrites);
    TEST_ASSERT_EQUAL_UINT(metadata, f.storage.metadataWrites);
}

void testLostAckAndBothRebootRetainsSingleActiveProof() {
    Fixture f; f.fresh(); EventProtocol::Event event = makeEvent(1);
    AdmissionResult first = f.ledger.admit(event);
    // The Node intentionally retains E: the synthetic ADMITTED response is lost.
    Ledger rebooted;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HubEventLedger::Status::READY),
                           static_cast<uint8_t>(rebooted.recover(f.storage, 1, 2)));
    const unsigned records = f.storage.recordWrites, metadata = f.storage.metadataWrites;
    AdmissionResult retry = rebooted.admit(event);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AdmissionStatus::EXACT_DUPLICATE),
                           static_cast<uint8_t>(retry.status));
    TEST_ASSERT_EQUAL_UINT32(first.admissionOrdinal, retry.admissionOrdinal);
    TEST_ASSERT_EQUAL_UINT(1, rebooted.activeCount());
    TEST_ASSERT_EQUAL_UINT(records, f.storage.recordWrites);
    TEST_ASSERT_EQUAL_UINT(metadata, f.storage.metadataWrites);
}

void testEmptyPreferredBeforeTombstoneReplacement() {
    Fixture f; f.fresh(); AdmissionResult old = f.admit(1);
    EventProtocol::Identity identity = {2, 7, 1}; f.ledger.consume(identity);
    AdmissionResult later = f.admit(2);
    TEST_ASSERT_EQUAL_UINT8(1, later.slot);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HubState::CONSUMED),
                           static_cast<uint8_t>(f.ledger.record(old.slot)->state));
}

void testOldestLegalSameSourceTombstoneReplacement() {
    Fixture f; f.fresh();
    for (uint32_t id = 1; id <= 8; ++id) f.admit(id);
    EventProtocol::Identity one = {2, 7, 1}, two = {2, 7, 2};
    f.ledger.consume(one); f.ledger.consume(two);
    AdmissionResult replacement = f.admit(9);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AdmissionStatus::DURABLE_NEW_ADMISSION),
                           static_cast<uint8_t>(replacement.status));
    TEST_ASSERT_EQUAL_UINT8(0, replacement.slot);
    TEST_ASSERT_EQUAL_UINT32(9, f.ledger.record(0)->eventId);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HubState::CONSUMED),
                           static_cast<uint8_t>(f.ledger.record(1)->state));
}

void testInvalidTombstoneReplacementReturnsCapacityWithoutOrdinal() {
    Fixture f; f.fresh();
    for (uint32_t id = 1; id <= 8; ++id) f.admit(id);
    EventProtocol::Identity identity = {2, 7, 8}; f.ledger.consume(identity);
    const unsigned metadata = f.storage.metadataWrites;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AdmissionStatus::CAPACITY),
                           static_cast<uint8_t>(f.admit(99, 6).status));
    EventProtocol::Event other = makeEvent(1, 7, 3);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AdmissionStatus::ROUTING_REJECTED),
                           static_cast<uint8_t>(f.ledger.admit(other).status));
    TEST_ASSERT_EQUAL_UINT(metadata, f.storage.metadataWrites);
}

void testRebootSkipsUnusedReservedOrdinals() {
    Fixture f; f.fresh(); TEST_ASSERT_EQUAL_UINT32(1, f.admit(1).admissionOrdinal);
    Ledger rebooted; TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HubEventLedger::Status::READY),
        static_cast<uint8_t>(rebooted.recover(f.storage, 1, 2)));
    AdmissionResult next = rebooted.admit(makeEvent(2));
    TEST_ASSERT_EQUAL_UINT32(33, next.admissionOrdinal);
    TEST_ASSERT_EQUAL_UINT32(65, rebooted.metadata().nextUnreservedAdmissionOrdinal);
}

void testOrdinalBlockSequencingThroughThirtyThree() {
    Fixture f; f.fresh();
    const unsigned initialMetadataWrites = f.storage.metadataWrites;
    for (uint32_t id = 1; id <= 32; ++id) {
        AdmissionResult admitted = f.admit(id);
        TEST_ASSERT_EQUAL_UINT32(id, admitted.admissionOrdinal);
        EventProtocol::Identity identity = {2, 7, id};
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ConsumeStatus::CONSUMED),
                               static_cast<uint8_t>(f.ledger.consume(identity).status));
    }
    TEST_ASSERT_EQUAL_UINT(initialMetadataWrites + 1, f.storage.metadataWrites);
    AdmissionResult thirtyThree = f.admit(33);
    TEST_ASSERT_EQUAL_UINT32(33, thirtyThree.admissionOrdinal);
    TEST_ASSERT_EQUAL_UINT32(65, f.ledger.metadata().nextUnreservedAdmissionOrdinal);
    TEST_ASSERT_EQUAL_UINT(initialMetadataWrites + 2, f.storage.metadataWrites);
}

void testOrdinalExhaustionFailsClosedWithoutTouchingLedger() {
    Fixture f; f.fresh();
    HubMetadata nearEnd = {};
    nearEnd.generation = 2;
    nearEnd.nextUnreservedAdmissionOrdinal = 0xFFFFFFE1U;
    uint8_t bytes[HUB_METADATA_SIZE] = {};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::OK),
        static_cast<uint8_t>(encodeHubMetadata(nearEnd, bytes, sizeof(bytes))));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StorageResult::OK),
        static_cast<uint8_t>(f.storage.writeHubMetadata(CopySlot::B, bytes, sizeof(bytes))));
    f.storage.afterWrite = false;
    Ledger rebooted;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HubEventLedger::Status::READY),
                           static_cast<uint8_t>(rebooted.recover(f.storage, 1, 2)));
    const unsigned recordWrites = f.storage.recordWrites;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AdmissionStatus::STORAGE_FAILURE),
                           static_cast<uint8_t>(rebooted.admit(makeEvent(1)).status));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HubEventLedger::Status::ORDINAL_EXHAUSTED),
                           static_cast<uint8_t>(rebooted.status()));
    TEST_ASSERT_EQUAL_UINT(recordWrites, f.storage.recordWrites);
    TEST_ASSERT_EQUAL_UINT(0, rebooted.activeCount());
}

void testOrdinalAndActiveWriteFaultsNeverAdmit() {
    const Fault faults[] = {Fault::WRITE, Fault::COMMIT, Fault::READ_MISSING,
                            Fault::READ_UNAVAILABLE, Fault::MISMATCH};
    for (Fault fault : faults) {
        Fixture f; f.fresh(); f.storage.fault = fault; f.storage.afterWrite = false;
        AdmissionResult result = f.admit(1);
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AdmissionStatus::STORAGE_FAILURE),
                               static_cast<uint8_t>(result.status));
        TEST_ASSERT_EQUAL_UINT(0, f.ledger.activeCount());
    }
    Fixture f; f.fresh(); f.admit(1);
    f.storage.fault = Fault::WRITE; f.storage.afterWrite = false;
    AdmissionResult failed = f.admit(2);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AdmissionStatus::STORAGE_FAILURE),
                           static_cast<uint8_t>(failed.status));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HubState::ACTIVE),
                           static_cast<uint8_t>(f.ledger.record(0)->state));
}

void testBurnedOrdinalIsSkippedAfterFailedActiveCommitAndReboot() {
    Fixture f; f.fresh();
    TEST_ASSERT_EQUAL_UINT32(1, f.admit(1).admissionOrdinal);
    f.storage.fault = Fault::WRITE; f.storage.afterWrite = false;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AdmissionStatus::STORAGE_FAILURE),
                           static_cast<uint8_t>(f.admit(2).status));
    f.storage.clearFault();
    Ledger rebooted;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HubEventLedger::Status::READY),
                           static_cast<uint8_t>(rebooted.recover(f.storage, 1, 2)));
    AdmissionResult admitted = rebooted.admit(makeEvent(3));
    TEST_ASSERT_EQUAL_UINT32(33, admitted.admissionOrdinal);
    TEST_ASSERT_EQUAL_UINT32(65, rebooted.metadata().nextUnreservedAdmissionOrdinal);
}

void testTornTombstoneReplacementRecoversOldOrNewNeverEmpty() {
    Fixture f; f.fresh();
    for (uint32_t id = 1; id <= 8; ++id) f.admit(id);
    EventProtocol::Identity identity = {2, 7, 1}; f.ledger.consume(identity);
    f.storage.fault = Fault::READ_MISSING; f.storage.afterWrite = false;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AdmissionStatus::STORAGE_FAILURE),
                           static_cast<uint8_t>(f.admit(9).status));
    f.storage.clearFault();
    Ledger rebooted;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HubEventLedger::Status::READY),
                           static_cast<uint8_t>(rebooted.recover(f.storage, 1, 2)));
    const HubRecord* record = rebooted.record(0);
    TEST_ASSERT_NOT_NULL(record);
    TEST_ASSERT_NOT_EQUAL(static_cast<uint8_t>(HubState::EMPTY),
                          static_cast<uint8_t>(record->state));
    TEST_ASSERT_TRUE(
        (record->state == HubState::CONSUMED && record->eventId == 1) ||
        (record->state == HubState::ACTIVE && record->eventId == 9));
}

void testConsumeAndReplacementFaultsDoNotFabricateSuccess() {
    Fixture f; f.fresh(); for (uint32_t id = 1; id <= 8; ++id) f.admit(id);
    EventProtocol::Identity identity = {2, 7, 1};
    f.storage.fault = Fault::WRITE; f.storage.afterWrite = false;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ConsumeStatus::STORAGE_FAILURE),
                           static_cast<uint8_t>(f.ledger.consume(identity).status));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HubState::ACTIVE),
                           static_cast<uint8_t>(f.ledger.record(0)->state));
}

void testDuplicateIdentityAndOrdinalRecoveryFailsClosed() {
    Fixture f; f.fresh(); f.admit(1); f.admit(2);
    HubRecord duplicate = *f.ledger.record(1);
    duplicate.generation += 1; duplicate.eventId = 1;
    uint8_t bytes[HUB_RECORD_SIZE] = {};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::OK),
        static_cast<uint8_t>(encodeHubRecord(duplicate, bytes, sizeof(bytes))));
    f.storage.writeHubEventCopy(1, CopySlot::B, bytes, sizeof(bytes));
    Ledger rebooted;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HubEventLedger::Status::DUPLICATE_IDENTITY),
                           static_cast<uint8_t>(rebooted.recover(f.storage, 1, 2)));
}

void testOrdinalMetadataFailurePreservesDuplicateAndConsumeCustody() {
    Fixture f; f.fresh(); EventProtocol::Event event = makeEvent(1); f.ledger.admit(event);
    f.storage.metadata[0].bytes[0] ^= 1; f.storage.metadata[1].bytes[0] ^= 1;
    Ledger rebooted;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HubEventLedger::Status::ORDINAL_UNAVAILABLE),
                           static_cast<uint8_t>(rebooted.recover(f.storage, 1, 2)));
    TEST_ASSERT_TRUE(rebooted.healthy()); TEST_ASSERT_FALSE(rebooted.ordinalReady());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AdmissionStatus::EXACT_DUPLICATE),
                           static_cast<uint8_t>(rebooted.admit(event).status));
    EventProtocol::Identity identity = {2, 7, 1};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ConsumeStatus::CONSUMED),
                           static_cast<uint8_t>(rebooted.consume(identity).status));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AdmissionStatus::STORAGE_FAILURE),
                           static_cast<uint8_t>(rebooted.admit(makeEvent(2)).status));
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(testFreshInitializationAndCanonicalEmpty);
    RUN_TEST(testCorruptExistingIsNotFresh);
    RUN_TEST(testFirstAdmissionReservesBeforeDurableActive);
    RUN_TEST(testAllFamiliesAndExactCanonicalContent);
    RUN_TEST(testValidationBeforeMutation);
    RUN_TEST(testActiveDuplicateAndEveryCanonicalMismatch);
    RUN_TEST(testCapacityNeverEvictsActiveOrAllocatesOrdinal);
    RUN_TEST(testDurableConsumeIdempotencyAndOldestActive);
    RUN_TEST(testConsumedDuplicateSurvivesRebootWithoutReactivation);
    RUN_TEST(testLostAckAndBothRebootRetainsSingleActiveProof);
    RUN_TEST(testEmptyPreferredBeforeTombstoneReplacement);
    RUN_TEST(testOldestLegalSameSourceTombstoneReplacement);
    RUN_TEST(testInvalidTombstoneReplacementReturnsCapacityWithoutOrdinal);
    RUN_TEST(testRebootSkipsUnusedReservedOrdinals);
    RUN_TEST(testOrdinalBlockSequencingThroughThirtyThree);
    RUN_TEST(testOrdinalExhaustionFailsClosedWithoutTouchingLedger);
    RUN_TEST(testOrdinalAndActiveWriteFaultsNeverAdmit);
    RUN_TEST(testBurnedOrdinalIsSkippedAfterFailedActiveCommitAndReboot);
    RUN_TEST(testTornTombstoneReplacementRecoversOldOrNewNeverEmpty);
    RUN_TEST(testConsumeAndReplacementFaultsDoNotFabricateSuccess);
    RUN_TEST(testDuplicateIdentityAndOrdinalRecoveryFailsClosed);
    RUN_TEST(testOrdinalMetadataFailurePreservesDuplicateAndConsumeCustody);
    return UNITY_END();
}
