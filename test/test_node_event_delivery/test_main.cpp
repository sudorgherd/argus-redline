#include <unity.h>
#include <initializer_list>
#include <stdio.h>
#include <string.h>

#include "node_event_delivery.h"
#include "node_event_creation.h"
#include "node_event_reclamation.h"
#include "node_event_service_time.h"

using namespace EventRecords;
using namespace EventStorage;
using namespace NodeEventDelivery;
using EventIdentity::EntropySource;
using EventIdentity::StorageResult;
using NodeEventStore::EnqueueStatus;
using NodeEventStore::MutationStatus;

namespace {

using RuntimeState = NodeEventDelivery::RuntimeState;

enum class Fault : uint8_t { NONE, WRITE, COMMIT, READ_MISSING, READ_UNAVAILABLE, MISMATCH, BAD_CRC };

class FakeStorage final : public NodeEventStore::Storage {
public:
    FixedCopy<NODE_METADATA_SIZE> metadata[2] = {};
    FixedCopy<NODE_RECORD_SIZE> events[NodeEventStore::NODE_EVENT_CAPACITY][2] = {};
    Fault fault = Fault::NONE;
    bool pending = false;
    bool eventOperation = false;
    uint8_t lastSlot = 0;
    CopySlot lastCopy = CopySlot::A;
    unsigned eventWrites = 0;
    unsigned metadataWrites = 0;

    FakeStorage() {
        metadata[0].status = metadata[1].status = FixedReadStatus::MISSING;
        for (size_t i = 0; i < NodeEventStore::NODE_EVENT_CAPACITY; ++i) {
            events[i][0].status = events[i][1].status = FixedReadStatus::MISSING;
        }
    }
    StorageResult read(CopySlot copy, uint8_t* out, size_t cap, size_t& len) override {
        return readFixed(metadata[index(copy)], NODE_METADATA_SIZE, false, 0, copy,
                         out, cap, len);
    }
    StorageResult write(CopySlot copy, const uint8_t* in, size_t len) override {
        ++metadataWrites;
        eventOperation = false;
        lastCopy = copy;
        pending = true;
        return writeFixed(metadata[index(copy)], NODE_METADATA_SIZE, in, len);
    }
    StorageResult commit() override {
        return pending && fault == Fault::COMMIT ? StorageResult::ERROR : StorageResult::OK;
    }
    StorageResult readEventCopy(uint8_t slot, CopySlot copy, uint8_t* out,
                                size_t cap, size_t& len) override {
        if (slot >= NodeEventStore::NODE_EVENT_CAPACITY) return StorageResult::ERROR;
        return readFixed(events[slot][index(copy)], NODE_RECORD_SIZE, true, slot,
                         copy, out, cap, len);
    }
    StorageResult writeEventCopy(uint8_t slot, CopySlot copy,
                                 const uint8_t* in, size_t len) override {
        if (slot >= NodeEventStore::NODE_EVENT_CAPACITY) return StorageResult::ERROR;
        ++eventWrites;
        eventOperation = true;
        lastSlot = slot;
        lastCopy = copy;
        pending = true;
        return writeFixed(events[slot][index(copy)], NODE_RECORD_SIZE, in, len);
    }
    void clearFault() { fault = Fault::NONE; pending = false; }

private:
    template <size_t Size>
    StorageResult writeFixed(FixedCopy<Size>& destination, size_t exact,
                             const uint8_t* input, size_t length) {
        if (fault == Fault::WRITE || input == nullptr || length != exact) return StorageResult::ERROR;
        destination.status = FixedReadStatus::OK;
        for (size_t i = 0; i < exact; ++i) destination.bytes[i] = input[i];
        return StorageResult::OK;
    }
    template <size_t Size>
    StorageResult readFixed(FixedCopy<Size>& source, size_t exact, bool isEvent,
                            uint8_t slot, CopySlot copy, uint8_t* out,
                            size_t cap, size_t& len) {
        len = 0;
        const bool readBack = pending && eventOperation == isEvent &&
            (!isEvent || slot == lastSlot) && copy == lastCopy;
        if (readBack && fault == Fault::READ_MISSING) return StorageResult::MISSING;
        if (readBack && fault == Fault::READ_UNAVAILABLE) return StorageResult::UNAVAILABLE;
        if (source.status == FixedReadStatus::MISSING) return StorageResult::MISSING;
        if (source.status == FixedReadStatus::UNAVAILABLE) return StorageResult::UNAVAILABLE;
        if (cap < exact) return StorageResult::ERROR;
        for (size_t i = 0; i < exact; ++i) out[i] = source.bytes[i];
        if (readBack && fault == Fault::MISMATCH) out[12] ^= 1U;
        if (readBack && fault == Fault::BAD_CRC) out[exact - 1U] ^= 1U;
        len = exact;
        return StorageResult::OK;
    }
    static size_t index(CopySlot copy) { return copy == CopySlot::A ? 0U : 1U; }
};

class FakeEntropy final : public EntropySource {
public:
    uint32_t value = 0x11223344U;
    bool nextUint32(uint32_t& output) override { output = value; return true; }
};

NodeEventStore::EventInput event(uint32_t lifetime = 300) {
    NodeEventStore::EventInput input = {};
    input.family = static_cast<uint8_t>(EventProtocol::Family::BUTTON);
    input.flags = EventProtocol::IMPORTANT_FLAG;
    input.lifetimeBudgetSeconds = lifetime;
    input.bodyLength = 1;
    input.body[0] = static_cast<uint8_t>(EventProtocol::ButtonEvent::PRESS);
    return input;
}

struct Fixture {
    FakeStorage storage;
    FakeEntropy entropy;
    NodeEventStore::Store store;
    Policy policy;

    void initialize(uint32_t now = 0) {
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NodeEventStore::Status::READY),
            static_cast<uint8_t>(store.recover(storage, entropy, 0x10)));
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Status::READY),
            static_cast<uint8_t>(policy.recover(store, now)));
        storage.clearFault();
    }
    NodeEventStore::EnqueueResult enqueue(uint32_t lifetime, uint32_t now) {
        NodeEventStore::EnqueueResult result = store.enqueue(event(lifetime));
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EnqueueStatus::ENQUEUED),
                                static_cast<uint8_t>(result.status));
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Status::READY),
            static_cast<uint8_t>(policy.trackEnqueued(result.slot, now)));
        storage.clearFault();
        return result;
    }
};

void seedFree(FakeStorage& storage) {
    for (uint8_t slot = 0; slot < NodeEventStore::NODE_EVENT_CAPACITY; ++slot) {
        NodeRecord record = {};
        record.generation = 1;
        record.state = NodeState::FREE;
        storage.events[slot][0].status = FixedReadStatus::OK;
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::OK),
            static_cast<uint8_t>(encodeNodeRecord(
                record, storage.events[slot][0].bytes, NODE_RECORD_SIZE)));
    }
}

void seedMetadata(FakeStorage& storage, uint32_t next = 33) {
    NodeMetadata metadata = {5, 0x10, 0x11223344U, next};
    storage.metadata[0].status = FixedReadStatus::OK;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::OK),
        static_cast<uint8_t>(encodeNodeMetadata(
            metadata, storage.metadata[0].bytes, NODE_METADATA_SIZE)));
}

void seedQueued(FakeStorage& storage, uint8_t slot, uint32_t remaining,
                uint8_t attempts = 0, uint32_t lifetime = 300) {
    NodeRecord record = {};
    record.generation = 2;
    record.state = NodeState::QUEUED;
    record.flags = EventProtocol::IMPORTANT_FLAG;
    record.family = static_cast<uint8_t>(EventProtocol::Family::BUTTON);
    record.bodyLength = 1;
    record.eventEpoch = 0x11223344U;
    record.eventId = slot + 1U;
    record.lifetimeBudgetSeconds = lifetime;
    record.remainingActiveSeconds = remaining;
    record.attemptsUsed = attempts;
    record.body[0] = static_cast<uint8_t>(EventProtocol::ButtonEvent::PRESS);
    storage.events[slot][1].status = FixedReadStatus::OK;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::OK),
        static_cast<uint8_t>(encodeNodeRecord(
            record, storage.events[slot][1].bytes, NODE_RECORD_SIZE)));
}

void testEnqueueBaselineAndSubCheckpointAccounting() {
    Fixture f;
    f.initialize(1000);
    auto queued = f.enqueue(300, 1000);
    const unsigned writes = f.storage.eventWrites;
    TEST_ASSERT_EQUAL_UINT32(300, f.store.recordAt(queued.slot)->remainingActiveSeconds);
    TEST_ASSERT_EQUAL_UINT8(0, f.store.recordAt(queued.slot)->attemptsUsed);
    TEST_ASSERT_EQUAL_UINT32(300, f.policy.effectiveRemainingSeconds(queued.slot));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Status::READY),
                            static_cast<uint8_t>(f.policy.service(2000)));
    TEST_ASSERT_EQUAL_UINT32(299, f.policy.effectiveRemainingSeconds(queued.slot));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Status::READY),
                            static_cast<uint8_t>(f.policy.service(60000)));
    TEST_ASSERT_EQUAL_UINT32(241, f.policy.effectiveRemainingSeconds(queued.slot));
    TEST_ASSERT_EQUAL_UINT(writes, f.storage.eventWrites);
}

void testExactAndMultipleCheckpointCatchup() {
    Fixture f;
    f.initialize();
    auto queued = f.enqueue(300, 0);
    const uint32_t generation = f.store.recordAt(queued.slot)->generation;
    const unsigned writes = f.storage.eventWrites;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Status::READY),
                            static_cast<uint8_t>(f.policy.service(60000)));
    const NodeRecord afterOne = *f.store.recordAt(queued.slot);
    TEST_ASSERT_EQUAL_UINT32(240, afterOne.remainingActiveSeconds);
    TEST_ASSERT_EQUAL_UINT32(generation + 1U, afterOne.generation);
    TEST_ASSERT_EQUAL_UINT8(0, afterOne.attemptsUsed);
    TEST_ASSERT_EQUAL_UINT(writes + 1U, f.storage.eventWrites);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Status::READY),
                            static_cast<uint8_t>(f.policy.service(180000)));
    TEST_ASSERT_EQUAL_UINT32(120, f.store.recordAt(queued.slot)->remainingActiveSeconds);
    TEST_ASSERT_EQUAL_UINT32(generation + 3U, f.store.recordAt(queued.slot)->generation);
    TEST_ASSERT_EQUAL_UINT8(afterOne.family, f.store.recordAt(queued.slot)->family);
    TEST_ASSERT_EQUAL_UINT32(afterOne.eventId, f.store.recordAt(queued.slot)->eventId);
}

