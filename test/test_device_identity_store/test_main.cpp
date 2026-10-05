#include <unity.h>

#include <stdint.h>

#include "device_identity_store.h"

using namespace DeviceIdentity;
using namespace DeviceIdentityStore;

namespace {

Record hub(uint32_t generation = 1) {
    Record value = {};
    value.recordGeneration = generation;
    value.provisioningGeneration = 1;
    value.state = DurableState::PROVISIONED;
    value.transaction = TransactionKind::NONE;
    value.role = Role::HUB;
    value.localDeviceId = 1;
    value.peerDeviceId = 0x10;
    value.hardwareProfile = HardwareProfile::HELTEC_V4;
    value.capabilityProfile = CapabilityProfile::HELTEC_V4_BASE;
    value.networkId = 0x11223344U;
    value.labelLength = 3;
    value.label[0] = 'H'; value.label[1] = 'U'; value.label[2] = 'B';
    return value;
}

Record node(uint32_t generation = 1) {
    Record value = hub(generation);
    value.role = Role::NODE;
    value.localDeviceId = 0x10;
    value.peerDeviceId = 1;
    return value;
}

Record pending(uint32_t generation = 1) {
    Record value = hub(generation);
    value.state = DurableState::PENDING;
    value.transaction = TransactionKind::APPLY;
    return value;
}

Record reset(uint32_t generation, bool pendingReset) {
    Record value = {};
    value.recordGeneration = generation;
    value.state = pendingReset ? DurableState::PENDING
                               : DurableState::UNPROVISIONED;
    value.transaction = pendingReset ? TransactionKind::FULL_RESET
                                     : TransactionKind::NONE;
    return value;
}

void encode(const Record& value, uint8_t* output) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::OK),
        static_cast<uint8_t>(encodeRecord(value, output, RECORD_SIZE)));
}

void refreshCrc(uint8_t* bytes) {
    uint32_t crc = 0;
    TEST_ASSERT_TRUE(EventRecords::crc32IsoHdlc(bytes, 60, crc));
    EventRecords::writeUint32Le(bytes + 60, crc);
}

struct Cell {
    bool exists = false;
    size_t length = 0;
    uint8_t bytes[RECORD_SIZE] = {};
};

enum class ReadBackFault { NONE, MISSING, UNAVAILABLE, MALFORMED, INVALID, MISMATCH };

class FakeStorage final : public Storage {
public:
    Cell cells[2];
    bool namespaceMissing = false;
    bool unavailable = false;
    bool failWrite = false;
    bool failCommit = false;
    ReadBackFault readBackFault = ReadBackFault::NONE;
    size_t reads = 0;
    size_t writes = 0;
    size_t commits = 0;
    CopySlot lastWritten = CopySlot::A;
    size_t lastWriteLength = 0;
    uint8_t lastWrittenBytes[RECORD_SIZE] = {};

    void seed(CopySlot slot, const Record& value) {
        Cell& cell = cells[index(slot)];
        cell.exists = true;
        cell.length = RECORD_SIZE;
        encode(value, cell.bytes);
    }

    StorageResult read(CopySlot slot, uint8_t* output,
                       size_t capacity, size_t& length) override {
        ++reads;
        length = 0;
        if (unavailable) return StorageResult::UNAVAILABLE;
        if (namespaceMissing) return StorageResult::MISSING;
        const bool readBack = commits > 0 && slot == lastWritten;
        if (readBack && readBackFault == ReadBackFault::MISSING) {
            return StorageResult::MISSING;
        }
        if (readBack && readBackFault == ReadBackFault::UNAVAILABLE) {
            return StorageResult::UNAVAILABLE;
        }
        if (readBack && readBackFault == ReadBackFault::MALFORMED) {
            return StorageResult::MALFORMED;
        }
        const Cell& cell = cells[index(slot)];
        if (!cell.exists) return StorageResult::MISSING;
        if (cell.length != RECORD_SIZE || capacity < RECORD_SIZE) {
            return StorageResult::MALFORMED;
        }
        for (size_t i = 0; i < RECORD_SIZE; ++i) output[i] = cell.bytes[i];
        if (readBack && readBackFault == ReadBackFault::INVALID) {
            output[60] ^= 1;
        }
        if (readBack && readBackFault == ReadBackFault::MISMATCH) {
            output[28] = output[28] == 'H' ? 'J' : 'H';
            refreshCrc(output);
        }
        length = RECORD_SIZE;
        return StorageResult::OK;
    }

