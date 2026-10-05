#include <unity.h>

#include <stdint.h>

#include "provisioning_service.h"

using namespace DeviceIdentity;
using namespace DeviceIdentityStore;
using namespace ProvisioningService;

namespace {

enum class Step : uint8_t {
    WRITE_PENDING, COMMIT_PENDING, READBACK_PENDING,
    WRITE_FINAL, COMMIT_FINAL, READBACK_FINAL,
    EVENT_INSPECT, EVENT_CLEAR, EVENT_VERIFY,
    SETTINGS_RESET, SETTINGS_VERIFY
};

struct Trace {
    Step steps[128] = {};
    size_t count = 0;

    void add(Step step) {
        TEST_ASSERT_LESS_THAN_UINT32(128, count);
        steps[count++] = step;
    }

    size_t first(Step wanted) const {
        for (size_t i = 0; i < count; ++i) {
            if (steps[i] == wanted) return i;
        }
        TEST_FAIL_MESSAGE("Expected trace step missing");
        return count;
    }

    bool contains(Step wanted) const {
        for (size_t i = 0; i < count; ++i) {
            if (steps[i] == wanted) return true;
        }
        return false;
    }
};

Record identity(Role role = Role::HUB, uint32_t recordGeneration = 7,
                uint32_t provisioningGeneration = 4) {
    Record value = {};
    value.recordGeneration = recordGeneration;
    value.provisioningGeneration = provisioningGeneration;
    value.state = DurableState::PROVISIONED;
    value.transaction = TransactionKind::NONE;
    value.role = role;
    value.localDeviceId = role == Role::HUB ? 1 : 0x10;
    value.peerDeviceId = role == Role::HUB ? 0x10 : 1;
    value.hardwareProfile = HardwareProfile::HELTEC_V4;
    value.capabilityProfile = CapabilityProfile::HELTEC_V4_BASE;
    value.networkId = 0x11223344U;
    value.labelLength = 3;
    value.label[0] = 'R'; value.label[1] = 'E'; value.label[2] = 'D';
    return value;
}

Proposal proposal(Role role = Role::HUB) {
    const Record record = identity(role);
    Proposal value = {};
    value.role = record.role;
    value.localDeviceId = record.localDeviceId;
    value.peerDeviceId = record.peerDeviceId;
    value.hardwareProfile = record.hardwareProfile;
    value.capabilityProfile = record.capabilityProfile;
    value.networkId = record.networkId;
    value.labelLength = record.labelLength;
    for (size_t i = 0; i < LABEL_SIZE; ++i) value.label[i] = record.label[i];
    return value;
}

void crc(uint8_t* bytes) {
    uint32_t value = 0;
    TEST_ASSERT_TRUE(EventRecords::crc32IsoHdlc(bytes, 60, value));
    EventRecords::writeUint32Le(bytes + 60, value);
}

enum class ReadFault : uint8_t { NONE, MISSING, UNAVAILABLE, MALFORMED, INVALID, MISMATCH };

struct Cell {
    bool exists = false;
    size_t length = 0;
    uint8_t bytes[RECORD_SIZE] = {};
};

class FakeIdentityStorage final : public Storage {
public:
    explicit FakeIdentityStorage(Trace& trace) : trace_(trace) {}
    Cell cells[2];
    bool unavailable = false;
    size_t failWriteAt = 0;
    size_t failCommitAt = 0;
    size_t readFaultAtWrite = 0;
    ReadFault readFault = ReadFault::NONE;
    size_t writes = 0;
    size_t commits = 0;
    size_t reads = 0;
    uint8_t pendingBytes[RECORD_SIZE] = {};
    uint8_t finalBytes[RECORD_SIZE] = {};
    bool capturedPending = false;
    bool capturedFinal = false;

    void seed(CopySlot slot, const Record& record) {
        Cell& cell = cells[index(slot)];
        cell.exists = true;
        cell.length = RECORD_SIZE;
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::OK),
            static_cast<uint8_t>(encodeRecord(record, cell.bytes, RECORD_SIZE)));
    }

    StorageResult read(CopySlot slot, uint8_t* output,
                       size_t capacity, size_t& length) override {
        ++reads;
        length = 0;
        if (unavailable) return StorageResult::UNAVAILABLE;
        if (readbackDue_ && slot == writtenSlot_) {
            readbackDue_ = false;
            trace_.add(writtenPending_ ? Step::READBACK_PENDING
                                       : Step::READBACK_FINAL);
            if (readFaultAtWrite == writes) {
                if (readFault == ReadFault::MISSING) return StorageResult::MISSING;
                if (readFault == ReadFault::UNAVAILABLE) {
                    return StorageResult::UNAVAILABLE;
                }
                if (readFault == ReadFault::MALFORMED) {
                    return StorageResult::MALFORMED;
                }
                activeReadFault_ = readFault;
            }
        }
        const Cell& cell = cells[index(slot)];
        if (!cell.exists) return StorageResult::MISSING;
        if (cell.length != RECORD_SIZE || capacity < RECORD_SIZE) {
            return StorageResult::MALFORMED;
        }
        for (size_t i = 0; i < RECORD_SIZE; ++i) output[i] = cell.bytes[i];
        if (activeReadFault_ == ReadFault::INVALID) output[60] ^= 1;
        if (activeReadFault_ == ReadFault::MISMATCH) {
            output[8] ^= 1;
            crc(output);
        }
        activeReadFault_ = ReadFault::NONE;
        length = RECORD_SIZE;
        return StorageResult::OK;
    }

    StorageResult write(CopySlot slot, const uint8_t* input,
                        size_t length) override {
        ++writes;
        writtenSlot_ = slot;
        writtenPending_ = input != nullptr && length == RECORD_SIZE &&
            input[16] == static_cast<uint8_t>(DurableState::PENDING);
        trace_.add(writtenPending_ ? Step::WRITE_PENDING : Step::WRITE_FINAL);
        if (failWriteAt == writes || input == nullptr || length != RECORD_SIZE) {
            return StorageResult::ERROR;
        }
        for (size_t i = 0; i < RECORD_SIZE; ++i) staged_[i] = input[i];
        if (writtenPending_) {
            capturedPending = true;
            for (size_t i = 0; i < RECORD_SIZE; ++i) pendingBytes[i] = input[i];
        } else {
            capturedFinal = true;
            for (size_t i = 0; i < RECORD_SIZE; ++i) finalBytes[i] = input[i];
        }
        return StorageResult::OK;
    }

    StorageResult commit() override {
        ++commits;
        trace_.add(writtenPending_ ? Step::COMMIT_PENDING : Step::COMMIT_FINAL);
        if (failCommitAt == commits) return StorageResult::ERROR;
        Cell& cell = cells[index(writtenSlot_)];
        cell.exists = true;
        cell.length = RECORD_SIZE;
        for (size_t i = 0; i < RECORD_SIZE; ++i) cell.bytes[i] = staged_[i];
        readbackDue_ = true;
        return StorageResult::OK;
    }

