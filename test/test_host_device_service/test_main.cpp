#include <unity.h>

#include "host_device_service.h"
#include "simulated_capabilities.h"

namespace {

using namespace HostOperationService;
using namespace HostProtocol;

OperationRequest makeRequest(
    OperationCategory category,
    OperationCode operation,
    uint8_t deviceId = 1
) {
    OperationRequest request = {};
    request.category = category;
    request.operation = operation;
    request.targetDeviceId = deviceId;
    setNoneValue(request.value);
    return request;
}

void assertOk(const Result& result) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Disposition::HANDLED),
        static_cast<uint8_t>(result.disposition));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ResultClass::OPERATION_RESULT),
        static_cast<uint8_t>(result.response.resultClass));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(
        DeviceCapabilities::OperationStatus::OK), result.response.resultCode);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PayloadResult::OK),
        static_cast<uint8_t>(validateOperationResponse(result.response)));
}

void roundTripResponse(uint16_t requestId, const OperationResponse& response) {
    Frame frame = {};
    frame.major = VERSION_MAJOR;
    frame.minor = VERSION_MINOR;
    frame.messageType = MessageType::OPERATION_RESPONSE;
    frame.requestId = requestId;
    size_t payloadLength = 0;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PayloadResult::OK),
        static_cast<uint8_t>(encodeOperationResponse(response, frame.payload,
            sizeof(frame.payload), payloadLength)));
    frame.payloadLength = static_cast<uint16_t>(payloadLength);
    uint8_t encoded[MAX_ENCODED_FRAME_SIZE] = {};
    size_t encodedLength = 0;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EncodeResult::OK),
        static_cast<uint8_t>(encodeFrame(frame, encoded, sizeof(encoded),
            encodedLength)));
    Frame decodedFrame = {};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DecodeResult::OK),
        static_cast<uint8_t>(decodeFrame(encoded, encodedLength, decodedFrame)));
    TEST_ASSERT_EQUAL_HEX16(requestId, decodedFrame.requestId);
    OperationResponse decoded = {};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PayloadResult::OK),
        static_cast<uint8_t>(decodeOperationResponse(decodedFrame.payload,
            decodedFrame.payloadLength, decoded)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(response.operation),
        static_cast<uint8_t>(decoded.operation));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(response.value.bytes, decoded.value.bytes,
        response.value.length);
}

void testVersionSchemaProfileAndRoleAuthorities() {
    TEST_ASSERT_EQUAL_STRING("v0.6.0", RedlineVersion::FIRMWARE);
    TEST_ASSERT_EQUAL_UINT8(0, RedlineVersion::FIRMWARE_MAJOR);
    TEST_ASSERT_EQUAL_UINT8(6, RedlineVersion::FIRMWARE_MINOR);
    TEST_ASSERT_EQUAL_UINT8(0, RedlineVersion::FIRMWARE_PATCH);
    TEST_ASSERT_EQUAL_UINT8(Protocol::VERSION, RedlineVersion::WIRE_PROTOCOL);
    TEST_ASSERT_EQUAL_UINT16(1, DeviceSettings::SCHEMA_VERSION);
    TEST_ASSERT_EQUAL_HEX8(0x01,
        static_cast<uint8_t>(HardwareProfile::HELTEC_V4));
    DeviceRole role;
    TEST_ASSERT_TRUE(mapRole(RuntimeState::DeviceRole::HUB, role));
    TEST_ASSERT_EQUAL_HEX8(0x01, static_cast<uint8_t>(role));
    TEST_ASSERT_TRUE(mapRole(RuntimeState::DeviceRole::NODE, role));
    TEST_ASSERT_EQUAL_HEX8(0x02, static_cast<uint8_t>(role));
}

void testSnapshotIsCopiedAndBounded() {
    RuntimeState::State state(RuntimeState::DeviceRole::HUB, 1, 16);
    state.setReady(true);
    state.incrementRetransmissions(7);
    DeviceSnapshot snapshot = makeDeviceSnapshot(state, 99);
    state.setReady(false);
    state.incrementRetransmissions(3);
    TEST_ASSERT_TRUE(snapshot.ready);
    TEST_ASSERT_EQUAL_UINT32(7, snapshot.counters.retransmissions);
    TEST_ASSERT_EQUAL_UINT32(99, snapshot.uptimeSeconds);
}

