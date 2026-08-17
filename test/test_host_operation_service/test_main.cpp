#include <unity.h>

#include "heltec_v4_capabilities.h"
#include "host_operation_service.h"
#include "simulated_capabilities.h"

namespace {

using namespace DeviceCapabilities;
using namespace HostOperationService;
using namespace HostProtocol;

OperationRequest request(
    OperationCode operation,
    uint8_t deviceId = 1,
    uint16_t targetId = 0
) {
    OperationRequest value = {};
    value.category = OperationCategory::CAPABILITY;
    value.operation = operation;
    value.targetDeviceId = deviceId;
    value.targetId = targetId;
    setNoneValue(value.value);
    return value;
}

bool heltecAvailability(
    const void* context, CapabilityId id, bool& available
) {
    return HeltecV4Capabilities::capabilityAvailability(
        *static_cast<const HeltecV4Capabilities::ProfileState*>(context),
        id, available);
}

bool simulatedAvailability(
    const void* context, CapabilityId id, bool& available
) {
    return SimulatedCapabilities::capabilityAvailability(
        *static_cast<const SimulatedCapabilities::State*>(context),
        id, available);
}

struct Fixture {
    SimulatedCapabilities::State state;
    SimulatedCapabilities::Handler handler;
    CapabilityDiagnostics diagnostics;
    RuntimeState::State runtime;
    AvailabilityProvider availability;

    Fixture() : handler(state),
        runtime(RuntimeState::DeviceRole::HUB, 1, 16),
        availability(makeAvailabilityProvider(&state, simulatedAvailability)) {}

    Result run(const OperationRequest& operation, bool valid = true) {
        return handleLocalCapability(0x1234, operation, 1, valid,
            SimulatedCapabilities::registryView(), handler,
            InterlockState::CLEAR, diagnostics, runtime, availability);
    }
};

void assertOperationStatus(const Result& result, OperationStatus status) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Disposition::HANDLED),
        static_cast<uint8_t>(result.disposition));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ResultClass::OPERATION_RESULT),
        static_cast<uint8_t>(result.response.resultClass));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(status),
        result.response.resultCode);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PayloadResult::OK),
        static_cast<uint8_t>(validateOperationResponse(result.response)));
}

void testHostLocalAuthorityAndPolicy() {
    TEST_ASSERT_EQUAL_HEX8(0x01, static_cast<uint8_t>(CallerClass::FIRMWARE_LOCAL));
    TEST_ASSERT_EQUAL_HEX8(0x02, static_cast<uint8_t>(CallerClass::UI_LOCAL));
    TEST_ASSERT_EQUAL_HEX8(0x03, static_cast<uint8_t>(CallerClass::TEST));
    TEST_ASSERT_EQUAL_HEX8(0x04, static_cast<uint8_t>(CallerClass::FUTURE_REMOTE));
    TEST_ASSERT_EQUAL_HEX8(0x05, static_cast<uint8_t>(CallerClass::HOST_LOCAL));
    CallerContext caller = {};
    caller.callerClass = CallerClass::HOST_LOCAL;
    TEST_ASSERT_TRUE(isValidCallerContext(caller));
    CapabilityDescriptor descriptor = SimulatedCapabilities::DESCRIPTORS[0];
    TEST_ASSERT_TRUE(isOperationAuthorized(descriptor, Operation::SET, caller));
    caller.callerClass = CallerClass::FUTURE_REMOTE;
    TEST_ASSERT_FALSE(isOperationAuthorized(descriptor, Operation::SET, caller));
    caller.callerClass = static_cast<CallerClass>(0xFE);
    TEST_ASSERT_FALSE(isValidCallerContext(caller));
    TEST_ASSERT_FALSE(isOperationAuthorized(descriptor, Operation::SET, caller));
    caller.callerClass = CallerClass::HOST_LOCAL;
    caller.reserved[1] = 1;
    TEST_ASSERT_FALSE(isValidCallerContext(caller));
}

