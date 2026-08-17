#include <unity.h>

#include <type_traits>

#include <host_protocol_messages.h>

using namespace HostProtocol;

namespace {

uint8_t raw(PayloadResult result) {
    return static_cast<uint8_t>(result);
}

TypedValue noneValue() {
    TypedValue value = {};
    setNoneValue(value);
    return value;
}

OperationRequest makeRequest(
    OperationCategory category,
    OperationCode operation,
    uint16_t targetId
) {
    OperationRequest request = {};
    request.category = category;
    request.operation = operation;
    request.targetDeviceId = 0xA5;
    request.targetId = targetId;
    request.value = noneValue();
    return request;
}

OperationResponse makeResponse(
    OperationCategory category,
    OperationCode operation,
    uint16_t targetId,
    OperationStatus status = OperationStatus::OK
) {
    OperationResponse response = {};
    response.category = category;
    response.operation = operation;
    response.targetDeviceId = 0xA5;
    response.targetId = targetId;
    response.resultClass = ResultClass::OPERATION_RESULT;
    response.resultCode = static_cast<uint8_t>(status);
    response.value = noneValue();
    return response;
}

HelloResponse validHelloResponse() {
    HelloResponse response = {};
    response.selectedMinor = VERSION_MINOR;
    response.firmwareMajor = 0;
    response.firmwareMinor = 6;
    response.firmwarePatch = 0;
    response.wireProtocol = 1;
    response.configurationSchema = 1;
    response.hardwareProfile = HardwareProfile::HELTEC_V4;
    response.role = DeviceRole::HUB;
    response.deviceId = 0x01;
    response.maximumHostPayload = MAX_PAYLOAD_SIZE;
    response.operationCategoryBitmap = CATEGORY_DEVICE_BIT |
        CATEGORY_CAPABILITY_BIT;
    response.featureBitmap = FEATURE_LOCAL_OPERATIONS;
    response.maximumOutstandingOperations = MAX_OUTSTANDING_OPERATIONS;
    response.reserved = 0;
    return response;
}

void testHelloRequestExactCodecAndValidation() {
    const HelloRequest request = {1, 1};
    uint8_t output[2] = {};
    size_t length = 0;
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::OK), raw(encodeHelloRequest(
        request, output, sizeof(output), length)));
    const uint8_t expected[] = {0x01, 0x01};
    TEST_ASSERT_EQUAL_UINT32(2, length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, output, 2);
    HelloRequest decoded = {};
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::OK), raw(decodeHelloRequest(
        output, length, decoded)));
    TEST_ASSERT_EQUAL_UINT8(1, decoded.minimumMinor);
    TEST_ASSERT_EQUAL_UINT8(1, decoded.maximumMinor);

    const uint8_t shortPayload[] = {1};
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::INVALID_LENGTH), raw(
        decodeHelloRequest(shortPayload, sizeof(shortPayload), decoded)));
    const uint8_t noOverlap[] = {0, 0};
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::INVALID_FIELD), raw(
        decodeHelloRequest(noOverlap, sizeof(noOverlap), decoded)));
    const uint8_t malformedRange[] = {2, 1};
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::INVALID_FIELD), raw(
        decodeHelloRequest(malformedRange, sizeof(malformedRange), decoded)));
    const uint8_t broadRange[] = {0, 1};
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::INVALID_FIELD), raw(
        decodeHelloRequest(broadRange, sizeof(broadRange), decoded)));
}

void testHelloResponseExactCodecAndLittleEndianBitmaps() {
    HelloResponse response = validHelloResponse();
    response.operationCategoryBitmap = 0x000B;
    response.featureBitmap = 0x0003;
    uint8_t output[HELLO_RESPONSE_PAYLOAD_SIZE] = {};
    size_t length = 0;
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::OK), raw(encodeHelloResponse(
        response, output, sizeof(output), length)));
    const uint8_t expected[] = {
        0x01, 0x00, 0x06, 0x00, 0x01, 0x01, 0x01, 0x01,
        0x01, 0x80, 0x0B, 0x00, 0x03, 0x00, 0x01, 0x00
    };
    TEST_ASSERT_EQUAL_UINT32(16, length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, output, sizeof(expected));
    HelloResponse decoded = {};
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::OK), raw(decodeHelloResponse(
        output, length, decoded)));
    TEST_ASSERT_EQUAL_HEX16(0x000B, decoded.operationCategoryBitmap);
    TEST_ASSERT_EQUAL_HEX16(0x0003, decoded.featureBitmap);
}

