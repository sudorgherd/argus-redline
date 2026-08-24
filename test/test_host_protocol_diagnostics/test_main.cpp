#include <unity.h>

#include "host_operation_lifecycle.h"
#include "host_protocol_diagnostics.h"
#include "simulated_capabilities.h"

namespace {

using namespace HostProtocol;
using namespace HostProtocolDiagnostics;

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
    DeviceCapabilities::CapabilityDiagnostics capabilityDiagnostics;
    RuntimeState::State runtime;
    HostOperationService::DeviceSnapshot device;
    HostOperationService::AvailabilityProvider availabilityProvider;
    HostOperationLifecycle::Lifecycle lifecycle;

    Fixture() : handler(capabilityState),
        runtime(RuntimeState::DeviceRole::HUB, HUB, NODE),
        device(HostOperationService::makeDeviceSnapshot(runtime, 55)),
        availabilityProvider(HostOperationService::makeAvailabilityProvider(
            &capabilityState, availability)) {}

    HostOperationLifecycle::Result submit(uint16_t id, const uint8_t* payload,
        size_t length) {
        return lifecycle.submit(id, payload, length, device, NODE, true,
            SimulatedCapabilities::registryView(), handler,
            DeviceCapabilities::InterlockState::CLEAR,
            capabilityDiagnostics, runtime, availabilityProvider, 10, 20, 0);
    }
};

struct Bytes {
    uint8_t data[MAX_ENCODED_FRAME_SIZE];
    size_t length;
};

Bytes makeRawFrame(uint8_t major, uint8_t minor, uint8_t type, uint8_t flags,
    uint16_t requestId, uint16_t declaredLength, const uint8_t* payload,
    size_t actualLength, bool corruptCrc = false) {
    uint8_t decoded[MAX_DECODED_FRAME_SIZE] = {};
    decoded[0] = major;
    decoded[1] = minor;
    decoded[2] = type;
    decoded[3] = flags;
    writeUint16Le(decoded + 4, requestId);
    writeUint16Le(decoded + 6, declaredLength);
    for (size_t index = 0; index < actualLength; ++index) {
        decoded[8 + index] = payload[index];
    }
    uint16_t crc = crc16CcittFalse(decoded, 8 + actualLength);
    if (corruptCrc) crc ^= 1;
    writeUint16Le(decoded + 8 + actualLength, crc);
    Bytes result = {};
    size_t candidateLength = 0;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CobsResult::OK),
        static_cast<uint8_t>(cobsEncode(decoded, 10 + actualLength,
            result.data, MAX_COBS_CANDIDATE_SIZE, candidateLength)));
    result.length = candidateLength + 1;
    result.data[candidateLength] = FRAME_DELIMITER;
    return result;
}

StreamResult parse(const Bytes& bytes) {
    StreamParser parser;
    return parser.consume(bytes.data, bytes.length);
}

OperationRequest request(OperationCode operation, uint8_t device = HUB) {
    OperationRequest value = {};
    value.category = operation == OperationCode::READ_CAPABILITY
        ? OperationCategory::CAPABILITY : OperationCategory::DEVICE;
    value.operation = operation;
    value.targetDeviceId = device;
    value.targetId = operation == OperationCode::READ_CAPABILITY
        ? SimulatedCapabilities::DIGITAL_INPUT_ID : 0;
    setNoneValue(value.value);
    return value;
}

struct RequestBytes {
    uint8_t data[MAX_PAYLOAD_SIZE];
    size_t length;
};

RequestBytes encodeRequest(const OperationRequest& requestValue) {
    RequestBytes bytes = {};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PayloadResult::OK),
        static_cast<uint8_t>(encodeOperationRequest(requestValue, bytes.data,
            sizeof(bytes.data), bytes.length)));
    return bytes;
}