void testImmediateExpiryAtExactBoundaryAndAfterCheckpoint() {
    Fixture f;
    f.initialize();
    auto shortEvent = f.enqueue(60, 0);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Status::READY),
                            static_cast<uint8_t>(f.policy.service(59999)));
    TEST_ASSERT_TRUE(f.policy.eligible(shortEvent.slot));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Status::READY),
                            static_cast<uint8_t>(f.policy.service(60000)));
    TEST_ASSERT_FALSE(f.policy.eligible(shortEvent.slot));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NodeState::EXPIRED),
        static_cast<uint8_t>(f.store.recordAt(shortEvent.slot)->state));

    auto longer = f.enqueue(121, 60000);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Status::READY),
                            static_cast<uint8_t>(f.policy.service(181000)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NodeState::EXPIRED),
        static_cast<uint8_t>(f.store.recordAt(longer.slot)->state));
}

void testRebootDebitPreservesCanonicalFieldsAndAttempts() {
    FakeStorage storage;
    FakeEntropy entropy;
    seedFree(storage);
    seedMetadata(storage);
    seedQueued(storage, 0, 180, 4, 300);
    NodeEventStore::Store store;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NodeEventStore::Status::READY),
        static_cast<uint8_t>(store.recover(storage, entropy, 0x10)));
    const NodeRecord before = *store.recordAt(0);
    Policy policy;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Status::READY),
                            static_cast<uint8_t>(policy.recover(store, 77)));
    const NodeRecord* after = store.recordAt(0);
    TEST_ASSERT_EQUAL_UINT32(120, after->remainingActiveSeconds);
    TEST_ASSERT_EQUAL_UINT32(300, after->lifetimeBudgetSeconds);
    TEST_ASSERT_EQUAL_UINT8(4, after->attemptsUsed);
    TEST_ASSERT_EQUAL_UINT32(before.eventEpoch, after->eventEpoch);
    TEST_ASSERT_TRUE(policy.eligible(0));
}

void testRebootDebitExpiresAtOrBelowSixty() {
    for (uint32_t remaining = 59; remaining <= 60; ++remaining) {
        FakeStorage storage;
        FakeEntropy entropy;
        seedFree(storage);
        seedMetadata(storage);
        seedQueued(storage, 0, remaining, 3, 300);
        NodeEventStore::Store store;
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NodeEventStore::Status::READY),
            static_cast<uint8_t>(store.recover(storage, entropy, 0x10)));
        Policy policy;
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Status::READY),
                                static_cast<uint8_t>(policy.recover(store, 0)));
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NodeState::EXPIRED),
                                static_cast<uint8_t>(store.recordAt(0)->state));
        TEST_ASSERT_EQUAL_UINT8(3, store.recordAt(0)->attemptsUsed);
    }
}

void testRepeatedBootsDebitWithoutReset() {
    Fixture first;
    first.initialize();
    auto queued = first.enqueue(300, 0);
    FakeStorage& storage = first.storage;
    FakeEntropy& entropy = first.entropy;
    storage.clearFault();
    NodeEventStore::Store secondStore;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NodeEventStore::Status::READY),
        static_cast<uint8_t>(secondStore.recover(storage, entropy, 0x10)));
    Policy second;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Status::READY),
                            static_cast<uint8_t>(second.recover(secondStore, 10)));
    TEST_ASSERT_EQUAL_UINT32(240, secondStore.recordAt(queued.slot)->remainingActiveSeconds);
    storage.clearFault();
    NodeEventStore::Store thirdStore;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NodeEventStore::Status::READY),
        static_cast<uint8_t>(thirdStore.recover(storage, entropy, 0x10)));
    Policy third;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Status::READY),
                            static_cast<uint8_t>(third.recover(thirdStore, 20)));
    TEST_ASSERT_EQUAL_UINT32(180, thirdStore.recordAt(queued.slot)->remainingActiveSeconds);
}

void testMonotonicWrapAndMultipleEventsAgeTogether() {
    Fixture f;
    f.initialize(0xFFFFFF00U);
    auto first = f.enqueue(180, 0xFFFFFF00U);
    auto second = f.enqueue(180, 0xFFFFFF00U);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Status::READY),
        static_cast<uint8_t>(f.policy.service(0x000002E8U)));
    TEST_ASSERT_EQUAL_UINT32(179, f.policy.effectiveRemainingSeconds(first.slot));
    TEST_ASSERT_EQUAL_UINT32(179, f.policy.effectiveRemainingSeconds(second.slot));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MutationStatus::OK),
        static_cast<uint8_t>(f.store.markFailed(first.slot)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Status::READY),
        static_cast<uint8_t>(f.policy.service(0x0000ED48U)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NodeState::FAILED),
        static_cast<uint8_t>(f.store.recordAt(first.slot)->state));
    TEST_ASSERT_EQUAL_UINT32(120, f.store.recordAt(second.slot)->remainingActiveSeconds);
}

void testFiveWriteAheadAttemptsPreserveLifetimeAndContent() {
    Fixture f;
    f.initialize();
    auto queued = f.enqueue(300, 0);
    const NodeRecord original = *f.store.recordAt(queued.slot);
    for (uint8_t attempt = 1; attempt <= 5; ++attempt) {
        const uint32_t generation = f.store.recordAt(queued.slot)->generation;
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AttemptResult::ARMED),
            static_cast<uint8_t>(f.policy.armAttempt(queued.slot, 0)));
        const NodeRecord* current = f.store.recordAt(queued.slot);
        TEST_ASSERT_EQUAL_UINT8(attempt, current->attemptsUsed);
        TEST_ASSERT_EQUAL_UINT32(generation + 1U, current->generation);
        TEST_ASSERT_EQUAL_UINT32(original.remainingActiveSeconds,
                                 current->remainingActiveSeconds);
        TEST_ASSERT_EQUAL_UINT32(original.eventId, current->eventId);
        TEST_ASSERT_EQUAL_UINT8(original.body[0], current->body[0]);
    }
    const unsigned writes = f.storage.eventWrites;
    const uint32_t generation = f.store.recordAt(queued.slot)->generation;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AttemptResult::EXHAUSTED),
        static_cast<uint8_t>(f.policy.armAttempt(queued.slot, 0)));
    TEST_ASSERT_EQUAL_UINT(writes, f.storage.eventWrites);
    TEST_ASSERT_EQUAL_UINT32(generation, f.store.recordAt(queued.slot)->generation);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NodeState::QUEUED),
                            static_cast<uint8_t>(f.store.recordAt(queued.slot)->state));
}

void testAttemptFaultsNeverArmAndDegrade() {
    const Fault faults[] = {Fault::WRITE, Fault::COMMIT, Fault::READ_MISSING,
                            Fault::READ_UNAVAILABLE, Fault::MISMATCH, Fault::BAD_CRC};
    for (size_t i = 0; i < sizeof(faults) / sizeof(faults[0]); ++i) {
        Fixture f;
        f.initialize();
        auto queued = f.enqueue(300, 0);
        f.storage.fault = faults[i];
        TEST_ASSERT_NOT_EQUAL(static_cast<uint8_t>(AttemptResult::ARMED),
            static_cast<uint8_t>(f.policy.armAttempt(queued.slot, 0)));
        TEST_ASSERT_FALSE(f.store.healthy());
        TEST_ASSERT_FALSE(f.policy.ready());
    }
}

void testAttemptAcrossRebootAndDebitIndependence() {
    FakeStorage storage;
    FakeEntropy entropy;
    seedFree(storage);
    seedMetadata(storage);
    seedQueued(storage, 0, 180, 4, 300);
    NodeEventStore::Store store;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NodeEventStore::Status::READY),
        static_cast<uint8_t>(store.recover(storage, entropy, 0x10)));
    Policy policy;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Status::READY),
                            static_cast<uint8_t>(policy.recover(store, 0)));
    TEST_ASSERT_EQUAL_UINT8(4, store.recordAt(0)->attemptsUsed);
    TEST_ASSERT_EQUAL_UINT32(120, store.recordAt(0)->remainingActiveSeconds);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AttemptResult::ARMED),
                            static_cast<uint8_t>(policy.armAttempt(0, 0)));
    TEST_ASSERT_EQUAL_UINT8(5, store.recordAt(0)->attemptsUsed);
    TEST_ASSERT_EQUAL_UINT32(120, store.recordAt(0)->remainingActiveSeconds);

    storage.clearFault();
    NodeEventStore::Store rebooted;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NodeEventStore::Status::READY),
        static_cast<uint8_t>(rebooted.recover(storage, entropy, 0x10)));
    Policy again;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Status::READY),
                            static_cast<uint8_t>(again.recover(rebooted, 0)));
    TEST_ASSERT_EQUAL_UINT8(5, rebooted.recordAt(0)->attemptsUsed);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AttemptResult::EXHAUSTED),
                            static_cast<uint8_t>(again.armAttempt(0, 0)));
}

void testExpiredAndTerminalEventsCannotArm() {
    Fixture f;
    f.initialize();
    auto expiring = f.enqueue(60, 0);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Status::READY),
                            static_cast<uint8_t>(f.policy.service(60000)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AttemptResult::EXPIRED),
                            static_cast<uint8_t>(f.policy.armAttempt(expiring.slot, 60000)));
    auto failing = f.enqueue(300, 60000);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MutationStatus::OK),
                            static_cast<uint8_t>(f.store.markFailed(failing.slot)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AttemptResult::NOT_QUEUED),
                            static_cast<uint8_t>(f.policy.armAttempt(failing.slot, 60000)));
}