private:
    static size_t index(CopySlot slot) { return slot == CopySlot::A ? 0 : 1; }
    Trace& trace_;
    CopySlot writtenSlot_ = CopySlot::A;
    bool writtenPending_ = false;
    bool readbackDue_ = false;
    ReadFault activeReadFault_ = ReadFault::NONE;
    uint8_t staged_[RECORD_SIZE] = {};
};

class FakeEvents final : public EventDomain {
public:
    explicit FakeEvents(Trace& trace) : trace_(trace) {}
    bool present = false;
    bool failInspect = false;
    bool failClear = false;
    bool failVerify = false;
    size_t clears = 0;
    size_t verifies = 0;

    Presence inspect() override {
        trace_.add(Step::EVENT_INSPECT);
        if (failInspect) return Presence::UNAVAILABLE;
        return present ? Presence::PRESENT : Presence::EMPTY;
    }
    bool clearAll() override {
        trace_.add(Step::EVENT_CLEAR);
        ++clears;
        if (failClear) return false;
        present = false;
        return true;
    }
    bool verifyEmpty() override {
        trace_.add(Step::EVENT_VERIFY);
        ++verifies;
        return !present && !failVerify;
    }

private:
    Trace& trace_;
};

class FakeSettings final : public SettingsReset {
public:
    explicit FakeSettings(Trace& trace) : trace_(trace) {}
    bool defaults = false;
    bool failReset = false;
    bool failVerify = false;
    size_t resets = 0;
    size_t verifies = 0;

    bool resetToDefaults() override {
        trace_.add(Step::SETTINGS_RESET);
        ++resets;
        if (failReset) return false;
        defaults = true;
        return true;
    }
    bool verifyDefaults() override {
        trace_.add(Step::SETTINGS_VERIFY);
        ++verifies;
        return defaults && !failVerify;
    }

private:
    Trace& trace_;
};

struct Fixture {
    Trace trace;
    FakeIdentityStorage storage;
    FakeEvents events;
    FakeSettings settings;
    Store store;
    Controller service;

    Fixture() : trace(), storage(trace), events(trace), settings(trace),
                store(), service(store, storage, events, settings) {}
};

void assertCode(ResultCode expected, Result actual) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected),
                            static_cast<uint8_t>(actual.code));
}

void assertAuthority(const Store& store, Outcome outcome, DurableState state,
                     uint32_t provisioningGeneration) {
    const Snapshot& snapshot = store.snapshot();
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(outcome),
                            static_cast<uint8_t>(snapshot.outcome));
    TEST_ASSERT_TRUE(snapshot.hasAuthority);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(state),
                            static_cast<uint8_t>(snapshot.record.state));
    TEST_ASSERT_EQUAL_UINT32(provisioningGeneration,
                             snapshot.record.provisioningGeneration);
}

void assertPendingBeforeDestruction(const Trace& trace) {
    TEST_ASSERT_LESS_THAN_UINT32(trace.first(Step::EVENT_CLEAR),
                                 trace.first(Step::READBACK_PENDING));
}

void assertExactPendingTarget(const FakeIdentityStorage& storage) {
    TEST_ASSERT_TRUE(storage.capturedPending);
    TEST_ASSERT_TRUE(storage.capturedFinal);
    Record pendingRecord = {};
    Record finalRecord = {};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::OK),
        static_cast<uint8_t>(decodeRecord(storage.pendingBytes,
            RECORD_SIZE, pendingRecord)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::OK),
        static_cast<uint8_t>(decodeRecord(storage.finalBytes,
            RECORD_SIZE, finalRecord)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DurableState::PENDING),
                            static_cast<uint8_t>(pendingRecord.state));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DurableState::PROVISIONED),
                            static_cast<uint8_t>(finalRecord.state));
    TEST_ASSERT_EQUAL_UINT32(pendingRecord.provisioningGeneration,
                             finalRecord.provisioningGeneration);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(storage.pendingBytes + 18,
                                  storage.finalBytes + 18, 42);
}

void testVirginHubProvisionOrderingAndExactTarget() {
    Fixture f;
    f.events.present = true;
    assertCode(ResultCode::ACCEPTED, f.service.prepareApply(proposal(), true));
    TEST_ASSERT_EQUAL_UINT32(0, f.storage.writes);
    assertCode(ResultCode::APPLIED_REBOOT_REQUIRED, f.service.execute());
    assertAuthority(f.store, Outcome::PROVISIONED, DurableState::PROVISIONED, 1);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Role::HUB),
        static_cast<uint8_t>(f.store.snapshot().record.role));
    TEST_ASSERT_EQUAL_UINT32(2, f.storage.writes);
    TEST_ASSERT_EQUAL_UINT32(2, f.storage.commits);
    TEST_ASSERT_EQUAL_UINT32(0, f.settings.resets);
    TEST_ASSERT_FALSE(f.events.present);
    assertPendingBeforeDestruction(f.trace);
    TEST_ASSERT_LESS_THAN_UINT32(f.trace.first(Step::WRITE_FINAL),
                                 f.trace.first(Step::EVENT_VERIFY));
    assertExactPendingTarget(f.storage);
}

void testVirginNodeProvisionStartsAtGenerationOne() {
    Fixture f;
    assertCode(ResultCode::ACCEPTED,
               f.service.prepareApply(proposal(Role::NODE), false));
    assertCode(ResultCode::APPLIED_REBOOT_REQUIRED, f.service.execute());
    assertAuthority(f.store, Outcome::PROVISIONED, DurableState::PROVISIONED, 1);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Role::NODE),
        static_cast<uint8_t>(f.store.snapshot().record.role));
    TEST_ASSERT_EQUAL_UINT32(0, f.events.clears);
    TEST_ASSERT_EQUAL_UINT32(1, f.events.verifies);
    assertExactPendingTarget(f.storage);
}

void testInitialProvisionDoesNotCreateUnprovisionedRecord() {
    Fixture f;
    assertCode(ResultCode::ACCEPTED, f.service.prepareApply(proposal(), false));
    assertCode(ResultCode::APPLIED_REBOOT_REQUIRED, f.service.execute());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DurableState::PENDING),
                            f.storage.cells[0].bytes[16]);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DurableState::PROVISIONED),
                            f.storage.cells[1].bytes[16]);
}

void runReprovisionChange(const Record& oldIdentity,
                          const Proposal& changed) {
    Fixture f;
    f.storage.seed(CopySlot::A, oldIdentity);
    f.events.present = true;
    assertCode(ResultCode::ACCEPTED, f.service.prepareApply(changed, true));
    assertCode(ResultCode::APPLIED_REBOOT_REQUIRED, f.service.execute());
    assertAuthority(f.store, Outcome::PROVISIONED, DurableState::PROVISIONED,
                    oldIdentity.provisioningGeneration + 1U);
    TEST_ASSERT_EQUAL_UINT32(2, f.storage.writes);
    TEST_ASSERT_EQUAL_UINT32(1, f.events.clears);
    TEST_ASSERT_EQUAL_UINT32(0, f.settings.resets);
    assertExactPendingTarget(f.storage);
}