Protocol::Packet responseFor(const Protocol::Packet& command,
    DeviceCapabilities::OperationStatus status) {
    WireOperations::Request decoded = {};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WireOperations::CodecResult::OK),
        static_cast<uint8_t>(WireOperations::decodeRequest(command.opcode,
            command.payload, command.payloadLength, decoded)));
    WireOperations::Response response = {};
    response.status = status;
    response.targetId = decoded.targetId;
    response.value.type = static_cast<uint8_t>(DeviceCapabilities::ValueType::NONE);
    return WireOperations::makeResponsePacket(command, response);
}

void testStorageZeroSnapshotAndReset() {
    Diagnostics diagnostics;
    Snapshot first = diagnostics.snapshot();
    const uint32_t* values = reinterpret_cast<const uint32_t*>(&first);
    for (size_t index = 0; index < 9; ++index) TEST_ASSERT_EQUAL_UINT32(0, values[index]);
    TEST_ASSERT_EQUAL_UINT(36, sizeof(Snapshot));
    TEST_ASSERT_EQUAL_UINT(sizeof(Snapshot), sizeof(Diagnostics));
    diagnostics.observeTransportReset();
    Snapshot copy = diagnostics.snapshot();
    diagnostics.observeTransportReset();
    TEST_ASSERT_EQUAL_UINT32(1, copy.transportResets);
    TEST_ASSERT_EQUAL_UINT32(2, diagnostics.snapshot().transportResets);
    diagnostics.reset();
    TEST_ASSERT_EQUAL_UINT32(0, diagnostics.snapshot().transportResets);
}

void testEveryCounterSaturatesAndZeroAmountIsNoOp() {
    uint32_t value = 0;
    saturatingAdd(value, 0);
    TEST_ASSERT_EQUAL_UINT32(0, value);
    value = UINT32_MAX - 5;
    saturatingAdd(value, 10);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, value);
    saturatingAdd(value);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, value);

    Snapshot direct = {};
    uint32_t* fields = reinterpret_cast<uint32_t*>(&direct);
    for (size_t index = 0; index < 9; ++index) {
        saturatingAdd(fields[index]);
        TEST_ASSERT_EQUAL_UINT32(1, fields[index]);
        fields[index] = UINT32_MAX - 1;
        saturatingAdd(fields[index]);
        TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, fields[index]);
        saturatingAdd(fields[index]);
        TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, fields[index]);
    }

    Snapshot initial = {};
    fields = reinterpret_cast<uint32_t*>(&initial);
    for (size_t index = 0; index < 9; ++index) fields[index] = UINT32_MAX - 1;
    Diagnostics diagnostics(initial);
    StreamResult accepted = {};
    accepted.event = StreamEvent::FRAME_READY;
    diagnostics.observeFrame(accepted);
    StreamResult malformed = {};
    malformed.event = StreamEvent::FRAME_REJECTED;
    malformed.decodeResult = DecodeResult::CRC_MISMATCH;
    diagnostics.observeFrame(malformed);
    malformed.decodeResult = DecodeResult::UNSUPPORTED_MAJOR;
    diagnostics.observeFrame(malformed);
    malformed.decodeResult = DecodeResult::UNSUPPORTED_MESSAGE_TYPE;
    diagnostics.observeFrame(malformed);
    diagnostics.observeRequestDispatched();
    OperationResponse rejected = {};
    rejected.resultClass = ResultClass::REQUEST_REJECTED;
    diagnostics.observeOperationResponse(rejected);
    diagnostics.observeResponseHandoff();
    diagnostics.observeTransportReset();
    Snapshot saturated = diagnostics.snapshot();
    fields = reinterpret_cast<uint32_t*>(&saturated);
    for (size_t index = 0; index < 9; ++index) {
        TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, fields[index]);
    }
}