void testDispositionTargetAndMalformedBoundaries() {
    Fixture fixture;
    OperationRequest device = request(OperationCode::PING);
    device.category = OperationCategory::DEVICE;
    Result result = fixture.run(device);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Disposition::NOT_HANDLED),
        static_cast<uint8_t>(result.disposition));

    OperationRequest wrong = request(OperationCode::READ_CAPABILITY, 2,
        SimulatedCapabilities::APPLICATION_INDICATOR_ID);
    result = fixture.run(wrong);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ResultClass::REQUEST_REJECTED),
        static_cast<uint8_t>(result.response.resultClass));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RequestRejectionCode::BAD_TARGET),
        result.response.resultCode);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PayloadResult::OK),
        static_cast<uint8_t>(validateOperationResponse(result.response)));

    OperationRequest malformed = wrong;
    malformed.targetDeviceId = 1;
    malformed.category = static_cast<OperationCategory>(0xFE);
    result = fixture.run(malformed);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Disposition::REJECTED),
        static_cast<uint8_t>(result.disposition));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RequestRejectionCode::MALFORMED_REQUEST),
        static_cast<uint8_t>(result.rejectionCode));
    TEST_ASSERT_EQUAL_UINT32(0, fixture.diagnostics.snapshot().counters.lookupAttempts);
}

void testEnumerationPaginationTerminalAndBounds() {
    CapabilityDescriptor descriptors[MAX_CAPABILITIES] = {};
    for (uint8_t index = 0; index < MAX_CAPABILITIES; ++index) {
        descriptors[index] = SimulatedCapabilities::DESCRIPTORS[1];
        descriptors[index].id = static_cast<uint16_t>(index + 1);
    }
    CapabilityRegistryView registry = {descriptors, MAX_CAPABILITIES};
    SimulatedCapabilities::State state;
    SimulatedCapabilities::Handler handler(state);
    CapabilityDiagnostics diagnostics;
    RuntimeState::State runtime(RuntimeState::DeviceRole::HUB, 1, 16);
    AvailabilityProvider availability = makeAvailabilityProvider(
        &state, simulatedAvailability);
    OperationRequest operation = request(OperationCode::GET_CAPABILITIES);
    Result result = handleLocalCapability(7, operation, 1, true, registry,
        handler, InterlockState::CLEAR, diagnostics, runtime, availability);
    assertOperationStatus(result, OperationStatus::OK);
    CapabilityPageRecord page = {};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PayloadResult::OK),
        static_cast<uint8_t>(decodeCapabilityPageRecord(result.response.value, page)));
    TEST_ASSERT_EQUAL_UINT8(9, page.count);
    TEST_ASSERT_EQUAL_UINT16(9, page.nextCursor);
    TEST_ASSERT_EQUAL_UINT16(1, page.capabilityIds[0]);
    TEST_ASSERT_EQUAL_UINT16(9, page.capabilityIds[8]);

    setUint32Value(operation.value, ValueType::UNSIGNED_32, 9);
    result = handleLocalCapability(7, operation, 1, true, registry, handler,
        InterlockState::CLEAR, diagnostics, runtime, availability);
    decodeCapabilityPageRecord(result.response.value, page);
    TEST_ASSERT_EQUAL_UINT8(7, page.count);
    TEST_ASSERT_EQUAL_HEX16(CAPABILITY_PAGE_END, page.nextCursor);

    setUint32Value(operation.value, ValueType::UNSIGNED_32, MAX_CAPABILITIES);
    result = handleLocalCapability(7, operation, 1, true, registry, handler,
        InterlockState::CLEAR, diagnostics, runtime, availability);
    decodeCapabilityPageRecord(result.response.value, page);
    TEST_ASSERT_EQUAL_UINT8(0, page.count);
    TEST_ASSERT_EQUAL_HEX16(CAPABILITY_PAGE_END, page.nextCursor);

    setUint32Value(operation.value, ValueType::UNSIGNED_32, MAX_CAPABILITIES + 1U);
    result = handleLocalCapability(7, operation, 1, true, registry, handler,
        InterlockState::CLEAR, diagnostics, runtime, availability);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(OperationStatus::VALUE_OUT_OF_RANGE),
        result.response.resultCode);
    TEST_ASSERT_EQUAL_UINT32(0, diagnostics.snapshot().counters.lookupAttempts);
}

void testEnumerationInvalidRegistryDoesNotExecute() {
    Fixture fixture;
    Result result = fixture.run(request(OperationCode::GET_CAPABILITIES), false);
    assertOperationStatus(result, OperationStatus::INVALID_DESCRIPTOR);
    TEST_ASSERT_EQUAL_UINT32(0, fixture.diagnostics.snapshot().counters.lookupAttempts);
}

