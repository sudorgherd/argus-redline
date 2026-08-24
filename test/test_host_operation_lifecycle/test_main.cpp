#include <unity.h>

#include "host_operation_lifecycle.h"
#include "simulated_capabilities.h"

namespace {

using namespace HostOperationLifecycle;
using namespace HostProtocol;

constexpr uint8_t HUB = 1;
constexpr uint8_t NODE = 16;

bool availability(const void* context, DeviceCapabilities::CapabilityId id,
    bool& available) {
    return SimulatedCapabilities::capabilityAvailability(
        *static_cast<const SimulatedCapabilities::State*>(context), id, available);
}

struct Fixture {
    SimulatedCapabilities::State capabilityState;
    SimulatedCapabilities::Handler handler;
    DeviceCapabilities::CapabilityDiagnostics diagnostics;
    RuntimeState::State runtime;
    HostOperationService::DeviceSnapshot snapshot;
    HostOperationService::AvailabilityProvider availabilityProvider;
    Lifecycle lifecycle;

    Fixture() : handler(capabilityState),
        runtime(RuntimeState::DeviceRole::HUB, HUB, NODE),
        snapshot(HostOperationService::makeDeviceSnapshot(runtime, 100)),
        availabilityProvider(HostOperationService::makeAvailabilityProvider(
            &capabilityState, availability)) {}

    Result submit(uint16_t id, const uint8_t* payload, size_t length,
        bool configured = false, uint8_t minor = VERSION_MINOR_0_1) {
        return lifecycle.submit(id, payload, length, snapshot, NODE, true,
            SimulatedCapabilities::registryView(), handler,
            DeviceCapabilities::InterlockState::CLEAR, diagnostics, runtime,
            availabilityProvider, 10, 20, 1, configured, minor);
    }
};

OperationRequest makeRequest(OperationCategory category, OperationCode operation,
    uint8_t targetDevice = HUB, uint16_t targetId = 0) {
    OperationRequest request = {};
    request.category = category;
    request.operation = operation;
    request.targetDeviceId = targetDevice;
    request.targetId = targetId;
    setNoneValue(request.value);
    return request;
}

struct EncodedRequest {
    uint8_t bytes[MAX_PAYLOAD_SIZE];
    size_t length;
};

EncodedRequest encodeRequest(const OperationRequest& request) {
    EncodedRequest encoded = {};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PayloadResult::OK),
        static_cast<uint8_t>(encodeOperationRequest(request, encoded.bytes,
            sizeof(encoded.bytes), encoded.length)));
    return encoded;
}

void assertRejection(const Result& result, RequestRejectionCode code) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Action::HOST_RESPONSE_READY),
        static_cast<uint8_t>(result.action));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ResultClass::REQUEST_REJECTED),
        static_cast<uint8_t>(result.response.resultClass));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(code), result.response.resultCode);
    TEST_ASSERT_TRUE(isNoneValue(result.response.value));
}

void assertOperation(const Result& result,
    DeviceCapabilities::OperationStatus status) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Action::HOST_RESPONSE_READY),
        static_cast<uint8_t>(result.action));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ResultClass::OPERATION_RESULT),
        static_cast<uint8_t>(result.response.resultClass));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(status), result.response.resultCode);
}

Protocol::Packet responseFor(const Protocol::Packet& command,
    DeviceCapabilities::OperationStatus status, uint32_t scalar = 0) {
    WireOperations::Response response = {};
    WireOperations::Request request = {};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WireOperations::CodecResult::OK),
        static_cast<uint8_t>(WireOperations::decodeRequest(command.opcode,
            command.payload, command.payloadLength, request)));
    response.status = status;
    response.targetId = request.targetId;
    response.value.type = static_cast<uint8_t>(DeviceCapabilities::ValueType::NONE);
    if (status == DeviceCapabilities::OperationStatus::OK &&
        command.opcode == static_cast<uint8_t>(OperationCode::PING)) {
        response.value.type = static_cast<uint8_t>(
            DeviceCapabilities::ValueType::UNSIGNED_32);
        response.value.length = 4;
        writeUint32Le(response.value.bytes, scalar);
    }
    return WireOperations::makeResponsePacket(command, response);
}