void testHubToChangedHub() {
    Proposal changed = proposal(); changed.label[0] = 'B';
    runReprovisionChange(identity(), changed);
}

void testHubToNode() {
    runReprovisionChange(identity(Role::HUB), proposal(Role::NODE));
}

void testNodeToHub() {
    runReprovisionChange(identity(Role::NODE), proposal(Role::HUB));
}

void testLocalIdChange() {
    Proposal changed = proposal(); changed.localDeviceId = 2;
    runReprovisionChange(identity(), changed);
}

void testPeerIdChange() {
    Proposal changed = proposal(); changed.peerDeviceId = 0x11;
    runReprovisionChange(identity(), changed);
}

void testNetworkIdChange() {
    Proposal changed = proposal(); changed.networkId += 1;
    runReprovisionChange(identity(), changed);
}

void testLabelChange() {
    Proposal changed = proposal(); changed.label[1] = 'X';
    runReprovisionChange(identity(), changed);
}

void testUnsupportedProfilesRejectedBeforeMutation() {
    Fixture f;
    Proposal bad = proposal();
    bad.hardwareProfile = static_cast<HardwareProfile>(2);
    assertCode(ResultCode::INVALID_REQUEST, f.service.prepareApply(bad, true));
    bad = proposal();
    bad.capabilityProfile = static_cast<CapabilityProfile>(2);
    assertCode(ResultCode::INVALID_REQUEST, f.service.prepareApply(bad, true));
    TEST_ASSERT_EQUAL_UINT32(0, f.storage.writes);
}

void testInvalidTargetIdsAndLabelRejected() {
    Fixture f;
    Proposal bad = proposal(); bad.peerDeviceId = bad.localDeviceId;
    assertCode(ResultCode::INVALID_REQUEST, f.service.prepareApply(bad, true));
    bad = proposal(); bad.label[0] = 0x1F;
    assertCode(ResultCode::INVALID_REQUEST, f.service.prepareApply(bad, true));
    TEST_ASSERT_EQUAL_UINT32(0, f.storage.writes);
}

void testUnchangedIgnoresRecordGenerationAndDoesNothing() {
    Fixture f;
    f.storage.seed(CopySlot::A, identity(Role::HUB, 777, 4));
    f.events.present = true;
    assertCode(ResultCode::UNCHANGED, f.service.prepareApply(proposal(), false));
    TEST_ASSERT_EQUAL_UINT32(0, f.storage.writes);
    TEST_ASSERT_EQUAL_UINT32(0, f.events.clears);
    TEST_ASSERT_EQUAL_UINT32(0, f.settings.resets);
    TEST_ASSERT_EQUAL_UINT32(4, f.store.snapshot().record.provisioningGeneration);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Phase::IDLE),
                            static_cast<uint8_t>(f.service.phase()));
}

void testAckRequiredBeforeAnyWriteOrErase() {
    Fixture f;
    f.storage.seed(CopySlot::A, identity());
    f.events.present = true;
    Proposal changed = proposal(); changed.label[0] = 'B';
    assertCode(ResultCode::ACK_EVENT_ERASURE_REQUIRED,
               f.service.prepareApply(changed, false));
    assertCode(ResultCode::ACK_EVENT_ERASURE_REQUIRED,
               f.service.prepareFullReset(false));
    TEST_ASSERT_EQUAL_UINT32(0, f.storage.writes);
    TEST_ASSERT_EQUAL_UINT32(0, f.events.clears);
    TEST_ASSERT_EQUAL_UINT32(0, f.settings.resets);
}

void testAckAllowsPresentEventDomain() {
    Fixture f;
    f.events.present = true;
    assertCode(ResultCode::ACCEPTED, f.service.prepareApply(proposal(), true));
    assertCode(ResultCode::APPLIED_REBOOT_REQUIRED, f.service.execute());
    TEST_ASSERT_EQUAL_UINT32(1, f.events.clears);
}

void testEmptyEventDomainNeedsNoAckAndNoClear() {
    Fixture f;
    assertCode(ResultCode::ACCEPTED, f.service.prepareApply(proposal(), false));
    assertCode(ResultCode::APPLIED_REBOOT_REQUIRED, f.service.execute());
    TEST_ASSERT_EQUAL_UINT32(0, f.events.clears);
    TEST_ASSERT_EQUAL_UINT32(1, f.events.verifies);
}

void testEventInspectionFailurePreventsPending() {
    Fixture f;
    f.events.failInspect = true;
    assertCode(ResultCode::EVENT_INSPECTION_FAILURE,
               f.service.prepareApply(proposal(), true));
    TEST_ASSERT_EQUAL_UINT32(0, f.storage.writes);
}

void testLateEventPresenceWithoutAckBlocksPending() {
    Fixture f;
    assertCode(ResultCode::ACCEPTED, f.service.prepareApply(proposal(), false));
    f.events.present = true;
    assertCode(ResultCode::ACK_EVENT_ERASURE_REQUIRED, f.service.execute());
    TEST_ASSERT_EQUAL_UINT32(0, f.storage.writes);
    TEST_ASSERT_EQUAL_UINT32(0, f.events.clears);
}

void testPendingWriteFailureLeavesOldAuthorityAndEvents() {
    Fixture f;
    f.storage.seed(CopySlot::A, identity());
    f.events.present = true;
    Proposal changed = proposal(); changed.label[0] = 'B';
    assertCode(ResultCode::ACCEPTED, f.service.prepareApply(changed, true));
    f.storage.failWriteAt = 1;
    assertCode(ResultCode::IDENTITY_STORE_FAILURE, f.service.execute());
    assertAuthority(f.store, Outcome::PROVISIONED, DurableState::PROVISIONED, 4);
    TEST_ASSERT_TRUE(f.events.present);
    TEST_ASSERT_EQUAL_UINT32(0, f.events.clears);
    TEST_ASSERT_EQUAL_UINT32(0, f.settings.resets);
    TEST_ASSERT_EQUAL_UINT32(1, f.storage.writes);
    TEST_ASSERT_EQUAL_UINT32(0, f.storage.commits);
}

void testPendingCommitFailureLeavesOldAuthorityAndEvents() {
    Fixture f;
    f.storage.seed(CopySlot::A, identity());
    f.events.present = true;
    Proposal changed = proposal(); changed.label[0] = 'B';
    assertCode(ResultCode::ACCEPTED, f.service.prepareApply(changed, true));
    f.storage.failCommitAt = 1;
    assertCode(ResultCode::IDENTITY_STORE_FAILURE, f.service.execute());
    assertAuthority(f.store, Outcome::PROVISIONED, DurableState::PROVISIONED, 4);
    TEST_ASSERT_TRUE(f.events.present);
    TEST_ASSERT_EQUAL_UINT32(0, f.events.clears);
    TEST_ASSERT_EQUAL_UINT32(0, f.settings.resets);
}