    StorageResult write(CopySlot slot, const uint8_t* input,
                        size_t length) override {
        ++writes;
        lastWritten = slot;
        lastWriteLength = length;
        if (failWrite || input == nullptr || length != RECORD_SIZE) {
            return StorageResult::ERROR;
        }
        for (size_t i = 0; i < RECORD_SIZE; ++i) {
            staged[i] = input[i];
            lastWrittenBytes[i] = input[i];
        }
        return StorageResult::OK;
    }

    StorageResult commit() override {
        ++commits;
        if (failCommit) return StorageResult::ERROR;
        Cell& cell = cells[index(lastWritten)];
        cell.exists = true;
        cell.length = RECORD_SIZE;
        for (size_t i = 0; i < RECORD_SIZE; ++i) cell.bytes[i] = staged[i];
        return StorageResult::OK;
    }

private:
    static size_t index(CopySlot slot) { return slot == CopySlot::A ? 0 : 1; }
    uint8_t staged[RECORD_SIZE] = {};
};

void assertSelection(const Snapshot& value, Selection selection,
                     Outcome outcome, CopySlot slot) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(selection),
        static_cast<uint8_t>(value.selection));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(outcome),
        static_cast<uint8_t>(value.outcome));
    TEST_ASSERT_TRUE(value.hasAuthority);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(slot),
        static_cast<uint8_t>(value.authoritativeSlot));
}

void assertNoAuthority(const Snapshot& value, Selection selection,
                       Outcome outcome) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(selection),
        static_cast<uint8_t>(value.selection));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(outcome),
        static_cast<uint8_t>(value.outcome));
    TEST_ASSERT_FALSE(value.hasAuthority);
}

void assertAuthorityPreserved(const Snapshot& before, const Snapshot& after) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(before.outcome),
        static_cast<uint8_t>(after.outcome));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(before.authoritativeSlot),
        static_cast<uint8_t>(after.authoritativeSlot));
    TEST_ASSERT_EQUAL_UINT32(before.record.recordGeneration,
                             after.record.recordGeneration);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(before.bytes, after.bytes, RECORD_SIZE);
}

void testStoreStartsFailClosedBeforeRecovery() {
    Store store;
    assertNoAuthority(store.snapshot(), Selection::STORAGE_UNAVAILABLE,
                      Outcome::STORAGE_UNAVAILABLE);
}

void testVirginBothMissingIsReadOnly() {
    FakeStorage storage;
    Store store;
    assertNoAuthority(store.recover(storage), Selection::VIRGIN,
                      Outcome::VIRGIN_UNPROVISIONED);
    TEST_ASSERT_EQUAL_UINT32(2, storage.reads);
    TEST_ASSERT_EQUAL_UINT32(0, storage.writes);
    TEST_ASSERT_EQUAL_UINT32(0, storage.commits);
    TEST_ASSERT_EQUAL_UINT32(0, storage.writes);
}

void testMissingNamespaceIsVirginAndReadOnly() {
    FakeStorage storage; storage.namespaceMissing = true;
    Store store;
    assertNoAuthority(store.recover(storage), Selection::VIRGIN,
                      Outcome::VIRGIN_UNPROVISIONED);
    TEST_ASSERT_EQUAL_UINT32(0, storage.writes);
}

void testOnlyAValid() {
    FakeStorage storage; storage.seed(CopySlot::A, hub(7));
    Store store;
    const Snapshot& result = store.recover(storage);
    assertSelection(result, Selection::ONLY_A_VALID, Outcome::PROVISIONED,
                    CopySlot::A);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CopyCondition::MISSING),
        static_cast<uint8_t>(result.copyB.condition));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(storage.cells[0].bytes, result.bytes, RECORD_SIZE);
}

void testOnlyBValid() {
    FakeStorage storage; storage.seed(CopySlot::B, node(7));
    Store store;
    assertSelection(store.recover(storage), Selection::ONLY_B_VALID,
                    Outcome::PROVISIONED, CopySlot::B);
}