void testHelloResponseRejectsEveryConstrainedField() {
    HelloResponse value = validHelloResponse();
    uint8_t output[16] = {};
    size_t length = 0;
#define ASSERT_BAD_HELLO(field, badValue) do { \
    HelloResponse bad = value; \
    bad.field = badValue; \
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::INVALID_FIELD), \
        raw(encodeHelloResponse(bad, output, sizeof(output), length))); \
} while (0)
    ASSERT_BAD_HELLO(selectedMinor, 0);
    ASSERT_BAD_HELLO(maximumHostPayload, 127);
    ASSERT_BAD_HELLO(maximumOutstandingOperations, 2);
    ASSERT_BAD_HELLO(reserved, 1);
    ASSERT_BAD_HELLO(hardwareProfile, static_cast<HardwareProfile>(0));
    ASSERT_BAD_HELLO(role, static_cast<DeviceRole>(0));
    ASSERT_BAD_HELLO(operationCategoryBitmap, 0x0010);
    ASSERT_BAD_HELLO(featureBitmap, 0x0004);
#undef ASSERT_BAD_HELLO
}

void testOperationClassificationMatrix() {
    const OperationCategory categories[] = {
        OperationCategory::DEVICE, OperationCategory::CAPABILITY,
        OperationCategory::PROCEDURE, OperationCategory::DIAGNOSTIC
    };
    const OperationCode operations[] = {
        OperationCode::PING, OperationCode::GET_DEVICE_INFO,
        OperationCode::GET_STATUS, OperationCode::GET_CAPABILITIES,
        OperationCode::DESCRIBE_CAPABILITY, OperationCode::READ_CAPABILITY,
        OperationCode::SET_INDICATOR, OperationCode::RUN_PROCEDURE,
        OperationCode::GET_DIAGNOSTICS
    };
    for (size_t category = 0; category < 4; ++category) {
        for (size_t operation = 0; operation < 9; ++operation) {
            const bool expected = isSupportedCategoryOperation(
                categories[category], operations[operation]);
            TEST_ASSERT_EQUAL_UINT8(
                static_cast<uint8_t>(expected
                    ? OperationClassification::VALID
                    : OperationClassification::UNSUPPORTED),
                static_cast<uint8_t>(classifyCategoryOperation(
                    static_cast<uint8_t>(categories[category]),
                    static_cast<uint8_t>(operations[operation]))));
        }
    }
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(OperationClassification::MALFORMED),
        static_cast<uint8_t>(classifyCategoryOperation(0, 0x20)));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(OperationClassification::MALFORMED),
        static_cast<uint8_t>(classifyCategoryOperation(1, 0x29)));
}

