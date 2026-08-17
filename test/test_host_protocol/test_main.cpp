#include <unity.h>

#include <type_traits>

#include <host_protocol.h>

using namespace HostProtocol;

template <typename T>
uint8_t byte(T value) {
    return static_cast<uint8_t>(value);
}

void testVersionAndFixedBounds() {
    TEST_ASSERT_EQUAL_UINT8(0, VERSION_MAJOR);
    TEST_ASSERT_EQUAL_UINT8(1, VERSION_MINOR);
    TEST_ASSERT_TRUE(isSupportedVersion(0, 1));
    TEST_ASSERT_FALSE(isSupportedVersion(1, 1));
    TEST_ASSERT_FALSE(isSupportedVersion(0, 0));
    TEST_ASSERT_EQUAL_UINT16(128, MAX_PAYLOAD_SIZE);
    TEST_ASSERT_EQUAL_UINT16(8, DECODED_HEADER_SIZE);
    TEST_ASSERT_EQUAL_UINT16(2, CRC_SIZE);
    TEST_ASSERT_EQUAL_UINT16(10, MIN_DECODED_FRAME_SIZE);
    TEST_ASSERT_EQUAL_UINT16(138, MAX_DECODED_FRAME_SIZE);
    TEST_ASSERT_EQUAL_UINT16(139, MAX_COBS_CANDIDATE_SIZE);
    TEST_ASSERT_EQUAL_UINT16(140, MAX_ENCODED_FRAME_SIZE);
    TEST_ASSERT_EQUAL_UINT8(1, MAX_OUTSTANDING_OPERATIONS);
}

void testMessageTypeAssignmentsAndUnknownRejection() {
    TEST_ASSERT_EQUAL_HEX8(0x01, byte(MessageType::HELLO_REQUEST));
    TEST_ASSERT_EQUAL_HEX8(0x02, byte(MessageType::HELLO_RESPONSE));
    TEST_ASSERT_EQUAL_HEX8(0x10, byte(MessageType::OPERATION_REQUEST));
    TEST_ASSERT_EQUAL_HEX8(0x11, byte(MessageType::OPERATION_RESPONSE));
    TEST_ASSERT_EQUAL_HEX8(0x7F, byte(MessageType::PROTOCOL_ERROR));
    TEST_ASSERT_TRUE(isKnownMessageType(MessageType::HELLO_REQUEST));
    TEST_ASSERT_FALSE(isKnownMessageType(static_cast<MessageType>(0x00)));
    TEST_ASSERT_FALSE(isKnownMessageType(static_cast<MessageType>(0x12)));
    TEST_ASSERT_FALSE(isKnownMessageType(static_cast<MessageType>(0xFF)));
}

void testFlagsAndRequestIds() {
    TEST_ASSERT_TRUE(hasSupportedFlags(0x00));
    TEST_ASSERT_FALSE(hasSupportedFlags(0x01));
    TEST_ASSERT_FALSE(hasSupportedFlags(0xFF));
    TEST_ASSERT_TRUE(isReservedRequestId(0x0000));
    TEST_ASSERT_FALSE(isValidRequestId(0x0000));
    TEST_ASSERT_TRUE(isValidRequestId(0x0001));
    TEST_ASSERT_TRUE(isValidRequestId(0xFFFF));
}

void testCategoryAndOperationAssignments() {
    TEST_ASSERT_EQUAL_HEX8(0x01, byte(OperationCategory::DEVICE));
    TEST_ASSERT_EQUAL_HEX8(0x02, byte(OperationCategory::CAPABILITY));
    TEST_ASSERT_EQUAL_HEX8(0x03, byte(OperationCategory::PROCEDURE));
    TEST_ASSERT_EQUAL_HEX8(0x04, byte(OperationCategory::DIAGNOSTIC));
    TEST_ASSERT_FALSE(isKnownOperationCategory(
        static_cast<OperationCategory>(0x00)
    ));
    TEST_ASSERT_FALSE(isKnownOperationCategory(
        static_cast<OperationCategory>(0x05)
    ));

    const OperationCode codes[] = {
        OperationCode::PING, OperationCode::GET_DEVICE_INFO,
        OperationCode::GET_STATUS, OperationCode::GET_CAPABILITIES,
        OperationCode::DESCRIBE_CAPABILITY, OperationCode::READ_CAPABILITY,
        OperationCode::SET_INDICATOR, OperationCode::RUN_PROCEDURE,
        OperationCode::GET_DIAGNOSTICS
    };
    for (uint8_t i = 0; i < 9; ++i) {
        TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(0x20 + i), byte(codes[i]));
        TEST_ASSERT_TRUE(isKnownOperationCode(codes[i]));
    }
    TEST_ASSERT_FALSE(isKnownOperationCode(static_cast<OperationCode>(0x1F)));
    TEST_ASSERT_FALSE(isKnownOperationCode(static_cast<OperationCode>(0x29)));
    TEST_ASSERT_TRUE(isSupportedCategoryOperation(
        OperationCategory::DEVICE, OperationCode::PING
    ));
    TEST_ASSERT_TRUE(isSupportedCategoryOperation(
        OperationCategory::CAPABILITY, OperationCode::SET_INDICATOR
    ));
    TEST_ASSERT_TRUE(isSupportedCategoryOperation(
        OperationCategory::PROCEDURE, OperationCode::RUN_PROCEDURE
    ));
    TEST_ASSERT_TRUE(isSupportedCategoryOperation(
        OperationCategory::DIAGNOSTIC, OperationCode::GET_DIAGNOSTICS
    ));
    TEST_ASSERT_FALSE(isSupportedCategoryOperation(
        OperationCategory::DEVICE, OperationCode::GET_DIAGNOSTICS
    ));
}