void testPingValuesTargetsAndMalformedInputs() {
    RuntimeState::State hub(RuntimeState::DeviceRole::HUB, 1, 16);
    DeviceSnapshot snapshot = makeDeviceSnapshot(hub, 0);
    OperationRequest ping = makeRequest(OperationCategory::DEVICE,
        OperationCode::PING);
    Result result = handleLocalDeviceOrDiagnostic(0x1234, ping, snapshot);
    assertOk(result);
    TEST_ASSERT_EQUAL_UINT32(0, readUint32Le(result.response.value.bytes));
    snapshot.uptimeSeconds = UINT32_MAX;
    result = handleLocalDeviceOrDiagnostic(0x1234, ping, snapshot);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX,
        readUint32Le(result.response.value.bytes));
    roundTripResponse(0x1234, result.response);

    ping.targetDeviceId = 2;
    result = handleLocalDeviceOrDiagnostic(1, ping, snapshot);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ResultClass::REQUEST_REJECTED),
        static_cast<uint8_t>(result.response.resultClass));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(
        RequestRejectionCode::BAD_TARGET), result.response.resultCode);
    ping.targetDeviceId = 1;
    ping.targetId = 1;
    result = handleLocalDeviceOrDiagnostic(1, ping, snapshot);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Disposition::REJECTED),
        static_cast<uint8_t>(result.disposition));
    ping.targetId = 0;
    setBooleanValue(ping.value, 1);
    result = handleLocalDeviceOrDiagnostic(1, ping, snapshot);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Disposition::REJECTED),
        static_cast<uint8_t>(result.disposition));
}

void testDeviceInfoExactForHubAndNode() {
    RuntimeState::State hub(RuntimeState::DeviceRole::HUB, 1, 16);
    OperationRequest request = makeRequest(OperationCategory::DEVICE,
        OperationCode::GET_DEVICE_INFO);
    Result result = handleLocalDeviceOrDiagnostic(7, request,
        makeDeviceSnapshot(hub, 5));
    assertOk(result);
    TEST_ASSERT_EQUAL_UINT8(DEVICE_INFO_SIZE, result.response.value.length);
    const uint8_t expected[] = {0, 6, 0, 1, 1, 1, 1, 1};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, result.response.value.bytes, 8);
    DeviceInfoRecord info = {};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PayloadResult::OK),
        static_cast<uint8_t>(decodeDeviceInfoRecord(result.response.value, info)));
    roundTripResponse(7, result.response);

    RuntimeState::State node(RuntimeState::DeviceRole::NODE, 16, 1);
    request.targetDeviceId = 16;
    result = handleLocalDeviceOrDiagnostic(8, request,
        makeDeviceSnapshot(node, 5));
    decodeDeviceInfoRecord(result.response.value, info);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DeviceRole::NODE),
        static_cast<uint8_t>(info.role));
    TEST_ASSERT_EQUAL_UINT8(16, info.deviceId);
}

