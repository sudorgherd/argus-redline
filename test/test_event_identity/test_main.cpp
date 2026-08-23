#include <unity.h>

#include <stdint.h>

#include "event_identity.h"

using namespace EventIdentity;
using namespace EventRecords;
using namespace EventStorage;

namespace {

enum class ReadBackFault : uint8_t { NONE, MISSING, UNAVAILABLE, MISMATCH, INVALID_CRC };

class FakeStorage final : public MetadataStorage {
public:
    FixedCopy<NODE_METADATA_SIZE> copies[2] = {};
    bool unavailableOnInitialRead = false;
    bool writeFails = false;
    bool commitFails = false;
    ReadBackFault readBackFault = ReadBackFault::NONE;
    bool hasWritten = false;
    unsigned writes = 0;
    unsigned commits = 0;
    CopySlot lastWritten = CopySlot::A;

    FakeStorage() {
        copies[0].status = FixedReadStatus::MISSING;
        copies[1].status = FixedReadStatus::MISSING;
    }

    StorageResult read(CopySlot slot, uint8_t* output, size_t capacity, size_t& length) override {
        length = 0;
        if (!hasWritten && unavailableOnInitialRead) return StorageResult::UNAVAILABLE;
        if (hasWritten && slot == lastWritten) {
            if (readBackFault == ReadBackFault::MISSING) return StorageResult::MISSING;
            if (readBackFault == ReadBackFault::UNAVAILABLE) return StorageResult::UNAVAILABLE;
        }
        FixedCopy<NODE_METADATA_SIZE>& copy = copies[index(slot)];
        if (copy.status == FixedReadStatus::MISSING) return StorageResult::MISSING;
        if (copy.status == FixedReadStatus::UNAVAILABLE) return StorageResult::UNAVAILABLE;
        if (capacity < NODE_METADATA_SIZE) return StorageResult::ERROR;
        for (size_t i = 0; i < NODE_METADATA_SIZE; ++i) output[i] = copy.bytes[i];
        if (hasWritten && slot == lastWritten &&
            readBackFault == ReadBackFault::MISMATCH) {
            output[20] = static_cast<uint8_t>(output[20] + 1U);
            uint32_t crc = 0;
            EventRecords::crc32IsoHdlc(output, NODE_METADATA_SIZE - 4, crc);
            EventRecords::writeUint32Le(output + NODE_METADATA_SIZE - 4, crc);
        }
        if (hasWritten && slot == lastWritten &&
            readBackFault == ReadBackFault::INVALID_CRC) output[24] ^= 0x01;
        length = NODE_METADATA_SIZE;
        return StorageResult::OK;
    }

    StorageResult write(CopySlot slot, const uint8_t* input, size_t length) override {
        ++writes;
        lastWritten = slot;
        hasWritten = true;
        if (writeFails || length != NODE_METADATA_SIZE) return StorageResult::ERROR;
        FixedCopy<NODE_METADATA_SIZE>& copy = copies[index(slot)];
        copy.status = FixedReadStatus::OK;
        for (size_t i = 0; i < NODE_METADATA_SIZE; ++i) copy.bytes[i] = input[i];
        return StorageResult::OK;
    }

    StorageResult commit() override {
        ++commits;
        return commitFails ? StorageResult::ERROR : StorageResult::OK;
    }

    static size_t index(CopySlot slot) { return slot == CopySlot::A ? 0U : 1U; }
};

class FakeEntropy final : public EntropySource {
public:
    bool succeeds = true;
    uint32_t value = 0x11223344U;
    unsigned calls = 0;
    bool nextUint32(uint32_t& output) override {
        ++calls;
        if (!succeeds) return false;
        output = value;
        return true;
    }
};

void seed(FakeStorage& storage, CopySlot slot, const NodeMetadata& metadata) {
    FixedCopy<NODE_METADATA_SIZE>& copy = storage.copies[FakeStorage::index(slot)];
    copy.status = FixedReadStatus::OK;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CodecResult::OK),
        static_cast<uint8_t>(encodeNodeMetadata(metadata, copy.bytes, sizeof(copy.bytes))));
}