void testResultClassAssignments() {
    TEST_ASSERT_EQUAL_HEX8(0x00, byte(ResultClass::SUCCESS));
    TEST_ASSERT_EQUAL_HEX8(0x01, byte(ResultClass::REQUEST_REJECTED));
    TEST_ASSERT_EQUAL_HEX8(0x02, byte(ResultClass::OPERATION_RESULT));
    TEST_ASSERT_EQUAL_HEX8(0x03, byte(ResultClass::RADIO_RESULT));
    TEST_ASSERT_EQUAL_HEX8(0x04, byte(ResultClass::LOCAL_RUNTIME_RESULT));
    TEST_ASSERT_FALSE(isKnownResultClass(static_cast<ResultClass>(0x05)));
}

void testResultCodeAuthoritiesAreClassScoped() {
    TEST_ASSERT_EQUAL_HEX8(0x00, byte(SuccessCode::OK));
    TEST_ASSERT_EQUAL_HEX8(0x01, byte(RequestRejectionCode::MALFORMED_REQUEST));
    TEST_ASSERT_EQUAL_HEX8(0x02, byte(RequestRejectionCode::UNSUPPORTED_OPERATION));
    TEST_ASSERT_EQUAL_HEX8(0x03, byte(RequestRejectionCode::BAD_TARGET));
    TEST_ASSERT_EQUAL_HEX8(0x04, byte(RequestRejectionCode::BUSY));
    TEST_ASSERT_EQUAL_HEX8(0x08, byte(RequestRejectionCode::MISMATCH));
    TEST_ASSERT_EQUAL_HEX8(0x06, byte(RadioResultCode::TIMEOUT));
    TEST_ASSERT_EQUAL_HEX8(0x07, byte(RadioResultCode::REMOTE_REJECTED));
    TEST_ASSERT_EQUAL_HEX8(0x08, byte(RadioResultCode::MISMATCH));
    TEST_ASSERT_EQUAL_HEX8(0x04, byte(LocalRuntimeResultCode::BUSY));
    TEST_ASSERT_EQUAL_HEX8(0x0A, byte(LocalRuntimeResultCode::OPERATION_FAILED));

    TEST_ASSERT_TRUE(isValidResultCode(ResultClass::SUCCESS, 0x00));
    TEST_ASSERT_FALSE(isValidResultCode(ResultClass::SUCCESS, 0x01));
    TEST_ASSERT_TRUE(isValidResultCode(ResultClass::REQUEST_REJECTED, 0x04));
    TEST_ASSERT_FALSE(isValidResultCode(ResultClass::RADIO_RESULT, 0x04));
    TEST_ASSERT_TRUE(isValidResultCode(ResultClass::RADIO_RESULT, 0x06));
    TEST_ASSERT_FALSE(isValidResultCode(ResultClass::REQUEST_REJECTED, 0x06));
    TEST_ASSERT_TRUE(isValidResultCode(ResultClass::LOCAL_RUNTIME_RESULT, 0x0A));
    TEST_ASSERT_FALSE(isValidResultCode(ResultClass::OPERATION_RESULT, 0x0B));
    TEST_ASSERT_FALSE(isValidResultCode(static_cast<ResultClass>(0xFF), 0x00));
}

void testOperationResultReusesCapabilityAuthority() {
    TEST_ASSERT_TRUE((std::is_same<
        OperationStatus,
        DeviceCapabilities::OperationStatus
    >::value));
    for (uint8_t code = 0x00; code <= 0x0A; ++code) {
        TEST_ASSERT_TRUE(isValidResultCode(ResultClass::OPERATION_RESULT, code));
    }
    TEST_ASSERT_EQUAL_HEX8(
        byte(DeviceCapabilities::OperationStatus::INVALID_DESCRIPTOR),
        byte(OperationStatus::INVALID_DESCRIPTOR)
    );
}