void testCheckpointAndDebitFailuresBlockEligibility() {
    Fixture checkpoint;
    checkpoint.initialize();
    checkpoint.enqueue(300, 0);
    checkpoint.storage.fault = Fault::WRITE;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Status::CHECKPOINT_FAILURE),
                            static_cast<uint8_t>(checkpoint.policy.service(60000)));
    TEST_ASSERT_FALSE(checkpoint.policy.ready());

    FakeStorage storage;
    FakeEntropy entropy;
    seedFree(storage);
    seedMetadata(storage);
    seedQueued(storage, 0, 180);
    NodeEventStore::Store store;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NodeEventStore::Status::READY),
        static_cast<uint8_t>(store.recover(storage, entropy, 0x10)));
    storage.fault = Fault::WRITE;
    Policy debit;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Status::DEBIT_FAILURE),
                            static_cast<uint8_t>(debit.recover(store, 0)));
    TEST_ASSERT_FALSE(debit.eligible(0));
}

void testAbruptShutdownBeforeAndAfterCheckpoint() {
    Fixture before;
    before.initialize();
    auto queued = before.enqueue(180, 0);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Status::READY),
                            static_cast<uint8_t>(before.policy.service(59999)));
    TEST_ASSERT_EQUAL_UINT32(180, before.store.recordAt(queued.slot)->remainingActiveSeconds);
    before.storage.clearFault();
    NodeEventStore::Store rebootedBefore;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NodeEventStore::Status::READY),
        static_cast<uint8_t>(rebootedBefore.recover(before.storage, before.entropy, 0x10)));
    Policy debitBefore;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Status::READY),
                            static_cast<uint8_t>(debitBefore.recover(rebootedBefore, 0)));
    TEST_ASSERT_EQUAL_UINT32(120, rebootedBefore.recordAt(queued.slot)->remainingActiveSeconds);

    Fixture after;
    after.initialize();
    auto second = after.enqueue(180, 0);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Status::READY),
                            static_cast<uint8_t>(after.policy.service(60000)));
    TEST_ASSERT_EQUAL_UINT32(120, after.store.recordAt(second.slot)->remainingActiveSeconds);
    after.storage.clearFault();
    NodeEventStore::Store rebootedAfter;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NodeEventStore::Status::READY),
        static_cast<uint8_t>(rebootedAfter.recover(after.storage, after.entropy, 0x10)));
    Policy debitAfter;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Status::READY),
                            static_cast<uint8_t>(debitAfter.recover(rebootedAfter, 0)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NodeState::QUEUED),
                            static_cast<uint8_t>(rebootedAfter.recordAt(second.slot)->state));
    TEST_ASSERT_EQUAL_UINT32(60,
        rebootedAfter.recordAt(second.slot)->remainingActiveSeconds);
}

class FakeSequence final : public SequenceSource {
public:
    uint8_t value = 0x40;
    unsigned calls = 0;
    bool succeed = true;
    bool next(uint8_t& output) override { ++calls; output = value++; return succeed; }
};

class FakeJitter final : public JitterSource {
public:
    uint16_t value = 0;
    unsigned calls = 0;
    uint16_t nextMilliseconds() override { ++calls; return value; }
};

struct DeliveryFixture {
    FakeStorage storage;
    FakeEntropy entropy;
    NodeEventStore::Store store;
    FakeSequence sequence;
    FakeJitter jitter;
    Controller controller;
    ::RuntimeState::State diagnostics{::RuntimeState::DeviceRole::NODE, 0x10, 0x20};

    void initializeEmpty(uint32_t now = 0) {
        store.setDiagnostics(&diagnostics);
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NodeEventStore::Status::READY),
            static_cast<uint8_t>(store.recover(storage, entropy, 0x10)));
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ControllerStatus::NO_ACTIVE_EVENT),
            static_cast<uint8_t>(controller.recover(store, 0x10, 0x20, sequence, jitter,
                now, &diagnostics).status));
        storage.clearFault();
    }
    uint8_t enqueue(uint32_t lifetime = 300) {
        const auto result = store.enqueue(event(lifetime));
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EnqueueStatus::ENQUEUED), static_cast<uint8_t>(result.status));
        return result.slot;
    }
    uint8_t enqueueTracked(uint32_t lifetime, uint32_t now) {
        const auto clock = [=] { return now; };
        TrackedCreationSink<decltype(clock)> sink(store, controller, clock);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)EventProducers::CreationResult::ENQUEUED,
            (uint8_t)sink.create(event(lifetime)));
        return sink.lastResult().slot;
    }
    void recoverController(uint32_t now = 0) {
        const ControllerStatus status = controller.recover(store, 0x10, 0x20, sequence,
            jitter, now, &diagnostics).status;
        TEST_ASSERT_TRUE(status == ControllerStatus::OK || status == ControllerStatus::NO_ACTIVE_EVENT);
    }
    ControllerResult ready(uint32_t now = 0) {
        ControllerResult output = controller.service(now, false);
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RuntimeState::READY), static_cast<uint8_t>(controller.state()));
        return output;
    }
    ControllerResult transmit(uint32_t now = 0) { ready(now); return controller.grantTransmit(now); }
    ControllerResult waitAdmission(uint32_t now = 0) {
        ControllerResult tx = transmit(now);
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RadioActionType::TRANSMIT), static_cast<uint8_t>(tx.action.type));
        return controller.txCompleted(now);
    }
    void response(EventProtocol::AdmissionStatus status, uint8_t* bytes, size_t& length) {
        const auto admission = EventProtocol::makeAdmissionResponse(controller.attemptEvent(), status);
        TEST_ASSERT_TRUE(EventProtocol::encodeAdmissionResponse(admission, bytes, Protocol::MAX_PACKET_SIZE, length));
    }
};

void assertReusedSlotStartsWithFreshLifetime(bool admitted, uint32_t idleMs) {
    constexpr uint32_t lifetime = 3600;
    constexpr uint32_t startAt = 1000;
    constexpr uint32_t terminalAt = 1030;
    DeliveryFixture f; f.initializeEmpty(startAt);
    EventTxDiagnostics::Observer observer; observer.enable();
    // Attach observations while empty, before A exists. Never recover the
    // controller or manually track a slot between A and B: the shared creation
    // sink must notify the controller after each successful durable enqueue.
    f.controller.recover(f.store, 0x10, 0x20, f.sequence, f.jitter,
        startAt, &f.diagnostics, &observer);
    const uint8_t first = f.enqueueTracked(lifetime, startAt);
    const uint32_t firstId = f.store.recordAt(first)->eventId;
    f.waitAdmission(startAt);
    uint8_t bytes[Protocol::MAX_PACKET_SIZE] = {}; size_t length = 0;
    f.response(admitted ? EventProtocol::AdmissionStatus::ADMITTED
                        : EventProtocol::AdmissionStatus::UNSUPPORTED_EVENT,
        bytes, length);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ControllerStatus::OK,
        (uint8_t)f.controller.admissionCandidate(bytes, length, terminalAt).status);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(admitted ? RuntimeState::RELEASED : RuntimeState::FAILED),
        (uint8_t)f.controller.state());
    if (!admitted) TEST_ASSERT_TRUE(reclaimTerminals(f.store, &f.controller, &observer, terminalAt));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)NodeState::FREE, (uint8_t)f.store.recordAt(first)->state);
    TEST_ASSERT_EQUAL_UINT(0, f.store.ownedCount());

    const uint32_t enqueuedAt = terminalAt + idleMs;
    for (uint32_t now : {terminalAt, terminalAt + idleMs / 2U, enqueuedAt}) {
        const auto idle = serviceNow(f.controller, false, [=] { return now; });
        TEST_ASSERT_EQUAL_UINT8((uint8_t)ControllerStatus::NO_ACTIVE_EVENT,
            (uint8_t)idle.result.status);
        TEST_ASSERT_FALSE(f.controller.hasActiveEvent());
    }

    // Same tracked creation sink -> serviceNow path as production.
    const uint8_t second = f.enqueueTracked(lifetime, enqueuedAt);
    TEST_ASSERT_EQUAL_UINT8(first, second);
    const NodeRecord before = *f.store.recordAt(second);
    TEST_ASSERT_NOT_EQUAL(firstId, before.eventId);
    TEST_ASSERT_EQUAL_UINT32(lifetime, before.remainingActiveSeconds);
    TEST_ASSERT_EQUAL_UINT8(0, before.attemptsUsed);
    const unsigned writes = f.storage.eventWrites;
    const auto serviced = serviceNow(f.controller, true, [=] { return enqueuedAt; });
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ControllerStatus::OK, (uint8_t)serviced.result.status);
    const uint16_t reversed = observer.snapshot().counters[
        (uint8_t)EventTxDiagnostics::Counter::CLOCK_REVERSED];
    char evidence[144];
    snprintf(evidence, sizeof(evidence),
        "reuse idle=%lu state=%u stored=%lu effective=%lu attempts=%u reversed=%u",
        (unsigned long)idleMs, (unsigned)f.controller.state(),
        (unsigned long)f.store.recordAt(second)->remainingActiveSeconds,
        (unsigned long)f.controller.effectiveRemainingSeconds(),
        f.store.recordAt(second)->attemptsUsed, reversed);
    TEST_MESSAGE(evidence);
    TEST_ASSERT_EQUAL_UINT16(0, reversed);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(lifetime, f.controller.effectiveRemainingSeconds(),
        "new Event must not inherit the prior slot occupant's elapsed lifetime");
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RuntimeState::QUEUED, (uint8_t)f.controller.state());
    TEST_ASSERT_EQUAL_MEMORY(&before, f.store.recordAt(second), sizeof(before));
    TEST_ASSERT_EQUAL_UINT(writes, f.storage.eventWrites);
    serviceNow(f.controller, false, [=] { return enqueuedAt; });
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RuntimeState::READY, (uint8_t)f.controller.state());
    const auto tx = f.controller.grantTransmit(enqueuedAt);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RadioActionType::TRANSMIT, (uint8_t)tx.action.type);
    TEST_ASSERT_EQUAL_UINT32(before.eventId, f.controller.attemptEvent().id);
    TEST_ASSERT_EQUAL_UINT32(lifetime, f.controller.attemptEvent().lifetimeBudgetSeconds);
    TEST_ASSERT_EQUAL_UINT8(1, f.store.recordAt(second)->attemptsUsed);
}

void testReleasedSlotReuseAfterLongIdleStartsFresh() {
    assertReusedSlotStartsWithFreshLifetime(true, 3600001U);
}

void testReleasedSlotReuseAfterOrdinaryIdleStartsFresh() {
    assertReusedSlotStartsWithFreshLifetime(true, 2000U);
}

