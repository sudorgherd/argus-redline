#include <unity.h>

#include "host_event_service.h"

using namespace EventStorage;
using namespace EventRecords;
using namespace EventIdentity;

namespace {

class FakeStorage final : public HubEventLedger::Storage {
public:
    FixedCopy<HUB_METADATA_SIZE> metadata[2] = {};
    FixedCopy<HUB_RECORD_SIZE> records[HubEventLedger::HUB_EVENT_CAPACITY][2] = {};
    unsigned recordWrites = 0;
    bool failWrites = false;

    FakeStorage() {
        metadata[0].status = metadata[1].status = FixedReadStatus::MISSING;
        for (size_t i = 0; i < HubEventLedger::HUB_EVENT_CAPACITY; ++i)
            records[i][0].status = records[i][1].status = FixedReadStatus::MISSING;
    }
    StorageResult readHubMetadata(CopySlot copy, uint8_t* out, size_t cap,
                                  size_t& length) override {
        return read(metadata[index(copy)], out, cap, length, HUB_METADATA_SIZE);
    }
    StorageResult writeHubMetadata(CopySlot copy, const uint8_t* in,
                                   size_t length) override {
        return write(metadata[index(copy)], in, length, HUB_METADATA_SIZE);
    }
    StorageResult readHubEventCopy(uint8_t slot, CopySlot copy, uint8_t* out,
                                  size_t cap, size_t& length) override {
        if (slot >= HubEventLedger::HUB_EVENT_CAPACITY) return StorageResult::ERROR;
        return read(records[slot][index(copy)], out, cap, length, HUB_RECORD_SIZE);
    }
    StorageResult writeHubEventCopy(uint8_t slot, CopySlot copy,
                                   const uint8_t* in, size_t length) override {
        ++recordWrites;
        if (failWrites || slot >= HubEventLedger::HUB_EVENT_CAPACITY)
            return StorageResult::ERROR;
        return write(records[slot][index(copy)], in, length, HUB_RECORD_SIZE);
    }
    StorageResult commit() override { return StorageResult::OK; }

private:
    static size_t index(CopySlot copy) { return copy == CopySlot::A ? 0U : 1U; }
    template <size_t N> static StorageResult read(FixedCopy<N>& source,
        uint8_t* out, size_t cap, size_t& length, size_t exact) {
        length = 0;
        if (source.status == FixedReadStatus::MISSING) return StorageResult::MISSING;
        if (source.status == FixedReadStatus::UNAVAILABLE) return StorageResult::UNAVAILABLE;
        if (cap < exact) return StorageResult::ERROR;
        for (size_t i = 0; i < exact; ++i) out[i] = source.bytes[i];
        length = exact;
        return StorageResult::OK;
    }
    template <size_t N> static StorageResult write(FixedCopy<N>& destination,
        const uint8_t* in, size_t length, size_t exact) {
        if (length != exact) return StorageResult::ERROR;
        destination.status = FixedReadStatus::OK;
        for (size_t i = 0; i < exact; ++i) destination.bytes[i] = in[i];
        return StorageResult::OK;
    }
};

EventProtocol::Event event(uint32_t id) {
    EventProtocol::Event value = {};
    value.source = 2; value.destination = 1; value.sequence = 7;
    value.family = static_cast<uint8_t>(EventProtocol::Family::BUTTON);
    value.epoch = 9; value.id = id;
    value.flags = EventProtocol::IMPORTANT_FLAG;
    value.lifetimeBudgetSeconds = 3600; value.bodyLength = 1; value.body[0] = 2;
    return value;
}

HostProtocol::OperationRequest pollRequest() {
    HostProtocol::OperationRequest request = {};
    request.category = HostProtocol::OperationCategory::EVENT;
    request.operation = HostProtocol::OperationCode::POLL_EVENTS;
    request.targetDeviceId = 1;
    HostProtocol::setNoneValue(request.value);
    return request;
}

HostProtocol::OperationRequest consumeRequest(uint32_t id) {
    HostProtocol::OperationRequest request = pollRequest();
    request.operation = HostProtocol::OperationCode::CONSUME_EVENT;
    HostProtocol::HostEventIdentity identity = {2, 9, id};
    size_t length = 0;
    request.value.type = HostProtocol::STRUCTURE_VALUE_TYPE;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HostProtocol::PayloadResult::OK),
        static_cast<uint8_t>(HostProtocol::encodeHostEventIdentity(identity,
            request.value.bytes, sizeof(request.value.bytes), length)));
    request.value.length = static_cast<uint8_t>(length);
    return request;
}