void testACurrentBCorrupt() {
    FakeStorage storage;
    storage.seed(CopySlot::A, hub(7));
    storage.seed(CopySlot::B, hub(8));
    storage.cells[1].bytes[60] ^= 1;
    Store store;
    const Snapshot& result = store.recover(storage);
    assertSelection(result, Selection::ONLY_A_VALID, Outcome::PROVISIONED,
                    CopySlot::A);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CopyCondition::CORRUPT),
        static_cast<uint8_t>(result.copyB.condition));
    TEST_ASSERT_EQUAL_UINT32(7, result.record.recordGeneration);
}

void testBCurrentACorrupt() {
    FakeStorage storage;
    storage.seed(CopySlot::B, node(7));
    storage.seed(CopySlot::A, node(8));
    storage.cells[0].bytes[60] ^= 1;
    Store store;
    assertSelection(store.recover(storage), Selection::ONLY_B_VALID,
                    Outcome::PROVISIONED, CopySlot::B);
}

void testTornPartialNewCopyLeavesOldAuthority() {
    FakeStorage storage;
    storage.seed(CopySlot::A, hub(7));
    storage.cells[1].exists = true;
    storage.cells[1].length = 12;
    Store store;
    const Snapshot& result = store.recover(storage);
    assertSelection(result, Selection::ONLY_A_VALID, Outcome::PROVISIONED,
                    CopySlot::A);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CopyCondition::MALFORMED),
        static_cast<uint8_t>(result.copyB.condition));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RepairResult::NOT_NEEDED),
        static_cast<uint8_t>(store.repairRedundancy(storage)));
    TEST_ASSERT_EQUAL_UINT32(0, storage.writes);
}

void testBothValidIdentical() {
    FakeStorage storage;
    storage.seed(CopySlot::A, hub(7));
    storage.seed(CopySlot::B, hub(7));
    Store store;
    assertSelection(store.recover(storage), Selection::EQUAL_IDENTICAL,
                    Outcome::PROVISIONED, CopySlot::A);
}

void testANewer() {
    FakeStorage storage;
    storage.seed(CopySlot::A, hub(8));
    storage.seed(CopySlot::B, hub(7));
    Store store;
    assertSelection(store.recover(storage), Selection::NEWEST_A,
                    Outcome::PROVISIONED, CopySlot::A);
}

void testBNewer() {
    FakeStorage storage;
    storage.seed(CopySlot::A, hub(7));
    storage.seed(CopySlot::B, hub(8));
    Store store;
    assertSelection(store.recover(storage), Selection::NEWEST_B,
                    Outcome::PROVISIONED, CopySlot::B);
}

void testGenerationSelectionNearBoundary() {
    FakeStorage storage;
    storage.seed(CopySlot::A, hub(UINT32_MAX));
    storage.seed(CopySlot::B, hub(0));
    Store store;
    assertSelection(store.recover(storage), Selection::NEWEST_B,
                    Outcome::PROVISIONED, CopySlot::B);
    storage.seed(CopySlot::A, hub(UINT32_MAX - 1));
    storage.seed(CopySlot::B, hub(UINT32_MAX));
    assertSelection(store.recover(storage), Selection::NEWEST_B,
                    Outcome::PROVISIONED, CopySlot::B);
}

void testEqualGenerationConflictingBytesInvalid() {
    FakeStorage storage;
    storage.seed(CopySlot::A, hub(7));
    storage.seed(CopySlot::B, node(7));
    Store store;
    assertNoAuthority(store.recover(storage), Selection::EQUAL_DISAGREEMENT,
                      Outcome::INVALID_PROVISIONING);
    TEST_ASSERT_EQUAL_UINT32(0, storage.writes);
}

void testHalfRangeAmbiguityInvalid() {
    FakeStorage storage;
    storage.seed(CopySlot::A, hub(0));
    storage.seed(CopySlot::B, hub(0x80000000U));
    Store store;
    assertNoAuthority(store.recover(storage), Selection::GENERATION_AMBIGUOUS,
                      Outcome::INVALID_PROVISIONING);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WriteResult::NO_AUTHORITY),
        static_cast<uint8_t>(store.writeNext(storage, pending())));
}