void testReclaimedFailedSlotReuseAfterLongIdleStartsFresh() {
    assertReusedSlotStartsWithFreshLifetime(false, 3600001U);
}

void testReclaimedFailedSlotReuseAfterOrdinaryIdleStartsFresh() {
    assertReusedSlotStartsWithFreshLifetime(false, 2000U);
}

void testExplicitEnqueueTrackingResetsOnlyReusedSlot() {
    Fixture f; f.initialize(1000);
    const auto first = f.enqueue(3600, 1000);
    const auto follower = f.enqueue(3600, 1000);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Status::READY, (uint8_t)f.policy.service(11000));
    TEST_ASSERT_EQUAL_UINT32(3590, f.policy.effectiveRemainingSeconds(follower.slot));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)MutationStatus::OK,
        (uint8_t)f.store.releaseQueued(first.slot));
    // No intervening policy tick sees FREE. Fixture::enqueue explicitly uses
    // the existing policy API, the single-slot primitive used by the controller.
    const auto replacement = f.enqueue(3600, 11000);
    TEST_ASSERT_EQUAL_UINT8(first.slot, replacement.slot);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Status::READY, (uint8_t)f.policy.service(11000));
    TEST_ASSERT_EQUAL_UINT32(3600, f.policy.effectiveRemainingSeconds(replacement.slot));
    TEST_ASSERT_EQUAL_UINT32(3590, f.policy.effectiveRemainingSeconds(follower.slot));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Status::READY, (uint8_t)f.policy.service(71000));
    TEST_ASSERT_EQUAL_UINT32(3540, f.policy.effectiveRemainingSeconds(replacement.slot));
    TEST_ASSERT_EQUAL_UINT32(3530, f.policy.effectiveRemainingSeconds(follower.slot));
    TEST_ASSERT_EQUAL_UINT32(3540, f.store.recordAt(replacement.slot)->remainingActiveSeconds);
    TEST_ASSERT_EQUAL_UINT32(3540, f.store.recordAt(follower.slot)->remainingActiveSeconds);
}

void testTrackedCreationSamplesAfterDurableCommitAndCountsBeforeFirstService() {
    DeliveryFixture f; f.initializeEmpty(1000);
    const unsigned writes = f.storage.eventWrites;
    unsigned clockReads = 0;
    const auto clock = [&] {
        ++clockReads;
        // The in-memory authoritative index is published only after durable
        // write/commit/readback. Tracking must not sample the pre-enqueue time.
        TEST_ASSERT_EQUAL_UINT(writes + 1U, f.storage.eventWrites);
        TEST_ASSERT_EQUAL_UINT(1, f.store.queuedCount());
        TEST_ASSERT_EQUAL_UINT32(3600, f.store.queuedAt(0)->remainingActiveSeconds);
        return 1100U;
    };
    TrackedCreationSink<decltype(clock)> sink(f.store, f.controller, clock);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)EventProducers::CreationResult::ENQUEUED,
        (uint8_t)sink.create(event(3600)));
    TEST_ASSERT_EQUAL_UINT(1, clockReads);
    serviceNow(f.controller, true, [] { return 61099U; });
    TEST_ASSERT_EQUAL_UINT32(3541, f.controller.effectiveRemainingSeconds());
    TEST_ASSERT_EQUAL_UINT32(3600, f.store.queuedAt(0)->remainingActiveSeconds);
    serviceNow(f.controller, true, [] { return 61100U; });
    TEST_ASSERT_EQUAL_UINT32(3540, f.controller.effectiveRemainingSeconds());
    TEST_ASSERT_EQUAL_UINT32(3540, f.store.queuedAt(0)->remainingActiveSeconds);
    TEST_ASSERT_EQUAL_UINT(writes + 2U, f.storage.eventWrites);
}

void testTrackedSlotReusePreservesWaitingHeadFollowerAndFifo() {
    DeliveryFixture f; f.initializeEmpty(1000);
    const uint8_t first = f.enqueueTracked(3600, 1000);
    const uint8_t head = f.enqueueTracked(3600, 1000);
    const uint8_t follower = f.enqueueTracked(3600, 1000);
    f.waitAdmission(1000);
    uint8_t bytes[Protocol::MAX_PACKET_SIZE] = {}; size_t length = 0;
    f.response(EventProtocol::AdmissionStatus::ADMITTED, bytes, length);
    f.controller.admissionCandidate(bytes, length, 1030);
    f.waitAdmission(1030);
    TEST_ASSERT_EQUAL_UINT8(head, f.controller.activeSlot());
    const auto headBefore = *f.store.recordAt(head);
    const auto followerBefore = *f.store.recordAt(follower);
    const unsigned writes = f.storage.eventWrites;
    const uint32_t deadline = f.controller.deadlineOrigin();
    const uint8_t replacement = f.enqueueTracked(3600, 1100);
    TEST_ASSERT_EQUAL_UINT8(first, replacement);
    TEST_ASSERT_EQUAL_UINT(writes + 1U, f.storage.eventWrites);
    TEST_ASSERT_EQUAL_MEMORY(&headBefore, f.store.recordAt(head), sizeof(headBefore));
    TEST_ASSERT_EQUAL_MEMORY(&followerBefore, f.store.recordAt(follower), sizeof(followerBefore));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RuntimeState::WAIT_ADMISSION, (uint8_t)f.controller.state());
    TEST_ASSERT_EQUAL_UINT32(deadline, f.controller.deadlineOrigin());
    const uint8_t expected[] = {head, follower, replacement};
    for (uint8_t i = 0; i < 3; ++i) {
        uint8_t slot = 0xFF;
        TEST_ASSERT_NOT_NULL(f.store.queuedAt(i, &slot));
        TEST_ASSERT_EQUAL_UINT8(expected[i], slot);
    }
    serviceNow(f.controller, false, [] { return 2000U; });
    TEST_ASSERT_EQUAL_UINT32(3599, f.controller.effectiveRemainingSeconds());
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RuntimeState::WAIT_ADMISSION, (uint8_t)f.controller.state());
    // No attempt/deadline/checkpoint refresh for existing records on enqueue.
    TEST_ASSERT_EQUAL_UINT32(deadline, f.controller.deadlineOrigin());
    serviceNow(f.controller, true, [] { return 61000U; });
    TEST_ASSERT_EQUAL_UINT32(3540, f.store.recordAt(head)->remainingActiveSeconds);
    TEST_ASSERT_EQUAL_UINT32(3540, f.store.recordAt(follower)->remainingActiveSeconds);
    TEST_ASSERT_EQUAL_UINT32(3600, f.store.recordAt(replacement)->remainingActiveSeconds);
    TEST_ASSERT_EQUAL_UINT8(1, f.store.recordAt(head)->attemptsUsed);
    TEST_ASSERT_EQUAL_UINT8(0, f.store.recordAt(follower)->attemptsUsed);
    TEST_ASSERT_EQUAL_UINT8(0, f.store.recordAt(replacement)->attemptsUsed);
    serviceNow(f.controller, true, [] { return 61100U; });
    TEST_ASSERT_EQUAL_UINT32(3540, f.store.recordAt(replacement)->remainingActiveSeconds);
}

void testTrackingFailurePreservesDurableEventAndBlocksDeliveryAndCreation() {
    DeliveryFixture f;
    TEST_ASSERT_EQUAL_UINT8((uint8_t)NodeEventStore::Status::READY,
        (uint8_t)f.store.recover(f.storage, f.entropy, 0x10));
    // Simulate unavailable controller tracking after successful store enqueue.
    const auto clock = [] { return 1000U; };
    TrackedCreationSink<decltype(clock)> sink(f.store, f.controller, clock);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)EventProducers::CreationResult::STORAGE_FAILURE,
        (uint8_t)sink.create(event(3600)));
    TEST_ASSERT_TRUE(sink.lastResultAvailable());
    TEST_ASSERT_EQUAL_UINT8((uint8_t)EnqueueStatus::ENQUEUED, (uint8_t)sink.lastResult().status);
    TEST_ASSERT_TRUE(f.controller.degraded());
    TEST_ASSERT_TRUE(f.store.healthy());
    TEST_ASSERT_EQUAL_UINT(1, f.store.ownedCount());
    const auto before = *f.store.queuedAt(0);
    const unsigned writes = f.storage.eventWrites, identityWrites = f.storage.metadataWrites;
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ControllerStatus::DEGRADED,
        (uint8_t)serviceNow(f.controller, false, clock).result.status);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RadioActionType::NONE,
        (uint8_t)f.controller.grantTransmit(1000).action.type);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)EventProducers::CreationResult::STORAGE_FAILURE,
        (uint8_t)sink.create(event(3600)));
    TEST_ASSERT_EQUAL_UINT(writes, f.storage.eventWrites);
    TEST_ASSERT_EQUAL_UINT(identityWrites, f.storage.metadataWrites);
    TEST_ASSERT_EQUAL_MEMORY(&before, f.store.queuedAt(0), sizeof(before));
    // Durable custody survives; normal recovery still applies its existing debit.
    f.storage.clearFault();
    TEST_ASSERT_EQUAL_UINT8((uint8_t)NodeEventStore::Status::READY,
        (uint8_t)f.store.recover(f.storage, f.entropy, 0x10));
    f.recoverController(0);
    TEST_ASSERT_EQUAL_UINT32(before.eventId, f.store.queuedAt(0)->eventId);
    TEST_ASSERT_EQUAL_UINT32(before.eventEpoch, f.store.queuedAt(0)->eventEpoch);
    TEST_ASSERT_EQUAL_UINT32(3540, f.controller.effectiveRemainingSeconds());
    TEST_ASSERT_EQUAL_UINT8(0, f.store.queuedAt(0)->attemptsUsed);
}