NodeMetadata metadata(
    uint32_t generation = 7,
    uint8_t source = 0x21,
    uint32_t epoch = 0x11223344U,
    uint32_t next = 33U
) {
    NodeMetadata value = {};
    value.generation = generation;
    value.custodySourceDeviceId = source;
    value.eventEpoch = epoch;
    value.nextUnreservedEventId = next;
    return value;
}

void assertStatus(Status expected, Status actual) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected), static_cast<uint8_t>(actual));
}

void testFreshInitializationAndFirstReservationAreDurable() {
    FakeStorage storage;
    FakeEntropy entropy;
    Allocator allocator;
    assertStatus(Status::READY, allocator.recover(storage, entropy, 0x21, CustodyState::EMPTY));
    TEST_ASSERT_TRUE(allocator.ready());
    TEST_ASSERT_EQUAL_UINT32(1, allocator.metadata().generation);
    TEST_ASSERT_EQUAL_UINT32(1, allocator.metadata().nextUnreservedEventId);
    TEST_ASSERT_EQUAL_UINT(1, storage.writes);

    const AllocationResult result = allocator.allocate();
    assertStatus(Status::READY, result.status);
    TEST_ASSERT_EQUAL_UINT8(0x21, result.identity.sourceDeviceId);
    TEST_ASSERT_EQUAL_HEX32(entropy.value, result.identity.epoch);
    TEST_ASSERT_EQUAL_UINT32(1, result.identity.id);
    TEST_ASSERT_EQUAL_UINT32(33, allocator.metadata().nextUnreservedEventId);
    TEST_ASSERT_EQUAL_UINT(2, storage.writes);
    TEST_ASSERT_EQUAL_UINT(2, storage.commits);
}

void testFreshInitializationRejectsInvalidInputs() {
    FakeStorage storage;
    FakeEntropy entropy;
    Allocator allocator;
    assertStatus(Status::INVALID_SOURCE_DEVICE_ID,
        allocator.recover(storage, entropy, 0, CustodyState::EMPTY));
    TEST_ASSERT_EQUAL_UINT(0, storage.writes);

    entropy.succeeds = false;
    assertStatus(Status::ENTROPY_FAILURE,
        allocator.recover(storage, entropy, 1, CustodyState::EMPTY));
    entropy.succeeds = true;
    entropy.value = 0;
    assertStatus(Status::ENTROPY_FAILURE,
        allocator.recover(storage, entropy, 1, CustodyState::EMPTY));
}

void testSequentialBlocksMutateOncePerBlockAndNeverReuse() {
    FakeStorage storage;
    FakeEntropy entropy;
    Allocator allocator;
    assertStatus(Status::READY, allocator.recover(storage, entropy, 0x21, CustodyState::EMPTY));
    for (uint32_t id = 1; id <= 32; ++id) {
        AllocationResult result = allocator.allocate();
        assertStatus(Status::READY, result.status);
        TEST_ASSERT_EQUAL_UINT32(id, result.identity.id);
    }
    TEST_ASSERT_EQUAL_UINT(2, storage.writes);
    AllocationResult next = allocator.allocate();
    TEST_ASSERT_EQUAL_UINT32(33, next.identity.id);
    TEST_ASSERT_EQUAL_UINT32(65, allocator.metadata().nextUnreservedEventId);
    TEST_ASSERT_EQUAL_UINT(3, storage.writes);
}