void testHeltecDescriptionsArePublicAndTruthful() {
    HeltecV4Capabilities::ProfileState state;
    HeltecV4Capabilities::HeltecV4CapabilityHandler handler(state);
    CapabilityDiagnostics diagnostics;
    RuntimeState::State runtime(RuntimeState::DeviceRole::HUB, 1, 16);
    AvailabilityProvider availability = makeAvailabilityProvider(&state,
        heltecAvailability);
    const CapabilityId ids[] = {
        HeltecV4Capabilities::APPLICATION_INDICATOR_ID,
        HeltecV4Capabilities::DIGITAL_INPUT_ID,
        HeltecV4Capabilities::ANALOG_INPUT_0_ID
    };
    for (uint8_t index = 0; index < 3; ++index) {
        OperationRequest operation = request(
            OperationCode::DESCRIBE_CAPABILITY, 1, ids[index]);
        Result result = handleLocalCapability(1, operation, 1, true,
            HeltecV4Capabilities::registryView(), handler,
            InterlockState::CLEAR, diagnostics, runtime, availability);
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(OperationStatus::OK),
            result.response.resultCode);
        CapabilityDescriptionRecord description = {};
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PayloadResult::OK),
            static_cast<uint8_t>(decodeCapabilityDescriptionRecord(
                result.response.value, description)));
        TEST_ASSERT_EQUAL_UINT8(index == 0 ? 1 : 0, description.availability);
        TEST_ASSERT_EQUAL_UINT8(0, description.reserved);
        TEST_ASSERT_EQUAL_UINT8(
            static_cast<uint8_t>(HeltecV4Capabilities::DESCRIPTORS[index].capabilityClass),
            static_cast<uint8_t>(description.capabilityClass));
        TEST_ASSERT_EQUAL_UINT8(HeltecV4Capabilities::DESCRIPTORS[index].operationFlags,
            description.operationFlags);
    }
    state.digitalInputAvailable = true;
    OperationRequest digital = request(OperationCode::DESCRIBE_CAPABILITY, 1,
        HeltecV4Capabilities::DIGITAL_INPUT_ID);
    Result result = handleLocalCapability(1, digital, 1, true,
        HeltecV4Capabilities::registryView(), handler, InterlockState::CLEAR,
        diagnostics, runtime, availability);
    CapabilityDescriptionRecord description = {};
    decodeCapabilityDescriptionRecord(result.response.value, description);
    TEST_ASSERT_EQUAL_UINT8(1, description.availability);
    TEST_ASSERT_EQUAL_UINT32(0, diagnostics.snapshot().counters.lookupAttempts);
}

void testDescribeUnknownAndSimulatedAvailable() {
    Fixture fixture;
    Result result = fixture.run(request(OperationCode::DESCRIBE_CAPABILITY, 1, 0x7777));
    assertOperationStatus(result, OperationStatus::CAPABILITY_NOT_FOUND);
    result = fixture.run(request(OperationCode::DESCRIBE_CAPABILITY, 1,
        SimulatedCapabilities::ANALOG_INPUT_0_ID));
    CapabilityDescriptionRecord description = {};
    decodeCapabilityDescriptionRecord(result.response.value, description);
    TEST_ASSERT_EQUAL_UINT8(1, description.availability);
}

void testHeltecReadAvailabilityAndExactValues() {
    HeltecV4Capabilities::ProfileState state;
    HeltecV4Capabilities::HeltecV4CapabilityHandler handler(state);
    CapabilityDiagnostics diagnostics;
    RuntimeState::State runtime(RuntimeState::DeviceRole::HUB, 1, 16);
    AvailabilityProvider availability = makeAvailabilityProvider(&state,
        heltecAvailability);
    auto run = [&](CapabilityId id) {
        return handleLocalCapability(1,
            request(OperationCode::READ_CAPABILITY, 1, id), 1, true,
            HeltecV4Capabilities::registryView(), handler,
            InterlockState::CLEAR, diagnostics, runtime, availability);
    };
    Result result = run(HeltecV4Capabilities::APPLICATION_INDICATOR_ID);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(OperationStatus::OK), result.response.resultCode);
    TEST_ASSERT_EQUAL_UINT8(0, result.response.value.bytes[0]);
    result = run(HeltecV4Capabilities::DIGITAL_INPUT_ID);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(OperationStatus::HARDWARE_UNAVAILABLE), result.response.resultCode);
    state.digitalInputAvailable = true;
    result = run(HeltecV4Capabilities::DIGITAL_INPUT_ID);
    TEST_ASSERT_EQUAL_UINT8(0, result.response.value.bytes[0]);
    state.digitalInputActive = true;
    result = run(HeltecV4Capabilities::DIGITAL_INPUT_ID);
    TEST_ASSERT_EQUAL_UINT8(1, result.response.value.bytes[0]);
    result = run(HeltecV4Capabilities::ANALOG_INPUT_0_ID);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(OperationStatus::HARDWARE_UNAVAILABLE), result.response.resultCode);
    TEST_ASSERT_FALSE(state.analogInputAvailable);
}