void testUnknownSchemaEvenWithOlderValidFailsClosedAndPreservesBytes() {
    FakeStorage storage;
    storage.seed(CopySlot::A, hub(7));
    storage.seed(CopySlot::B, hub(8));
    storage.cells[1].bytes[4] = 2;
    refreshCrc(storage.cells[1].bytes);
    uint8_t original[RECORD_SIZE] = {};
    for (size_t i = 0; i < RECORD_SIZE; ++i) {
        original[i] = storage.cells[1].bytes[i];
    }
    Store store;
    const Snapshot& result = store.recover(storage);
    assertNoAuthority(result, Selection::UNSUPPORTED_SCHEMA,
                      Outcome::INVALID_PROVISIONING);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CopyCondition::UNSUPPORTED_SCHEMA),
        static_cast<uint8_t>(result.copyB.condition));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RepairResult::NOT_NEEDED),
        static_cast<uint8_t>(store.repairRedundancy(storage)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(original, storage.cells[1].bytes, RECORD_SIZE);
    TEST_ASSERT_EQUAL_UINT32(0, storage.writes);
}

void testUnknownSchemaAloneInvalid() {
    FakeStorage storage;
    storage.seed(CopySlot::A, hub());
    storage.cells[0].bytes[4] = 2;
    refreshCrc(storage.cells[0].bytes);
    Store store;
    assertNoAuthority(store.recover(storage), Selection::UNSUPPORTED_SCHEMA,
                      Outcome::INVALID_PROVISIONING);
}

void testMalformedSizeWithoutAuthorityInvalid() {
    FakeStorage storage;
    storage.cells[0].exists = true;
    storage.cells[0].length = 63;
    Store store;
    assertNoAuthority(store.recover(storage), Selection::NEITHER_VALID,
                      Outcome::INVALID_PROVISIONING);
}

void testBadCrcWithoutAuthorityInvalid() {
    FakeStorage storage;
    storage.seed(CopySlot::A, hub());
    storage.cells[0].bytes[60] ^= 1;
    Store store;
    assertNoAuthority(store.recover(storage), Selection::NEITHER_VALID,
                      Outcome::INVALID_PROVISIONING);
}

void testUnknownDurableStateWithoutAuthorityInvalid() {
    FakeStorage storage;
    storage.seed(CopySlot::A, hub());
    storage.cells[0].bytes[16] = 0xFF;
    refreshCrc(storage.cells[0].bytes);
    Store store;
    const Snapshot& result = store.recover(storage);
    assertNoAuthority(result, Selection::NEITHER_VALID,
                      Outcome::INVALID_PROVISIONING);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::INVALID_STATE),
        static_cast<uint8_t>(result.copyA.codecResult));
}

void testStorageUnavailableFailsClosedEvenWithValidCopy() {
    FakeStorage storage;
    storage.seed(CopySlot::A, hub());
    storage.unavailable = true;
    Store store;
    assertNoAuthority(store.recover(storage), Selection::STORAGE_UNAVAILABLE,
                      Outcome::STORAGE_UNAVAILABLE);
}

void testCanonicalUnprovisionedAndPendingOutcomes() {
    FakeStorage storage;
    storage.seed(CopySlot::A, reset(7, false));
    Store store;
    assertSelection(store.recover(storage), Selection::ONLY_A_VALID,
                    Outcome::UNPROVISIONED, CopySlot::A);
    storage.seed(CopySlot::B, reset(8, true));
    assertSelection(store.recover(storage), Selection::NEWEST_B,
                    Outcome::PENDING, CopySlot::B);
    storage.seed(CopySlot::A, pending(9));
    assertSelection(store.recover(storage), Selection::NEWEST_A,
                    Outcome::PENDING, CopySlot::A);
}

void testVirginWriteTargetsAAndPublishesOnlyVerifiedRecord() {
    FakeStorage storage;
    Store store; store.recover(storage);
    Record proposal = pending(777);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WriteResult::OK),
        static_cast<uint8_t>(store.writeNext(storage, proposal)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CopySlot::A),
        static_cast<uint8_t>(storage.lastWritten));
    TEST_ASSERT_EQUAL_UINT32(RECORD_SIZE, storage.lastWriteLength);
    TEST_ASSERT_EQUAL_UINT32(1, storage.writes);
    TEST_ASSERT_EQUAL_UINT32(1, storage.commits);
    TEST_ASSERT_EQUAL_UINT32(1, store.snapshot().record.recordGeneration);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(storage.lastWrittenBytes,
        store.snapshot().bytes, RECORD_SIZE);
    assertSelection(store.snapshot(), Selection::ONLY_A_VALID,
                    Outcome::PENDING, CopySlot::A);
}