void testRejectedCreationNeverTracksOrRefreshesExistingSlots() {
    using EventProducers::CreationResult;
    for (bool queueFull : {false, true}) {
        DeliveryFixture f; f.initializeEmpty();
        for (unsigned i = 0; i < (queueFull ? 8U : 1U); ++i) f.enqueueTracked(3600, 0);
        serviceNow(f.controller, true, [] { return 10000U; });
        unsigned clockReads = 0;
        const auto clock = [&] { ++clockReads; return 10000U; };
        TrackedCreationSink<decltype(clock)> sink(f.store, f.controller, clock);
        if (!queueFull) f.storage.fault = Fault::WRITE;
        TEST_ASSERT_EQUAL_UINT8((uint8_t)(queueFull ? CreationResult::QUEUE_FULL : CreationResult::STORAGE_FAILURE),
            (uint8_t)sink.create(event(3600)));
        TEST_ASSERT_EQUAL_UINT(0, clockReads);
        TEST_ASSERT_EQUAL_UINT32(3590, f.controller.effectiveRemainingSeconds());
        TEST_ASSERT_EQUAL_UINT8(0, f.store.queuedAt(0)->attemptsUsed);
    }
    for (uint8_t slot : {(uint8_t)0, (uint8_t)NodeEventStore::NODE_EVENT_CAPACITY}) {
        DeliveryFixture f; f.initializeEmpty();
        TEST_ASSERT_EQUAL_UINT8((uint8_t)ControllerStatus::POLICY_FAILURE,
            (uint8_t)f.controller.trackEnqueued(slot, 1));
        TEST_ASSERT_TRUE(f.controller.degraded());
        TEST_ASSERT_EQUAL_UINT(0, f.store.ownedCount());
    }
}

void assertCompletionSamplesAfterEnqueue(uint32_t loopStart, uint32_t enqueuedAt,
                                        uint32_t completedAt) {
    DeliveryFixture f; f.initializeEmpty(loopStart);
    EventTxDiagnostics::Observer observer; observer.enable();
    f.controller.recover(f.store, 0x10, 0x20, f.sequence, f.jitter,
        loopStart, &f.diagnostics, &observer);
    const uint8_t head = f.enqueueTracked(3600, loopStart);
    f.transmit(loopStart);
    const uint8_t follower = f.enqueueTracked(3600, enqueuedAt);
    const unsigned writes = f.storage.eventWrites;
    unsigned clockReads = 0;
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ControllerStatus::OK,
        (uint8_t)txCompletedNow(f.controller, [&] { ++clockReads; return completedAt; }).status);
    TEST_ASSERT_EQUAL_UINT(1, clockReads);
    serviceNow(f.controller, false, [=] { return completedAt; });
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RuntimeState::WAIT_ADMISSION, (uint8_t)f.controller.state());
    TEST_ASSERT_EQUAL_UINT32(completedAt, f.controller.deadlineOrigin());
    TEST_ASSERT_EQUAL_UINT8(head, f.controller.activeSlot());
    TEST_ASSERT_EQUAL_UINT8(1, f.store.recordAt(head)->attemptsUsed);
    TEST_ASSERT_EQUAL_UINT8(0, f.store.recordAt(follower)->attemptsUsed);
    TEST_ASSERT_EQUAL_UINT(writes, f.storage.eventWrites);
    TEST_ASSERT_EQUAL_UINT32(0, f.diagnostics.eventSnapshot().counters.eventsExpired);
    TEST_ASSERT_EQUAL_UINT16(0, observer.snapshot().counters[(uint8_t)EventTxDiagnostics::Counter::CLOCK_REVERSED]);
    serviceNow(f.controller, false, [=] { return completedAt + 2499U; });
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RuntimeState::WAIT_ADMISSION, (uint8_t)f.controller.state());
    serviceNow(f.controller, false, [=] { return completedAt + 2500U; });
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RuntimeState::BACKOFF, (uint8_t)f.controller.state());
    serviceNow(f.controller, false, [=] { return completedAt + 3500U; });
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RuntimeState::READY, (uint8_t)f.controller.state());
    // Both occupied slots retain their own checkpoint boundary across wrap.
    serviceNow(f.controller, true, [=] { return loopStart + 60000U; });
    TEST_ASSERT_EQUAL_UINT32(3540, f.store.recordAt(head)->remainingActiveSeconds);
    TEST_ASSERT_EQUAL_UINT32(3600, f.store.recordAt(follower)->remainingActiveSeconds);
    serviceNow(f.controller, true, [=] { return enqueuedAt + 60000U; });
    TEST_ASSERT_EQUAL_UINT32(3540, f.store.recordAt(follower)->remainingActiveSeconds);
    TEST_ASSERT_EQUAL_UINT16(0, observer.snapshot().counters[(uint8_t)EventTxDiagnostics::Counter::CLOCK_REVERSED]);
}

void testEnqueueThenCompletionUsesFreshClockNotLoopStart() {
    assertCompletionSamplesAfterEnqueue(224677U, 224678U, 224678U);
    assertCompletionSamplesAfterEnqueue(1000U, 1100U, 1110U);
}

void testEnqueueThenCompletionPreservesMillisWrap() {
    assertCompletionSamplesAfterEnqueue(0xFFFFFFFEU, 2U, 3U);
}

void testControllerRecoveryArbitrationAndFifo() {
    DeliveryFixture f; f.initializeEmpty();
    TEST_ASSERT_FALSE(f.controller.hasActiveEvent());
    const uint8_t first = f.enqueue(); const uint8_t second = f.enqueue();
    f.recoverController();
    TEST_ASSERT_EQUAL_UINT8(first, f.controller.activeSlot());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RuntimeState::QUEUED), static_cast<uint8_t>(f.controller.state()));
    f.controller.service(0, true);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RuntimeState::QUEUED), static_cast<uint8_t>(f.controller.state()));
    TEST_ASSERT_EQUAL_UINT8(0, f.store.recordAt(first)->attemptsUsed);
    f.ready();
    f.controller.service(0, true);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RuntimeState::QUEUED), static_cast<uint8_t>(f.controller.state()));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ControllerStatus::INVALID_STATE), static_cast<uint8_t>(f.controller.grantTransmit(0).status));
    TEST_ASSERT_NOT_EQUAL(second, f.controller.activeSlot());
}

void testPrepareWritesAttemptBeforeCanonicalTransmit() {
    DeliveryFixture f; f.initializeEmpty(); const uint8_t slot = f.enqueue(); f.recoverController();
    const unsigned before = f.storage.eventWrites;
    ControllerResult tx = f.transmit(1000);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RadioActionType::TRANSMIT), static_cast<uint8_t>(tx.action.type));
    TEST_ASSERT_EQUAL_UINT8(1, f.store.recordAt(slot)->attemptsUsed);
    TEST_ASSERT_TRUE(f.storage.eventWrites > before);
    TEST_ASSERT_EQUAL_UINT(1, f.sequence.calls);
    EventProtocol::Event decoded = {};
    TEST_ASSERT_TRUE(EventProtocol::decodeEvent(tx.action.bytes, tx.action.length, decoded));
    TEST_ASSERT_EQUAL_UINT32(300, decoded.lifetimeBudgetSeconds);
    TEST_ASSERT_EQUAL_UINT32(f.store.recordAt(slot)->eventId, decoded.id);
    TEST_ASSERT_EQUAL_UINT8(0x40, decoded.sequence);
}

void testAttemptPersistenceFailureExposesNoTransmit() {
    DeliveryFixture f; f.initializeEmpty(); f.enqueue(); f.recoverController(); f.ready();
    f.storage.fault = Fault::WRITE;
    ControllerResult output = f.controller.grantTransmit(0);
    TEST_ASSERT_NOT_EQUAL(static_cast<uint8_t>(RadioActionType::TRANSMIT), static_cast<uint8_t>(output.action.type));
    TEST_ASSERT_TRUE(f.controller.degraded());
    TEST_ASSERT_EQUAL_UINT(0, f.sequence.calls);
}

void testTxLifecycleTimeoutBackoffAndJitter() {
    DeliveryFixture f; f.initializeEmpty(); f.enqueue(); f.recoverController();
    f.jitter.value = 250;
    ControllerResult tx = f.transmit(100);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RuntimeState::TX), static_cast<uint8_t>(f.controller.state()));
    f.controller.txStarted(500);
    TEST_ASSERT_EQUAL_UINT32(0, f.controller.deadlineDuration());
    ControllerResult done = f.controller.txCompleted(1000);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RadioActionType::RECEIVE), static_cast<uint8_t>(done.action.type));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RuntimeState::WAIT_ADMISSION), static_cast<uint8_t>(f.controller.state()));
    f.controller.service(3499, false);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RuntimeState::WAIT_ADMISSION), static_cast<uint8_t>(f.controller.state()));
    ControllerResult timeout = f.controller.service(3500, false);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RadioActionType::RECEIVE), static_cast<uint8_t>(timeout.action.type));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RuntimeState::BACKOFF), static_cast<uint8_t>(f.controller.state()));
    TEST_ASSERT_EQUAL_UINT32(1250, f.controller.deadlineDuration()); TEST_ASSERT_EQUAL_UINT(1, f.jitter.calls);
    f.controller.service(4749, false); TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RuntimeState::BACKOFF), static_cast<uint8_t>(f.controller.state()));
    f.controller.service(4750, true); TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RuntimeState::QUEUED), static_cast<uint8_t>(f.controller.state()));
    TEST_ASSERT_EQUAL_UINT(1, f.jitter.calls);
}

void testAdmissionCorrelationIgnoredWithoutDeadlineChange() {
    DeliveryFixture f; f.initializeEmpty(); f.enqueue(); f.recoverController(); f.waitAdmission(0);
    uint8_t bytes[Protocol::MAX_PACKET_SIZE] = {}; size_t length = 0;
    f.response(EventProtocol::AdmissionStatus::ADMITTED, bytes, length);
    const uint32_t origin = f.controller.deadlineOrigin();
    const size_t fields[] = {2, 3, 4, 5, 7, 11};
    for (size_t i = 0; i < sizeof(fields)/sizeof(fields[0]); ++i) {
        uint8_t altered[Protocol::MAX_PACKET_SIZE] = {};
        for (size_t j = 0; j < length; ++j) altered[j] = bytes[j];
        altered[fields[i]] ^= 1U;
        ControllerResult ignored = f.controller.admissionCandidate(altered, length, 100 + static_cast<uint32_t>(i));
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RadioActionType::RECEIVE), static_cast<uint8_t>(ignored.action.type));
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RuntimeState::WAIT_ADMISSION), static_cast<uint8_t>(f.controller.state()));
        TEST_ASSERT_EQUAL_UINT32(origin, f.controller.deadlineOrigin());
    }
    f.controller.admissionCandidate(bytes, length - 1, 200);
    TEST_ASSERT_EQUAL_UINT32(origin, f.controller.deadlineOrigin());
}