void testRebootSkipsUnusedReservedIds() {
    FakeStorage storage;
    FakeEntropy entropy;
    Allocator first;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Status::READY),
        static_cast<uint8_t>(first.recover(storage, entropy, 0x21, CustodyState::EMPTY)));
    TEST_ASSERT_EQUAL_UINT32(1, first.allocate().identity.id);
    TEST_ASSERT_EQUAL_UINT32(2, first.allocate().identity.id);

    storage.hasWritten = false;
    Allocator second;
    assertStatus(Status::READY, second.recover(storage, entropy, 0x21, CustodyState::EMPTY));
    TEST_ASSERT_EQUAL_UINT32(33, second.allocate().identity.id);
    storage.hasWritten = false;
    Allocator third;
    assertStatus(Status::READY, third.recover(storage, entropy, 0x21, CustodyState::EMPTY));
    TEST_ASSERT_EQUAL_UINT32(65, third.allocate().identity.id);
}

void testIssuedIdentityIsBurnedWithoutCallerFeedback() {
    FakeStorage storage;
    FakeEntropy entropy;
    Allocator allocator;
    allocator.recover(storage, entropy, 0x21, CustodyState::EMPTY);
    const uint32_t abandoned = allocator.allocate().identity.id;
    const uint32_t following = allocator.allocate().identity.id;
    TEST_ASSERT_EQUAL_UINT32(1, abandoned);
    TEST_ASSERT_EQUAL_UINT32(2, following);
}

void testCopySelectionFallbackIdenticalAndAlternation() {
    FakeStorage storage;
    FakeEntropy entropy;
    NodeMetadata value = metadata();
    seed(storage, CopySlot::A, value);
    Allocator allocator;
    assertStatus(Status::READY, allocator.recover(storage, entropy, 0x21, CustodyState::NONEMPTY));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CopySlot::A), static_cast<uint8_t>(allocator.activeSlot()));
    TEST_ASSERT_EQUAL_UINT32(33, allocator.allocate().identity.id);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CopySlot::B), static_cast<uint8_t>(storage.lastWritten));
    TEST_ASSERT_EQUAL_UINT32(8, allocator.metadata().generation);

    storage.hasWritten = false;
    storage.copies[0] = storage.copies[1];
    Allocator identical;
    assertStatus(Status::READY, identical.recover(storage, entropy, 0x21, CustodyState::NONEMPTY));
}

void testMetadataConflictsAndUnavailableReadsFailClosed() {
    FakeEntropy entropy;
    NodeMetadata left = metadata(7);
    NodeMetadata right = metadata(7, 0x21, 0x11223344U, 65);
    FakeStorage conflict;
    seed(conflict, CopySlot::A, left);
    seed(conflict, CopySlot::B, right);
    Allocator allocator;
    assertStatus(Status::METADATA_CONFLICT,
        allocator.recover(conflict, entropy, 0x21, CustodyState::EMPTY));
    TEST_ASSERT_FALSE(allocator.ready());
    TEST_ASSERT_EQUAL_UINT(0, entropy.calls);

    FakeStorage ambiguous;
    seed(ambiguous, CopySlot::A, metadata(0));
    seed(ambiguous, CopySlot::B, metadata(0x80000000U));
    assertStatus(Status::GENERATION_AMBIGUOUS,
        allocator.recover(ambiguous, entropy, 0x21, CustodyState::EMPTY));

    FakeStorage unavailable;
    unavailable.unavailableOnInitialRead = true;
    assertStatus(Status::STORAGE_UNAVAILABLE,
        allocator.recover(unavailable, entropy, 0x21, CustodyState::EMPTY));
}

void testBothInvalidDependsOnCustodyAuthority() {
    FakeStorage storage;
    FakeEntropy entropy;
    Allocator allocator;
    assertStatus(Status::IDENTITY_RECOVERY_REQUIRED,
        allocator.recover(storage, entropy, 0x21, CustodyState::NONEMPTY));
    TEST_ASSERT_EQUAL_UINT(0, storage.writes);
    assertStatus(Status::IDENTITY_RECOVERY_REQUIRED,
        allocator.recover(storage, entropy, 0x21, CustodyState::UNPROVABLE));
    TEST_ASSERT_EQUAL_UINT(0, storage.writes);
}