void testTypedValueExactLengthsBytesAndRoundTrips() {
    TypedValue values[7] = {};
    setNoneValue(values[0]);
    TEST_ASSERT_TRUE(setBooleanValue(values[1], 1));
    TEST_ASSERT_TRUE(setUint32Value(
        values[2], CapabilityValueType::UNSIGNED_32, 0x78563412));
    TEST_ASSERT_TRUE(setUint32Value(
        values[3], CapabilityValueType::SIGNED_32, 0x80000001));
    TEST_ASSERT_TRUE(setUint16Value(
        values[4], CapabilityValueType::NORMALIZED_U16, 0xBEEF));
    TEST_ASSERT_TRUE(setUint32Value(
        values[5], CapabilityValueType::FIXED_Q16_16, 0xFFFF8000));
    TEST_ASSERT_TRUE(setUint16Value(
        values[6], CapabilityValueType::ENUM_U16, 0x1234));
    const uint8_t lengths[] = {0, 1, 4, 4, 2, 4, 2};
    for (size_t index = 0; index < 7; ++index) {
        TEST_ASSERT_TRUE(isValidScalarValue(values[index]));
        TEST_ASSERT_EQUAL_UINT8(lengths[index], values[index].length);
        uint8_t encoded[4] = {};
        size_t encodedLength = 0;
        TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::OK), raw(encodeTypedValue(
            values[index], encoded, sizeof(encoded), encodedLength)));
        TypedValue decoded = {};
        TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::OK), raw(decodeTypedValue(
            values[index].type, encoded, encodedLength, decoded)));
        TEST_ASSERT_EQUAL_UINT8(values[index].type, decoded.type);
        TEST_ASSERT_EQUAL_UINT8(values[index].length, decoded.length);
        if (encodedLength != 0) {
            TEST_ASSERT_EQUAL_UINT8_ARRAY(
                values[index].bytes, decoded.bytes, encodedLength);
        }
    }
    TEST_ASSERT_EQUAL_HEX8(0x12, values[2].bytes[0]);
    TEST_ASSERT_EQUAL_HEX8(0x34, values[2].bytes[1]);
    TEST_ASSERT_EQUAL_HEX8(0x56, values[2].bytes[2]);
    TEST_ASSERT_EQUAL_HEX8(0x78, values[2].bytes[3]);
    TEST_ASSERT_EQUAL_HEX8(0xEF, values[4].bytes[0]);
    TEST_ASSERT_EQUAL_HEX8(0xBE, values[4].bytes[1]);
}

void testTypedValueRejectsMalformedAndStructureInput() {
    TypedValue value = {};
    TEST_ASSERT_TRUE(setBooleanValue(value, 0));
    TEST_ASSERT_TRUE(setBooleanValue(value, 1));
    TEST_ASSERT_FALSE(setBooleanValue(value, 2));
    const uint8_t badBoolean[] = {2};
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::INVALID_VALUE), raw(
        decodeTypedValue(0x01, badBoolean, 1, value)));
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::INVALID_VALUE_LENGTH), raw(
        decodeTypedValue(0x00, badBoolean, 1, value)));
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::INVALID_VALUE_TYPE), raw(
        decodeTypedValue(STRUCTURE_VALUE_TYPE, badBoolean, 1, value)));
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::INVALID_VALUE_TYPE), raw(
        decodeTypedValue(0x07, badBoolean, 1, value)));
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::INVALID_VALUE_LENGTH), raw(
        decodeTypedValue(0x02, badBoolean, 1, value)));
}

void testOperationRequestExactPingAndTargetPreservation() {
    OperationRequest request = makeRequest(
        OperationCategory::DEVICE, OperationCode::PING, 0);
    request.targetDeviceId = 0x01;
    uint8_t payload[MAX_PAYLOAD_SIZE] = {};
    size_t length = 0;
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::OK), raw(encodeOperationRequest(
        request, payload, sizeof(payload), length)));
    const uint8_t expected[] = {0x01, 0x20, 0x01, 0, 0, 0, 0};
    TEST_ASSERT_EQUAL_UINT32(7, length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, payload, 7);
    OperationRequest decoded = {};
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::OK), raw(decodeOperationRequest(
        payload, length, decoded)));
    TEST_ASSERT_EQUAL_UINT8(0x01, decoded.targetDeviceId);
    TEST_ASSERT_EQUAL_UINT16(0, decoded.targetId);

    request = makeRequest(OperationCategory::CAPABILITY,
        OperationCode::READ_CAPABILITY, 0x1234);
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::OK), raw(encodeOperationRequest(
        request, payload, sizeof(payload), length)));
    TEST_ASSERT_EQUAL_HEX8(0x34, payload[3]);
    TEST_ASSERT_EQUAL_HEX8(0x12, payload[4]);
}