size_t encodeTerminal(uint16_t requestId, const OperationResponse& response,
    uint8_t* output) {
    Frame frame = {};
    frame.major = VERSION_MAJOR;
    frame.minor = VERSION_MINOR;
    frame.messageType = MessageType::OPERATION_RESPONSE;
    frame.flags = 0;
    frame.requestId = requestId;
    size_t payloadLength = 0;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PayloadResult::OK),
        static_cast<uint8_t>(encodeOperationResponse(response, frame.payload,
            sizeof(frame.payload), payloadLength)));
    frame.payloadLength = static_cast<uint16_t>(payloadLength);
    size_t encodedLength = 0;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EncodeResult::OK),
        static_cast<uint8_t>(encodeFrame(frame, output,
            MAX_ENCODED_FRAME_SIZE, encodedLength)));
    return encodedLength;
}

void testEmptyLocalCompletionReplayAndZeroId() {
    Fixture fixture;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(State::EMPTY),
        static_cast<uint8_t>(fixture.lifecycle.state()));
    EncodedRequest ping = encodeRequest(makeRequest(OperationCategory::DEVICE,
        OperationCode::PING));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Action::INVALID_REQUEST),
        static_cast<uint8_t>(fixture.submit(0, ping.bytes, ping.length).action));
    Result first = fixture.submit(0x1001, ping.bytes, ping.length);
    assertOperation(first, DeviceCapabilities::OperationStatus::OK);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(State::COMPLETED),
        static_cast<uint8_t>(fixture.lifecycle.state()));
    fixture.snapshot.uptimeSeconds = 999;
    Result replay = fixture.submit(0x1001, ping.bytes, ping.length);
    assertOperation(replay, DeviceCapabilities::OperationStatus::OK);
    TEST_ASSERT_EQUAL_HEX32(100, readUint32Le(replay.response.value.bytes));
}

void testLocalCapabilityExecutesOnceAndExactFrameReplays() {
    Fixture fixture;
    OperationRequest set = makeRequest(OperationCategory::CAPABILITY,
        OperationCode::SET_INDICATOR, HUB,
        SimulatedCapabilities::APPLICATION_INDICATOR_ID);
    setBooleanValue(set.value, 1);
    EncodedRequest payload = encodeRequest(set);
    Result first = fixture.submit(0x2001, payload.bytes, payload.length);
    assertOperation(first, DeviceCapabilities::OperationStatus::OK);
    TEST_ASSERT_TRUE(fixture.capabilityState.indicator);
    const uint32_t attempts = fixture.diagnostics.snapshot().counters.lookupAttempts;
    uint8_t firstBytes[MAX_ENCODED_FRAME_SIZE] = {};
    const size_t firstLength = encodeTerminal(first.requestId, first.response,
        firstBytes);
    fixture.capabilityState.indicator = false;
    Result replay = fixture.submit(0x2001, payload.bytes, payload.length);
    TEST_ASSERT_EQUAL_UINT32(attempts,
        fixture.diagnostics.snapshot().counters.lookupAttempts);
    uint8_t replayBytes[MAX_ENCODED_FRAME_SIZE] = {};
    const size_t replayLength = encodeTerminal(replay.requestId, replay.response,
        replayBytes);
    TEST_ASSERT_EQUAL_UINT(firstLength, replayLength);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(firstBytes, replayBytes, firstLength);
    TEST_ASSERT_FALSE(fixture.capabilityState.indicator);
}