void testProtocolErrorAssignmentsAndReservedRejection() {
    TEST_ASSERT_EQUAL_HEX8(0x01, byte(ProtocolErrorCode::UNSUPPORTED_MAJOR));
    TEST_ASSERT_EQUAL_HEX8(0x02, byte(ProtocolErrorCode::UNSUPPORTED_MINOR));
    TEST_ASSERT_EQUAL_HEX8(0x03, byte(ProtocolErrorCode::UNSUPPORTED_MESSAGE_TYPE));
    TEST_ASSERT_EQUAL_HEX8(0x04, byte(ProtocolErrorCode::UNSUPPORTED_FLAGS));
    TEST_ASSERT_EQUAL_HEX8(0x05, byte(ProtocolErrorCode::MALFORMED_PAYLOAD));
    TEST_ASSERT_EQUAL_HEX8(0x06, byte(ProtocolErrorCode::INVALID_REQUEST_ID));
    TEST_ASSERT_TRUE(isKnownProtocolError(ProtocolErrorCode::INVALID_REQUEST_ID));
    TEST_ASSERT_FALSE(isKnownProtocolError(static_cast<ProtocolErrorCode>(0x00)));
    TEST_ASSERT_FALSE(isKnownProtocolError(static_cast<ProtocolErrorCode>(0x07)));
}

void testHelloVocabulary() {
    TEST_ASSERT_EQUAL_UINT8(2, HELLO_REQUEST_PAYLOAD_SIZE);
    TEST_ASSERT_EQUAL_UINT8(16, HELLO_RESPONSE_PAYLOAD_SIZE);
    TEST_ASSERT_EQUAL_UINT8(4, PROTOCOL_ERROR_PAYLOAD_SIZE);
    TEST_ASSERT_TRUE(isValidHelloMinorRange(1, 1));
    TEST_ASSERT_FALSE(isValidHelloMinorRange(0, 1));
    TEST_ASSERT_FALSE(isValidHelloMinorRange(1, 2));
    TEST_ASSERT_EQUAL_HEX16(0x0001, CATEGORY_DEVICE_BIT);
    TEST_ASSERT_EQUAL_HEX16(0x0002, CATEGORY_CAPABILITY_BIT);
    TEST_ASSERT_EQUAL_HEX16(0x0004, CATEGORY_PROCEDURE_BIT);
    TEST_ASSERT_EQUAL_HEX16(0x0008, CATEGORY_DIAGNOSTIC_BIT);
    TEST_ASSERT_EQUAL_HEX16(0x000F, KNOWN_CATEGORY_BITMAP);
    TEST_ASSERT_TRUE(hasValidCategoryBitmap(0x000F));
    TEST_ASSERT_FALSE(hasValidCategoryBitmap(0x0010));
    TEST_ASSERT_EQUAL_HEX16(0x0001, FEATURE_LOCAL_OPERATIONS);
    TEST_ASSERT_EQUAL_HEX16(0x0002, FEATURE_RADIO_BRIDGE);
    TEST_ASSERT_EQUAL_HEX16(0x0003, KNOWN_FEATURE_BITMAP);
    TEST_ASSERT_TRUE(hasValidFeatureBitmap(0x0003));
    TEST_ASSERT_FALSE(hasValidFeatureBitmap(0x0004));
    TEST_ASSERT_EQUAL_HEX8(0x01, byte(HardwareProfile::HELTEC_V4));
    TEST_ASSERT_EQUAL_HEX8(0x01, byte(DeviceRole::HUB));
    TEST_ASSERT_EQUAL_HEX8(0x02, byte(DeviceRole::NODE));
    TEST_ASSERT_FALSE(isKnownHardwareProfile(static_cast<HardwareProfile>(0)));
    TEST_ASSERT_FALSE(isKnownDeviceRole(static_cast<DeviceRole>(0)));
    TEST_ASSERT_EQUAL_UINT8(0, HELLO_RESERVED_VALUE);
}

void testCapabilityValueTypeAuthorityIsReused() {
    TEST_ASSERT_TRUE((std::is_same<
        CapabilityValueType,
        DeviceCapabilities::ValueType
    >::value));
    TEST_ASSERT_TRUE(isKnownHostValueType(
        byte(DeviceCapabilities::ValueType::NONE)
    ));
    TEST_ASSERT_TRUE(isKnownHostValueType(
        byte(DeviceCapabilities::ValueType::ENUM_U16)
    ));
    TEST_ASSERT_TRUE(isKnownHostValueType(STRUCTURE_VALUE_TYPE));
    TEST_ASSERT_FALSE(isKnownHostValueType(0x07));
    TEST_ASSERT_FALSE(isKnownHostValueType(0xFF));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(testVersionAndFixedBounds);
    RUN_TEST(testMessageTypeAssignmentsAndUnknownRejection);
    RUN_TEST(testFlagsAndRequestIds);
    RUN_TEST(testCategoryAndOperationAssignments);
    RUN_TEST(testResultClassAssignments);
    RUN_TEST(testResultCodeAuthoritiesAreClassScoped);
    RUN_TEST(testOperationResultReusesCapabilityAuthority);
    RUN_TEST(testProtocolErrorAssignmentsAndReservedRejection);
    RUN_TEST(testHelloVocabulary);
    RUN_TEST(testCapabilityValueTypeAuthorityIsReused);
    return UNITY_END();
}