void testEveryOperationRequestSchema() {
    OperationRequest requests[] = {
        makeRequest(OperationCategory::DEVICE, OperationCode::PING, 0),
        makeRequest(OperationCategory::DEVICE, OperationCode::GET_DEVICE_INFO, 0),
        makeRequest(OperationCategory::DEVICE, OperationCode::GET_STATUS, 0),
        makeRequest(OperationCategory::CAPABILITY, OperationCode::GET_CAPABILITIES, 0),
        makeRequest(OperationCategory::CAPABILITY, OperationCode::DESCRIBE_CAPABILITY, 1),
        makeRequest(OperationCategory::CAPABILITY, OperationCode::READ_CAPABILITY, 1),
        makeRequest(OperationCategory::CAPABILITY, OperationCode::SET_INDICATOR, 1),
        makeRequest(OperationCategory::PROCEDURE, OperationCode::RUN_PROCEDURE, 1),
        makeRequest(OperationCategory::DIAGNOSTIC, OperationCode::GET_DIAGNOSTICS, 0)
    };
    TEST_ASSERT_TRUE(setBooleanValue(requests[6].value, 1));
    TEST_ASSERT_TRUE(setUint16Value(
        requests[7].value, CapabilityValueType::ENUM_U16, 2));
    TEST_ASSERT_TRUE(setUint32Value(
        requests[8].value, CapabilityValueType::UNSIGNED_32, 3));
    uint8_t payload[MAX_PAYLOAD_SIZE] = {};
    for (size_t index = 0; index < 9; ++index) {
        size_t length = 0;
        TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::OK), raw(
            encodeOperationRequest(requests[index], payload, sizeof(payload), length)));
    }
    TEST_ASSERT_TRUE(setUint32Value(
        requests[3].value, CapabilityValueType::UNSIGNED_32, 0x12345678));
    size_t length = 0;
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::OK), raw(encodeOperationRequest(
        requests[3], payload, sizeof(payload), length)));
}

void testOperationRequestRejectsTargetsValuesAndGeometry() {
    uint8_t payload[MAX_PAYLOAD_SIZE + 1] = {};
    size_t length = 0;
    OperationRequest request = makeRequest(
        OperationCategory::DEVICE, OperationCode::PING, 1);
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::INVALID_TARGET), raw(
        encodeOperationRequest(request, payload, sizeof(payload), length)));
    request = makeRequest(OperationCategory::CAPABILITY,
        OperationCode::READ_CAPABILITY, 0);
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::INVALID_TARGET), raw(
        encodeOperationRequest(request, payload, sizeof(payload), length)));
    request = makeRequest(OperationCategory::DEVICE, OperationCode::PING, 0);
    TEST_ASSERT_TRUE(setBooleanValue(request.value, 1));
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::INVALID_VALUE), raw(
        encodeOperationRequest(request, payload, sizeof(payload), length)));
    request.category = static_cast<OperationCategory>(0);
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::UNKNOWN_CATEGORY), raw(
        validateOperationRequest(request)));
    request = makeRequest(OperationCategory::DEVICE,
        static_cast<OperationCode>(0x29), 0);
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::UNKNOWN_OPERATION), raw(
        validateOperationRequest(request)));
    request = makeRequest(OperationCategory::DEVICE,
        OperationCode::GET_DIAGNOSTICS, 0);
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::UNSUPPORTED_CATEGORY_OPERATION),
        raw(validateOperationRequest(request)));

    const uint8_t truncated[] = {1, 0x20, 1, 0, 0, 0};
    OperationRequest decoded = {};
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::INVALID_LENGTH), raw(
        decodeOperationRequest(truncated, sizeof(truncated), decoded)));
    const uint8_t trailing[] = {1, 0x20, 1, 0, 0, 0, 0, 0};
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::INVALID_LENGTH), raw(
        decodeOperationRequest(trailing, sizeof(trailing), decoded)));
    payload[0] = 1; payload[1] = 0x20; payload[2] = 1;
    payload[5] = 0; payload[6] = 122;
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::INVALID_LENGTH), raw(
        decodeOperationRequest(payload, sizeof(payload), decoded)));
}