void testStatusPhaseHealthAndCombinedFlags() {
    RuntimeState::State state(RuntimeState::DeviceRole::HUB, 1, 16);
    OperationRequest request = makeRequest(OperationCategory::DEVICE,
        OperationCode::GET_STATUS);
    const RuntimeState::RuntimePhase phases[] = {
        RuntimeState::RuntimePhase::IDLE,
        RuntimeState::RuntimePhase::LISTENING,
        RuntimeState::RuntimePhase::TRANSMITTING,
        RuntimeState::RuntimePhase::WAITING_FOR_ACK,
        RuntimeState::RuntimePhase::TRANSMITTING_ACK,
        RuntimeState::RuntimePhase::WAITING_FOR_RESPONSE,
        RuntimeState::RuntimePhase::TRANSMITTING_RESPONSE
    };
    for (uint8_t index = 0; index < 7; ++index) {
        state.setPhase(phases[index]);
        Result result = handleLocalDeviceOrDiagnostic(1, request,
            makeDeviceSnapshot(state, 0));
        StatusRecord status = {};
        decodeStatusRecord(result.response.value, status);
        TEST_ASSERT_EQUAL(index >= 2,
            (status.statusFlags & STATUS_TRANSACTION_ACTIVE) != 0);
    }
    state.setReady(true);
    state.setHealth(RuntimeState::Health::DEGRADED);
    state.setPhase(RuntimeState::RuntimePhase::WAITING_FOR_ACK);
    Result result = handleLocalDeviceOrDiagnostic(2, request,
        makeDeviceSnapshot(state, 0x12345678));
    StatusRecord status = {};
    decodeStatusRecord(result.response.value, status);
    TEST_ASSERT_EQUAL_HEX16(STATUS_READY | STATUS_RADIO_OPERATIONAL |
        STATUS_TRANSACTION_ACTIVE | STATUS_DEGRADED, status.statusFlags);
    TEST_ASSERT_EQUAL_HEX32(0x12345678, status.uptimeSeconds);
    TEST_ASSERT_EQUAL_HEX16(0, status.statusFlags & RESERVED_STATUS_FLAGS);
    state.setHealth(RuntimeState::Health::ERROR);
    result = handleLocalDeviceOrDiagnostic(2, request,
        makeDeviceSnapshot(state, 0));
    decodeStatusRecord(result.response.value, status);
    TEST_ASSERT_BITS_HIGH(STATUS_ERROR, status.statusFlags);
    TEST_ASSERT_BITS_LOW(STATUS_DEGRADED, status.statusFlags);
    roundTripResponse(2, result.response);
}

void testStatusCounterSaturationBoundaries() {
    TEST_ASSERT_EQUAL_UINT16(0, saturateUint16(0));
    TEST_ASSERT_EQUAL_UINT16(1, saturateUint16(1));
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, saturateUint16(0xFFFF));
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, saturateUint16(0x10000));
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, saturateUint16(UINT32_MAX));
    RuntimeState::State state(RuntimeState::DeviceRole::HUB, 1, 16);
    state.incrementRetransmissions(0x10000);
    state.incrementAcknowledgmentTimeouts(UINT32_MAX);
    Result result = handleLocalDeviceOrDiagnostic(1,
        makeRequest(OperationCategory::DEVICE, OperationCode::GET_STATUS),
        makeDeviceSnapshot(state, 0));
    StatusRecord status = {};
    decodeStatusRecord(result.response.value, status);
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, status.retryCount);
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, status.timeoutCount);
}

void populateCounters(RuntimeState::State& state) {
    state.incrementTransmissionsCompleted(1);
    state.incrementDecodedPacketsReceived(2);
    state.incrementSuccessfulTransactions(3);
    state.incrementAcceptedCommands(4);
    state.incrementRetransmissions(5);
    state.incrementAcknowledgmentTimeouts(6);
    state.incrementDuplicates(7);
    state.incrementMalformedPackets(8);
    state.incrementIgnoredPackets(9);
    state.incrementRadioErrors(UINT32_MAX);
}

void assertDiagnosticPage(
    const DeviceSnapshot& snapshot,
    uint32_t cursor,
    uint8_t expectedCount,
    uint8_t expectedNext
) {
    OperationRequest request = makeRequest(OperationCategory::DIAGNOSTIC,
        OperationCode::GET_DIAGNOSTICS);
    setUint32Value(request.value, DeviceCapabilities::ValueType::UNSIGNED_32,
        cursor);
    Result result = handleLocalDeviceOrDiagnostic(1, request, snapshot);
    assertOk(result);
    DiagnosticPageRecord page = {};
    decodeDiagnosticPageRecord(result.response.value, page);
    TEST_ASSERT_EQUAL_UINT8(expectedCount, page.count);
    TEST_ASSERT_EQUAL_UINT8(expectedNext, page.nextCursor);
    for (uint8_t index = 0; index < page.count; ++index) {
        TEST_ASSERT_EQUAL_UINT8(cursor + index + 1,
            page.entries[index].metricId);
        TEST_ASSERT_EQUAL_UINT32(diagnosticMetricValue(snapshot.counters,
            static_cast<uint8_t>(cursor + index)), page.entries[index].value);
    }
}

