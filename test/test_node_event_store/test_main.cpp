#include <unity.h>

#include "node_event_store.h"

using namespace EventIdentity;
using namespace EventRecords;
using namespace EventStorage;
using namespace NodeEventStore;

namespace {

enum class Fault : uint8_t { NONE, WRITE, COMMIT, READ_MISSING, READ_UNAVAILABLE, MISMATCH, BAD_CRC };

class FakeStorage final : public Storage {
public:
    FixedCopy<NODE_METADATA_SIZE> metadata[2] = {};
    FixedCopy<NODE_RECORD_SIZE> events[NODE_EVENT_CAPACITY][2] = {};
    Fault fault = Fault::NONE;
    bool mutationPending = false;
    bool writingEvent = false;
    uint8_t lastEventSlot = 0;
    CopySlot lastCopy = CopySlot::A;
    unsigned metadataWrites = 0;
    unsigned eventWrites = 0;
    unsigned commits = 0;

    FakeStorage() {
        metadata[0].status = metadata[1].status = FixedReadStatus::MISSING;
        for (size_t s = 0; s < NODE_EVENT_CAPACITY; ++s) {
            events[s][0].status = events[s][1].status = FixedReadStatus::MISSING;
        }
    }

    StorageResult read(CopySlot copy, uint8_t* out, size_t cap, size_t& len) override {
        return readFixed(metadata[index(copy)], NODE_METADATA_SIZE, false, 0, copy, out, cap, len);
    }
    StorageResult write(CopySlot copy, const uint8_t* in, size_t len) override {
        ++metadataWrites;
        writingEvent = false;
        lastCopy = copy;
        mutationPending = true;
        return writeFixed(metadata[index(copy)], NODE_METADATA_SIZE, in, len);
    }
    StorageResult commit() override {
        ++commits;
        return mutationPending && fault == Fault::COMMIT ? StorageResult::ERROR : StorageResult::OK;
    }
    StorageResult readEventCopy(uint8_t slot, CopySlot copy, uint8_t* out,
                                size_t cap, size_t& len) override {
        if (slot >= NODE_EVENT_CAPACITY) return StorageResult::ERROR;
        return readFixed(events[slot][index(copy)], NODE_RECORD_SIZE, true, slot, copy,
                         out, cap, len);
    }
    StorageResult writeEventCopy(uint8_t slot, CopySlot copy,
                                 const uint8_t* in, size_t len) override {
        if (slot >= NODE_EVENT_CAPACITY) return StorageResult::ERROR;
        ++eventWrites;
        writingEvent = true;
        lastEventSlot = slot;
        lastCopy = copy;
        mutationPending = true;
        return writeFixed(events[slot][index(copy)], NODE_RECORD_SIZE, in, len);
    }