void testActiveDuplicateMismatchBusyAndNoQueue() {
    Fixture fixture;
    fixture.lifecycle.remoteBridge().configurePeerSupport(
        WireOperations::PeerSupport::SUPPORTED);
    EncodedRequest ping = encodeRequest(makeRequest(OperationCategory::DEVICE,
        OperationCode::PING, NODE));
    Result first = fixture.submit(0x1234, ping.bytes, ping.length);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Action::TRANSMIT_COMMAND),
        static_cast<uint8_t>(first.action));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Action::ACTIVE_DUPLICATE),
        static_cast<uint8_t>(fixture.submit(0x1234, ping.bytes, ping.length).action));
    OperationRequest statusRequest = makeRequest(OperationCategory::DEVICE,
        OperationCode::GET_STATUS, NODE);
    EncodedRequest status = encodeRequest(statusRequest);
    assertRejection(fixture.submit(0x1234, status.bytes, status.length),
        RequestRejectionCode::MISMATCH);
    assertRejection(fixture.submit(0x1235, status.bytes, status.length),
        RequestRejectionCode::BUSY);
    TEST_ASSERT_TRUE(fixture.lifecycle.remoteBridge().active());
    Result terminal = fixture.lifecycle.receive(responseFor(first.packet,
        DeviceCapabilities::OperationStatus::OK, 0x12345678));
    assertOperation(terminal, DeviceCapabilities::OperationStatus::OK);
    Result acceptedLater = fixture.submit(0x1235, status.bytes, status.length);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Action::TRANSMIT_COMMAND),
        static_cast<uint8_t>(acceptedLater.action));
}

void testCompletedMismatchSurvivesAndDifferentAcceptedReplaces() {
    Fixture fixture;
    EncodedRequest ping = encodeRequest(makeRequest(OperationCategory::DEVICE,
        OperationCode::PING));
    Result original = fixture.submit(0x1001, ping.bytes, ping.length);
    OperationRequest statusRequest = makeRequest(OperationCategory::DEVICE,
        OperationCode::GET_STATUS);
    EncodedRequest status = encodeRequest(statusRequest);
    assertRejection(fixture.submit(0x1001, status.bytes, status.length),
        RequestRejectionCode::MISMATCH);
    Result replay = fixture.submit(0x1001, ping.bytes, ping.length);
    TEST_ASSERT_EQUAL_HEX32(readUint32Le(original.response.value.bytes),
        readUint32Le(replay.response.value.bytes));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Action::INVALID_REQUEST),
        static_cast<uint8_t>(fixture.submit(0x2002, status.bytes,
            status.length - 1).action));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Action::HOST_RESPONSE_READY),
        static_cast<uint8_t>(fixture.submit(0x1001, ping.bytes, ping.length).action));
    Result replacement = fixture.submit(0x2002, status.bytes, status.length);
    assertOperation(replacement, DeviceCapabilities::OperationStatus::OK);
    TEST_ASSERT_EQUAL_HEX16(0x2002, fixture.lifecycle.entry().requestId);
    Result old = fixture.submit(0x1001, ping.bytes, ping.length);
    assertOperation(old, DeviceCapabilities::OperationStatus::OK);
    TEST_ASSERT_EQUAL_HEX16(0x1001, fixture.lifecycle.entry().requestId);
}

void testBadTargetAndPeerGateDoNotEvictCompletion() {
    Fixture fixture;
    EncodedRequest local = encodeRequest(makeRequest(OperationCategory::DEVICE,
        OperationCode::PING));
    fixture.submit(0x3001, local.bytes, local.length);
    EncodedRequest bad = encodeRequest(makeRequest(OperationCategory::DEVICE,
        OperationCode::PING, 99));
    assertRejection(fixture.submit(0x3002, bad.bytes, bad.length),
        RequestRejectionCode::BAD_TARGET);
    EncodedRequest remoteStatus = encodeRequest(makeRequest(
        OperationCategory::DEVICE, OperationCode::GET_STATUS, NODE));
    assertRejection(fixture.submit(0x3003, remoteStatus.bytes,
        remoteStatus.length), RequestRejectionCode::UNSUPPORTED_OPERATION);
    TEST_ASSERT_EQUAL_HEX16(0x3001, fixture.lifecycle.entry().requestId);
    assertOperation(fixture.submit(0x3001, local.bytes, local.length),
        DeviceCapabilities::OperationStatus::OK);
}