struct Fixture {
    FakeStorage storage;
    HubEventLedger::Ledger ledger;
    HostEventService::Service service;
    Fixture() : service(ledger) {
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HubEventLedger::Status::READY),
            static_cast<uint8_t>(ledger.recover(storage, 1, 2)));
    }
    HostOperationService::Result poll() {
        return service.handle(1, pollRequest(), RuntimeState::DeviceRole::HUB, 1);
    }
    HostOperationService::Result consume(uint32_t id) {
        return service.handle(2, consumeRequest(id), RuntimeState::DeviceRole::HUB, 1);
    }
};

void testPollEmptyIsExplicitNonDestructiveAbsence() {
    Fixture f; const unsigned writes = f.storage.recordWrites;
    HostOperationService::Result result = f.poll();
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HostProtocol::ResultClass::SUCCESS),
        static_cast<uint8_t>(result.response.resultClass));
    HostProtocol::HostEventRecord record = {};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HostProtocol::PayloadResult::OK),
        static_cast<uint8_t>(HostProtocol::decodeHostEventRecord(
            result.response.value.bytes, result.response.value.length, record)));
    TEST_ASSERT_EQUAL_UINT8(0, record.available);
    TEST_ASSERT_EQUAL_UINT(writes, f.storage.recordWrites);
}

void testPollReturnsOldestActiveWithoutMutation() {
    Fixture f; f.ledger.admit(event(1)); f.ledger.admit(event(2));
    const unsigned writes = f.storage.recordWrites;
    HostOperationService::Result result = f.poll();
    HostProtocol::HostEventRecord record = {};
    HostProtocol::decodeHostEventRecord(result.response.value.bytes,
        result.response.value.length, record);
    TEST_ASSERT_EQUAL_UINT8(1, record.available);
    TEST_ASSERT_EQUAL_UINT32(1, record.eventId);
    TEST_ASSERT_EQUAL_UINT32(9, record.eventEpoch);
    TEST_ASSERT_EQUAL_UINT32(3600, record.lifetimeBudgetSeconds);
    TEST_ASSERT_EQUAL_UINT(writes, f.storage.recordWrites);
}

void testPollSkipsConsumedAndRepeatedPollIsStable() {
    Fixture f; f.ledger.admit(event(1)); f.ledger.admit(event(2));
    EventProtocol::Identity first = {2, 9, 1}; f.ledger.consume(first);
    HostOperationService::Result a = f.poll(), b = f.poll();
    HostProtocol::HostEventRecord ra = {}, rb = {};
    HostProtocol::decodeHostEventRecord(a.response.value.bytes, a.response.value.length, ra);
    HostProtocol::decodeHostEventRecord(b.response.value.bytes, b.response.value.length, rb);
    TEST_ASSERT_EQUAL_UINT32(2, ra.eventId);
    TEST_ASSERT_EQUAL_UINT32(ra.eventId, rb.eventId);
}

void testConsumeIsDurableAndIdempotent() {
    Fixture f; f.ledger.admit(event(1));
    HostOperationService::Result first = f.consume(1);
    const unsigned writes = f.storage.recordWrites;
    HostOperationService::Result second = f.consume(1);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HostProtocol::ResultClass::SUCCESS),
        static_cast<uint8_t>(first.response.resultClass));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HostProtocol::ResultClass::SUCCESS),
        static_cast<uint8_t>(second.response.resultClass));
    TEST_ASSERT_EQUAL_UINT(writes, f.storage.recordWrites);
    TEST_ASSERT_EQUAL_UINT(1, f.ledger.consumedCount());
}

void testConsumeNotFoundAndStorageFailureAreEventResults() {
    Fixture f;
    HostOperationService::Result missing = f.consume(77);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HostProtocol::ResultClass::EVENT_RESULT),
        static_cast<uint8_t>(missing.response.resultClass));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HostProtocol::EventResultCode::NOT_FOUND),
        missing.response.resultCode);
    f.ledger.admit(event(1)); f.storage.failWrites = true;
    HostOperationService::Result failed = f.consume(1);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HostProtocol::EventResultCode::STORAGE_FAILURE),
        failed.response.resultCode);
}