void testAActiveWritesBAndBActiveWritesA() {
    FakeStorage storage;
    storage.seed(CopySlot::A, hub(7));
    Store store; store.recover(storage);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WriteResult::OK),
        static_cast<uint8_t>(store.writeNext(storage, pending())));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CopySlot::B),
        static_cast<uint8_t>(storage.lastWritten));
    TEST_ASSERT_EQUAL_UINT32(8, store.snapshot().record.recordGeneration);
    storage.readBackFault = ReadBackFault::NONE;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WriteResult::OK),
        static_cast<uint8_t>(store.writeNext(storage, hub())));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CopySlot::A),
        static_cast<uint8_t>(storage.lastWritten));
    TEST_ASSERT_EQUAL_UINT32(9, store.snapshot().record.recordGeneration);
}

void testBActiveWritesA() {
    FakeStorage storage;
    storage.seed(CopySlot::B, hub(7));
    Store store; store.recover(storage);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WriteResult::OK),
        static_cast<uint8_t>(store.writeNext(storage, pending())));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CopySlot::A),
        static_cast<uint8_t>(storage.lastWritten));
}

void testEqualIdenticalWritesB() {
    FakeStorage storage;
    storage.seed(CopySlot::A, hub(7));
    storage.seed(CopySlot::B, hub(7));
    Store store; store.recover(storage);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WriteResult::OK),
        static_cast<uint8_t>(store.writeNext(storage, pending())));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CopySlot::B),
        static_cast<uint8_t>(storage.lastWritten));
}

void testInvalidProposalMakesNoWrite() {
    FakeStorage storage;
    storage.seed(CopySlot::A, hub(7));
    Store store; store.recover(storage);
    Record bad = pending(); bad.peerDeviceId = bad.localDeviceId;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WriteResult::INVALID_RECORD),
        static_cast<uint8_t>(store.writeNext(storage, bad)));
    TEST_ASSERT_EQUAL_UINT32(0, storage.writes);
}

void testWriteFailurePreservesAuthority() {
    FakeStorage storage;
    storage.seed(CopySlot::A, hub(7));
    Store store; const Snapshot before = store.recover(storage);
    storage.failWrite = true;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WriteResult::WRITE_FAILED),
        static_cast<uint8_t>(store.writeNext(storage, pending())));
    assertAuthorityPreserved(before, store.snapshot());
    TEST_ASSERT_EQUAL_UINT32(0, storage.commits);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WriteResult::RECOVERY_REQUIRED),
        static_cast<uint8_t>(store.writeNext(storage, pending())));
    TEST_ASSERT_EQUAL_UINT32(1, storage.writes);
}

void testCommitFailurePreservesAuthority() {
    FakeStorage storage;
    storage.seed(CopySlot::A, hub(7));
    Store store; const Snapshot before = store.recover(storage);
    storage.failCommit = true;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WriteResult::COMMIT_FAILED),
        static_cast<uint8_t>(store.writeNext(storage, pending())));
    assertAuthorityPreserved(before, store.snapshot());
    TEST_ASSERT_FALSE(storage.cells[1].exists);
}

void testEveryReadBackFailurePreservesAuthority() {
    const ReadBackFault faults[] = {
        ReadBackFault::MISSING, ReadBackFault::UNAVAILABLE,
        ReadBackFault::MALFORMED, ReadBackFault::INVALID,
        ReadBackFault::MISMATCH
    };
    const WriteResult expected[] = {
        WriteResult::READBACK_MISSING, WriteResult::READBACK_UNAVAILABLE,
        WriteResult::READBACK_MALFORMED, WriteResult::READBACK_INVALID,
        WriteResult::READBACK_MISMATCH
    };
    for (size_t i = 0; i < sizeof(faults) / sizeof(faults[0]); ++i) {
        FakeStorage storage;
        storage.seed(CopySlot::A, hub(7));
        Store store; const Snapshot before = store.recover(storage);
        storage.readBackFault = faults[i];
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected[i]),
            static_cast<uint8_t>(store.writeNext(storage, pending())));
        assertAuthorityPreserved(before, store.snapshot());
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WriteResult::RECOVERY_REQUIRED),
            static_cast<uint8_t>(store.writeNext(storage, pending())));
        TEST_ASSERT_EQUAL_UINT32(1, storage.writes);
    }
}