void testAcceptedRemoteFailuresReplaceAndReplay() {
    Fixture fixture;
    EncodedRequest local = encodeRequest(makeRequest(OperationCategory::DEVICE,
        OperationCode::PING));
    fixture.submit(0x4001, local.bytes, local.length);
    fixture.lifecycle.remoteBridge().configurePeerSupport(
        WireOperations::PeerSupport::SUPPORTED);
    EncodedRequest read = encodeRequest(makeRequest(OperationCategory::CAPABILITY,
        OperationCode::READ_CAPABILITY, NODE,
        SimulatedCapabilities::DIGITAL_INPUT_ID));
    Result submitted = fixture.submit(0x4002, read.bytes, read.length);
    Result denied = fixture.lifecycle.receive(responseFor(submitted.packet,
        DeviceCapabilities::OperationStatus::UNAUTHORIZED));
    assertOperation(denied, DeviceCapabilities::OperationStatus::UNAUTHORIZED);
    assertOperation(fixture.submit(0x4002, read.bytes, read.length),
        DeviceCapabilities::OperationStatus::UNAUTHORIZED);
    TEST_ASSERT_EQUAL_HEX16(0x4002, fixture.lifecycle.entry().requestId);

    EncodedRequest ping = encodeRequest(makeRequest(OperationCategory::DEVICE,
        OperationCode::PING, NODE));
    submitted = fixture.submit(0x4003, ping.bytes, ping.length);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Action::TRANSMIT_COMMAND),
        static_cast<uint8_t>(submitted.action));
    fixture.lifecycle.service(30);
    Result timeout = fixture.lifecycle.service(30);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ResultClass::RADIO_RESULT),
        static_cast<uint8_t>(timeout.response.resultClass));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RadioResultCode::TIMEOUT),
        timeout.response.resultCode);
    Result replay = fixture.submit(0x4003, ping.bytes, ping.length);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ResultClass::RADIO_RESULT),
        static_cast<uint8_t>(replay.response.resultClass));

    Fixture rejectionFixture;
    Result probe = rejectionFixture.submit(0x4004, ping.bytes, ping.length);
    Protocol::Packet rejectedAck = TransactionEngine::makeAcknowledgment(
        probe.packet, Protocol::AckStatus::UNSUPPORTED_OPCODE);
    Result rejected = rejectionFixture.lifecycle.receive(rejectedAck);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ResultClass::RADIO_RESULT),
        static_cast<uint8_t>(rejected.response.resultClass));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RadioResultCode::REMOTE_REJECTED),
        rejected.response.resultCode);
    replay = rejectionFixture.submit(0x4004, ping.bytes, ping.length);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RadioResultCode::REMOTE_REJECTED),
        replay.response.resultCode);
}

void testDisconnectCompletionRetentionAndNodeCacheIndependence() {
    Fixture fixture;
    EncodedRequest ping = encodeRequest(makeRequest(OperationCategory::DEVICE,
        OperationCode::PING, NODE));
    Result submitted = fixture.submit(0x5001, ping.bytes, ping.length);
    fixture.lifecycle.onHostDisconnect();
    Result completed = fixture.lifecycle.receive(responseFor(submitted.packet,
        DeviceCapabilities::OperationStatus::OK, 77));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Action::RESPONSE_RETAINED),
        static_cast<uint8_t>(completed.action));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(State::COMPLETED),
        static_cast<uint8_t>(fixture.lifecycle.state()));
    fixture.lifecycle.onHostReconnect();
    TEST_ASSERT_TRUE(fixture.lifecycle.hostConnected());
    Result replay = fixture.submit(0x5001, ping.bytes, ping.length);
    assertOperation(replay, DeviceCapabilities::OperationStatus::OK);
    TEST_ASSERT_EQUAL_HEX32(77, readUint32Le(replay.response.value.bytes));
    TEST_ASSERT_FALSE(fixture.lifecycle.remoteBridge().active());
}