void testFixedStructureRecordsRoundTrip() {
    TypedValue value = {};
    DeviceInfoRecord device = {0, 6, 0, 1, 1,
        HardwareProfile::HELTEC_V4, DeviceRole::NODE, 0x10};
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::OK), raw(
        encodeDeviceInfoRecord(device, value)));
    TEST_ASSERT_EQUAL_UINT8(DEVICE_INFO_SIZE, value.length);
    DeviceInfoRecord decodedDevice = {};
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::OK), raw(
        decodeDeviceInfoRecord(value, decodedDevice)));
    TEST_ASSERT_EQUAL_UINT8(0x10, decodedDevice.deviceId);

    StatusRecord status = {STATUS_READY | STATUS_RADIO_OPERATIONAL,
        0x78563412, 0xABCD, 0x1234};
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::OK), raw(
        encodeStatusRecord(status, value)));
    TEST_ASSERT_EQUAL_HEX8(0x12, value.bytes[2]);
    TEST_ASSERT_EQUAL_HEX8(0x78, value.bytes[5]);
    StatusRecord decodedStatus = {};
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::OK), raw(
        decodeStatusRecord(value, decodedStatus)));
    TEST_ASSERT_EQUAL_HEX32(0x78563412, decodedStatus.uptimeSeconds);

    CapabilityDescriptionRecord description = {
        DeviceCapabilities::CapabilityClass::SENSOR,
        CapabilityValueType::NORMALIZED_U16,
        DeviceCapabilities::OPERATION_FLAG_DESCRIBE |
            DeviceCapabilities::OPERATION_FLAG_READ,
        DeviceCapabilities::UnitCode::NORMALIZED, 1, 0};
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::OK), raw(
        encodeCapabilityDescriptionRecord(description, value)));
    CapabilityDescriptionRecord decodedDescription = {};
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::OK), raw(
        decodeCapabilityDescriptionRecord(value, decodedDescription)));
}

void testBoundedPageRecordsRoundTripAndRejectInvalidCounts() {
    CapabilityPageRecord capabilities = {};
    capabilities.nextCursor = CAPABILITY_PAGE_END;
    capabilities.count = 2;
    capabilities.capabilityIds[0] = 0x0102;
    capabilities.capabilityIds[1] = 0x0304;
    TypedValue value = {};
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::OK), raw(
        encodeCapabilityPageRecord(capabilities, value)));
    TEST_ASSERT_EQUAL_UINT8(7, value.length);
    CapabilityPageRecord decodedCapabilities = {};
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::OK), raw(
        decodeCapabilityPageRecord(value, decodedCapabilities)));
    TEST_ASSERT_EQUAL_HEX16(0x0304, decodedCapabilities.capabilityIds[1]);

    capabilities.count = MAX_CAPABILITY_PAGE_ENTRIES;
    for (uint8_t index = 0; index < capabilities.count; ++index) {
        capabilities.capabilityIds[index] = static_cast<uint16_t>(index + 1);
    }
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::OK), raw(
        encodeCapabilityPageRecord(capabilities, value)));
    TEST_ASSERT_EQUAL_UINT8(21, value.length);
    capabilities.count = MAX_CAPABILITY_PAGE_ENTRIES + 1;
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::INVALID_FIELD), raw(
        encodeCapabilityPageRecord(capabilities, value)));

    DiagnosticPageRecord diagnostics = {};
    diagnostics.nextCursor = DIAGNOSTIC_PAGE_END;
    diagnostics.count = 1;
    diagnostics.entries[0].metricId = 7;
    diagnostics.entries[0].value = 0x78563412;
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::OK), raw(
        encodeDiagnosticPageRecord(diagnostics, value)));
    TEST_ASSERT_EQUAL_UINT8(7, value.length);
    DiagnosticPageRecord decodedDiagnostics = {};
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::OK), raw(
        decodeDiagnosticPageRecord(value, decodedDiagnostics)));
    TEST_ASSERT_EQUAL_HEX32(0x78563412, decodedDiagnostics.entries[0].value);

    diagnostics.count = MAX_DIAGNOSTIC_PAGE_ENTRIES;
    for (uint8_t index = 0; index < diagnostics.count; ++index) {
        diagnostics.entries[index].metricId = index;
        diagnostics.entries[index].value = index;
    }
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::OK), raw(
        encodeDiagnosticPageRecord(diagnostics, value)));
    TEST_ASSERT_EQUAL_UINT8(17, value.length);
    diagnostics.count = MAX_DIAGNOSTIC_PAGE_ENTRIES + 1;
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::INVALID_FIELD), raw(
        encodeDiagnosticPageRecord(diagnostics, value)));
}