void testAdmittedReleaseAndNextHead() {
    DeliveryFixture f; f.initializeEmpty(); const uint8_t first = f.enqueue(); const uint8_t second = f.enqueue(); f.recoverController(); f.waitAdmission();
    uint8_t bytes[Protocol::MAX_PACKET_SIZE] = {}; size_t length = 0; f.response(EventProtocol::AdmissionStatus::ADMITTED, bytes, length);
    ControllerResult admitted = f.controller.admissionCandidate(bytes, length, 1);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RuntimeState::RELEASED), static_cast<uint8_t>(f.controller.state()));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NodeState::FREE), static_cast<uint8_t>(f.store.recordAt(first)->state));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RadioActionType::RECEIVE), static_cast<uint8_t>(admitted.action.type));
    f.controller.service(2, false);
    TEST_ASSERT_EQUAL_UINT8(second, f.controller.activeSlot());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RuntimeState::READY), static_cast<uint8_t>(f.controller.state()));
}

void testAdmissionStatusesAndTerminalReclaim() {
    const EventProtocol::AdmissionStatus terminal[] = {EventProtocol::AdmissionStatus::IDENTITY_CONTENT_MISMATCH,
        EventProtocol::AdmissionStatus::UNSUPPORTED_EVENT, EventProtocol::AdmissionStatus::MALFORMED_EVENT};
    for (auto status : terminal) {
        DeliveryFixture f; f.initializeEmpty(); const uint8_t slot = f.enqueue(); f.recoverController(); f.waitAdmission();
        uint8_t bytes[Protocol::MAX_PACKET_SIZE] = {}; size_t length = 0; f.response(status, bytes, length);
        f.controller.admissionCandidate(bytes, length, 1);
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RuntimeState::FAILED), static_cast<uint8_t>(f.controller.state()));
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NodeState::FAILED), static_cast<uint8_t>(f.store.recordAt(slot)->state));
        f.controller.service(2, false); TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RuntimeState::FAILED), static_cast<uint8_t>(f.controller.state()));
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ControllerStatus::OK), static_cast<uint8_t>(f.controller.reclaimTerminal().status));
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NodeState::FREE), static_cast<uint8_t>(f.store.recordAt(slot)->state));
    }
}

void testAttemptFiveFailureAndSuccess() {
    const bool outcomes[] = {false, true};
    for (bool admitted : outcomes) {
        FakeStorage storage; FakeEntropy entropy; seedFree(storage); seedMetadata(storage); seedQueued(storage, 0, 300, 4);
        NodeEventStore::Store store; TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NodeEventStore::Status::READY), static_cast<uint8_t>(store.recover(storage, entropy, 0x10)));
        FakeSequence sequence; FakeJitter jitter; Controller controller;
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ControllerStatus::OK), static_cast<uint8_t>(controller.recover(store, 0x10, 0x20, sequence, jitter, 0).status));
        controller.service(0, false); controller.grantTransmit(0); controller.txCompleted(0);
        TEST_ASSERT_EQUAL_UINT8(5, store.recordAt(0)->attemptsUsed);
        if (admitted) {
            uint8_t bytes[Protocol::MAX_PACKET_SIZE] = {}; size_t length = 0;
            auto response = EventProtocol::makeAdmissionResponse(controller.attemptEvent(), EventProtocol::AdmissionStatus::ADMITTED);
            TEST_ASSERT_TRUE(EventProtocol::encodeAdmissionResponse(response, bytes, sizeof(bytes), length));
            controller.admissionCandidate(bytes, length, 1);
            TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RuntimeState::RELEASED), static_cast<uint8_t>(controller.state()));
        } else {
            controller.service(2500, false);
            TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RuntimeState::FAILED), static_cast<uint8_t>(controller.state()));
            TEST_ASSERT_EQUAL_UINT8(5, store.recordAt(0)->attemptsUsed);
            TEST_ASSERT_EQUAL_UINT(0, jitter.calls);
        }
    }
}

void testExpiryPreemptsTxWaitAndBackoff() {
    DeliveryFixture f; f.initializeEmpty(); const uint8_t slot = f.enqueue(120); f.recoverController(); f.transmit(0);
    ControllerResult expired = f.controller.txStarted(60000);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RuntimeState::EXPIRED), static_cast<uint8_t>(f.controller.state()));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NodeState::EXPIRED), static_cast<uint8_t>(f.store.recordAt(slot)->state));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RadioActionType::RECEIVE), static_cast<uint8_t>(expired.action.type));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ControllerStatus::OK), static_cast<uint8_t>(f.controller.reclaimTerminal().status));
}

void testTxStartFailureBackoffAndStorageFailureFailClosed() {
    DeliveryFixture f; f.initializeEmpty(); f.enqueue(); f.recoverController(); f.transmit(0);
    ControllerResult failed = f.controller.txStartFailed(10);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RadioActionType::RECEIVE), static_cast<uint8_t>(failed.action.type));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RuntimeState::BACKOFF), static_cast<uint8_t>(f.controller.state()));

    DeliveryFixture g; g.initializeEmpty(); g.enqueue(); g.recoverController(); g.waitAdmission();
    uint8_t bytes[Protocol::MAX_PACKET_SIZE] = {}; size_t length = 0; g.response(EventProtocol::AdmissionStatus::ADMITTED, bytes, length);
    g.storage.fault = Fault::WRITE;
    ControllerResult release = g.controller.admissionCandidate(bytes, length, 1);
    TEST_ASSERT_TRUE(g.controller.degraded());
    TEST_ASSERT_NOT_EQUAL(static_cast<uint8_t>(RuntimeState::RELEASED), static_cast<uint8_t>(g.controller.state()));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RadioActionType::RECEIVE), static_cast<uint8_t>(release.action.type));
}

void testHubUnavailableUsesOnlyFrozenEvidencePaths() {
    DeliveryFixture timeout; timeout.initializeEmpty(); timeout.enqueue();
    timeout.recoverController(); timeout.waitAdmission(0);
    TEST_ASSERT_FALSE(timeout.diagnostics.eventSnapshot().hubUnavailable);
    timeout.controller.service(2499, false);
    TEST_ASSERT_FALSE(timeout.diagnostics.eventSnapshot().hubUnavailable);
    timeout.controller.service(2500, false);
    TEST_ASSERT_TRUE(timeout.diagnostics.eventSnapshot().hubUnavailable);
    timeout.controller.service(3000, false);
    TEST_ASSERT_TRUE(timeout.diagnostics.eventSnapshot().hubUnavailable);
    timeout.controller.successfulHubExchange();
    TEST_ASSERT_FALSE(timeout.diagnostics.eventSnapshot().hubUnavailable);

    DeliveryFixture capacity; capacity.initializeEmpty(); capacity.enqueue();
    capacity.recoverController(); capacity.waitAdmission(0);
    uint8_t bytes[Protocol::MAX_PACKET_SIZE] = {}; size_t length = 0;
    capacity.response(EventProtocol::AdmissionStatus::CAPACITY, bytes, length);
    capacity.controller.admissionCandidate(bytes, length, 1);
    TEST_ASSERT_TRUE(capacity.diagnostics.eventSnapshot().hubUnavailable);
    capacity.controller.service(1001, false);
    capacity.controller.service(1001, false);
    capacity.controller.grantTransmit(1001);
    capacity.controller.txCompleted(1001);
    capacity.response(EventProtocol::AdmissionStatus::ADMITTED, bytes, length);
    capacity.controller.admissionCandidate(bytes, length, 1002);
    TEST_ASSERT_FALSE(capacity.diagnostics.eventSnapshot().hubUnavailable);
    TEST_ASSERT_EQUAL_UINT32(1,
        capacity.diagnostics.eventSnapshot().counters.admissionsAcknowledged);

    DeliveryFixture localFailure; localFailure.initializeEmpty(); localFailure.enqueue();
    localFailure.recoverController(); localFailure.transmit(0);
    localFailure.controller.txStartFailed(1);
    TEST_ASSERT_FALSE(localFailure.diagnostics.eventSnapshot().hubUnavailable);
}

void testRecoveredTerminalsReclaimedAndMixedQueuePreserved() {
    DeliveryFixture f; f.initializeEmpty();
    const auto expired = f.enqueue(); const auto failed = f.enqueue();
    const auto queued = f.enqueue();
    f.store.markExpired(expired); f.store.markFailed(failed);
    const NodeRecord before = *f.store.recordAt(queued);
    NodeEventStore::Store rebooted;
    TEST_ASSERT_EQUAL_UINT8((uint8_t)NodeEventStore::Status::READY,
        (uint8_t)rebooted.recover(f.storage, f.entropy, 0x10));
    const unsigned metadataWrites = f.storage.metadataWrites;
    TEST_ASSERT_TRUE(reclaimTerminals(rebooted, nullptr));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)NodeState::FREE, (uint8_t)rebooted.recordAt(expired)->state);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)NodeState::FREE, (uint8_t)rebooted.recordAt(failed)->state);
    TEST_ASSERT_EQUAL_MEMORY(&before, rebooted.recordAt(queued), sizeof(before));
    TEST_ASSERT_EQUAL_UINT(metadataWrites, f.storage.metadataWrites);
    TEST_ASSERT_EQUAL_UINT(1, rebooted.ownedCount());
    Controller controller;
    controller.recover(rebooted, 0x10, 0x20, f.sequence, f.jitter, 0);
    TEST_ASSERT_EQUAL_UINT8(queued, controller.activeSlot());
}

void testRuntimeTerminalReclaimAdvancesFifo() {
    for (bool expire : {false, true}) {
        DeliveryFixture f; f.initializeEmpty();
        const auto first = f.enqueue(120); const auto next = f.enqueue(300);
        f.recoverController();
        if (expire) f.controller.service(60000, true);
        else {
            f.waitAdmission();
            uint8_t bytes[32]; size_t length = 0;
            f.response(EventProtocol::AdmissionStatus::UNSUPPORTED_EVENT, bytes, length);
            f.controller.admissionCandidate(bytes, length, 1);
        }
        const unsigned writes = f.storage.eventWrites;
        TEST_ASSERT_TRUE(isTerminalRecord(f.store.recordAt(first)));
        TEST_ASSERT_TRUE(reclaimTerminals(f.store, &f.controller));
        TEST_ASSERT_EQUAL_UINT(writes + 1, f.storage.eventWrites);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)NodeState::FREE, (uint8_t)f.store.recordAt(first)->state);
        f.controller.service(expire ? 60000 : 2, false);
        TEST_ASSERT_EQUAL_UINT8(next, f.controller.activeSlot());
        TEST_ASSERT_EQUAL_UINT8((uint8_t)RuntimeState::READY, (uint8_t)f.controller.state());
    }
}