void testResetClearsActiveAndCompletedWithoutAmbiguity() {
    Fixture fixture;
    fixture.lifecycle.reset();
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(State::EMPTY),
        static_cast<uint8_t>(fixture.lifecycle.state()));
    EncodedRequest remote = encodeRequest(makeRequest(OperationCategory::DEVICE,
        OperationCode::PING, NODE));
    fixture.submit(0x6001, remote.bytes, remote.length);
    fixture.lifecycle.reset();
    TEST_ASSERT_FALSE(fixture.lifecycle.remoteBridge().active());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WireOperations::PeerSupport::UNKNOWN),
        static_cast<uint8_t>(fixture.lifecycle.remoteBridge().peerSupport()));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(State::EMPTY),
        static_cast<uint8_t>(fixture.lifecycle.state()));
    EncodedRequest local = encodeRequest(makeRequest(OperationCategory::DEVICE,
        OperationCode::PING));
    fixture.submit(0x6002, local.bytes, local.length);
    fixture.lifecycle.reset();
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(State::EMPTY),
        static_cast<uint8_t>(fixture.lifecycle.state()));
    TEST_ASSERT_EQUAL_HEX16(0, fixture.lifecycle.entry().requestId);
}

void testResultClassAuthoritiesRemainDistinct() {
    Fixture fixture;
    fixture.lifecycle.remoteBridge().configurePeerSupport(
        WireOperations::PeerSupport::SUPPORTED);
    EncodedRequest ping = encodeRequest(makeRequest(OperationCategory::DEVICE,
        OperationCode::PING, NODE));
    fixture.submit(0x7001, ping.bytes, ping.length);
    EncodedRequest other = encodeRequest(makeRequest(OperationCategory::DEVICE,
        OperationCode::GET_STATUS, NODE));
    Result busy = fixture.submit(0x7002, other.bytes, other.length);
    assertRejection(busy, RequestRejectionCode::BUSY);
    TEST_ASSERT_NOT_EQUAL(static_cast<uint8_t>(
        DeviceCapabilities::OperationStatus::BUSY), busy.response.resultCode);
    fixture.lifecycle.reset();
    fixture.lifecycle.remoteBridge().configurePeerSupport(
        WireOperations::PeerSupport::SUPPORTED);
    OperationRequest set = makeRequest(OperationCategory::CAPABILITY,
        OperationCode::SET_INDICATOR, NODE,
        SimulatedCapabilities::APPLICATION_INDICATOR_ID);
    setBooleanValue(set.value, 1);
    EncodedRequest setPayload = encodeRequest(set);
    Result submitted = fixture.submit(0x7003, setPayload.bytes, setPayload.length);
    Result denied = fixture.lifecycle.receive(responseFor(submitted.packet,
        DeviceCapabilities::OperationStatus::UNAUTHORIZED));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ResultClass::OPERATION_RESULT),
        static_cast<uint8_t>(denied.response.resultClass));

    fixture.lifecycle.remoteBridge().configurePeerSupport(
        WireOperations::PeerSupport::SUPPORTED);
    EncodedRequest read = encodeRequest(makeRequest(OperationCategory::CAPABILITY,
        OperationCode::READ_CAPABILITY, NODE,
        SimulatedCapabilities::DIGITAL_INPUT_ID));
    submitted = fixture.submit(0x7004, read.bytes, read.length);
    Result handlerBusy = fixture.lifecycle.receive(responseFor(submitted.packet,
        DeviceCapabilities::OperationStatus::BUSY));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ResultClass::OPERATION_RESULT),
        static_cast<uint8_t>(handlerBusy.response.resultClass));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(
        DeviceCapabilities::OperationStatus::BUSY), handlerBusy.response.resultCode);
}