void testSetIndicatorUsesHostLocalAndInterlock() {
    HeltecV4Capabilities::ProfileState state;
    HeltecV4Capabilities::HeltecV4CapabilityHandler handler(state);
    CapabilityDiagnostics diagnostics;
    RuntimeState::State runtime(RuntimeState::DeviceRole::HUB, 1, 16);
    AvailabilityProvider availability = makeAvailabilityProvider(&state,
        heltecAvailability);
    OperationRequest operation = request(OperationCode::SET_INDICATOR, 1,
        HeltecV4Capabilities::APPLICATION_INDICATOR_ID);
    setBooleanValue(operation.value, 1);
    Result result = handleLocalCapability(1, operation, 1, true,
        HeltecV4Capabilities::registryView(), handler, InterlockState::CLEAR,
        diagnostics, runtime, availability);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(OperationStatus::OK), result.response.resultCode);
    TEST_ASSERT_TRUE(state.indicatorRequested);
    TEST_ASSERT_TRUE(isNoneValue(result.response.value));
    setBooleanValue(operation.value, 0);
    result = handleLocalCapability(1, operation, 1, true,
        HeltecV4Capabilities::registryView(), handler, InterlockState::ACTIVE,
        diagnostics, runtime, availability);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(OperationStatus::INTERLOCK_ACTIVE), result.response.resultCode);
    TEST_ASSERT_TRUE(state.indicatorRequested);
}

void testReadSetFailuresAndDiagnosticsAreExact() {
    Fixture fixture;
    Result result = fixture.run(request(OperationCode::READ_CAPABILITY, 1, 0x7777));
    assertOperationStatus(result, OperationStatus::CAPABILITY_NOT_FOUND);
    OperationRequest unsupported = request(OperationCode::SET_INDICATOR, 1,
        SimulatedCapabilities::DIGITAL_INPUT_ID);
    setBooleanValue(unsupported.value, 1);
    result = fixture.run(unsupported);
    assertOperationStatus(result, OperationStatus::UNSUPPORTED_OPERATION);
    OperationRequest set = request(OperationCode::SET_INDICATOR, 1,
        SimulatedCapabilities::APPLICATION_INDICATOR_ID);
    setBooleanValue(set.value, 1);
    result = fixture.run(set);
    assertOperationStatus(result, OperationStatus::OK);
    TEST_ASSERT_TRUE(fixture.state.indicator);
    const CapabilityDiagnosticsSnapshot snapshot = fixture.diagnostics.snapshot();
    TEST_ASSERT_EQUAL_UINT32(3, snapshot.counters.lookupAttempts);
    TEST_ASSERT_EQUAL_UINT32(1, snapshot.counters.acceptedOperations);
    TEST_ASSERT_EQUAL_UINT32(1, snapshot.counters.unsupportedOperations);
    TEST_ASSERT_TRUE(fixture.runtime.hasCapabilityDiagnostics());
    TEST_ASSERT_EQUAL_UINT32(3,
        fixture.runtime.capabilityDiagnostics().counters.lookupAttempts);
}

class CountingHandler : public LocalCapabilityHandler {
public:
    uint8_t calls = 0;
    OperationResult next = makeCanonicalFailureResult(OperationStatus::BUSY);
    OperationResult execute(const CapabilityDescriptor&, Operation,
        const CapabilityValue&) override {
        ++calls;
        return next;
    }
};