void testRecoveryAfterUncertainWriteFindsCommittedCandidate() {
    FakeStorage storage;
    storage.seed(CopySlot::A, hub(7));
    Store store; store.recover(storage);
    storage.readBackFault = ReadBackFault::UNAVAILABLE;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WriteResult::READBACK_UNAVAILABLE),
        static_cast<uint8_t>(store.writeNext(storage, pending())));
    TEST_ASSERT_EQUAL_UINT32(7, store.snapshot().record.recordGeneration);
    storage.readBackFault = ReadBackFault::NONE;
    assertSelection(store.recover(storage), Selection::NEWEST_B,
                    Outcome::PENDING, CopySlot::B);
    TEST_ASSERT_EQUAL_UINT32(8, store.snapshot().record.recordGeneration);
}

void testRepairMissingRedundantCopyOnce() {
    FakeStorage storage;
    storage.seed(CopySlot::A, hub(7));
    Store store; const Snapshot before = store.recover(storage);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RepairResult::OK),
        static_cast<uint8_t>(store.repairRedundancy(storage)));
    TEST_ASSERT_EQUAL_UINT32(1, storage.writes);
    TEST_ASSERT_EQUAL_UINT32(1, storage.commits);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CopySlot::B),
        static_cast<uint8_t>(storage.lastWritten));
    assertAuthorityPreserved(before, store.snapshot());
    assertSelection(store.snapshot(), Selection::EQUAL_IDENTICAL,
                    Outcome::PROVISIONED, CopySlot::A);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(before.bytes, storage.cells[1].bytes, RECORD_SIZE);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RepairResult::NOT_NEEDED),
        static_cast<uint8_t>(store.repairRedundancy(storage)));
    TEST_ASSERT_EQUAL_UINT32(1, storage.writes);
}

void testRepairCorruptRedundantCopy() {
    FakeStorage storage;
    storage.seed(CopySlot::A, hub(7));
    storage.seed(CopySlot::B, hub(8));
    storage.cells[1].bytes[60] ^= 1;
    Store store; store.recover(storage);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RepairResult::OK),
        static_cast<uint8_t>(store.repairRedundancy(storage)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CopySlot::B),
        static_cast<uint8_t>(storage.lastWritten));
    assertSelection(store.recover(storage), Selection::EQUAL_IDENTICAL,
                    Outcome::PROVISIONED, CopySlot::A);
}

void testRepairFailureLeavesAuthorityAndDoesNotLoop() {
    FakeStorage storage;
    storage.seed(CopySlot::A, hub(7));
    Store store; const Snapshot before = store.recover(storage);
    storage.failCommit = true;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RepairResult::FAILED),
        static_cast<uint8_t>(store.repairRedundancy(storage)));
    assertAuthorityPreserved(before, store.snapshot());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RepairResult::RECOVERY_REQUIRED),
        static_cast<uint8_t>(store.repairRedundancy(storage)));
    TEST_ASSERT_EQUAL_UINT32(1, storage.writes);
    storage.failCommit = false;
    store.recover(storage);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RepairResult::ALREADY_ATTEMPTED),
        static_cast<uint8_t>(store.repairRedundancy(storage)));
    TEST_ASSERT_EQUAL_UINT32(1, storage.writes);
}

void testHealthyCopiesNeverRepairOrWriteOnRepeatedRecovery() {
    FakeStorage storage;
    storage.seed(CopySlot::A, hub(7));
    storage.seed(CopySlot::B, hub(8));
    Store store;
    for (unsigned i = 0; i < 3; ++i) {
        store.recover(storage);
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RepairResult::NOT_NEEDED),
            static_cast<uint8_t>(store.repairRedundancy(storage)));
    }
    TEST_ASSERT_EQUAL_UINT32(0, storage.writes);
    TEST_ASSERT_EQUAL_UINT32(0, storage.commits);
}

void testStatusAccessAfterRecoveryIsReadOnly() {
    FakeStorage storage;
    storage.seed(CopySlot::A, hub(7));
    Store store; store.recover(storage);
    for (unsigned i = 0; i < 10; ++i) {
        TEST_ASSERT_EQUAL_UINT32(7, store.snapshot().record.recordGeneration);
    }
    TEST_ASSERT_EQUAL_UINT32(0, storage.writes);
    TEST_ASSERT_EQUAL_UINT32(0, storage.commits);
}