    void clearFault() { fault = Fault::NONE; mutationPending = false; }

private:
    template <size_t N>
    StorageResult writeFixed(FixedCopy<N>& destination, size_t exact,
                             const uint8_t* input, size_t length) {
        if (fault == Fault::WRITE || input == nullptr || length != exact) return StorageResult::ERROR;
        destination.status = FixedReadStatus::OK;
        for (size_t i = 0; i < exact; ++i) destination.bytes[i] = input[i];
        return StorageResult::OK;
    }
    template <size_t N>
    StorageResult readFixed(FixedCopy<N>& source, size_t exact, bool isEvent,
                            uint8_t slot, CopySlot copy, uint8_t* output,
                            size_t capacity, size_t& length) {
        length = 0;
        const bool isReadBack = mutationPending && writingEvent == isEvent &&
            (!isEvent || slot == lastEventSlot) && copy == lastCopy;
        if (isReadBack && fault == Fault::READ_MISSING) return StorageResult::MISSING;
        if (isReadBack && fault == Fault::READ_UNAVAILABLE) return StorageResult::UNAVAILABLE;
        if (source.status == FixedReadStatus::MISSING) return StorageResult::MISSING;
        if (source.status == FixedReadStatus::UNAVAILABLE) return StorageResult::UNAVAILABLE;
        if (capacity < exact) return StorageResult::ERROR;
        for (size_t i = 0; i < exact; ++i) output[i] = source.bytes[i];
        if (isReadBack && fault == Fault::MISMATCH) output[12] ^= 1U;
        if (isReadBack && fault == Fault::BAD_CRC) output[exact - 1U] ^= 1U;
        length = exact;
        return StorageResult::OK;
    }
    static size_t index(CopySlot copy) { return copy == CopySlot::A ? 0U : 1U; }
};

class FakeEntropy final : public EntropySource {
public:
    uint32_t value = 0x10203040U;
    bool nextUint32(uint32_t& output) override { output = value; return value != 0; }
};

EventInput button(uint8_t value = 1, uint32_t lifetime = 600) {
    EventInput input = {};
    input.family = static_cast<uint8_t>(EventProtocol::Family::BUTTON);
    input.flags = EventProtocol::IMPORTANT_FLAG;
    input.lifetimeBudgetSeconds = lifetime;
    input.bodyLength = 1;
    input.body[0] = value;
    return input;
}

EventInput sensor() {
    EventInput input = {};
    input.family = static_cast<uint8_t>(EventProtocol::Family::SENSOR_THRESHOLD);
    input.lifetimeBudgetSeconds = 600;
    input.bodyLength = 8;
    input.body[0] = 1;
    input.body[2] = static_cast<uint8_t>(EventProtocol::SensorValueType::UNSIGNED_32);
    input.body[7] = static_cast<uint8_t>(EventProtocol::ThresholdRelation::CROSSED_ABOVE);
    return input;
}

EventInput manual() {
    EventInput input = {};
    input.family = static_cast<uint8_t>(EventProtocol::Family::MANUAL_CHECK_IN);
    input.lifetimeBudgetSeconds = 600;
    input.bodyLength = 1;
    input.body[0] = static_cast<uint8_t>(EventProtocol::ManualReason::USER_REQUEST);
    return input;
}

void seedMetadata(FakeStorage& storage, uint8_t source = 0x10,
                  uint32_t epoch = 0x10203040U, uint32_t next = 33U) {
    NodeMetadata metadata = {7U, source, epoch, next};
    storage.metadata[0].status = FixedReadStatus::OK;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::OK),
        static_cast<uint8_t>(encodeNodeMetadata(metadata, storage.metadata[0].bytes,
                                                NODE_METADATA_SIZE)));
}

NodeRecord record(NodeState state, uint32_t generation, uint32_t epoch, uint32_t id) {
    NodeRecord value = {};
    value.generation = generation;
    value.state = state;
    if (state != NodeState::FREE) {
        value.flags = EventProtocol::IMPORTANT_FLAG;
        value.family = static_cast<uint8_t>(EventProtocol::Family::BUTTON);
        value.bodyLength = 1;
        value.eventEpoch = epoch;
        value.eventId = id;
        value.lifetimeBudgetSeconds = 600;
        value.remainingActiveSeconds = 590;
        value.attemptsUsed = 2;
        value.body[0] = static_cast<uint8_t>(EventProtocol::ButtonEvent::PRESS);
    }
    return value;
}

void seedRecord(FakeStorage& storage, uint8_t slot, CopySlot copy,
                const NodeRecord& value) {
    FixedCopy<NODE_RECORD_SIZE>& target = storage.events[slot][copy == CopySlot::A ? 0 : 1];
    target.status = FixedReadStatus::OK;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::OK),
        static_cast<uint8_t>(encodeNodeRecord(value, target.bytes, NODE_RECORD_SIZE)));
}

void seedFreeSlots(FakeStorage& storage, uint32_t generation = 1) {
    for (uint8_t slot = 0; slot < NODE_EVENT_CAPACITY; ++slot) {
        seedRecord(storage, slot, CopySlot::A, record(NodeState::FREE, generation, 0, 0));
    }
}

void assertReady(Store& store, FakeStorage& storage, FakeEntropy& entropy,
                 uint8_t source = 0x10) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NodeEventStore::Status::READY),
        static_cast<uint8_t>(store.recover(storage, entropy, source)));
    TEST_ASSERT_TRUE(store.healthy());
}

void testFreshInitializationCreatesEightCanonicalFreeRecords() {
    FakeStorage storage;
    FakeEntropy entropy;
    Store store;
    assertReady(store, storage, entropy);
    TEST_ASSERT_EQUAL_UINT(8, storage.eventWrites);
    TEST_ASSERT_EQUAL_UINT(1, storage.metadataWrites);
    TEST_ASSERT_EQUAL_UINT(0, store.queuedCount());
    TEST_ASSERT_EQUAL_UINT(0, store.ownedCount());
    for (uint8_t i = 0; i < NODE_EVENT_CAPACITY; ++i) {
        const NodeRecord* value = store.recordAt(i);
        TEST_ASSERT_NOT_NULL(value);
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NodeState::FREE),
                                static_cast<uint8_t>(value->state));
        TEST_ASSERT_EQUAL_UINT32(1, value->generation);
        TEST_ASSERT_TRUE(canonicalFree(*value));
    }
}