void testValidEmptyPartialAndSemanticParserMapping() {
    const uint8_t helloPayload[] = {1, 1};
    Bytes hello = makeRawFrame(VERSION_MAJOR, VERSION_MINOR,
        static_cast<uint8_t>(MessageType::HELLO_REQUEST), 0, 1, 2,
        helloPayload, sizeof(helloPayload));
    Diagnostics diagnostics;
    diagnostics.observeFrame(parse(hello), MessageBoundaryResult::ACCEPTED);

    StreamParser parser;
    const uint8_t empty[] = {0, 0};
    diagnostics.observeFrame(parser.consume(empty, sizeof(empty)));
    const uint8_t partial[] = {1, 2, 3};
    diagnostics.observeFrame(parser.consume(partial, sizeof(partial)));

    const uint8_t malformedPayload[] = {1};
    Bytes semantic = makeRawFrame(VERSION_MAJOR, VERSION_MINOR,
        static_cast<uint8_t>(MessageType::OPERATION_REQUEST), 0, 2, 1,
        malformedPayload, sizeof(malformedPayload));
    diagnostics.observeFrame(parse(semantic), MessageBoundaryResult::MALFORMED);
    Snapshot snapshot = diagnostics.snapshot();
    TEST_ASSERT_EQUAL_UINT32(2, snapshot.framesReceived);
    TEST_ASSERT_EQUAL_UINT32(1, snapshot.framesAccepted);
    TEST_ASSERT_EQUAL_UINT32(1, snapshot.malformedFrames);
}

void testMalformedParserCasesAndOversizeCountOnce() {
    Diagnostics diagnostics;
    const uint8_t malformedCobs[] = {5, 1, 0};
    StreamParser parser;
    diagnostics.observeFrame(parser.consume(malformedCobs,
        sizeof(malformedCobs)));

    const uint8_t payload[] = {1, 1};
    diagnostics.observeFrame(parse(makeRawFrame(VERSION_MAJOR, VERSION_MINOR,
        static_cast<uint8_t>(MessageType::HELLO_REQUEST), 0, 1, 2,
        payload, sizeof(payload), true)));
    diagnostics.observeFrame(parse(makeRawFrame(VERSION_MAJOR, VERSION_MINOR,
        static_cast<uint8_t>(MessageType::HELLO_REQUEST), 0, 1, 3,
        payload, sizeof(payload))));
    diagnostics.observeFrame(parse(makeRawFrame(VERSION_MAJOR, VERSION_MINOR,
        static_cast<uint8_t>(MessageType::HELLO_REQUEST), 1, 1, 2,
        payload, sizeof(payload))));
    diagnostics.observeFrame(parse(makeRawFrame(VERSION_MAJOR, VERSION_MINOR,
        static_cast<uint8_t>(MessageType::HELLO_REQUEST), 0, 0, 2,
        payload, sizeof(payload))));

    uint8_t oversized[MAX_COBS_CANDIDATE_SIZE + 102] = {};
    for (size_t index = 0; index + 1 < sizeof(oversized); ++index) oversized[index] = 1;
    oversized[sizeof(oversized) - 1] = 0;
    StreamParser oversizedParser;
    diagnostics.observeFrame(oversizedParser.consume(oversized,
        sizeof(oversized)));
    Snapshot snapshot = diagnostics.snapshot();
    TEST_ASSERT_EQUAL_UINT32(6, snapshot.framesReceived);
    TEST_ASSERT_EQUAL_UINT32(6, snapshot.malformedFrames);
}

void testUnsupportedVersionAndMessageTypeDoNotOverlapMalformed() {
    const uint8_t payload[] = {1, 1};
    Diagnostics diagnostics;
    diagnostics.observeFrame(parse(makeRawFrame(1, VERSION_MINOR,
        static_cast<uint8_t>(MessageType::HELLO_REQUEST), 0, 1, 2,
        payload, sizeof(payload))));
    diagnostics.observeFrame(parse(makeRawFrame(VERSION_MAJOR, 3,
        static_cast<uint8_t>(MessageType::HELLO_REQUEST), 0, 1, 2,
        payload, sizeof(payload))));
    diagnostics.observeFrame(parse(makeRawFrame(VERSION_MAJOR, VERSION_MINOR,
        0x55, 0, 1, 2, payload, sizeof(payload))));
    Bytes hello = makeRawFrame(VERSION_MAJOR, VERSION_MINOR,
        static_cast<uint8_t>(MessageType::HELLO_REQUEST), 0, 1, 2,
        payload, sizeof(payload));
    diagnostics.observeFrame(parse(hello),
        MessageBoundaryResult::UNSUPPORTED_VERSION);
    diagnostics.observeFrame(parse(hello),
        MessageBoundaryResult::UNSUPPORTED_MESSAGE_TYPE);
    Snapshot snapshot = diagnostics.snapshot();
    TEST_ASSERT_EQUAL_UINT32(5, snapshot.framesReceived);
    TEST_ASSERT_EQUAL_UINT32(3, snapshot.unsupportedVersions);
    TEST_ASSERT_EQUAL_UINT32(2, snapshot.unsupportedMessageTypes);
    TEST_ASSERT_EQUAL_UINT32(0, snapshot.malformedFrames);
}