void testInvalidReservationBoundaryFailsClosedWithCustody() {
    FakeStorage storage;
    FakeEntropy entropy;
    seed(storage, CopySlot::A, metadata(7, 0x21, 9, 2));
    Allocator allocator;
    assertStatus(Status::IDENTITY_RECOVERY_REQUIRED,
        allocator.recover(storage, entropy, 0x21, CustodyState::NONEMPTY));
    TEST_ASSERT_EQUAL_UINT(0, storage.writes);
    TEST_ASSERT_EQUAL_UINT(0, entropy.calls);
}

void testSourceBindingAndEmptyAdoption() {
    FakeStorage storage;
    FakeEntropy entropy;
    seed(storage, CopySlot::A, metadata());
    Allocator allocator;
    assertStatus(Status::IDENTITY_CONTEXT_MISMATCH,
        allocator.recover(storage, entropy, 0x22, CustodyState::NONEMPTY));
    TEST_ASSERT_EQUAL_UINT(0, entropy.calls);
    TEST_ASSERT_EQUAL_UINT(0, storage.writes);

    assertStatus(Status::READY,
        allocator.recover(storage, entropy, 0x22, CustodyState::EMPTY));
    TEST_ASSERT_EQUAL_UINT8(0x22, allocator.metadata().custodySourceDeviceId);
    TEST_ASSERT_EQUAL_HEX32(entropy.value, allocator.metadata().eventEpoch);
    TEST_ASSERT_EQUAL_UINT32(1, allocator.metadata().nextUnreservedEventId);
    TEST_ASSERT_EQUAL_UINT32(8, allocator.metadata().generation);
    TEST_ASSERT_EQUAL_UINT(1, storage.writes);
}

void testEveryMutationFaultReturnsNoIdentityAndLatchesFailure() {
    const ReadBackFault faults[] = {
        ReadBackFault::MISSING,
        ReadBackFault::UNAVAILABLE,
        ReadBackFault::MISMATCH,
        ReadBackFault::INVALID_CRC
    };
    for (size_t i = 0; i < sizeof(faults) / sizeof(faults[0]); ++i) {
        FakeStorage storage;
        FakeEntropy entropy;
        seed(storage, CopySlot::A, metadata());
        Allocator allocator;
        assertStatus(Status::READY,
            allocator.recover(storage, entropy, 0x21, CustodyState::NONEMPTY));
        storage.readBackFault = faults[i];
        AllocationResult failed = allocator.allocate();
        TEST_ASSERT_NOT_EQUAL(static_cast<uint8_t>(Status::READY), static_cast<uint8_t>(failed.status));
        TEST_ASSERT_EQUAL_UINT32(0, failed.identity.id);
        TEST_ASSERT_FALSE(allocator.ready());
        TEST_ASSERT_EQUAL_UINT32(0, allocator.allocate().identity.id);
    }

    FakeStorage writeFailure;
    FakeEntropy entropy;
    seed(writeFailure, CopySlot::A, metadata());
    Allocator allocator;
    allocator.recover(writeFailure, entropy, 0x21, CustodyState::NONEMPTY);
    writeFailure.writeFails = true;
    assertStatus(Status::STORAGE_FAILURE, allocator.allocate().status);

    FakeStorage commitFailure;
    seed(commitFailure, CopySlot::A, metadata());
    allocator.recover(commitFailure, entropy, 0x21, CustodyState::NONEMPTY);
    commitFailure.commitFails = true;
    assertStatus(Status::STORAGE_FAILURE, allocator.allocate().status);
}