void testDiagnosticRegistryPagesAndTerminal() {
    RuntimeState::State state(RuntimeState::DeviceRole::HUB, 1, 16);
    populateCounters(state);
    DeviceSnapshot snapshot = makeDeviceSnapshot(state, 0);
    OperationRequest first = makeRequest(OperationCategory::DIAGNOSTIC,
        OperationCode::GET_DIAGNOSTICS);
    Result result = handleLocalDeviceOrDiagnostic(1, first, snapshot);
    DiagnosticPageRecord page = {};
    decodeDiagnosticPageRecord(result.response.value, page);
    TEST_ASSERT_EQUAL_UINT8(3, page.count);
    TEST_ASSERT_EQUAL_UINT8(3, page.nextCursor);
    TEST_ASSERT_EQUAL_UINT8(1, page.entries[0].metricId);
    assertDiagnosticPage(snapshot, 0, 3, 3);
    assertDiagnosticPage(snapshot, 3, 3, 6);
    assertDiagnosticPage(snapshot, 6, 3, 9);
    assertDiagnosticPage(snapshot, 9, 1, DIAGNOSTIC_PAGE_END);
    assertDiagnosticPage(snapshot, 10, 0, DIAGNOSTIC_PAGE_END);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX,
        diagnosticMetricValue(snapshot.counters, 9));
    roundTripResponse(1, result.response);
}

void testDiagnosticBadCursorAndNoMutation() {
    RuntimeState::State state(RuntimeState::DeviceRole::HUB, 1, 16);
    populateCounters(state);
    const RuntimeState::DiagnosticCounters before = state.counters();
    OperationRequest request = makeRequest(OperationCategory::DIAGNOSTIC,
        OperationCode::GET_DIAGNOSTICS);
    setUint32Value(request.value, DeviceCapabilities::ValueType::UNSIGNED_32,
        11);
    Result result = handleLocalDeviceOrDiagnostic(1, request,
        makeDeviceSnapshot(state, 0));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(
        DeviceCapabilities::OperationStatus::VALUE_OUT_OF_RANGE),
        result.response.resultCode);
    TEST_ASSERT_TRUE(isNoneValue(result.response.value));
    setBooleanValue(request.value, 1);
    result = handleLocalDeviceOrDiagnostic(1, request,
        makeDeviceSnapshot(state, 0));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Disposition::REJECTED),
        static_cast<uint8_t>(result.disposition));
    TEST_ASSERT_EQUAL_UINT32(before.radioErrors, state.counters().radioErrors);
    TEST_ASSERT_EQUAL_UINT32(before.retransmissions,
        state.counters().retransmissions);
}