void testEveryPendingReadbackFaultLeavesOldRamAuthorityAndEvents() {
    const ReadFault faults[] = {
        ReadFault::MISSING, ReadFault::UNAVAILABLE,
        ReadFault::MALFORMED, ReadFault::INVALID, ReadFault::MISMATCH
    };
    for (size_t i = 0; i < sizeof(faults) / sizeof(faults[0]); ++i) {
        Fixture f;
        f.storage.seed(CopySlot::A, identity());
        f.events.present = true;
        Proposal changed = proposal(); changed.label[0] = 'B';
        assertCode(ResultCode::ACCEPTED, f.service.prepareApply(changed, true));
        f.storage.readFaultAtWrite = 1;
        f.storage.readFault = faults[i];
        assertCode(ResultCode::IDENTITY_STORE_FAILURE, f.service.execute());
        assertAuthority(f.store, Outcome::PROVISIONED,
                        DurableState::PROVISIONED, 4);
        TEST_ASSERT_TRUE(f.events.present);
        TEST_ASSERT_EQUAL_UINT32(0, f.events.clears);
        TEST_ASSERT_EQUAL_UINT32(0, f.settings.resets);
        TEST_ASSERT_EQUAL_UINT32(1, f.storage.writes);
    }
}

void testEventClearFailurePreservesPendingAndRecovers() {
    Fixture f;
    f.events.present = true;
    f.events.failClear = true;
    assertCode(ResultCode::ACCEPTED, f.service.prepareApply(proposal(), true));
    assertCode(ResultCode::EVENT_CLEAR_FAILURE, f.service.execute());
    assertAuthority(f.store, Outcome::PENDING, DurableState::PENDING, 1);
    TEST_ASSERT_EQUAL_UINT32(1, f.storage.writes);
    TEST_ASSERT_TRUE(f.events.present);
    assertPendingBeforeDestruction(f.trace);
    f.events.failClear = false;
    Store rebootStore;
    Controller reboot(rebootStore, f.storage, f.events, f.settings);
    assertCode(ResultCode::APPLIED_REBOOT_REQUIRED, reboot.recoverPending());
    assertAuthority(rebootStore, Outcome::PROVISIONED,
                    DurableState::PROVISIONED, 1);
    assertExactPendingTarget(f.storage);
}

void testEventVerificationFailurePreservesPendingAndRecovers() {
    Fixture f;
    f.events.present = true;
    f.events.failVerify = true;
    assertCode(ResultCode::ACCEPTED, f.service.prepareApply(proposal(), true));
    assertCode(ResultCode::EVENT_VERIFY_FAILURE, f.service.execute());
    assertAuthority(f.store, Outcome::PENDING, DurableState::PENDING, 1);
    TEST_ASSERT_FALSE(f.events.present);
    TEST_ASSERT_EQUAL_UINT32(1, f.storage.writes);
    f.events.failVerify = false;
    Store rebootStore;
    Controller reboot(rebootStore, f.storage, f.events, f.settings);
    assertCode(ResultCode::APPLIED_REBOOT_REQUIRED, reboot.recoverPending());
    TEST_ASSERT_EQUAL_UINT32(1, f.events.clears);
    assertAuthority(rebootStore, Outcome::PROVISIONED,
                    DurableState::PROVISIONED, 1);
}

void testEveryFinalWritePhaseFailureKeepsPendingInRam() {
    for (unsigned fault = 0; fault < 7; ++fault) {
        Fixture f;
        f.events.present = true;
        assertCode(ResultCode::ACCEPTED, f.service.prepareApply(proposal(), true));
        if (fault == 0) f.storage.failWriteAt = 2;
        if (fault == 1) f.storage.failCommitAt = 2;
        if (fault >= 2) {
            const ReadFault modes[] = {ReadFault::MISSING,
                ReadFault::UNAVAILABLE, ReadFault::MALFORMED,
                ReadFault::INVALID, ReadFault::MISMATCH};
            f.storage.readFaultAtWrite = 2;
            f.storage.readFault = modes[fault - 2];
        }
        assertCode(ResultCode::IDENTITY_STORE_FAILURE, f.service.execute());
        assertAuthority(f.store, Outcome::PENDING, DurableState::PENDING, 1);
        TEST_ASSERT_FALSE(f.events.present);
        TEST_ASSERT_EQUAL_UINT32(1, f.events.verifies);
        TEST_ASSERT_EQUAL_UINT32(0, f.settings.resets);
        TEST_ASSERT_EQUAL_UINT32(2, f.storage.writes);
    }
}

void testRebootAfterFailedFinalCommitCompletesPending() {
    Fixture f;
    f.storage.failCommitAt = 2;
    assertCode(ResultCode::ACCEPTED, f.service.prepareApply(proposal(), false));
    assertCode(ResultCode::IDENTITY_STORE_FAILURE, f.service.execute());
    assertAuthority(f.store, Outcome::PENDING, DurableState::PENDING, 1);
    f.storage.failCommitAt = 0;
    Store rebootStore;
    Controller reboot(rebootStore, f.storage, f.events, f.settings);
    assertCode(ResultCode::APPLIED_REBOOT_REQUIRED, reboot.recoverPending());
    assertAuthority(rebootStore, Outcome::PROVISIONED,
                    DurableState::PROVISIONED, 1);
}

void testRebootAfterFailedFinalReadbackRecognizesCommittedFinal() {
    Fixture f;
    f.storage.readFaultAtWrite = 2;
    f.storage.readFault = ReadFault::UNAVAILABLE;
    assertCode(ResultCode::ACCEPTED, f.service.prepareApply(proposal(), false));
    assertCode(ResultCode::IDENTITY_STORE_FAILURE, f.service.execute());
    assertAuthority(f.store, Outcome::PENDING, DurableState::PENDING, 1);
    Store rebootStore;
    Controller reboot(rebootStore, f.storage, f.events, f.settings);
    assertCode(ResultCode::NO_RECOVERY_NEEDED, reboot.recoverPending());
    assertAuthority(rebootStore, Outcome::PROVISIONED,
                    DurableState::PROVISIONED, 1);
    TEST_ASSERT_EQUAL_UINT32(2, f.storage.writes);
}

void testRepeatedApplyRecoveryDoesNotWriteAgain() {
    Fixture f;
    f.events.present = true;
    f.events.failClear = true;
    assertCode(ResultCode::ACCEPTED, f.service.prepareApply(proposal(), true));
    assertCode(ResultCode::EVENT_CLEAR_FAILURE, f.service.execute());
    f.events.failClear = false;
    Store rebootStore;
    Controller reboot(rebootStore, f.storage, f.events, f.settings);
    assertCode(ResultCode::APPLIED_REBOOT_REQUIRED, reboot.recoverPending());
    const size_t writes = f.storage.writes;
    const size_t clears = f.events.clears;
    assertCode(ResultCode::NO_RECOVERY_NEEDED, reboot.recoverPending());
    TEST_ASSERT_EQUAL_UINT32(writes, f.storage.writes);
    TEST_ASSERT_EQUAL_UINT32(clears, f.events.clears);
}