void testReclaimFailuresFailClosedAtBootAndRuntime() {
    const Fault faults[] = {Fault::WRITE, Fault::COMMIT, Fault::READ_MISSING,
        Fault::READ_UNAVAILABLE, Fault::MISMATCH, Fault::BAD_CRC};
    for (auto fault : faults) for (bool runtime : {false, true}) {
        DeliveryFixture f; f.initializeEmpty(); const auto slot = f.enqueue(120);
        f.recoverController(); f.controller.service(60000, true);
        f.storage.fault = fault;
        TEST_ASSERT_FALSE(reclaimTerminals(f.store, runtime ? &f.controller : nullptr));
        TEST_ASSERT_FALSE(f.store.healthy());
        TEST_ASSERT_TRUE(f.diagnostics.eventSnapshot().persistenceDegraded);
        TEST_ASSERT_EQUAL_UINT32(1, f.diagnostics.eventSnapshot().counters.persistenceFailures);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)NodeState::EXPIRED, (uint8_t)f.store.recordAt(slot)->state);
        TEST_ASSERT_NOT_EQUAL((uint8_t)RadioActionType::TRANSMIT,
            (uint8_t)f.controller.grantTransmit(60001).action.type);
    }
}

void testReclaimNeverTouchesNonterminalDeliveryStates() {
    for (unsigned state = 0; state < 5; ++state) {
        DeliveryFixture f; f.initializeEmpty(); const auto slot = f.enqueue(); f.recoverController();
        if (state >= 1) f.ready();
        if (state >= 2) f.controller.grantTransmit(0);
        if (state >= 3) f.controller.txCompleted(0);
        if (state >= 4) f.controller.service(2500, false);
        const NodeRecord before = *f.store.recordAt(slot);
        const auto runtimeBefore = f.controller.state();
        const unsigned writes = f.storage.eventWrites;
        TEST_ASSERT_TRUE(reclaimTerminals(f.store, &f.controller));
        TEST_ASSERT_EQUAL_UINT(writes, f.storage.eventWrites);
        TEST_ASSERT_EQUAL_MEMORY(&before, f.store.recordAt(slot), sizeof(before));
        TEST_ASSERT_EQUAL_UINT8((uint8_t)runtimeBefore, (uint8_t)f.controller.state());
    }
}

void testParallelExpiryReclamationRestoresCapacity() {
    DeliveryFixture f; f.initializeEmpty();
    for (unsigned i = 0; i < 8; ++i) f.enqueue(120);
    f.recoverController(); f.controller.service(60000, false);
    TEST_ASSERT_EQUAL_UINT(8, f.store.ownedCount());
    TEST_ASSERT_TRUE(reclaimTerminals(f.store, &f.controller));
    TEST_ASSERT_EQUAL_UINT(0, f.store.ownedCount());
    const auto next = f.store.enqueue(event());
    TEST_ASSERT_EQUAL_UINT8((uint8_t)EnqueueStatus::ENQUEUED, (uint8_t)next.status);
    TEST_ASSERT_TRUE(next.identity.id > 8);
    f.controller.service(60001, false);
    TEST_ASSERT_EQUAL_UINT8(next.slot, f.controller.activeSlot());
}

void testLifecycleObservationDoesNotChangeDeliveryOrStorage() {
    DeliveryFixture observed, plain;
    observed.initializeEmpty(); plain.initializeEmpty();
    observed.enqueue(); plain.enqueue();
    EventTxDiagnostics::Observer observer; observer.enable();
    observed.controller.recover(observed.store, 0x10, 0x20, observed.sequence,
        observed.jitter, 0, &observed.diagnostics, &observer);
    plain.recoverController();
    for (auto* f : {&observed, &plain}) {
        f->waitAdmission(100);
        f->controller.service(2600, false);
        f->controller.service(3600, false);
        f->controller.grantTransmit(3600);
        f->controller.txStartFailed(3601);
    }
    TEST_ASSERT_EQUAL_MEMORY(&plain.storage.events, &observed.storage.events,
        sizeof(plain.storage.events));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)plain.controller.state(), (uint8_t)observed.controller.state());
    using C = EventTxDiagnostics::Counter;
    const auto& s = observer.snapshot();
    TEST_ASSERT_EQUAL_UINT16(2, s.counters[(uint8_t)C::TX_ENTERED]);
    TEST_ASSERT_EQUAL_UINT16(1, s.counters[(uint8_t)C::WAIT_ENTERED]);
    TEST_ASSERT_EQUAL_UINT16(1, s.counters[(uint8_t)C::ADMISSION_TIMEOUT]);
    TEST_ASSERT_EQUAL_UINT16(2, s.counters[(uint8_t)C::BACKOFF_ENTERED]);
    TEST_ASSERT_EQUAL_UINT16(1, s.counters[(uint8_t)C::BACKOFF_DUE]);
    TEST_ASSERT_EQUAL_UINT16(1, s.counters[(uint8_t)C::RETRY_READY]);
    TEST_ASSERT_EQUAL_UINT8(2, s.attempt);
}

void testExistingBackwardTimeInputExpiresQueueAndIsObserved() {
    // Negative control for the former production call order. The policy and
    // observer still expose bad callers; serviceNow repairs integration only.
    DeliveryFixture f; f.initializeEmpty(); f.enqueue(3600); f.enqueue(3600);
    EventTxDiagnostics::Observer observer; observer.enable();
    f.controller.recover(f.store, 0x10, 0x20, f.sequence, f.jitter, 0, &f.diagnostics, &observer);
    f.waitAdmission(100);
    uint8_t unrelated[6] = {};
    f.controller.admissionCandidate(unrelated, sizeof(unrelated), 110);
    f.controller.service(105, false);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RuntimeState::EXPIRED, (uint8_t)f.controller.state());
    TEST_ASSERT_EQUAL_UINT32(2, f.diagnostics.eventSnapshot().counters.eventsExpired);
    using C = EventTxDiagnostics::Counter;
    TEST_ASSERT_EQUAL_UINT16(1, observer.snapshot().counters[(uint8_t)C::CLOCK_REVERSED]);
    TEST_ASSERT_EQUAL_UINT16(1, observer.snapshot().counters[(uint8_t)C::EXPIRED_WAIT]);
    TEST_ASSERT_EQUAL_UINT16(1, observer.snapshot().counters[(uint8_t)C::EXPIRED_QUEUED]);
    TEST_ASSERT_EQUAL_UINT32(110, observer.snapshot().previousClock);
    TEST_ASSERT_EQUAL_UINT32(105, observer.snapshot().inputClock);
}

void assertServiceSamplesAfterPacket(uint32_t loopStart, uint32_t packetTime,
                                    uint32_t serviceTime) {
    DeliveryFixture f; f.initializeEmpty(loopStart);
    const uint8_t head = f.enqueue(3600), follower = f.enqueue(3600);
    EventTxDiagnostics::Observer observer; observer.enable();
    f.controller.recover(f.store, 0x10, 0x20, f.sequence, f.jitter,
        loopStart, &f.diagnostics, &observer);
    f.waitAdmission(loopStart);
    const EventRecords::NodeRecord originalHead = *f.store.recordAt(head);
    const EventRecords::NodeRecord originalFollower = *f.store.recordAt(follower);
    const unsigned writes = f.storage.eventWrites;
    const unsigned identityWrites = f.storage.metadataWrites;
    uint8_t unrelated[6] = {};
    f.controller.admissionCandidate(unrelated, sizeof(unrelated), packetTime);
    unsigned clockReads = 0;
    // Same boundary called by production serviceNodeEvents(). loopStart is
    // deliberately still in scope but cannot be supplied instead of a clock.
    const auto tick = serviceNow(f.controller, false, [&] {
        ++clockReads; return serviceTime;
    });
    TEST_ASSERT_EQUAL_UINT(1, clockReads);
    TEST_ASSERT_EQUAL_UINT32(serviceTime, tick.now);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ControllerStatus::OK, (uint8_t)tick.result.status);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RuntimeState::WAIT_ADMISSION, (uint8_t)f.controller.state());
    TEST_ASSERT_EQUAL_UINT8(head, f.controller.activeSlot());
    TEST_ASSERT_EQUAL_UINT(2, f.store.queuedCount());
    TEST_ASSERT_EQUAL_UINT8(1, f.store.recordAt(head)->attemptsUsed);
    TEST_ASSERT_EQUAL_UINT8(0, f.store.recordAt(follower)->attemptsUsed);
    const EventRecords::NodeRecord* originals[] = {&originalHead, &originalFollower};
    const uint8_t slots[] = {head, follower};
    for (unsigned i = 0; i < 2; ++i) {
        uint8_t before[NODE_RECORD_SIZE] = {}, after[NODE_RECORD_SIZE] = {};
        encodeNodeRecord(*originals[i], before, sizeof(before));
        encodeNodeRecord(*f.store.recordAt(slots[i]), after, sizeof(after));
        TEST_ASSERT_EQUAL_MEMORY(before, after, sizeof(before));
    }
    TEST_ASSERT_EQUAL_UINT(writes, f.storage.eventWrites);
    TEST_ASSERT_EQUAL_UINT(identityWrites, f.storage.metadataWrites);
    TEST_ASSERT_EQUAL_UINT32(0, f.diagnostics.eventSnapshot().counters.eventsExpired);
    TEST_ASSERT_EQUAL_UINT16(0, observer.snapshot().counters[
        (uint8_t)EventTxDiagnostics::Counter::CLOCK_REVERSED]);
    // Fresh packet/service times must not reset the original admission deadline.
    serviceNow(f.controller, false, [=] { return loopStart + 2499U; });
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RuntimeState::WAIT_ADMISSION, (uint8_t)f.controller.state());
    serviceNow(f.controller, false, [=] { return loopStart + 2500U; });
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RuntimeState::BACKOFF, (uint8_t)f.controller.state());
    serviceNow(f.controller, false, [=] { return loopStart + 3499U; });
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RuntimeState::BACKOFF, (uint8_t)f.controller.state());
    serviceNow(f.controller, false, [=] { return loopStart + 3500U; });
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RuntimeState::READY, (uint8_t)f.controller.state());
    TEST_ASSERT_EQUAL_UINT16(0, observer.snapshot().counters[
        (uint8_t)EventTxDiagnostics::Counter::CLOCK_REVERSED]);
}