void testGenerationExhaustionNeverWritesOrWraps() {
    FakeStorage storage;
    storage.seed(CopySlot::A, hub(UINT32_MAX));
    Store store; store.recover(storage);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WriteResult::GENERATION_EXHAUSTED),
        static_cast<uint8_t>(store.writeNext(storage, pending())));
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX,
                             store.snapshot().record.recordGeneration);
    TEST_ASSERT_EQUAL_UINT32(0, storage.writes);
}

void testNextAfterMaxMinusOneIsMaxThenExhausted() {
    FakeStorage storage;
    storage.seed(CopySlot::A, hub(UINT32_MAX - 1));
    Store store; store.recover(storage);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WriteResult::OK),
        static_cast<uint8_t>(store.writeNext(storage, pending())));
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX,
                             store.snapshot().record.recordGeneration);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WriteResult::GENERATION_EXHAUSTED),
        static_cast<uint8_t>(store.writeNext(storage, hub())));
    TEST_ASSERT_EQUAL_UINT32(1, storage.writes);
}

void testMutationRequiresRecoveryAndUnambiguousAuthority() {
    FakeStorage storage;
    Store store;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WriteResult::NO_AUTHORITY),
        static_cast<uint8_t>(store.writeNext(storage, pending())));
    storage.seed(CopySlot::A, hub(7));
    storage.seed(CopySlot::B, node(7));
    store.recover(storage);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WriteResult::NO_AUTHORITY),
        static_cast<uint8_t>(store.writeNext(storage, pending())));
    TEST_ASSERT_EQUAL_UINT32(0, storage.writes);
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(testStoreStartsFailClosedBeforeRecovery);
    RUN_TEST(testVirginBothMissingIsReadOnly);
    RUN_TEST(testMissingNamespaceIsVirginAndReadOnly);
    RUN_TEST(testOnlyAValid);
    RUN_TEST(testOnlyBValid);
    RUN_TEST(testACurrentBCorrupt);
    RUN_TEST(testBCurrentACorrupt);
    RUN_TEST(testTornPartialNewCopyLeavesOldAuthority);
    RUN_TEST(testBothValidIdentical);
    RUN_TEST(testANewer);
    RUN_TEST(testBNewer);
    RUN_TEST(testGenerationSelectionNearBoundary);
    RUN_TEST(testEqualGenerationConflictingBytesInvalid);
    RUN_TEST(testHalfRangeAmbiguityInvalid);
    RUN_TEST(testUnknownSchemaEvenWithOlderValidFailsClosedAndPreservesBytes);
    RUN_TEST(testUnknownSchemaAloneInvalid);
    RUN_TEST(testMalformedSizeWithoutAuthorityInvalid);
    RUN_TEST(testBadCrcWithoutAuthorityInvalid);
    RUN_TEST(testUnknownDurableStateWithoutAuthorityInvalid);
    RUN_TEST(testStorageUnavailableFailsClosedEvenWithValidCopy);
    RUN_TEST(testCanonicalUnprovisionedAndPendingOutcomes);
    RUN_TEST(testVirginWriteTargetsAAndPublishesOnlyVerifiedRecord);
    RUN_TEST(testAActiveWritesBAndBActiveWritesA);
    RUN_TEST(testBActiveWritesA);
    RUN_TEST(testEqualIdenticalWritesB);
    RUN_TEST(testInvalidProposalMakesNoWrite);
    RUN_TEST(testWriteFailurePreservesAuthority);
    RUN_TEST(testCommitFailurePreservesAuthority);
    RUN_TEST(testEveryReadBackFailurePreservesAuthority);
    RUN_TEST(testRecoveryAfterUncertainWriteFindsCommittedCandidate);
    RUN_TEST(testRepairMissingRedundantCopyOnce);
    RUN_TEST(testRepairCorruptRedundantCopy);
    RUN_TEST(testRepairFailureLeavesAuthorityAndDoesNotLoop);
    RUN_TEST(testHealthyCopiesNeverRepairOrWriteOnRepeatedRecovery);
    RUN_TEST(testStatusAccessAfterRecoveryIsReadOnly);
    RUN_TEST(testGenerationExhaustionNeverWritesOrWraps);
    RUN_TEST(testNextAfterMaxMinusOneIsMaxThenExhausted);
    RUN_TEST(testMutationRequiresRecoveryAndUnambiguousAuthority);
    return UNITY_END();
}