void testHandlerRuntimeOutcomesExecuteExactlyOnce() {
    CountingHandler handler;
    SimulatedCapabilities::State availabilityState;
    CapabilityDiagnostics diagnostics;
    RuntimeState::State runtime(RuntimeState::DeviceRole::HUB, 1, 16);
    AvailabilityProvider availability = makeAvailabilityProvider(
        &availabilityState, simulatedAvailability);
    OperationRequest operation = request(OperationCode::READ_CAPABILITY, 1,
        SimulatedCapabilities::APPLICATION_INDICATOR_ID);
    const OperationStatus statuses[] = {OperationStatus::BUSY,
        OperationStatus::HARDWARE_UNAVAILABLE,
        OperationStatus::OPERATION_FAILED};
    for (OperationStatus status : statuses) {
        handler.next = makeCanonicalFailureResult(status);
        Result result = handleLocalCapability(1, operation, 1, true,
            SimulatedCapabilities::registryView(), handler,
            InterlockState::CLEAR, diagnostics, runtime, availability);
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(status), result.response.resultCode);
    }
    TEST_ASSERT_EQUAL_UINT8(3, handler.calls);
}

void testRejectedInputsAndInterlockNeverExecuteHandler() {
    CountingHandler handler;
    SimulatedCapabilities::State availabilityState;
    CapabilityDiagnostics diagnostics;
    RuntimeState::State runtime(RuntimeState::DeviceRole::HUB, 1, 16);
    AvailabilityProvider availability = makeAvailabilityProvider(
        &availabilityState, simulatedAvailability);
    OperationRequest operation = request(OperationCode::SET_INDICATOR, 1,
        SimulatedCapabilities::APPLICATION_INDICATOR_ID);
    setBooleanValue(operation.value, 1);

    Result result = handleLocalCapability(1, operation, 2, true,
        SimulatedCapabilities::registryView(), handler,
        InterlockState::CLEAR, diagnostics, runtime, availability);
    TEST_ASSERT_EQUAL_UINT8(0, handler.calls);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RequestRejectionCode::BAD_TARGET),
        result.response.resultCode);

    operation.targetDeviceId = 2;
    operation.value.type = static_cast<uint8_t>(ValueType::UNSIGNED_32);
    operation.value.length = 4;
    result = handleLocalCapability(1, operation, 2, true,
        SimulatedCapabilities::registryView(), handler,
        InterlockState::CLEAR, diagnostics, runtime, availability);
    TEST_ASSERT_EQUAL_UINT8(0, handler.calls);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Disposition::REJECTED),
        static_cast<uint8_t>(result.disposition));

    setBooleanValue(operation.value, 1);
    result = handleLocalCapability(1, operation, 2, true,
        SimulatedCapabilities::registryView(), handler,
        InterlockState::ACTIVE, diagnostics, runtime, availability);
    TEST_ASSERT_EQUAL_UINT8(0, handler.calls);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(OperationStatus::INTERLOCK_ACTIVE),
        result.response.resultCode);

    operation.targetId = SimulatedCapabilities::DIGITAL_INPUT_ID;
    result = handleLocalCapability(1, operation, 2, true,
        SimulatedCapabilities::registryView(), handler,
        InterlockState::CLEAR, diagnostics, runtime, availability);
    TEST_ASSERT_EQUAL_UINT8(0, handler.calls);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(OperationStatus::UNSUPPORTED_OPERATION),
        result.response.resultCode);

    operation.targetId = 0x7777;
    result = handleLocalCapability(1, operation, 2, true,
        SimulatedCapabilities::registryView(), handler,
        InterlockState::CLEAR, diagnostics, runtime, availability);
    TEST_ASSERT_EQUAL_UINT8(0, handler.calls);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(OperationStatus::CAPABILITY_NOT_FOUND),
        result.response.resultCode);

    operation.targetId = SimulatedCapabilities::APPLICATION_INDICATOR_ID;
    result = handleLocalCapability(1, operation, 2, false,
        SimulatedCapabilities::registryView(), handler,
        InterlockState::CLEAR, diagnostics, runtime, availability);
    TEST_ASSERT_EQUAL_UINT8(0, handler.calls);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(OperationStatus::INVALID_DESCRIPTOR),
        result.response.resultCode);
}