void testPartialInitializationFailureAndCorruptionFailClosed() {
    FakeStorage storage;
    FakeEntropy entropy;
    Store store;
    storage.fault = Fault::WRITE;
    TEST_ASSERT_NOT_EQUAL(static_cast<uint8_t>(NodeEventStore::Status::READY),
                          static_cast<uint8_t>(store.recover(storage, entropy, 0x10)));
    TEST_ASSERT_FALSE(store.healthy());

    FakeStorage corrupt;
    corrupt.events[0][0].status = FixedReadStatus::OK;
    corrupt.events[0][0].bytes[0] = 0xAA;
    Store other;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NodeEventStore::Status::INDETERMINATE_SLOT),
                            static_cast<uint8_t>(other.recover(corrupt, entropy, 0x10)));
}

void testEnqueuePersistsCanonicalFieldsAndLowestSlot() {
    FakeStorage storage;
    FakeEntropy entropy;
    Store store;
    assertReady(store, storage, entropy);
    storage.clearFault();
    EnqueueResult result = store.enqueue(button());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EnqueueStatus::ENQUEUED),
                            static_cast<uint8_t>(result.status));
    TEST_ASSERT_EQUAL_UINT8(0, result.slot);
    TEST_ASSERT_EQUAL_UINT32(1, result.identity.id);
    const NodeRecord* value = store.recordAt(0);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NodeState::QUEUED),
                            static_cast<uint8_t>(value->state));
    TEST_ASSERT_EQUAL_UINT32(600, value->lifetimeBudgetSeconds);
    TEST_ASSERT_EQUAL_UINT32(600, value->remainingActiveSeconds);
    TEST_ASSERT_EQUAL_UINT8(0, value->attemptsUsed);
    TEST_ASSERT_EQUAL_UINT(1, store.queuedCount());
}

void testAllFamiliesAndInvalidInputBeforeIdentity() {
    FakeStorage storage;
    FakeEntropy entropy;
    Store store;
    assertReady(store, storage, entropy);
    storage.clearFault();
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EnqueueStatus::ENQUEUED),
        static_cast<uint8_t>(store.enqueue(button()).status));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EnqueueStatus::ENQUEUED),
        static_cast<uint8_t>(store.enqueue(sensor()).status));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EnqueueStatus::ENQUEUED),
        static_cast<uint8_t>(store.enqueue(manual()).status));
    const unsigned metadataWrites = storage.metadataWrites;
    const unsigned eventWrites = storage.eventWrites;
    EventInput invalid = button(0);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EnqueueStatus::INVALID_EVENT),
        static_cast<uint8_t>(store.enqueue(invalid).status));
    TEST_ASSERT_EQUAL_UINT(metadataWrites, storage.metadataWrites);
    TEST_ASSERT_EQUAL_UINT(eventWrites, storage.eventWrites);
}

void testCapacityNewestRejectDoesNotAllocateOrMutate() {
    FakeStorage storage;
    FakeEntropy entropy;
    Store store;
    assertReady(store, storage, entropy);
    storage.clearFault();
    for (uint8_t i = 0; i < NODE_EVENT_CAPACITY; ++i) {
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EnqueueStatus::ENQUEUED),
            static_cast<uint8_t>(store.enqueue(button()).status));
    }
    const unsigned metadataWrites = storage.metadataWrites;
    const unsigned eventWrites = storage.eventWrites;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EnqueueStatus::QUEUE_FULL),
        static_cast<uint8_t>(store.enqueue(button()).status));
    TEST_ASSERT_EQUAL_UINT(metadataWrites, storage.metadataWrites);
    TEST_ASSERT_EQUAL_UINT(eventWrites, storage.eventWrites);
    TEST_ASSERT_EQUAL_UINT(8, store.ownedCount());
}