void testPendingCannotStartFreshApply() {
    Fixture f;
    Record durablePending = identity();
    durablePending.state = DurableState::PENDING;
    durablePending.transaction = TransactionKind::APPLY;
    f.storage.seed(CopySlot::A, durablePending);
    assertCode(ResultCode::RECOVERY_REQUIRED,
               f.service.prepareApply(proposal(), true));
    TEST_ASSERT_EQUAL_UINT32(0, f.storage.writes);
}

void testFullResetOrderingAndCanonicalFinal() {
    Fixture f;
    f.storage.seed(CopySlot::A, identity());
    f.events.present = true;
    assertCode(ResultCode::ACCEPTED, f.service.prepareFullReset(true));
    assertCode(ResultCode::RESET_REBOOT_REQUIRED, f.service.execute());
    assertAuthority(f.store, Outcome::UNPROVISIONED,
                    DurableState::UNPROVISIONED, 0);
    TEST_ASSERT_FALSE(f.events.present);
    TEST_ASSERT_TRUE(f.settings.defaults);
    TEST_ASSERT_EQUAL_UINT32(1, f.settings.resets);
    TEST_ASSERT_EQUAL_UINT32(1, f.events.clears);
    TEST_ASSERT_EQUAL_UINT32(2, f.storage.writes);
    TEST_ASSERT_LESS_THAN_UINT32(f.trace.first(Step::SETTINGS_RESET),
                                 f.trace.first(Step::READBACK_PENDING));
    TEST_ASSERT_LESS_THAN_UINT32(f.trace.first(Step::EVENT_CLEAR),
                                 f.trace.first(Step::SETTINGS_VERIFY));
    TEST_ASSERT_LESS_THAN_UINT32(f.trace.first(Step::WRITE_FINAL),
                                 f.trace.first(Step::EVENT_VERIFY));
    const Record& final = f.store.snapshot().record;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TransactionKind::NONE),
                            static_cast<uint8_t>(final.transaction));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Role::NONE),
                            static_cast<uint8_t>(final.role));
    for (size_t i = 12; i < 44; ++i) {
        if (i == 16) {
            TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DurableState::UNPROVISIONED),
                                    f.store.snapshot().bytes[i]);
        } else {
            TEST_ASSERT_EQUAL_UINT8(0, f.store.snapshot().bytes[i]);
        }
    }
}

void testFullResetPendingWriteAndCommitFaultsDoNotDestroy() {
    for (unsigned fault = 0; fault < 2; ++fault) {
        Fixture f;
        f.storage.seed(CopySlot::A, identity());
        f.events.present = true;
        assertCode(ResultCode::ACCEPTED, f.service.prepareFullReset(true));
        if (fault == 0) f.storage.failWriteAt = 1;
        else f.storage.failCommitAt = 1;
        assertCode(ResultCode::IDENTITY_STORE_FAILURE, f.service.execute());
        assertAuthority(f.store, Outcome::PROVISIONED,
                        DurableState::PROVISIONED, 4);
        TEST_ASSERT_FALSE(f.trace.contains(Step::SETTINGS_RESET));
        TEST_ASSERT_FALSE(f.trace.contains(Step::EVENT_CLEAR));
        TEST_ASSERT_TRUE(f.events.present);
    }
}

void testFullResetPendingReadbackFaultsDoNotDestroy() {
    const ReadFault faults[] = {
        ReadFault::MISSING, ReadFault::UNAVAILABLE,
        ReadFault::MALFORMED, ReadFault::INVALID, ReadFault::MISMATCH
    };
    for (size_t i = 0; i < sizeof(faults) / sizeof(faults[0]); ++i) {
        Fixture f;
        f.storage.seed(CopySlot::A, identity());
        f.events.present = true;
        assertCode(ResultCode::ACCEPTED, f.service.prepareFullReset(true));
        f.storage.readFaultAtWrite = 1;
        f.storage.readFault = faults[i];
        assertCode(ResultCode::IDENTITY_STORE_FAILURE, f.service.execute());
        assertAuthority(f.store, Outcome::PROVISIONED,
                        DurableState::PROVISIONED, 4);
        TEST_ASSERT_EQUAL_UINT32(0, f.settings.resets);
        TEST_ASSERT_EQUAL_UINT32(0, f.events.clears);
    }
}

void testSettingsResetFailureLeavesPending() {
    Fixture f;
    f.storage.seed(CopySlot::A, identity());
    f.events.present = true;
    f.settings.failReset = true;
    assertCode(ResultCode::ACCEPTED, f.service.prepareFullReset(true));
    assertCode(ResultCode::SETTINGS_RESET_FAILURE, f.service.execute());
    assertAuthority(f.store, Outcome::PENDING, DurableState::PENDING, 0);
    TEST_ASSERT_EQUAL_UINT32(1, f.storage.writes);
    TEST_ASSERT_EQUAL_UINT32(0, f.events.clears);
    TEST_ASSERT_TRUE(f.events.present);
}

void testSettingsVerificationFailureLeavesPending() {
    Fixture f;
    f.storage.seed(CopySlot::A, identity());
    f.events.present = true;
    f.settings.failVerify = true;
    assertCode(ResultCode::ACCEPTED, f.service.prepareFullReset(true));
    assertCode(ResultCode::SETTINGS_VERIFY_FAILURE, f.service.execute());
    assertAuthority(f.store, Outcome::PENDING, DurableState::PENDING, 0);
    TEST_ASSERT_TRUE(f.settings.defaults);
    TEST_ASSERT_EQUAL_UINT32(0, f.events.clears);
}

void testResetEventClearAndVerifyFailuresLeavePending() {
    for (unsigned fault = 0; fault < 2; ++fault) {
        Fixture f;
        f.storage.seed(CopySlot::A, identity());
        f.events.present = true;
        if (fault == 0) f.events.failClear = true;
        else f.events.failVerify = true;
        assertCode(ResultCode::ACCEPTED, f.service.prepareFullReset(true));
        assertCode(fault == 0 ? ResultCode::EVENT_CLEAR_FAILURE
                              : ResultCode::EVENT_VERIFY_FAILURE,
                   f.service.execute());
        assertAuthority(f.store, Outcome::PENDING, DurableState::PENDING, 0);
        TEST_ASSERT_TRUE(f.settings.defaults);
        TEST_ASSERT_EQUAL_UINT32(1, f.settings.resets);
        TEST_ASSERT_EQUAL_UINT32(1, f.storage.writes);
    }
}