void testObservedOneMillisecondStaleLoopUsesFreshServiceClock() {
    assertServiceSamplesAfterPacket(224677U, 224678U, 224678U);
}

void testPacketProcessingThenServiceDoesNotReuseLoopStart() {
    assertServiceSamplesAfterPacket(105U, 110U, 112U);
}

void testFreshServiceClockPreservesLegitimateWrapAndRetryDeadlines() {
    assertServiceSamplesAfterPacket(0xFFFFFFFEU, 2U, 3U);
}

void testFreshServiceClockPreservesPoweredLifetimeAndCheckpoint() {
    DeliveryFixture f; f.initializeEmpty();
    const uint8_t head = f.enqueue(3600), follower = f.enqueue(3600);
    EventTxDiagnostics::Observer observer; observer.enable();
    f.controller.recover(f.store, 0x10, 0x20, f.sequence, f.jitter, 0,
        &f.diagnostics, &observer);
    // Recovery's existing sixty-second debit remains authoritative.
    TEST_ASSERT_EQUAL_UINT32(3540, f.store.recordAt(head)->remainingActiveSeconds);
    serviceNow(f.controller, true, [] { return 59999U; });
    TEST_ASSERT_EQUAL_UINT32(3540, f.store.recordAt(follower)->remainingActiveSeconds);
    serviceNow(f.controller, true, [] { return 60000U; });
    TEST_ASSERT_EQUAL_UINT32(3480, f.store.recordAt(head)->remainingActiveSeconds);
    TEST_ASSERT_EQUAL_UINT32(3480, f.store.recordAt(follower)->remainingActiveSeconds);
    serviceNow(f.controller, true, [] { return 3540000U; });
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RuntimeState::EXPIRED, (uint8_t)f.controller.state());
    TEST_ASSERT_EQUAL_UINT32(2, f.diagnostics.eventSnapshot().counters.eventsExpired);
    TEST_ASSERT_EQUAL_UINT16(0, observer.snapshot().counters[
        (uint8_t)EventTxDiagnostics::Counter::CLOCK_REVERSED]);
}

void testAdmissionObservationsDoNotReleaseWrongAck() {
    DeliveryFixture f; f.initializeEmpty(); auto slot = f.enqueue();
    EventTxDiagnostics::Observer observer; observer.enable();
    f.controller.recover(f.store, 0x10, 0x20, f.sequence, f.jitter, 0, &f.diagnostics, &observer);
    f.waitAdmission(100);
    auto ack = EventProtocol::makeAdmissionResponse(f.controller.attemptEvent(), EventProtocol::AdmissionStatus::ADMITTED);
    uint8_t bytes[32]; size_t length = 0;
    ++ack.sequence;
    TEST_ASSERT_TRUE(EventProtocol::encodeAdmissionResponse(ack, bytes, sizeof(bytes), length));
    const auto writes = f.storage.eventWrites;
    f.controller.admissionCandidate(bytes, length, 101);
    TEST_ASSERT_EQUAL_UINT(writes, f.storage.eventWrites);
    using C = EventTxDiagnostics::Counter;
    TEST_ASSERT_EQUAL_UINT16(1, observer.snapshot().counters[(uint8_t)C::ACK_DECODED]);
    TEST_ASSERT_EQUAL_UINT16(0, observer.snapshot().counters[(uint8_t)C::ACK_MATCHED]);
    --ack.sequence;
    TEST_ASSERT_TRUE(EventProtocol::encodeAdmissionResponse(ack, bytes, sizeof(bytes), length));
    f.controller.admissionCandidate(bytes, length, 102);
    TEST_ASSERT_EQUAL_UINT16(2, observer.snapshot().counters[(uint8_t)C::ACK_DECODED]);
    TEST_ASSERT_EQUAL_UINT16(1, observer.snapshot().counters[(uint8_t)C::ACK_MATCHED]);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)NodeState::FREE, (uint8_t)f.store.recordAt(slot)->state);
}

void testExpiryObservationsByStateAndWrapIsNotClockReversal() {
    using C = EventTxDiagnostics::Counter;
    const C expected[] = {C::EXPIRED_QUEUED, C::EXPIRED_READY, C::EXPIRED_TX,
        C::EXPIRED_WAIT, C::EXPIRED_BACKOFF};
    for (unsigned mode = 0; mode < 5; ++mode) {
        DeliveryFixture f; f.initializeEmpty(); f.enqueue(120);
        EventTxDiagnostics::Observer observer; observer.enable();
        f.controller.recover(f.store, 0x10, 0x20, f.sequence, f.jitter, 0, &f.diagnostics, &observer);
        if (mode >= 1) f.ready();
        if (mode >= 2) f.controller.grantTransmit(0);
        if (mode >= 3) f.controller.txCompleted(0);
        if (mode >= 4) f.controller.service(2500, false);
        f.controller.service(60000, false);
        TEST_ASSERT_EQUAL_UINT16(1, observer.snapshot().counters[(uint8_t)expected[mode]]);
    }
    EventTxDiagnostics::Observer wrap;
    wrap.clock(0xFFFFFFF0U); wrap.clock(0x20U);
    TEST_ASSERT_EQUAL_UINT16(0, wrap.snapshot().counters[(uint8_t)C::CLOCK_REVERSED]);
    for (unsigned i = 0; i < 65540; ++i) wrap.note(C::START_CALLED, i);
    TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, wrap.snapshot().counters[(uint8_t)C::START_CALLED]);
}

}  // namespace

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(testReleasedSlotReuseAfterLongIdleStartsFresh);
    RUN_TEST(testReleasedSlotReuseAfterOrdinaryIdleStartsFresh);
    RUN_TEST(testReclaimedFailedSlotReuseAfterLongIdleStartsFresh);
    RUN_TEST(testReclaimedFailedSlotReuseAfterOrdinaryIdleStartsFresh);
    RUN_TEST(testExplicitEnqueueTrackingResetsOnlyReusedSlot);
    if (argc == 2 && strcmp(argv[1], "--slot-reuse-only") == 0) return UNITY_END();
    RUN_TEST(testTrackedCreationSamplesAfterDurableCommitAndCountsBeforeFirstService);
    RUN_TEST(testTrackedSlotReusePreservesWaitingHeadFollowerAndFifo);
    RUN_TEST(testTrackingFailurePreservesDurableEventAndBlocksDeliveryAndCreation);
    RUN_TEST(testRejectedCreationNeverTracksOrRefreshesExistingSlots);
    RUN_TEST(testEnqueueThenCompletionUsesFreshClockNotLoopStart);
    RUN_TEST(testEnqueueThenCompletionPreservesMillisWrap);
    RUN_TEST(testRecoveredTerminalsReclaimedAndMixedQueuePreserved);
    RUN_TEST(testRuntimeTerminalReclaimAdvancesFifo);
    RUN_TEST(testReclaimFailuresFailClosedAtBootAndRuntime);
    RUN_TEST(testReclaimNeverTouchesNonterminalDeliveryStates);
    RUN_TEST(testParallelExpiryReclamationRestoresCapacity);
    RUN_TEST(testLifecycleObservationDoesNotChangeDeliveryOrStorage);
    RUN_TEST(testExistingBackwardTimeInputExpiresQueueAndIsObserved);
    RUN_TEST(testObservedOneMillisecondStaleLoopUsesFreshServiceClock);
    RUN_TEST(testPacketProcessingThenServiceDoesNotReuseLoopStart);
    RUN_TEST(testFreshServiceClockPreservesLegitimateWrapAndRetryDeadlines);
    RUN_TEST(testFreshServiceClockPreservesPoweredLifetimeAndCheckpoint);
    RUN_TEST(testAdmissionObservationsDoNotReleaseWrongAck);
    RUN_TEST(testExpiryObservationsByStateAndWrapIsNotClockReversal);
    RUN_TEST(testEnqueueBaselineAndSubCheckpointAccounting);
    RUN_TEST(testExactAndMultipleCheckpointCatchup);
    RUN_TEST(testImmediateExpiryAtExactBoundaryAndAfterCheckpoint);
    RUN_TEST(testRebootDebitPreservesCanonicalFieldsAndAttempts);
    RUN_TEST(testRebootDebitExpiresAtOrBelowSixty);
    RUN_TEST(testRepeatedBootsDebitWithoutReset);
    RUN_TEST(testMonotonicWrapAndMultipleEventsAgeTogether);
    RUN_TEST(testFiveWriteAheadAttemptsPreserveLifetimeAndContent);
    RUN_TEST(testAttemptFaultsNeverArmAndDegrade);
    RUN_TEST(testAttemptAcrossRebootAndDebitIndependence);
    RUN_TEST(testExpiredAndTerminalEventsCannotArm);
    RUN_TEST(testCheckpointAndDebitFailuresBlockEligibility);
    RUN_TEST(testAbruptShutdownBeforeAndAfterCheckpoint);
    RUN_TEST(testControllerRecoveryArbitrationAndFifo);
    RUN_TEST(testPrepareWritesAttemptBeforeCanonicalTransmit);
    RUN_TEST(testAttemptPersistenceFailureExposesNoTransmit);
    RUN_TEST(testTxLifecycleTimeoutBackoffAndJitter);
    RUN_TEST(testAdmissionCorrelationIgnoredWithoutDeadlineChange);
    RUN_TEST(testAdmittedReleaseAndNextHead);
    RUN_TEST(testAdmissionStatusesAndTerminalReclaim);
    RUN_TEST(testAttemptFiveFailureAndSuccess);
    RUN_TEST(testExpiryPreemptsTxWaitAndBackoff);
    RUN_TEST(testTxStartFailureBackoffAndStorageFailureFailClosed);
    RUN_TEST(testHubUnavailableUsesOnlyFrozenEvidencePaths);
    return UNITY_END();
}