void testRebootReconstructsIdentityFifoNotSlotOrder() {
    FakeStorage storage;
    FakeEntropy entropy;
    seedFreeSlots(storage);
    seedMetadata(storage, 0x10, 8, 33);
    seedRecord(storage, 7, CopySlot::B, record(NodeState::QUEUED, 2, 8, 5));
    seedRecord(storage, 2, CopySlot::B, record(NodeState::QUEUED, 2, 8, 3));
    Store store;
    assertReady(store, storage, entropy);
    uint8_t firstSlot = 0;
    uint8_t secondSlot = 0;
    TEST_ASSERT_EQUAL_UINT32(3, store.queuedAt(0, &firstSlot)->eventId);
    TEST_ASSERT_EQUAL_UINT32(5, store.queuedAt(1, &secondSlot)->eventId);
    TEST_ASSERT_EQUAL_UINT8(2, firstSlot);
    TEST_ASSERT_EQUAL_UINT8(7, secondSlot);
    TEST_ASSERT_EQUAL_UINT8(2, store.queuedAt(0)->attemptsUsed);
    TEST_ASSERT_EQUAL_UINT32(590, store.queuedAt(0)->remainingActiveSeconds);
}

void testEpochAdvanceOrdersAfterPriorEpochTail() {
    FakeStorage storage;
    FakeEntropy entropy;
    seedFreeSlots(storage);
    seedMetadata(storage, 0x10, 9, 33);
    seedRecord(storage, 6, CopySlot::B,
               record(NodeState::QUEUED, 2, 8, UINT32_MAX));
    seedRecord(storage, 1, CopySlot::B,
               record(NodeState::QUEUED, 2, 9, 1));
    Store store;
    assertReady(store, storage, entropy);
    TEST_ASSERT_EQUAL_UINT32(8, store.queuedAt(0)->eventEpoch);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, store.queuedAt(0)->eventId);
    TEST_ASSERT_EQUAL_UINT32(9, store.queuedAt(1)->eventEpoch);
    TEST_ASSERT_EQUAL_UINT32(1, store.queuedAt(1)->eventId);
}

void testFailedCommitBurnsIdentityAndNoFalseCustody() {
    FakeStorage storage;
    FakeEntropy entropy;
    Store first;
    assertReady(first, storage, entropy);
    storage.clearFault();
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EnqueueStatus::ENQUEUED),
        static_cast<uint8_t>(first.enqueue(button()).status));
    storage.fault = Fault::WRITE;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EnqueueStatus::STORAGE_FAILURE),
        static_cast<uint8_t>(first.enqueue(button()).status));
    storage.clearFault();
    Store rebooted;
    assertReady(rebooted, storage, entropy);
    EnqueueResult next = rebooted.enqueue(button());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EnqueueStatus::ENQUEUED),
                            static_cast<uint8_t>(next.status));
    TEST_ASSERT_EQUAL_UINT32(33, next.identity.id);
}

void testReadBackFaultsNeverPublishSuccess() {
    const Fault faults[] = {Fault::READ_MISSING, Fault::READ_UNAVAILABLE,
                            Fault::MISMATCH, Fault::BAD_CRC};
    for (size_t i = 0; i < sizeof(faults) / sizeof(faults[0]); ++i) {
        FakeStorage storage;
        FakeEntropy entropy;
        Store store;
        assertReady(store, storage, entropy);
        storage.clearFault();
        storage.fault = faults[i];
        TEST_ASSERT_NOT_EQUAL(static_cast<uint8_t>(EnqueueStatus::ENQUEUED),
                              static_cast<uint8_t>(store.enqueue(button()).status));
        TEST_ASSERT_FALSE(store.healthy());
    }
}

void testCopyFallbackConflictsAndAmbiguity() {
    FakeStorage fallback;
    FakeEntropy entropy;
    seedFreeSlots(fallback);
    seedMetadata(fallback);
    fallback.events[0][1].status = FixedReadStatus::OK;
    fallback.events[0][1].bytes[0] = 0xAA;
    Store okay;
    assertReady(okay, fallback, entropy);

    FakeStorage conflict;
    seedFreeSlots(conflict, 7);
    seedMetadata(conflict);
    seedRecord(conflict, 0, CopySlot::A, record(NodeState::QUEUED, 7, 8, 1));
    seedRecord(conflict, 0, CopySlot::B, record(NodeState::QUEUED, 7, 8, 2));
    Store bad;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NodeEventStore::Status::RECORD_CONFLICT),
                            static_cast<uint8_t>(bad.recover(conflict, entropy, 0x10)));

    FakeStorage ambiguous;
    seedFreeSlots(ambiguous, 1);
    seedMetadata(ambiguous);
    seedRecord(ambiguous, 0, CopySlot::B,
               record(NodeState::FREE, 0x80000001U, 0, 0));
    Store uncertain;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NodeEventStore::Status::GENERATION_AMBIGUOUS),
                            static_cast<uint8_t>(uncertain.recover(ambiguous, entropy, 0x10)));
}