void testHelloResponseAndErrors() {
    RuntimeState::State hub(RuntimeState::DeviceRole::HUB, 1, 16);
    HelloRequest request = {1, 1};
    HelloResult result = handleHello(0x1234, VERSION_MAJOR, request,
        makeDeviceSnapshot(hub, 0));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HelloDisposition::RESPONSE),
        static_cast<uint8_t>(result.disposition));
    TEST_ASSERT_EQUAL_HEX16(CATEGORY_DEVICE_BIT | CATEGORY_CAPABILITY_BIT |
        CATEGORY_DIAGNOSTIC_BIT, result.response.operationCategoryBitmap);
    TEST_ASSERT_EQUAL_HEX16(FEATURE_LOCAL_OPERATIONS,
        result.response.featureBitmap);
    uint8_t payload[MAX_PAYLOAD_SIZE] = {};
    size_t length = 0;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PayloadResult::OK),
        static_cast<uint8_t>(encodeHelloResponse(result.response, payload,
            sizeof(payload), length)));
    const uint8_t expected[] = {
        1, 0, 6, 0, 1, 1, 1, 1, 1, 128, 0x0B, 0, 1, 0, 1, 0
    };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, payload, sizeof(expected));
    Frame frame = {};
    frame.major = VERSION_MAJOR; frame.minor = VERSION_MINOR;
    frame.messageType = MessageType::HELLO_RESPONSE;
    frame.requestId = 0x1234; frame.payloadLength = length;
    for (size_t index = 0; index < length; ++index) frame.payload[index] = payload[index];
    uint8_t encoded[MAX_ENCODED_FRAME_SIZE] = {}; size_t encodedLength = 0;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EncodeResult::OK),
        static_cast<uint8_t>(encodeFrame(frame, encoded, sizeof(encoded), encodedLength)));
    Frame decoded = {};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DecodeResult::OK),
        static_cast<uint8_t>(decodeFrame(encoded, encodedLength, decoded)));
    HelloResponse decodedHello = {};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PayloadResult::OK),
        static_cast<uint8_t>(decodeHelloResponse(decoded.payload,
            decoded.payloadLength, decodedHello)));

    result = handleHello(1, 1, request, makeDeviceSnapshot(hub, 0));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(
        ProtocolErrorCode::UNSUPPORTED_MAJOR),
        static_cast<uint8_t>(result.error.errorCode));
    request = {0, 0};
    result = handleHello(1, VERSION_MAJOR, request,
        makeDeviceSnapshot(hub, 0));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(
        ProtocolErrorCode::UNSUPPORTED_MINOR),
        static_cast<uint8_t>(result.error.errorCode));
    TEST_ASSERT_TRUE(isValidProtocolError(result.error));
}

bool simulatedAvailability(
    const void* context,
    DeviceCapabilities::CapabilityId id,
    bool& available
) {
    return SimulatedCapabilities::capabilityAvailability(
        *static_cast<const SimulatedCapabilities::State*>(context), id,
        available);
}

void testCompositionPreservesCapabilityAndProcedureDisposition() {
    RuntimeState::State runtime(RuntimeState::DeviceRole::HUB, 1, 16);
    DeviceSnapshot snapshot = makeDeviceSnapshot(runtime, 44);
    SimulatedCapabilities::State capabilityState;
    SimulatedCapabilities::Handler handler(capabilityState);
    DeviceCapabilities::CapabilityDiagnostics diagnostics;
    AvailabilityProvider availability = makeAvailabilityProvider(
        &capabilityState, simulatedAvailability);
    OperationRequest capability = makeRequest(OperationCategory::CAPABILITY,
        OperationCode::GET_CAPABILITIES);
    Result result = handleLocalOperation(1, capability, snapshot, true,
        SimulatedCapabilities::registryView(), handler,
        DeviceCapabilities::InterlockState::CLEAR, diagnostics, runtime,
        availability);
    assertOk(result);
    OperationRequest device = makeRequest(OperationCategory::DEVICE,
        OperationCode::PING);
    result = handleLocalOperation(2, device, snapshot, true,
        SimulatedCapabilities::registryView(), handler,
        DeviceCapabilities::InterlockState::CLEAR, diagnostics, runtime,
        availability);
    assertOk(result);
    OperationRequest procedure = makeRequest(OperationCategory::PROCEDURE,
        OperationCode::RUN_PROCEDURE);
    procedure.targetId = 1;
    result = handleLocalOperation(3, procedure, snapshot, true,
        SimulatedCapabilities::registryView(), handler,
        DeviceCapabilities::InterlockState::CLEAR, diagnostics, runtime,
        availability);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Disposition::NOT_HANDLED),
        static_cast<uint8_t>(result.disposition));
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(testVersionSchemaProfileAndRoleAuthorities);
    RUN_TEST(testSnapshotIsCopiedAndBounded);
    RUN_TEST(testPingValuesTargetsAndMalformedInputs);
    RUN_TEST(testDeviceInfoExactForHubAndNode);
    RUN_TEST(testStatusPhaseHealthAndCombinedFlags);
    RUN_TEST(testStatusCounterSaturationBoundaries);
    RUN_TEST(testDiagnosticRegistryPagesAndTerminal);
    RUN_TEST(testDiagnosticBadCursorAndNoMutation);
    RUN_TEST(testHelloResponseAndErrors);
    RUN_TEST(testCompositionPreservesCapabilityAndProcedureDisposition);
    return UNITY_END();
}