void testOperationResponseResultClassesRemainScoped() {
    OperationResponse response = makeResponse(
        OperationCategory::DEVICE, OperationCode::PING, 0,
        OperationStatus::OPERATION_FAILED);
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::OK), raw(
        validateOperationResponse(response)));
    response.resultClass = ResultClass::REQUEST_REJECTED;
    response.resultCode = 0x08;
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::OK), raw(
        validateOperationResponse(response)));
    response.resultClass = ResultClass::RADIO_RESULT;
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::OK), raw(
        validateOperationResponse(response)));
    response.resultCode = 0x04;
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::INVALID_RESULT_CODE), raw(
        validateOperationResponse(response)));
    response.resultClass = ResultClass::LOCAL_RUNTIME_RESULT;
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::OK), raw(
        validateOperationResponse(response)));
    response.resultClass = ResultClass::SUCCESS;
    response.resultCode = 0;
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::OK), raw(
        validateOperationResponse(response)));
    response.resultClass = static_cast<ResultClass>(0x05);
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::INVALID_RESULT_CLASS), raw(
        validateOperationResponse(response)));
}

void testEveryResultClassCodePair() {
    const uint8_t rejectionCodes[] = {0x01, 0x02, 0x03, 0x04, 0x08};
    const uint8_t radioCodes[] = {0x06, 0x07, 0x08};
    const uint8_t runtimeCodes[] = {0x04, 0x0A};
    OperationResponse response = makeResponse(
        OperationCategory::DEVICE,
        OperationCode::PING,
        0,
        OperationStatus::OPERATION_FAILED
    );
    response.resultClass = ResultClass::SUCCESS;
    response.resultCode = 0;
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::OK), raw(
        validateOperationResponse(response)));
    response.resultClass = ResultClass::REQUEST_REJECTED;
    for (size_t index = 0; index < sizeof(rejectionCodes); ++index) {
        response.resultCode = rejectionCodes[index];
        TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::OK), raw(
            validateOperationResponse(response)));
    }
    response.resultClass = ResultClass::RADIO_RESULT;
    for (size_t index = 0; index < sizeof(radioCodes); ++index) {
        response.resultCode = radioCodes[index];
        TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::OK), raw(
            validateOperationResponse(response)));
    }
    response.resultClass = ResultClass::LOCAL_RUNTIME_RESULT;
    for (size_t index = 0; index < sizeof(runtimeCodes); ++index) {
        response.resultCode = runtimeCodes[index];
        TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::OK), raw(
            validateOperationResponse(response)));
    }
    response.resultClass = ResultClass::OPERATION_RESULT;
    for (uint8_t code = 0x01; code <= 0x0A; ++code) {
        response.resultCode = code;
        TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::OK), raw(
            validateOperationResponse(response)));
    }
}

void testOperationResultReusesExistingStatusAuthority() {
    TEST_ASSERT_TRUE((std::is_same<OperationStatus,
        DeviceCapabilities::OperationStatus>::value));
    OperationResponse response = makeResponse(
        OperationCategory::DEVICE, OperationCode::PING, 0);
    response.resultCode = 0x0B;
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::INVALID_RESULT_CODE), raw(
        validateOperationResponse(response)));
}

void testSuccessfulOperationResponseSchemas() {
    OperationResponse ping = makeResponse(
        OperationCategory::DEVICE, OperationCode::PING, 0);
    TEST_ASSERT_TRUE(setUint32Value(
        ping.value, CapabilityValueType::UNSIGNED_32, 42));
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::OK), raw(
        validateOperationResponse(ping)));
    setNoneValue(ping.value);
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::INVALID_RESPONSE_VALUE), raw(
        validateOperationResponse(ping)));

    OperationResponse set = makeResponse(OperationCategory::CAPABILITY,
        OperationCode::SET_INDICATOR, 1);
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::OK), raw(
        validateOperationResponse(set)));
    TEST_ASSERT_TRUE(setBooleanValue(set.value, 1));
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::INVALID_RESPONSE_VALUE), raw(
        validateOperationResponse(set)));

    DeviceInfoRecord record = {0, 6, 0, 1, 1,
        HardwareProfile::HELTEC_V4, DeviceRole::HUB, 1};
    OperationResponse info = makeResponse(OperationCategory::DEVICE,
        OperationCode::GET_DEVICE_INFO, 0);
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::OK), raw(
        encodeDeviceInfoRecord(record, info.value)));
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::OK), raw(
        validateOperationResponse(info)));
    info.value.bytes[5] = 0;
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::INVALID_RESPONSE_VALUE), raw(
        validateOperationResponse(info)));
}