void testTypedValueConversionsPreserveBits() {
    const ValueType types[] = {ValueType::NONE, ValueType::BOOLEAN,
        ValueType::UNSIGNED_32, ValueType::SIGNED_32,
        ValueType::NORMALIZED_U16, ValueType::FIXED_Q16_16,
        ValueType::ENUM_U16};
    for (ValueType type : types) {
        CapabilityValue input = {};
        input.type = type;
        if (type == ValueType::BOOLEAN) input.bits = 1;
        else if (type == ValueType::NORMALIZED_U16 || type == ValueType::ENUM_U16)
            input.bits = 0xABCD;
        else if (type != ValueType::NONE) input.bits = 0x89ABCDEF;
        TypedValue host = {};
        TEST_ASSERT_TRUE(capabilityValueToHostValue(input, host));
        CapabilityValue output = {};
        TEST_ASSERT_TRUE(hostValueToCapabilityValue(host, output));
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(type), static_cast<uint8_t>(output.type));
        TEST_ASSERT_EQUAL_HEX32(input.bits, output.bits);
        TEST_ASSERT_EQUAL_UINT8(0, output.reserved[0]);
        TEST_ASSERT_EQUAL_UINT8(0, output.reserved[1]);
        TEST_ASSERT_EQUAL_UINT8(0, output.reserved[2]);
    }
    TypedValue structure = {};
    structure.type = STRUCTURE_VALUE_TYPE;
    CapabilityValue ignored = {};
    TEST_ASSERT_FALSE(hostValueToCapabilityValue(structure, ignored));
}

void testResponsePayloadAndFrameRoundTripPreserveCorrelation() {
    Fixture fixture;
    fixture.state.indicator = true;
    OperationRequest operation = request(OperationCode::READ_CAPABILITY, 1,
        SimulatedCapabilities::APPLICATION_INDICATOR_ID);
    Result result = fixture.run(operation);
    uint8_t payload[MAX_PAYLOAD_SIZE] = {};
    size_t payloadLength = 0;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PayloadResult::OK),
        static_cast<uint8_t>(encodeOperationResponse(result.response, payload,
            sizeof(payload), payloadLength)));
    Frame frame = {};
    frame.major = VERSION_MAJOR;
    frame.minor = VERSION_MINOR;
    frame.messageType = MessageType::OPERATION_RESPONSE;
    frame.requestId = result.requestId;
    frame.payloadLength = static_cast<uint16_t>(payloadLength);
    for (size_t index = 0; index < payloadLength; ++index) frame.payload[index] = payload[index];
    uint8_t encoded[MAX_ENCODED_FRAME_SIZE] = {};
    size_t encodedLength = 0;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EncodeResult::OK),
        static_cast<uint8_t>(encodeFrame(frame, encoded, sizeof(encoded), encodedLength)));
    Frame decoded = {};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DecodeResult::OK),
        static_cast<uint8_t>(decodeFrame(encoded, encodedLength, decoded)));
    TEST_ASSERT_EQUAL_HEX16(0x1234, decoded.requestId);
    OperationResponse response = {};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PayloadResult::OK),
        static_cast<uint8_t>(decodeOperationResponse(decoded.payload,
            decoded.payloadLength, response)));
    TEST_ASSERT_EQUAL_UINT8(1, response.value.bytes[0]);
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(testHostLocalAuthorityAndPolicy);
    RUN_TEST(testDispositionTargetAndMalformedBoundaries);
    RUN_TEST(testEnumerationPaginationTerminalAndBounds);
    RUN_TEST(testEnumerationInvalidRegistryDoesNotExecute);
    RUN_TEST(testHeltecDescriptionsArePublicAndTruthful);
    RUN_TEST(testDescribeUnknownAndSimulatedAvailable);
    RUN_TEST(testHeltecReadAvailabilityAndExactValues);
    RUN_TEST(testSetIndicatorUsesHostLocalAndInterlock);
    RUN_TEST(testReadSetFailuresAndDiagnosticsAreExact);
    RUN_TEST(testHandlerRuntimeOutcomesExecuteExactlyOnce);
    RUN_TEST(testRejectedInputsAndInterlockNeverExecuteHandler);
    RUN_TEST(testTypedValueConversionsPreserveBits);
    RUN_TEST(testResponsePayloadAndFrameRoundTripPreserveCorrelation);
    return UNITY_END();
}