void testDuplicateIdentityAndSourceMismatchFailClosed() {
    FakeStorage duplicate;
    FakeEntropy entropy;
    seedFreeSlots(duplicate);
    seedMetadata(duplicate);
    seedRecord(duplicate, 0, CopySlot::B, record(NodeState::QUEUED, 2, 7, 9));
    seedRecord(duplicate, 1, CopySlot::B, record(NodeState::FAILED, 2, 7, 9));
    Store store;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NodeEventStore::Status::DUPLICATE_IDENTITY),
                            static_cast<uint8_t>(store.recover(duplicate, entropy, 0x10)));

    FakeStorage mismatch;
    seedFreeSlots(mismatch);
    seedMetadata(mismatch, 0x11);
    seedRecord(mismatch, 0, CopySlot::B, record(NodeState::QUEUED, 2, 7, 1));
    Store bound;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NodeEventStore::Status::IDENTITY_FAILURE),
                            static_cast<uint8_t>(bound.recover(mismatch, entropy, 0x10)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EventIdentity::Status::IDENTITY_CONTEXT_MISMATCH),
                            static_cast<uint8_t>(bound.identityStatus()));
}

void testEmptyStoreMayAdoptNewSourceContext() {
    FakeStorage storage;
    FakeEntropy entropy;
    seedFreeSlots(storage);
    seedMetadata(storage, 0x11, 99, 33);
    entropy.value = 1234;
    Store store;
    assertReady(store, storage, entropy, 0x10);
    EnqueueResult result = store.enqueue(button());
    TEST_ASSERT_EQUAL_UINT8(0x10, result.identity.sourceDeviceId);
    TEST_ASSERT_EQUAL_UINT32(1234, result.identity.epoch);
    TEST_ASSERT_EQUAL_UINT32(1, result.identity.id);
}

void testTerminalTransitionsReclaimReleaseAndReuse() {
    FakeStorage storage;
    FakeEntropy entropy;
    Store store;
    assertReady(store, storage, entropy);
    storage.clearFault();
    EnqueueResult first = store.enqueue(button());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MutationStatus::OK),
                            static_cast<uint8_t>(store.markFailed(first.slot)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NodeState::FAILED),
        static_cast<uint8_t>(store.recordAt(first.slot)->state));
    TEST_ASSERT_EQUAL_UINT32(first.identity.id, store.recordAt(first.slot)->eventId);

    Store rebooted;
    storage.clearFault();
    assertReady(rebooted, storage, entropy);
    TEST_ASSERT_EQUAL_UINT(1, rebooted.ownedCount());
    TEST_ASSERT_EQUAL_UINT(0, rebooted.queuedCount());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MutationStatus::OK),
                            static_cast<uint8_t>(rebooted.reclaim(first.slot)));
    TEST_ASSERT_TRUE(canonicalFree(*rebooted.recordAt(first.slot)));
    EnqueueResult reused = rebooted.enqueue(button());
    TEST_ASSERT_EQUAL_UINT8(first.slot, reused.slot);
    TEST_ASSERT_NOT_EQUAL(first.identity.id, reused.identity.id);

    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MutationStatus::OK),
                            static_cast<uint8_t>(rebooted.releaseQueued(reused.slot)));
    TEST_ASSERT_TRUE(canonicalFree(*rebooted.recordAt(reused.slot)));

    EnqueueResult expiring = rebooted.enqueue(manual());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MutationStatus::OK),
                            static_cast<uint8_t>(rebooted.markExpired(expiring.slot)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NodeState::EXPIRED),
        static_cast<uint8_t>(rebooted.recordAt(expiring.slot)->state));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MutationStatus::OK),
                            static_cast<uint8_t>(rebooted.reclaim(expiring.slot)));
}