void testResetFinalWriteCommitReadbackFailuresLeavePending() {
    for (unsigned fault = 0; fault < 7; ++fault) {
        Fixture f;
        f.storage.seed(CopySlot::A, identity());
        f.events.present = true;
        assertCode(ResultCode::ACCEPTED, f.service.prepareFullReset(true));
        if (fault == 0) f.storage.failWriteAt = 2;
        if (fault == 1) f.storage.failCommitAt = 2;
        if (fault >= 2) {
            const ReadFault modes[] = {ReadFault::MISSING,
                ReadFault::UNAVAILABLE, ReadFault::MALFORMED,
                ReadFault::INVALID, ReadFault::MISMATCH};
            f.storage.readFaultAtWrite = 2;
            f.storage.readFault = modes[fault - 2];
        }
        assertCode(ResultCode::IDENTITY_STORE_FAILURE, f.service.execute());
        assertAuthority(f.store, Outcome::PENDING, DurableState::PENDING, 0);
        TEST_ASSERT_TRUE(f.settings.defaults);
        TEST_ASSERT_FALSE(f.events.present);
        TEST_ASSERT_EQUAL_UINT32(2, f.storage.writes);
    }
}

void testResetRecoveryAfterEveryIntermediateFailure() {
    for (unsigned fault = 0; fault < 5; ++fault) {
        Fixture f;
        f.storage.seed(CopySlot::A, identity());
        f.events.present = true;
        assertCode(ResultCode::ACCEPTED, f.service.prepareFullReset(true));
        if (fault == 0) f.settings.failReset = true;
        if (fault == 1) f.settings.failVerify = true;
        if (fault == 2) f.events.failClear = true;
        if (fault == 3) f.events.failVerify = true;
        if (fault == 4) f.storage.failCommitAt = 2;
        const Result failed = f.service.execute();
        TEST_ASSERT_NOT_EQUAL(static_cast<uint8_t>(ResultCode::RESET_REBOOT_REQUIRED),
                              static_cast<uint8_t>(failed.code));
        assertAuthority(f.store, Outcome::PENDING, DurableState::PENDING, 0);
        f.settings.failReset = false;
        f.settings.failVerify = false;
        f.events.failClear = false;
        f.events.failVerify = false;
        f.storage.failCommitAt = 0;
        Store rebootStore;
        Controller reboot(rebootStore, f.storage, f.events, f.settings);
        assertCode(ResultCode::RESET_REBOOT_REQUIRED, reboot.recoverPending());
        assertAuthority(rebootStore, Outcome::UNPROVISIONED,
                        DurableState::UNPROVISIONED, 0);
        TEST_ASSERT_TRUE(f.settings.defaults);
        TEST_ASSERT_FALSE(f.events.present);
    }
}

void testRepeatedResetRecoveryIsBoundedAndIdempotent() {
    Fixture f;
    f.storage.seed(CopySlot::A, identity());
    f.settings.failReset = true;
    assertCode(ResultCode::ACCEPTED, f.service.prepareFullReset(false));
    assertCode(ResultCode::SETTINGS_RESET_FAILURE, f.service.execute());
    f.settings.failReset = false;
    Store rebootStore;
    Controller reboot(rebootStore, f.storage, f.events, f.settings);
    assertCode(ResultCode::RESET_REBOOT_REQUIRED, reboot.recoverPending());
    const size_t writes = f.storage.writes;
    const size_t resets = f.settings.resets;
    assertCode(ResultCode::NO_RECOVERY_NEEDED, reboot.recoverPending());
    TEST_ASSERT_EQUAL_UINT32(writes, f.storage.writes);
    TEST_ASSERT_EQUAL_UINT32(resets, f.settings.resets);
}

void testFullResetOnUnprovisionedWithMigratedSettings() {
    Fixture f;
    assertCode(ResultCode::ACCEPTED, f.service.prepareFullReset(false));
    assertCode(ResultCode::RESET_REBOOT_REQUIRED, f.service.execute());
    assertAuthority(f.store, Outcome::UNPROVISIONED,
                    DurableState::UNPROVISIONED, 0);
    TEST_ASSERT_TRUE(f.settings.defaults);
    TEST_ASSERT_EQUAL_UINT32(0, f.events.clears);
}

void testAlreadyFullyResetIsReadOnlyNoOp() {
    Fixture f;
    f.settings.defaults = true;
    assertCode(ResultCode::ALREADY_UNPROVISIONED,
               f.service.prepareFullReset(false));
    TEST_ASSERT_EQUAL_UINT32(0, f.storage.writes);
    TEST_ASSERT_EQUAL_UINT32(0, f.settings.resets);
    TEST_ASSERT_EQUAL_UINT32(0, f.events.clears);
}

void testInvalidSchemaAllowsOnlyDeliberateReset() {
    Fixture f;
    f.storage.seed(CopySlot::A, identity());
    f.storage.cells[0].bytes[4] = 2;
    crc(f.storage.cells[0].bytes);
    assertCode(ResultCode::INVALID_PROVISIONING,
               f.service.prepareApply(proposal(), true));
    TEST_ASSERT_EQUAL_UINT32(0, f.storage.writes);
    assertCode(ResultCode::ACCEPTED, f.service.prepareFullReset(false));
    assertCode(ResultCode::RESET_REBOOT_REQUIRED, f.service.execute());
    assertAuthority(f.store, Outcome::UNPROVISIONED,
                    DurableState::UNPROVISIONED, 0);
    TEST_ASSERT_EQUAL_UINT32(3, f.storage.writes);
    TEST_ASSERT_LESS_THAN_UINT32(f.trace.first(Step::SETTINGS_RESET),
                                 f.trace.first(Step::READBACK_PENDING));
}

void testStorageUnavailableBlocksApplyAndReset() {
    Fixture f;
    f.storage.unavailable = true;
    assertCode(ResultCode::STORAGE_UNAVAILABLE,
               f.service.prepareApply(proposal(), true));
    assertCode(ResultCode::STORAGE_UNAVAILABLE,
               f.service.prepareFullReset(true));
    TEST_ASSERT_EQUAL_UINT32(0, f.storage.writes);
}

void testCancelBeforePendingStopsAllMutation() {
    Fixture f;
    f.events.present = true;
    assertCode(ResultCode::ACCEPTED, f.service.prepareApply(proposal(), true));
    assertCode(ResultCode::CANCELLED, f.service.cancel());
    assertCode(ResultCode::INVALID_LIFECYCLE, f.service.execute());
    TEST_ASSERT_EQUAL_UINT32(0, f.storage.writes);
    TEST_ASSERT_EQUAL_UINT32(0, f.events.clears);
    TEST_ASSERT_EQUAL_UINT32(0, f.settings.resets);
}

void testCancelPreparedResetBeforePending() {
    Fixture f;
    f.storage.seed(CopySlot::A, identity());
    assertCode(ResultCode::ACCEPTED, f.service.prepareFullReset(false));
    assertCode(ResultCode::CANCELLED, f.service.cancel());
    TEST_ASSERT_EQUAL_UINT32(0, f.storage.writes);
    TEST_ASSERT_EQUAL_UINT32(0, f.settings.resets);
}

void testCancelAfterPendingRejectedAndRecoveryMandatory() {
    Fixture f;
    f.events.present = true;
    f.events.failClear = true;
    assertCode(ResultCode::ACCEPTED, f.service.prepareApply(proposal(), true));
    assertCode(ResultCode::EVENT_CLEAR_FAILURE, f.service.execute());
    assertCode(ResultCode::CANCEL_REJECTED, f.service.cancel());
    assertAuthority(f.store, Outcome::PENDING, DurableState::PENDING, 1);
    f.events.failClear = false;
    assertCode(ResultCode::APPLIED_REBOOT_REQUIRED, f.service.recoverPending());
}