void testOperationResponseExactGeometryAndRoundTrip() {
    OperationResponse response = makeResponse(OperationCategory::DEVICE,
        OperationCode::PING, 0);
    response.targetDeviceId = 0x01;
    TEST_ASSERT_TRUE(setUint32Value(
        response.value, CapabilityValueType::UNSIGNED_32, 0x78563412));
    uint8_t payload[MAX_PAYLOAD_SIZE] = {};
    size_t length = 0;
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::OK), raw(
        encodeOperationResponse(response, payload, sizeof(payload), length)));
    const uint8_t expected[] = {
        0x01, 0x20, 0x01, 0, 0, 0x02, 0x00, 0x02, 0x04,
        0x12, 0x34, 0x56, 0x78
    };
    TEST_ASSERT_EQUAL_UINT32(13, length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, payload, sizeof(expected));
    OperationResponse decoded = {};
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::OK), raw(
        decodeOperationResponse(payload, length, decoded)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(response.value.bytes, decoded.value.bytes, 4);
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::INVALID_LENGTH), raw(
        decodeOperationResponse(payload, length - 1, decoded)));
    payload[length] = 0;
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::INVALID_LENGTH), raw(
        decodeOperationResponse(payload, length + 1, decoded)));
}

void testProtocolErrorExactCodecAndValidation() {
    const ProtocolError error = {
        ProtocolErrorCode::UNSUPPORTED_MESSAGE_TYPE, 0xA5, 0};
    uint8_t payload[4] = {};
    size_t length = 0;
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::OK), raw(
        encodeProtocolError(error, payload, sizeof(payload), length)));
    const uint8_t expected[] = {0x03, 0xA5, 0x00, 0x00};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, payload, 4);
    ProtocolError decoded = {};
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::OK), raw(
        decodeProtocolError(payload, length, decoded)));
    TEST_ASSERT_EQUAL_HEX8(0xA5, decoded.offendingType);
    for (uint8_t code = 1; code <= 6; ++code) {
        payload[0] = code;
        TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::OK), raw(
            decodeProtocolError(payload, 4, decoded)));
    }
    payload[0] = 0;
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::INVALID_FIELD), raw(
        decodeProtocolError(payload, 4, decoded)));
    payload[0] = 1; payload[2] = 0x34; payload[3] = 0x12;
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::INVALID_FIELD), raw(
        decodeProtocolError(payload, 4, decoded)));
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::INVALID_LENGTH), raw(
        decodeProtocolError(payload, 3, decoded)));
}

template <typename Message>
Frame frameWithPayload(MessageType type, const uint8_t* payload, size_t length) {
    Frame frame = {};
    frame.major = VERSION_MAJOR;
    frame.minor = VERSION_MINOR;
    frame.messageType = type;
    frame.requestId = 1;
    frame.payloadLength = static_cast<uint16_t>(length);
    for (size_t index = 0; index < length; ++index) frame.payload[index] = payload[index];
    return frame;
}