void testIdBoundaryAdvancesEpochWithoutPartialTail() {
    FakeStorage storage;
    FakeEntropy entropy;
    seed(storage, CopySlot::A, metadata(9, 0x21, 7, 0xFFFFFFE1U));
    Allocator allocator;
    assertStatus(Status::READY,
        allocator.recover(storage, entropy, 0x21, CustodyState::NONEMPTY));
    AllocationResult result = allocator.allocate();
    assertStatus(Status::READY, result.status);
    TEST_ASSERT_EQUAL_UINT32(8, result.identity.epoch);
    TEST_ASSERT_EQUAL_UINT32(1, result.identity.id);
    TEST_ASSERT_EQUAL_UINT32(33, allocator.metadata().nextUnreservedEventId);
}

void testLastSafeBlockBeforeBoundaryIsReservedNormally() {
    FakeStorage storage;
    FakeEntropy entropy;
    seed(storage, CopySlot::A, metadata(9, 0x21, 7, 0xFFFFFFC1U));
    Allocator allocator;
    allocator.recover(storage, entropy, 0x21, CustodyState::NONEMPTY);
    AllocationResult result = allocator.allocate();
    TEST_ASSERT_EQUAL_UINT32(7, result.identity.epoch);
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFC1U, result.identity.id);
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFE1U, allocator.metadata().nextUnreservedEventId);
}

void testEpochExhaustionNeverWraps() {
    FakeStorage storage;
    FakeEntropy entropy;
    seed(storage, CopySlot::A, metadata(9, 0x21, UINT32_MAX, 0xFFFFFFE1U));
    Allocator allocator;
    allocator.recover(storage, entropy, 0x21, CustodyState::NONEMPTY);
    AllocationResult result = allocator.allocate();
    assertStatus(Status::EPOCH_EXHAUSTED, result.status);
    TEST_ASSERT_EQUAL_UINT32(0, result.identity.epoch);
    TEST_ASSERT_EQUAL_UINT32(0, result.identity.id);
    TEST_ASSERT_EQUAL_UINT(0, storage.writes);
}

void testGenerationWrapMutationRemainsOrdered() {
    FakeStorage storage;
    FakeEntropy entropy;
    seed(storage, CopySlot::A, metadata(UINT32_MAX, 0x21, 7, 33));
    Allocator allocator;
    allocator.recover(storage, entropy, 0x21, CustodyState::NONEMPTY);
    TEST_ASSERT_EQUAL_UINT32(33, allocator.allocate().identity.id);
    TEST_ASSERT_EQUAL_UINT32(0, allocator.metadata().generation);
    NodeMetadata selected = {};
    CopySlot slot = CopySlot::A;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(SelectionResult::NEWEST_B),
        static_cast<uint8_t>(selectCopies(
            storage.copies[0], storage.copies[1],
            decodeNodeMetadataForSelection, selected, slot)));
}

}  // namespace

void setUp() {}
void tearDown() {}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(testFreshInitializationAndFirstReservationAreDurable);
    RUN_TEST(testFreshInitializationRejectsInvalidInputs);
    RUN_TEST(testSequentialBlocksMutateOncePerBlockAndNeverReuse);
    RUN_TEST(testRebootSkipsUnusedReservedIds);
    RUN_TEST(testIssuedIdentityIsBurnedWithoutCallerFeedback);
    RUN_TEST(testCopySelectionFallbackIdenticalAndAlternation);
    RUN_TEST(testMetadataConflictsAndUnavailableReadsFailClosed);
    RUN_TEST(testBothInvalidDependsOnCustodyAuthority);
    RUN_TEST(testInvalidReservationBoundaryFailsClosedWithCustody);
    RUN_TEST(testSourceBindingAndEmptyAdoption);
    RUN_TEST(testEveryMutationFaultReturnsNoIdentityAndLatchesFailure);
    RUN_TEST(testIdBoundaryAdvancesEpochWithoutPartialTail);
    RUN_TEST(testLastSafeBlockBeforeBoundaryIsReservedNormally);
    RUN_TEST(testEpochExhaustionNeverWraps);
    RUN_TEST(testGenerationWrapMutationRemainsOrdered);
    return UNITY_END();
}