void testCancelAfterUncertainPendingCommitReReadsAndRejects() {
    Fixture f;
    f.storage.readFaultAtWrite = 1;
    f.storage.readFault = ReadFault::UNAVAILABLE;
    assertCode(ResultCode::ACCEPTED, f.service.prepareApply(proposal(), false));
    assertCode(ResultCode::IDENTITY_STORE_FAILURE, f.service.execute());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Phase::READY_APPLY),
                            static_cast<uint8_t>(f.service.phase()));
    assertCode(ResultCode::CANCEL_REJECTED, f.service.cancel());
    assertAuthority(f.store, Outcome::PENDING, DurableState::PENDING, 1);
    TEST_ASSERT_EQUAL_UINT32(0, f.events.clears);
    assertCode(ResultCode::APPLIED_REBOOT_REQUIRED, f.service.recoverPending());
}

void testCancelAfterProvenFailedPendingWriteAllowed() {
    Fixture f;
    f.storage.seed(CopySlot::A, identity());
    f.storage.failWriteAt = 1;
    Proposal changed = proposal(); changed.label[0] = 'B';
    assertCode(ResultCode::ACCEPTED, f.service.prepareApply(changed, false));
    assertCode(ResultCode::IDENTITY_STORE_FAILURE, f.service.execute());
    assertCode(ResultCode::CANCELLED, f.service.cancel());
    assertAuthority(f.store, Outcome::PROVISIONED,
                    DurableState::PROVISIONED, 4);
    TEST_ASSERT_EQUAL_UINT32(0, f.events.clears);
}

void testCancelAfterResetPendingRejected() {
    Fixture f;
    f.storage.seed(CopySlot::A, identity());
    f.settings.failReset = true;
    assertCode(ResultCode::ACCEPTED, f.service.prepareFullReset(false));
    assertCode(ResultCode::SETTINGS_RESET_FAILURE, f.service.execute());
    assertCode(ResultCode::CANCEL_REJECTED, f.service.cancel());
    assertAuthority(f.store, Outcome::PENDING, DurableState::PENDING, 0);
}

void testNormalProvisioningGenerationIncrement() {
    Fixture f;
    f.storage.seed(CopySlot::A, identity(Role::HUB, 7, 25));
    Proposal changed = proposal(); changed.label[0] = 'B';
    assertCode(ResultCode::ACCEPTED, f.service.prepareApply(changed, false));
    assertCode(ResultCode::APPLIED_REBOOT_REQUIRED, f.service.execute());
    assertAuthority(f.store, Outcome::PROVISIONED,
                    DurableState::PROVISIONED, 26);
}

void testFinalValidProvisioningGeneration() {
    Fixture f;
    f.storage.seed(CopySlot::A, identity(Role::HUB, 7, UINT32_MAX - 2U));
    Proposal changed = proposal(); changed.label[0] = 'B';
    assertCode(ResultCode::ACCEPTED, f.service.prepareApply(changed, false));
    assertCode(ResultCode::APPLIED_REBOOT_REQUIRED, f.service.execute());
    assertAuthority(f.store, Outcome::PROVISIONED,
                    DurableState::PROVISIONED, UINT32_MAX - 1U);
}

void testProvisioningGenerationExhaustionRejectsChange() {
    Fixture f;
    f.storage.seed(CopySlot::A, identity(Role::HUB, 7, UINT32_MAX - 1U));
    Proposal changed = proposal(); changed.label[0] = 'B';
    assertCode(ResultCode::PROVISIONING_GENERATION_EXHAUSTED,
               f.service.prepareApply(changed, false));
    TEST_ASSERT_EQUAL_UINT32(0, f.storage.writes);
    TEST_ASSERT_EQUAL_UINT32(0, f.events.clears);
}

void testUnchangedAtProvisioningGenerationLimitStillAllowed() {
    Fixture f;
    f.storage.seed(CopySlot::A, identity(Role::HUB, 7, UINT32_MAX - 1U));
    f.events.present = true;
    assertCode(ResultCode::UNCHANGED, f.service.prepareApply(proposal(), false));
    TEST_ASSERT_EQUAL_UINT32(0, f.storage.writes);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX - 1U,
                             f.store.snapshot().record.provisioningGeneration);
}

void testRecordGenerationExhaustionPropagatesWithoutDestruction() {
    Fixture f;
    f.storage.seed(CopySlot::A, identity(Role::HUB, UINT32_MAX, 4));
    f.events.present = true;
    Proposal changed = proposal(); changed.label[0] = 'B';
    const Result result = f.service.prepareApply(changed, true);
    assertCode(ResultCode::RECORD_GENERATION_EXHAUSTED, result);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WriteResult::GENERATION_EXHAUSTED),
                            static_cast<uint8_t>(result.storeResult));
    TEST_ASSERT_EQUAL_UINT32(0, f.storage.writes);
    TEST_ASSERT_TRUE(f.events.present);
    TEST_ASSERT_EQUAL_UINT32(0, f.events.clears);
}

void testApplyRequiresCapacityForPendingAndFinalRecord() {
    Fixture f;
    f.storage.seed(CopySlot::A, identity(Role::HUB, UINT32_MAX - 1U, 4));
    f.events.present = true;
    Proposal changed = proposal(); changed.label[0] = 'B';
    assertCode(ResultCode::RECORD_GENERATION_EXHAUSTED,
               f.service.prepareApply(changed, true));
    TEST_ASSERT_EQUAL_UINT32(0, f.storage.writes);
    TEST_ASSERT_TRUE(f.events.present);
    TEST_ASSERT_EQUAL_UINT32(0, f.events.clears);
}

void testDeliberateFullResetCanRecoverExhaustedRecordGeneration() {
    Fixture f;
    f.storage.seed(CopySlot::A, identity(Role::HUB, UINT32_MAX, 4));
    assertCode(ResultCode::ACCEPTED, f.service.prepareFullReset(false));
    assertCode(ResultCode::RESET_REBOOT_REQUIRED, f.service.execute());
    assertAuthority(f.store, Outcome::UNPROVISIONED,
                    DurableState::UNPROVISIONED, 0);
    TEST_ASSERT_EQUAL_UINT32(3, f.storage.writes);
    TEST_ASSERT_EQUAL_UINT32(3, f.store.snapshot().record.recordGeneration);
}

void testDeliberateFullResetRebasesBeforePendingWouldExhaust() {
    Fixture f;
    f.storage.seed(CopySlot::A, identity(Role::HUB, UINT32_MAX - 1U, 4));
    f.events.present = true;
    assertCode(ResultCode::ACCEPTED, f.service.prepareFullReset(true));
    assertCode(ResultCode::RESET_REBOOT_REQUIRED, f.service.execute());
    assertAuthority(f.store, Outcome::UNPROVISIONED,
                    DurableState::UNPROVISIONED, 0);
    TEST_ASSERT_EQUAL_UINT32(3, f.storage.writes);
    TEST_ASSERT_LESS_THAN_UINT32(f.trace.first(Step::SETTINGS_RESET),
                                 f.trace.first(Step::READBACK_PENDING));
}