void testBoundsAreSingleEntryAndAllocationFreeShape() {
    TEST_ASSERT_EQUAL_UINT(MAX_PAYLOAD_SIZE,
        sizeof(RetainedEntry::requestPayload));
    TEST_ASSERT_TRUE(sizeof(Lifecycle) < 1024);
    TEST_ASSERT_TRUE(sizeof(RetainedEntry) < 512);
}

void testRequestMinorIsRetainedAcrossImmediateDeferredAndReplay() {
    Fixture local;
    EncodedRequest ping = encodeRequest(makeRequest(
        OperationCategory::DEVICE, OperationCode::PING));
    Result immediate = local.submit(0x7101, ping.bytes, ping.length, false,
        VERSION_MINOR_0_2);
    TEST_ASSERT_EQUAL_UINT8(VERSION_MINOR_0_2, immediate.responseMinor);
    Result crossMinorReplay = local.submit(0x7101, ping.bytes, ping.length, false,
        VERSION_MINOR_0_1);
    TEST_ASSERT_EQUAL_UINT8(VERSION_MINOR_0_2, crossMinorReplay.responseMinor);
    TEST_ASSERT_EQUAL_UINT8(VERSION_MINOR_0_2, local.lifecycle.entry().requestMinor);

    Fixture remote;
    remote.lifecycle.remoteBridge().configurePeerSupport(
        WireOperations::PeerSupport::SUPPORTED);
    OperationRequest request = makeRequest(OperationCategory::DEVICE,
        OperationCode::PING, NODE);
    EncodedRequest encoded = encodeRequest(request);
    Result started = remote.submit(0x7102, encoded.bytes, encoded.length, false,
        VERSION_MINOR_0_1);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Action::TRANSMIT_COMMAND),
                           static_cast<uint8_t>(started.action));
    Result completed = remote.lifecycle.receive(responseFor(started.packet,
        DeviceCapabilities::OperationStatus::OK, 7));
    TEST_ASSERT_EQUAL_UINT8(VERSION_MINOR_0_1, completed.responseMinor);
    TEST_ASSERT_EQUAL_UINT8(VERSION_MINOR_0_1,
                           remote.lifecycle.entry().requestMinor);

    Fixture busy;
    busy.lifecycle.remoteBridge().configurePeerSupport(
        WireOperations::PeerSupport::SUPPORTED);
    Result active = busy.submit(0x7103, encoded.bytes, encoded.length, false,
        VERSION_MINOR_0_1);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Action::TRANSMIT_COMMAND),
                           static_cast<uint8_t>(active.action));
    Result rejected = busy.submit(0x7104, ping.bytes, ping.length, false,
        VERSION_MINOR_0_2);
    TEST_ASSERT_EQUAL_UINT8(VERSION_MINOR_0_2, rejected.responseMinor);
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(testEmptyLocalCompletionReplayAndZeroId);
    RUN_TEST(testLocalCapabilityExecutesOnceAndExactFrameReplays);
    RUN_TEST(testActiveDuplicateMismatchBusyAndNoQueue);
    RUN_TEST(testCompletedMismatchSurvivesAndDifferentAcceptedReplaces);
    RUN_TEST(testBadTargetAndPeerGateDoNotEvictCompletion);
    RUN_TEST(testAcceptedRemoteFailuresReplaceAndReplay);
    RUN_TEST(testDisconnectCompletionRetentionAndNodeCacheIndependence);
    RUN_TEST(testResetClearsActiveAndCompletedWithoutAmbiguity);
    RUN_TEST(testResultClassAuthoritiesRemainDistinct);
    RUN_TEST(testBoundsAreSingleEntryAndAllocationFreeShape);
    RUN_TEST(testRequestMinorIsRetainedAcrossImmediateDeferredAndReplay);
    return UNITY_END();
}