void testExpiredTransitionAndFailedMutationPreserveAuthority() {
    FakeStorage storage;
    FakeEntropy entropy;
    Store store;
    assertReady(store, storage, entropy);
    storage.clearFault();
    EnqueueResult queued = store.enqueue(button());
    storage.fault = Fault::WRITE;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MutationStatus::STORAGE_FAILURE),
                            static_cast<uint8_t>(store.markExpired(queued.slot)));
    storage.clearFault();
    Store rebooted;
    assertReady(rebooted, storage, entropy);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NodeState::QUEUED),
                            static_cast<uint8_t>(rebooted.recordAt(queued.slot)->state));
}

void testBoundsAreRejected() {
    FakeStorage storage;
    FakeEntropy entropy;
    Store store;
    assertReady(store, storage, entropy);
    TEST_ASSERT_NULL(store.recordAt(NODE_EVENT_CAPACITY));
    TEST_ASSERT_NULL(store.queuedAt(0));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MutationStatus::INVALID_SLOT),
                            static_cast<uint8_t>(store.markFailed(NODE_EVENT_CAPACITY)));
}

void testStage12DiagnosticsOwnEnqueueRecoveryQueueAndStorageFacts() {
    FakeStorage storage; FakeEntropy entropy; Store store;
    RuntimeState::State diagnostics(RuntimeState::DeviceRole::NODE, 2, 1);
    store.setDiagnostics(&diagnostics);
    assertReady(store, storage, entropy);
    for (uint8_t i = 0; i < NODE_EVENT_CAPACITY; ++i)
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EnqueueStatus::ENQUEUED),
            static_cast<uint8_t>(store.enqueue(button()).status));
    TEST_ASSERT_EQUAL_UINT32(8, diagnostics.eventSnapshot().counters.enqueueAccepted);
    TEST_ASSERT_EQUAL_UINT8(8, diagnostics.eventSnapshot().queuedCount);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EnqueueStatus::QUEUE_FULL),
        static_cast<uint8_t>(store.enqueue(button()).status));
    TEST_ASSERT_EQUAL_UINT32(1, diagnostics.eventSnapshot().counters.queueFullRejected);

    RuntimeState::State recoveredDiagnostics(RuntimeState::DeviceRole::NODE, 2, 1);
    Store recovered; recovered.setDiagnostics(&recoveredDiagnostics);
    assertReady(recovered, storage, entropy);
    TEST_ASSERT_EQUAL_UINT32(8,
        recoveredDiagnostics.eventSnapshot().counters.eventsRecovered);

    FakeStorage failingStorage; Store failing; failing.setDiagnostics(&diagnostics);
    assertReady(failing, failingStorage, entropy);
    failingStorage.clearFault(); failingStorage.fault = Fault::WRITE;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EnqueueStatus::IDENTITY_FAILURE),
        static_cast<uint8_t>(failing.enqueue(button()).status));
    TEST_ASSERT_EQUAL_UINT32(1, diagnostics.eventSnapshot().counters.persistenceFailures);
    TEST_ASSERT_TRUE(diagnostics.eventSnapshot().persistenceDegraded);
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(testFreshInitializationCreatesEightCanonicalFreeRecords);
    RUN_TEST(testPartialInitializationFailureAndCorruptionFailClosed);
    RUN_TEST(testEnqueuePersistsCanonicalFieldsAndLowestSlot);
    RUN_TEST(testAllFamiliesAndInvalidInputBeforeIdentity);
    RUN_TEST(testCapacityNewestRejectDoesNotAllocateOrMutate);
    RUN_TEST(testRebootReconstructsIdentityFifoNotSlotOrder);
    RUN_TEST(testEpochAdvanceOrdersAfterPriorEpochTail);
    RUN_TEST(testFailedCommitBurnsIdentityAndNoFalseCustody);
    RUN_TEST(testReadBackFaultsNeverPublishSuccess);
    RUN_TEST(testCopyFallbackConflictsAndAmbiguity);
    RUN_TEST(testDuplicateIdentityAndSourceMismatchFailClosed);
    RUN_TEST(testEmptyStoreMayAdoptNewSourceContext);
    RUN_TEST(testTerminalTransitionsReclaimReleaseAndReuse);
    RUN_TEST(testExpiredTransitionAndFailedMutationPreserveAuthority);
    RUN_TEST(testBoundsAreRejected);
    RUN_TEST(testStage12DiagnosticsOwnEnqueueRecoveryQueueAndStorageFacts);
    return UNITY_END();
}