void testLifecycleDispatchDuplicateBusyMismatchAndReplayMapping() {
    Fixture fixture;
    Diagnostics diagnostics;
    fixture.lifecycle.remoteBridge().configurePeerSupport(
        WireOperations::PeerSupport::SUPPORTED);
    RequestBytes ping = encodeRequest(request(OperationCode::PING, NODE));
    HostOperationLifecycle::Result first = fixture.submit(0x1234,
        ping.data, ping.length);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(
        HostOperationLifecycle::Action::TRANSMIT_COMMAND),
        static_cast<uint8_t>(first.action));
    diagnostics.observeRequestDispatched();

    HostOperationLifecycle::Result duplicate = fixture.submit(0x1234,
        ping.data, ping.length);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(
        HostOperationLifecycle::Action::ACTIVE_DUPLICATE),
        static_cast<uint8_t>(duplicate.action));

    RequestBytes status = encodeRequest(request(OperationCode::GET_STATUS, NODE));
    HostOperationLifecycle::Result mismatch = fixture.submit(0x1234,
        status.data, status.length);
    diagnostics.observeOperationResponse(mismatch.response);
    diagnostics.observeResponseHandoff();
    HostOperationLifecycle::Result busy = fixture.submit(0x1235,
        status.data, status.length);
    diagnostics.observeOperationResponse(busy.response);
    diagnostics.observeResponseHandoff();

    HostOperationLifecycle::Result terminal = fixture.lifecycle.receive(
        responseFor(first.packet, DeviceCapabilities::OperationStatus::BUSY));
    diagnostics.observeOperationResponse(terminal.response);
    diagnostics.observeResponseHandoff();
    HostOperationLifecycle::Result replay = fixture.submit(0x1234,
        ping.data, ping.length);
    diagnostics.observeResponseHandoff();
    Snapshot snapshot = diagnostics.snapshot();
    TEST_ASSERT_EQUAL_UINT32(1, snapshot.requestsDispatched);
    TEST_ASSERT_EQUAL_UINT32(2, snapshot.busyOrRejectedRequests);
    TEST_ASSERT_EQUAL_UINT32(4, snapshot.responsesEmitted);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ResultClass::OPERATION_RESULT),
        static_cast<uint8_t>(replay.response.resultClass));
}

void testLocalBadTargetAcceptedFailureAndDisconnectedEmissionBoundary() {
    Fixture fixture;
    Diagnostics diagnostics;
    RequestBytes local = encodeRequest(request(OperationCode::PING));
    HostOperationLifecycle::Result localResult = fixture.submit(1,
        local.data, local.length);
    diagnostics.observeRequestDispatched();
    diagnostics.observeResponseHandoff();

    RequestBytes bad = encodeRequest(request(OperationCode::PING, 99));
    HostOperationLifecycle::Result rejected = fixture.submit(2,
        bad.data, bad.length);
    diagnostics.observeOperationResponse(rejected.response);
    diagnostics.observeResponseHandoff();

    Fixture remote;
    remote.lifecycle.remoteBridge().configurePeerSupport(
        WireOperations::PeerSupport::SUPPORTED);
    RequestBytes read = encodeRequest(request(OperationCode::READ_CAPABILITY, NODE));
    HostOperationLifecycle::Result submitted = remote.submit(3,
        read.data, read.length);
    diagnostics.observeRequestDispatched();
    remote.lifecycle.onHostDisconnect();
    HostOperationLifecycle::Result retained = remote.lifecycle.receive(
        responseFor(submitted.packet,
            DeviceCapabilities::OperationStatus::UNAUTHORIZED));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(
        HostOperationLifecycle::Action::RESPONSE_RETAINED),
        static_cast<uint8_t>(retained.action));
    remote.lifecycle.onHostReconnect();
    HostOperationLifecycle::Result replay = remote.submit(3, read.data,
        read.length);
    diagnostics.observeResponseHandoff();
    Snapshot snapshot = diagnostics.snapshot();
    TEST_ASSERT_EQUAL_UINT32(2, snapshot.requestsDispatched);
    TEST_ASSERT_EQUAL_UINT32(1, snapshot.busyOrRejectedRequests);
    TEST_ASSERT_EQUAL_UINT32(3, snapshot.responsesEmitted);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ResultClass::OPERATION_RESULT),
        static_cast<uint8_t>(replay.response.resultClass));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(
        DeviceCapabilities::OperationStatus::UNAUTHORIZED),
        replay.response.resultCode);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ResultClass::OPERATION_RESULT),
        static_cast<uint8_t>(localResult.response.resultClass));
}