void testRoleTargetAndMalformedRejectBeforeCustody() {
    Fixture f; const unsigned writes = f.storage.recordWrites;
    HostProtocol::OperationRequest request = pollRequest();
    HostOperationService::Result node = f.service.handle(1, request,
        RuntimeState::DeviceRole::NODE, 1);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HostProtocol::RequestRejectionCode::UNSUPPORTED_OPERATION),
        node.response.resultCode);
    request.targetDeviceId = 3;
    HostOperationService::Result target = f.service.handle(1, request,
        RuntimeState::DeviceRole::HUB, 1);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HostProtocol::RequestRejectionCode::BAD_TARGET),
        target.response.resultCode);
    TEST_ASSERT_EQUAL_UINT(writes, f.storage.recordWrites);
}

void testRebootPreservesActiveAndConsumedProof() {
    FakeStorage storage;
    HubEventLedger::Ledger first; first.recover(storage, 1, 2); first.admit(event(1));
    HubEventLedger::Ledger recovered; recovered.recover(storage, 1, 2);
    HostEventService::Service service(recovered);
    HostOperationService::Result polled = service.handle(1, pollRequest(),
        RuntimeState::DeviceRole::HUB, 1);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HostProtocol::ResultClass::SUCCESS),
        static_cast<uint8_t>(polled.response.resultClass));
    service.handle(2, consumeRequest(1), RuntimeState::DeviceRole::HUB, 1);
    HubEventLedger::Ledger rebooted; rebooted.recover(storage, 1, 2);
    HostEventService::Service after(rebooted);
    const unsigned writes = storage.recordWrites;
    HostOperationService::Result repeated = after.handle(3, consumeRequest(1),
        RuntimeState::DeviceRole::HUB, 1);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HostProtocol::ResultClass::SUCCESS),
        static_cast<uint8_t>(repeated.response.resultClass));
    TEST_ASSERT_EQUAL_UINT(writes, storage.recordWrites);
}

void testLedgerCustodyRemainsUsableWhenOrdinalAllocatorIsUnavailable() {
    FakeStorage storage;
    HubEventLedger::Ledger initial; initial.recover(storage, 1, 2);
    initial.admit(event(1));
    storage.metadata[0].bytes[0] ^= 0xFF;
    storage.metadata[1].bytes[0] ^= 0xFF;
    HubEventLedger::Ledger recovered;
    recovered.recover(storage, 1, 2);
    TEST_ASSERT_TRUE(recovered.healthy());
    TEST_ASSERT_FALSE(recovered.ordinalReady());
    HostEventService::Service service(recovered);
    HostOperationService::Result polled = service.handle(1, pollRequest(),
        RuntimeState::DeviceRole::HUB, 1);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HostProtocol::ResultClass::SUCCESS),
        static_cast<uint8_t>(polled.response.resultClass));
    HostOperationService::Result consumed = service.handle(2, consumeRequest(1),
        RuntimeState::DeviceRole::HUB, 1);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HostProtocol::ResultClass::SUCCESS),
        static_cast<uint8_t>(consumed.response.resultClass));
    const unsigned writes = storage.recordWrites;
    HostOperationService::Result repeated = service.handle(3, consumeRequest(1),
        RuntimeState::DeviceRole::HUB, 1);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HostProtocol::ResultClass::SUCCESS),
        static_cast<uint8_t>(repeated.response.resultClass));
    TEST_ASSERT_EQUAL_UINT(writes, storage.recordWrites);
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(testPollEmptyIsExplicitNonDestructiveAbsence);
    RUN_TEST(testPollReturnsOldestActiveWithoutMutation);
    RUN_TEST(testPollSkipsConsumedAndRepeatedPollIsStable);
    RUN_TEST(testConsumeIsDurableAndIdempotent);
    RUN_TEST(testConsumeNotFoundAndStorageFailureAreEventResults);
    RUN_TEST(testRoleTargetAndMalformedRejectBeforeCustody);
    RUN_TEST(testRebootPreservesActiveAndConsumedProof);
    RUN_TEST(testLedgerCustodyRemainsUsableWhenOrdinalAllocatorIsUnavailable);
    return UNITY_END();
}