void testEnvelopeIntegrationRoundTripsFourSemanticMessages() {
    uint8_t payload[MAX_PAYLOAD_SIZE] = {};
    uint8_t encoded[MAX_ENCODED_FRAME_SIZE] = {};
    size_t payloadLength = 0;
    size_t encodedLength = 0;

    HelloRequest hello = {1, 1};
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::OK), raw(
        encodeHelloRequest(hello, payload, sizeof(payload), payloadLength)));
    Frame outbound = frameWithPayload<HelloRequest>(
        MessageType::HELLO_REQUEST, payload, payloadLength);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EncodeResult::OK),
        static_cast<uint8_t>(encodeFrame(
            outbound, encoded, sizeof(encoded), encodedLength)));
    Frame inbound = {};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DecodeResult::OK),
        static_cast<uint8_t>(decodeFrame(encoded, encodedLength, inbound)));
    HelloRequest decodedHello = {};
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::OK), raw(decodeHelloRequest(
        inbound.payload, inbound.payloadLength, decodedHello)));

    OperationRequest request = makeRequest(
        OperationCategory::DEVICE, OperationCode::PING, 0);
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::OK), raw(encodeOperationRequest(
        request, payload, sizeof(payload), payloadLength)));
    outbound = frameWithPayload<OperationRequest>(
        MessageType::OPERATION_REQUEST, payload, payloadLength);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EncodeResult::OK),
        static_cast<uint8_t>(encodeFrame(
            outbound, encoded, sizeof(encoded), encodedLength)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DecodeResult::OK),
        static_cast<uint8_t>(decodeFrame(encoded, encodedLength, inbound)));
    OperationRequest decodedRequest = {};
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::OK), raw(decodeOperationRequest(
        inbound.payload, inbound.payloadLength, decodedRequest)));

    OperationResponse response = makeResponse(
        OperationCategory::DEVICE, OperationCode::PING, 0);
    TEST_ASSERT_TRUE(setUint32Value(
        response.value, CapabilityValueType::UNSIGNED_32, 42));
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::OK), raw(encodeOperationResponse(
        response, payload, sizeof(payload), payloadLength)));
    outbound = frameWithPayload<OperationResponse>(
        MessageType::OPERATION_RESPONSE, payload, payloadLength);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EncodeResult::OK),
        static_cast<uint8_t>(encodeFrame(
            outbound, encoded, sizeof(encoded), encodedLength)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DecodeResult::OK),
        static_cast<uint8_t>(decodeFrame(encoded, encodedLength, inbound)));
    OperationResponse decodedResponse = {};
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::OK), raw(decodeOperationResponse(
        inbound.payload, inbound.payloadLength, decodedResponse)));

    ProtocolError error = {ProtocolErrorCode::UNSUPPORTED_FLAGS, 0x10, 0};
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::OK), raw(encodeProtocolError(
        error, payload, sizeof(payload), payloadLength)));
    outbound = frameWithPayload<ProtocolError>(
        MessageType::PROTOCOL_ERROR, payload, payloadLength);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EncodeResult::OK),
        static_cast<uint8_t>(encodeFrame(
            outbound, encoded, sizeof(encoded), encodedLength)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DecodeResult::OK),
        static_cast<uint8_t>(decodeFrame(encoded, encodedLength, inbound)));
    ProtocolError decodedError = {};
    TEST_ASSERT_EQUAL_UINT8(raw(PayloadResult::OK), raw(decodeProtocolError(
        inbound.payload, inbound.payloadLength, decodedError)));
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(testHelloRequestExactCodecAndValidation);
    RUN_TEST(testHelloResponseExactCodecAndLittleEndianBitmaps);
    RUN_TEST(testHelloResponseRejectsEveryConstrainedField);
    RUN_TEST(testOperationClassificationMatrix);
    RUN_TEST(testTypedValueExactLengthsBytesAndRoundTrips);
    RUN_TEST(testTypedValueRejectsMalformedAndStructureInput);
    RUN_TEST(testOperationRequestExactPingAndTargetPreservation);
    RUN_TEST(testEveryOperationRequestSchema);
    RUN_TEST(testOperationRequestRejectsTargetsValuesAndGeometry);
    RUN_TEST(testFixedStructureRecordsRoundTrip);
    RUN_TEST(testBoundedPageRecordsRoundTripAndRejectInvalidCounts);
    RUN_TEST(testOperationResponseResultClassesRemainScoped);
    RUN_TEST(testEveryResultClassCodePair);
    RUN_TEST(testOperationResultReusesExistingStatusAuthority);
    RUN_TEST(testSuccessfulOperationResponseSchemas);
    RUN_TEST(testOperationResponseExactGeometryAndRoundTrip);
    RUN_TEST(testProtocolErrorExactCodecAndValidation);
    RUN_TEST(testEnvelopeIntegrationRoundTripsFourSemanticMessages);
    return UNITY_END();
}