void testHelloProtocolErrorAndTransportResetHandoffs() {
    Diagnostics diagnostics;
    diagnostics.observeResponseHandoff();
    diagnostics.observeResponseHandoff();
    diagnostics.observeTransportReset();
    diagnostics.observeTransportReset();
    Snapshot snapshot = diagnostics.snapshot();
    TEST_ASSERT_EQUAL_UINT32(2, snapshot.responsesEmitted);
    TEST_ASSERT_EQUAL_UINT32(0, snapshot.requestsDispatched);
    TEST_ASSERT_EQUAL_UINT32(0, snapshot.busyOrRejectedRequests);
    TEST_ASSERT_EQUAL_UINT32(2, snapshot.transportResets);
}

void testDiagnosticAuthoritiesAndRuntimeHealthRemainIndependent() {
    Diagnostics diagnostics;
    RuntimeState::State runtime(RuntimeState::DeviceRole::HUB, HUB, NODE);
    DeviceCapabilities::CapabilityDiagnostics capabilities;
    const RuntimeState::DiagnosticCounters radioBefore = runtime.counters();
    const auto capabilityBefore = capabilities.snapshot();
    const RuntimeState::Health healthBefore = runtime.health();
    StreamResult malformed = {};
    malformed.event = StreamEvent::FRAME_REJECTED;
    malformed.decodeResult = DecodeResult::CRC_MISMATCH;
    diagnostics.observeFrame(malformed);
    diagnostics.observeRequestDispatched();
    diagnostics.observeTransportReset();
    TEST_ASSERT_EQUAL_MEMORY(&radioBefore, &runtime.counters(),
        sizeof(radioBefore));
    const auto capabilityAfter = capabilities.snapshot();
    TEST_ASSERT_EQUAL_MEMORY(&capabilityBefore, &capabilityAfter,
        sizeof(capabilityBefore));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(healthBefore),
        static_cast<uint8_t>(runtime.health()));
    TEST_ASSERT_EQUAL_UINT8(10, HostOperationService::DIAGNOSTIC_METRIC_COUNT);
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(testStorageZeroSnapshotAndReset);
    RUN_TEST(testEveryCounterSaturatesAndZeroAmountIsNoOp);
    RUN_TEST(testValidEmptyPartialAndSemanticParserMapping);
    RUN_TEST(testMalformedParserCasesAndOversizeCountOnce);
    RUN_TEST(testUnsupportedVersionAndMessageTypeDoNotOverlapMalformed);
    RUN_TEST(testLifecycleDispatchDuplicateBusyMismatchAndReplayMapping);
    RUN_TEST(testLocalBadTargetAcceptedFailureAndDisconnectedEmissionBoundary);
    RUN_TEST(testHelloProtocolErrorAndTransportResetHandoffs);
    RUN_TEST(testDiagnosticAuthoritiesAndRuntimeHealthRemainIndependent);
    return UNITY_END();
}