void testPreexistingExhaustedPendingFailsBeforeDestruction() {
    Fixture f;
    Record durablePending = identity(Role::HUB, UINT32_MAX, 4);
    durablePending.state = DurableState::PENDING;
    durablePending.transaction = TransactionKind::APPLY;
    f.storage.seed(CopySlot::A, durablePending);
    f.events.present = true;
    assertCode(ResultCode::RECORD_GENERATION_EXHAUSTED,
               f.service.recoverPending());
    assertAuthority(f.store, Outcome::PENDING, DurableState::PENDING, 4);
    TEST_ASSERT_EQUAL_UINT32(0, f.storage.writes);
    TEST_ASSERT_EQUAL_UINT32(0, f.events.clears);
}

void testInvalidStorageResetPendingFailureDoesNotDestroy() {
    Fixture f;
    f.storage.seed(CopySlot::A, identity());
    f.storage.cells[0].bytes[4] = 2;
    crc(f.storage.cells[0].bytes);
    assertCode(ResultCode::ACCEPTED, f.service.prepareFullReset(false));
    f.storage.failCommitAt = 2;
    assertCode(ResultCode::IDENTITY_STORE_FAILURE, f.service.execute());
    TEST_ASSERT_EQUAL_UINT32(0, f.settings.resets);
    TEST_ASSERT_EQUAL_UINT32(0, f.events.clears);
    TEST_ASSERT_EQUAL_UINT32(2, f.storage.writes);
}

void testExistingPendingResetRequiresRecovery() {
    Fixture f;
    Record pendingReset = {};
    pendingReset.recordGeneration = 7;
    pendingReset.state = DurableState::PENDING;
    pendingReset.transaction = TransactionKind::FULL_RESET;
    f.storage.seed(CopySlot::A, pendingReset);
    assertCode(ResultCode::RECOVERY_REQUIRED,
               f.service.prepareFullReset(true));
    assertCode(ResultCode::RESET_REBOOT_REQUIRED, f.service.recoverPending());
    assertAuthority(f.store, Outcome::UNPROVISIONED,
                    DurableState::UNPROVISIONED, 0);
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(testVirginHubProvisionOrderingAndExactTarget);
    RUN_TEST(testVirginNodeProvisionStartsAtGenerationOne);
    RUN_TEST(testInitialProvisionDoesNotCreateUnprovisionedRecord);
    RUN_TEST(testHubToChangedHub);
    RUN_TEST(testHubToNode);
    RUN_TEST(testNodeToHub);
    RUN_TEST(testLocalIdChange);
    RUN_TEST(testPeerIdChange);
    RUN_TEST(testNetworkIdChange);
    RUN_TEST(testLabelChange);
    RUN_TEST(testUnsupportedProfilesRejectedBeforeMutation);
    RUN_TEST(testInvalidTargetIdsAndLabelRejected);
    RUN_TEST(testUnchangedIgnoresRecordGenerationAndDoesNothing);
    RUN_TEST(testAckRequiredBeforeAnyWriteOrErase);
    RUN_TEST(testAckAllowsPresentEventDomain);
    RUN_TEST(testEmptyEventDomainNeedsNoAckAndNoClear);
    RUN_TEST(testEventInspectionFailurePreventsPending);
    RUN_TEST(testLateEventPresenceWithoutAckBlocksPending);
    RUN_TEST(testPendingWriteFailureLeavesOldAuthorityAndEvents);
    RUN_TEST(testPendingCommitFailureLeavesOldAuthorityAndEvents);
    RUN_TEST(testEveryPendingReadbackFaultLeavesOldRamAuthorityAndEvents);
    RUN_TEST(testEventClearFailurePreservesPendingAndRecovers);
    RUN_TEST(testEventVerificationFailurePreservesPendingAndRecovers);
    RUN_TEST(testEveryFinalWritePhaseFailureKeepsPendingInRam);
    RUN_TEST(testRebootAfterFailedFinalCommitCompletesPending);
    RUN_TEST(testRebootAfterFailedFinalReadbackRecognizesCommittedFinal);
    RUN_TEST(testRepeatedApplyRecoveryDoesNotWriteAgain);
    RUN_TEST(testPendingCannotStartFreshApply);
    RUN_TEST(testFullResetOrderingAndCanonicalFinal);
    RUN_TEST(testFullResetPendingWriteAndCommitFaultsDoNotDestroy);
    RUN_TEST(testFullResetPendingReadbackFaultsDoNotDestroy);
    RUN_TEST(testSettingsResetFailureLeavesPending);
    RUN_TEST(testSettingsVerificationFailureLeavesPending);
    RUN_TEST(testResetEventClearAndVerifyFailuresLeavePending);
    RUN_TEST(testResetFinalWriteCommitReadbackFailuresLeavePending);
    RUN_TEST(testResetRecoveryAfterEveryIntermediateFailure);
    RUN_TEST(testRepeatedResetRecoveryIsBoundedAndIdempotent);
    RUN_TEST(testFullResetOnUnprovisionedWithMigratedSettings);
    RUN_TEST(testAlreadyFullyResetIsReadOnlyNoOp);
    RUN_TEST(testInvalidSchemaAllowsOnlyDeliberateReset);
    RUN_TEST(testStorageUnavailableBlocksApplyAndReset);
    RUN_TEST(testCancelBeforePendingStopsAllMutation);
    RUN_TEST(testCancelPreparedResetBeforePending);
    RUN_TEST(testCancelAfterPendingRejectedAndRecoveryMandatory);
    RUN_TEST(testCancelAfterUncertainPendingCommitReReadsAndRejects);
    RUN_TEST(testCancelAfterProvenFailedPendingWriteAllowed);
    RUN_TEST(testCancelAfterResetPendingRejected);
    RUN_TEST(testNormalProvisioningGenerationIncrement);
    RUN_TEST(testFinalValidProvisioningGeneration);
    RUN_TEST(testProvisioningGenerationExhaustionRejectsChange);
    RUN_TEST(testUnchangedAtProvisioningGenerationLimitStillAllowed);
    RUN_TEST(testRecordGenerationExhaustionPropagatesWithoutDestruction);
    RUN_TEST(testApplyRequiresCapacityForPendingAndFinalRecord);
    RUN_TEST(testDeliberateFullResetCanRecoverExhaustedRecordGeneration);
    RUN_TEST(testDeliberateFullResetRebasesBeforePendingWouldExhaust);
    RUN_TEST(testPreexistingExhaustedPendingFailsBeforeDestruction);
    RUN_TEST(testInvalidStorageResetPendingFailureDoesNotDestroy);
    RUN_TEST(testExistingPendingResetRequiresRecovery);
    return UNITY_END();
}
