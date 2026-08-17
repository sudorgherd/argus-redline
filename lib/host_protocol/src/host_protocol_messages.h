#pragma once

#include <stddef.h>
#include <stdint.h>

#include <host_protocol.h>

namespace HostProtocol {

constexpr uint8_t OPERATION_REQUEST_FIXED_SIZE = 7;
constexpr uint8_t OPERATION_RESPONSE_FIXED_SIZE = 9;
constexpr uint8_t MAX_REQUEST_VALUE_SIZE =
    MAX_PAYLOAD_SIZE - OPERATION_REQUEST_FIXED_SIZE;
constexpr uint8_t MAX_RESPONSE_VALUE_SIZE =
    MAX_PAYLOAD_SIZE - OPERATION_RESPONSE_FIXED_SIZE;

constexpr uint8_t DEVICE_INFO_SIZE = 8;
constexpr uint8_t STATUS_SIZE = 10;
constexpr uint8_t CAPABILITY_DESCRIPTION_SIZE = 6;
constexpr uint8_t MAX_CAPABILITY_PAGE_ENTRIES = 9;
constexpr uint8_t MAX_DIAGNOSTIC_PAGE_ENTRIES = 3;
constexpr uint16_t CAPABILITY_PAGE_END = 0xFFFF;
constexpr uint8_t DIAGNOSTIC_PAGE_END = 0xFF;

constexpr uint16_t STATUS_READY = 0x0001;
constexpr uint16_t STATUS_RADIO_OPERATIONAL = 0x0002;
constexpr uint16_t STATUS_TRANSACTION_ACTIVE = 0x0004;
constexpr uint16_t STATUS_DEGRADED = 0x0008;
constexpr uint16_t STATUS_ERROR = 0x0010;
constexpr uint16_t KNOWN_STATUS_FLAGS = 0x001F;
constexpr uint16_t RESERVED_STATUS_FLAGS = 0xFFE0;

enum class PayloadResult : uint8_t {
    OK = 0x00,
    NULL_ARGUMENT = 0x01,
    OUTPUT_TOO_SMALL = 0x02,
    INVALID_LENGTH = 0x03,
    INVALID_FIELD = 0x04,
    UNKNOWN_CATEGORY = 0x05,
    UNKNOWN_OPERATION = 0x06,
    UNSUPPORTED_CATEGORY_OPERATION = 0x07,
    INVALID_TARGET = 0x08,
    INVALID_VALUE_TYPE = 0x09,
    INVALID_VALUE_LENGTH = 0x0A,
    INVALID_VALUE = 0x0B,
    INVALID_RESULT_CLASS = 0x0C,
    INVALID_RESULT_CODE = 0x0D,
    INVALID_RESPONSE_VALUE = 0x0E
};

enum class OperationClassification : uint8_t {
    VALID = 0x00,
    MALFORMED = 0x01,
    UNSUPPORTED = 0x02
};

struct HelloRequest {
    uint8_t minimumMinor;
    uint8_t maximumMinor;
};

struct HelloResponse {
    uint8_t selectedMinor;
    uint8_t firmwareMajor;
    uint8_t firmwareMinor;
    uint8_t firmwarePatch;
    uint8_t wireProtocol;
    uint8_t configurationSchema;
    HardwareProfile hardwareProfile;
    DeviceRole role;
    uint8_t deviceId;
    uint8_t maximumHostPayload;
    uint16_t operationCategoryBitmap;
    uint16_t featureBitmap;
    uint8_t maximumOutstandingOperations;
    uint8_t reserved;
};

struct TypedValue {
    uint8_t type;
    uint8_t length;
    uint8_t bytes[MAX_REQUEST_VALUE_SIZE];
};

struct OperationRequest {
    OperationCategory category;
    OperationCode operation;
    uint8_t targetDeviceId;
    uint16_t targetId;
    TypedValue value;
};

struct OperationResponse {
    OperationCategory category;
    OperationCode operation;
    uint8_t targetDeviceId;
    uint16_t targetId;
    ResultClass resultClass;
    uint8_t resultCode;
    TypedValue value;
};

struct ProtocolError {
    ProtocolErrorCode errorCode;
    uint8_t offendingType;
    uint16_t detail;
};

struct DeviceInfoRecord {
    uint8_t firmwareMajor;
    uint8_t firmwareMinor;
    uint8_t firmwarePatch;
    uint8_t wireProtocol;
    uint8_t configurationSchema;
    HardwareProfile hardwareProfile;
    DeviceRole role;
    uint8_t deviceId;
};

struct StatusRecord {
    uint16_t statusFlags;
    uint32_t uptimeSeconds;
    uint16_t retryCount;
    uint16_t timeoutCount;
};

struct CapabilityPageRecord {
    uint16_t nextCursor;
    uint8_t count;
    DeviceCapabilities::CapabilityId capabilityIds[MAX_CAPABILITY_PAGE_ENTRIES];
};

struct CapabilityDescriptionRecord {
    DeviceCapabilities::CapabilityClass capabilityClass;
    CapabilityValueType valueType;
    uint8_t operationFlags;
    DeviceCapabilities::UnitCode unit;
    uint8_t availability;
    uint8_t reserved;
};

struct DiagnosticEntry {
    uint8_t metricId;
    uint32_t value;
};

struct DiagnosticPageRecord {
    uint8_t nextCursor;
    uint8_t count;
    DiagnosticEntry entries[MAX_DIAGNOSTIC_PAGE_ENTRIES];
};

inline void writeUint16Le(uint8_t* output, uint16_t value) {
    output[0] = static_cast<uint8_t>(value);
    output[1] = static_cast<uint8_t>(value >> 8);
}

inline void writeUint32Le(uint8_t* output, uint32_t value) {
    output[0] = static_cast<uint8_t>(value);
    output[1] = static_cast<uint8_t>(value >> 8);
    output[2] = static_cast<uint8_t>(value >> 16);
    output[3] = static_cast<uint8_t>(value >> 24);
}

inline uint16_t readUint16Le(const uint8_t* input) {
    return static_cast<uint16_t>(input[0]) |
        (static_cast<uint16_t>(input[1]) << 8);
}

inline uint32_t readUint32Le(const uint8_t* input) {
    return static_cast<uint32_t>(input[0]) |
        (static_cast<uint32_t>(input[1]) << 8) |
        (static_cast<uint32_t>(input[2]) << 16) |
        (static_cast<uint32_t>(input[3]) << 24);
}

inline uint8_t expectedScalarValueLength(uint8_t type) {
    switch (static_cast<CapabilityValueType>(type)) {
        case CapabilityValueType::NONE:
            return 0;
        case CapabilityValueType::BOOLEAN:
            return 1;
        case CapabilityValueType::NORMALIZED_U16:
        case CapabilityValueType::ENUM_U16:
            return 2;
        case CapabilityValueType::UNSIGNED_32:
        case CapabilityValueType::SIGNED_32:
        case CapabilityValueType::FIXED_Q16_16:
            return 4;
    }
    return 0xFF;
}

inline bool isValidScalarValue(const TypedValue& value) {
    if (value.type == STRUCTURE_VALUE_TYPE ||
        !DeviceCapabilities::isKnownValueType(
            static_cast<CapabilityValueType>(value.type)
        )) {
        return false;
    }
    const uint8_t expectedLength = expectedScalarValueLength(value.type);
    if (value.length != expectedLength) {
        return false;
    }
    return value.type != static_cast<uint8_t>(CapabilityValueType::BOOLEAN) ||
        value.bytes[0] <= 1;
}

inline bool isNoneValue(const TypedValue& value) {
    return value.type == static_cast<uint8_t>(CapabilityValueType::NONE) &&
        value.length == 0;
}

inline bool isNonNoneScalarValue(const TypedValue& value) {
    return isValidScalarValue(value) && !isNoneValue(value);
}

inline void setNoneValue(TypedValue& value) {
    value = {};
    value.type = static_cast<uint8_t>(CapabilityValueType::NONE);
}

inline bool setBooleanValue(TypedValue& value, uint8_t booleanValue) {
    if (booleanValue > 1) return false;
    value = {};
    value.type = static_cast<uint8_t>(CapabilityValueType::BOOLEAN);
    value.length = 1;
    value.bytes[0] = booleanValue;
    return true;
}

inline bool setUint16Value(
    TypedValue& value,
    CapabilityValueType type,
    uint16_t numericValue
) {
    if (type != CapabilityValueType::NORMALIZED_U16 &&
        type != CapabilityValueType::ENUM_U16) return false;
    value = {};
    value.type = static_cast<uint8_t>(type);
    value.length = 2;
    writeUint16Le(value.bytes, numericValue);
    return true;
}

inline bool setUint32Value(
    TypedValue& value,
    CapabilityValueType type,
    uint32_t bits
) {
    if (type != CapabilityValueType::UNSIGNED_32 &&
        type != CapabilityValueType::SIGNED_32 &&
        type != CapabilityValueType::FIXED_Q16_16) return false;
    value = {};
    value.type = static_cast<uint8_t>(type);
    value.length = 4;
    writeUint32Le(value.bytes, bits);
    return true;
}

inline PayloadResult encodeTypedValue(
    const TypedValue& value,
    uint8_t* output,
    size_t outputCapacity,
    size_t& outputLength
) {
    outputLength = 0;
    if (output == nullptr) {
        return PayloadResult::NULL_ARGUMENT;
    }
    if (!isValidScalarValue(value)) {
        return DeviceCapabilities::isKnownValueType(
            static_cast<CapabilityValueType>(value.type)
        ) ? PayloadResult::INVALID_VALUE : PayloadResult::INVALID_VALUE_TYPE;
    }
    if (outputCapacity < value.length) {
        return PayloadResult::OUTPUT_TOO_SMALL;
    }
    for (size_t index = 0; index < value.length; ++index) {
        output[index] = value.bytes[index];
    }
    outputLength = value.length;
    return PayloadResult::OK;
}

inline PayloadResult decodeTypedValue(
    uint8_t type,
    const uint8_t* input,
    size_t inputLength,
    TypedValue& value
) {
    if (input == nullptr && inputLength != 0) {
        return PayloadResult::NULL_ARGUMENT;
    }
    if (type == STRUCTURE_VALUE_TYPE ||
        !DeviceCapabilities::isKnownValueType(
            static_cast<CapabilityValueType>(type)
        )) {
        return PayloadResult::INVALID_VALUE_TYPE;
    }
    if (inputLength != expectedScalarValueLength(type)) {
        return PayloadResult::INVALID_VALUE_LENGTH;
    }
    TypedValue candidate = {};
    candidate.type = type;
    candidate.length = static_cast<uint8_t>(inputLength);
    for (size_t index = 0; index < inputLength; ++index) {
        candidate.bytes[index] = input[index];
    }
    if (!isValidScalarValue(candidate)) {
        return PayloadResult::INVALID_VALUE;
    }
    value = candidate;
    return PayloadResult::OK;
}

inline OperationClassification classifyCategoryOperation(
    uint8_t category,
    uint8_t operation
) {
    const OperationCategory typedCategory =
        static_cast<OperationCategory>(category);
    const OperationCode typedOperation = static_cast<OperationCode>(operation);
    if (!isKnownOperationCategory(typedCategory) ||
        !isKnownOperationCode(typedOperation)) {
        return OperationClassification::MALFORMED;
    }
    return isSupportedCategoryOperation(typedCategory, typedOperation)
        ? OperationClassification::VALID
        : OperationClassification::UNSUPPORTED;
}

inline bool isValidOperationTarget(
    OperationCategory category,
    OperationCode operation,
    uint16_t targetId
) {
    switch (category) {
        case OperationCategory::DEVICE:
        case OperationCategory::DIAGNOSTIC:
            return targetId == 0;
        case OperationCategory::CAPABILITY:
            return operation == OperationCode::GET_CAPABILITIES
                ? targetId == 0
                : targetId != DeviceCapabilities::INVALID_CAPABILITY_ID;
        case OperationCategory::PROCEDURE:
            return targetId != 0;
    }
    return false;
}

inline bool isValidOperationRequestValue(
    OperationCode operation,
    const TypedValue& value
) {
    switch (operation) {
        case OperationCode::PING:
        case OperationCode::GET_DEVICE_INFO:
        case OperationCode::GET_STATUS:
        case OperationCode::DESCRIBE_CAPABILITY:
        case OperationCode::READ_CAPABILITY:
            return isNoneValue(value);
        case OperationCode::GET_CAPABILITIES:
            return isNoneValue(value) ||
                (value.type == static_cast<uint8_t>(
                    CapabilityValueType::UNSIGNED_32
                ) && isValidScalarValue(value));
        case OperationCode::SET_INDICATOR:
            return value.type == static_cast<uint8_t>(
                    CapabilityValueType::BOOLEAN
                ) && isValidScalarValue(value);
        case OperationCode::RUN_PROCEDURE:
            return isValidScalarValue(value);
        case OperationCode::GET_DIAGNOSTICS:
            return isNoneValue(value) ||
                (value.type == static_cast<uint8_t>(
                    CapabilityValueType::UNSIGNED_32
                ) && isValidScalarValue(value));
    }
    return false;
}

inline PayloadResult validateOperationRequest(const OperationRequest& request) {
    const OperationClassification classification = classifyCategoryOperation(
        static_cast<uint8_t>(request.category),
        static_cast<uint8_t>(request.operation)
    );
    if (classification == OperationClassification::MALFORMED) {
        return !isKnownOperationCategory(request.category)
            ? PayloadResult::UNKNOWN_CATEGORY
            : PayloadResult::UNKNOWN_OPERATION;
    }
    if (classification == OperationClassification::UNSUPPORTED) {
        return PayloadResult::UNSUPPORTED_CATEGORY_OPERATION;
    }
    if (!isValidOperationTarget(
            request.category, request.operation, request.targetId)) {
        return PayloadResult::INVALID_TARGET;
    }
    if (!isValidOperationRequestValue(request.operation, request.value)) {
        return PayloadResult::INVALID_VALUE;
    }
    return PayloadResult::OK;
}

inline PayloadResult encodeHelloRequest(
    const HelloRequest& request,
    uint8_t* output,
    size_t outputCapacity,
    size_t& outputLength
) {
    outputLength = 0;
    if (output == nullptr) return PayloadResult::NULL_ARGUMENT;
    if (!isValidHelloMinorRange(request.minimumMinor, request.maximumMinor)) {
        return PayloadResult::INVALID_FIELD;
    }
    if (outputCapacity < HELLO_REQUEST_PAYLOAD_SIZE) {
        return PayloadResult::OUTPUT_TOO_SMALL;
    }
    output[0] = request.minimumMinor;
    output[1] = request.maximumMinor;
    outputLength = HELLO_REQUEST_PAYLOAD_SIZE;
    return PayloadResult::OK;
}

inline PayloadResult decodeHelloRequest(
    const uint8_t* input,
    size_t inputLength,
    HelloRequest& request
) {
    if (input == nullptr) return PayloadResult::NULL_ARGUMENT;
    if (inputLength != HELLO_REQUEST_PAYLOAD_SIZE) {
        return PayloadResult::INVALID_LENGTH;
    }
    HelloRequest candidate = {input[0], input[1]};
    if (!isValidHelloMinorRange(candidate.minimumMinor, candidate.maximumMinor)) {
        return PayloadResult::INVALID_FIELD;
    }
    request = candidate;
    return PayloadResult::OK;
}

inline bool isValidHelloResponse(const HelloResponse& response) {
    return response.selectedMinor == VERSION_MINOR &&
        isKnownHardwareProfile(response.hardwareProfile) &&
        isKnownDeviceRole(response.role) &&
        response.maximumHostPayload == MAX_PAYLOAD_SIZE &&
        hasValidCategoryBitmap(response.operationCategoryBitmap) &&
        hasValidFeatureBitmap(response.featureBitmap) &&
        response.maximumOutstandingOperations == MAX_OUTSTANDING_OPERATIONS &&
        response.reserved == HELLO_RESERVED_VALUE;
}

inline PayloadResult encodeHelloResponse(
    const HelloResponse& response,
    uint8_t* output,
    size_t outputCapacity,
    size_t& outputLength
) {
    outputLength = 0;
    if (output == nullptr) return PayloadResult::NULL_ARGUMENT;
    if (!isValidHelloResponse(response)) return PayloadResult::INVALID_FIELD;
    if (outputCapacity < HELLO_RESPONSE_PAYLOAD_SIZE) {
        return PayloadResult::OUTPUT_TOO_SMALL;
    }
    output[0] = response.selectedMinor;
    output[1] = response.firmwareMajor;
    output[2] = response.firmwareMinor;
    output[3] = response.firmwarePatch;
    output[4] = response.wireProtocol;
    output[5] = response.configurationSchema;
    output[6] = static_cast<uint8_t>(response.hardwareProfile);
    output[7] = static_cast<uint8_t>(response.role);
    output[8] = response.deviceId;
    output[9] = response.maximumHostPayload;
    writeUint16Le(output + 10, response.operationCategoryBitmap);
    writeUint16Le(output + 12, response.featureBitmap);
    output[14] = response.maximumOutstandingOperations;
    output[15] = response.reserved;
    outputLength = HELLO_RESPONSE_PAYLOAD_SIZE;
    return PayloadResult::OK;
}

inline PayloadResult decodeHelloResponse(
    const uint8_t* input,
    size_t inputLength,
    HelloResponse& response
) {
    if (input == nullptr) return PayloadResult::NULL_ARGUMENT;
    if (inputLength != HELLO_RESPONSE_PAYLOAD_SIZE) {
        return PayloadResult::INVALID_LENGTH;
    }
    HelloResponse candidate = {};
    candidate.selectedMinor = input[0];
    candidate.firmwareMajor = input[1];
    candidate.firmwareMinor = input[2];
    candidate.firmwarePatch = input[3];
    candidate.wireProtocol = input[4];
    candidate.configurationSchema = input[5];
    candidate.hardwareProfile = static_cast<HardwareProfile>(input[6]);
    candidate.role = static_cast<DeviceRole>(input[7]);
    candidate.deviceId = input[8];
    candidate.maximumHostPayload = input[9];
    candidate.operationCategoryBitmap = readUint16Le(input + 10);
    candidate.featureBitmap = readUint16Le(input + 12);
    candidate.maximumOutstandingOperations = input[14];
    candidate.reserved = input[15];
    if (!isValidHelloResponse(candidate)) return PayloadResult::INVALID_FIELD;
    response = candidate;
    return PayloadResult::OK;
}

inline PayloadResult encodeOperationRequest(
    const OperationRequest& request,
    uint8_t* output,
    size_t outputCapacity,
    size_t& outputLength
) {
    outputLength = 0;
    if (output == nullptr) return PayloadResult::NULL_ARGUMENT;
    const PayloadResult validation = validateOperationRequest(request);
    if (validation != PayloadResult::OK) return validation;
    const size_t required = OPERATION_REQUEST_FIXED_SIZE + request.value.length;
    if (required > MAX_PAYLOAD_SIZE) return PayloadResult::INVALID_LENGTH;
    if (outputCapacity < required) return PayloadResult::OUTPUT_TOO_SMALL;
    output[0] = static_cast<uint8_t>(request.category);
    output[1] = static_cast<uint8_t>(request.operation);
    output[2] = request.targetDeviceId;
    writeUint16Le(output + 3, request.targetId);
    output[5] = request.value.type;
    output[6] = request.value.length;
    for (size_t index = 0; index < request.value.length; ++index) {
        output[7 + index] = request.value.bytes[index];
    }
    outputLength = required;
    return PayloadResult::OK;
}

inline PayloadResult decodeOperationRequest(
    const uint8_t* input,
    size_t inputLength,
    OperationRequest& request
) {
    if (input == nullptr) return PayloadResult::NULL_ARGUMENT;
    if (inputLength < OPERATION_REQUEST_FIXED_SIZE ||
        inputLength > MAX_PAYLOAD_SIZE) return PayloadResult::INVALID_LENGTH;
    const uint8_t valueLength = input[6];
    if (inputLength != OPERATION_REQUEST_FIXED_SIZE + valueLength) {
        return PayloadResult::INVALID_LENGTH;
    }
    OperationRequest candidate = {};
    candidate.category = static_cast<OperationCategory>(input[0]);
    candidate.operation = static_cast<OperationCode>(input[1]);
    candidate.targetDeviceId = input[2];
    candidate.targetId = readUint16Le(input + 3);
    const PayloadResult valueResult = decodeTypedValue(
        input[5], input + 7, valueLength, candidate.value
    );
    if (valueResult != PayloadResult::OK) return valueResult;
    const PayloadResult validation = validateOperationRequest(candidate);
    if (validation != PayloadResult::OK) return validation;
    request = candidate;
    return PayloadResult::OK;
}

inline bool isValidStructureLength(OperationCode operation, uint8_t length) {
    switch (operation) {
        case OperationCode::GET_DEVICE_INFO:
            return length == DEVICE_INFO_SIZE;
        case OperationCode::GET_STATUS:
            return length == STATUS_SIZE;
        case OperationCode::GET_CAPABILITIES:
            return length >= 3 && length <= 21 && ((length - 3) % 2) == 0;
        case OperationCode::DESCRIBE_CAPABILITY:
            return length == CAPABILITY_DESCRIPTION_SIZE;
        case OperationCode::GET_DIAGNOSTICS:
            return length >= 2 && length <= 17 && ((length - 2) % 5) == 0;
        default:
            return false;
    }
}

inline bool isValidStructureValue(
    OperationCode operation,
    const TypedValue& value
) {
    if (value.type != STRUCTURE_VALUE_TYPE ||
        !isValidStructureLength(operation, value.length)) return false;
    switch (operation) {
        case OperationCode::GET_DEVICE_INFO:
            return isKnownHardwareProfile(
                    static_cast<HardwareProfile>(value.bytes[5])) &&
                isKnownDeviceRole(static_cast<DeviceRole>(value.bytes[6]));
        case OperationCode::GET_STATUS:
            return (readUint16Le(value.bytes) & RESERVED_STATUS_FLAGS) == 0;
        case OperationCode::GET_CAPABILITIES: {
            const uint8_t count = value.bytes[2];
            if (count > MAX_CAPABILITY_PAGE_ENTRIES ||
                value.length != 3 + 2 * count) return false;
            for (size_t index = 0; index < count; ++index) {
                if (readUint16Le(value.bytes + 3 + 2 * index) ==
                    DeviceCapabilities::INVALID_CAPABILITY_ID) return false;
            }
            return true;
        }
        case OperationCode::DESCRIBE_CAPABILITY:
            return DeviceCapabilities::isKnownCapabilityClass(
                    static_cast<DeviceCapabilities::CapabilityClass>(
                        value.bytes[0])) &&
                DeviceCapabilities::isKnownValueType(
                    static_cast<CapabilityValueType>(value.bytes[1])) &&
                DeviceCapabilities::hasKnownOperationFlags(value.bytes[2]) &&
                DeviceCapabilities::isKnownUnitCode(value.bytes[3]) &&
                value.bytes[4] <= 1 && value.bytes[5] == 0;
        case OperationCode::GET_DIAGNOSTICS: {
            const uint8_t count = value.bytes[1];
            return count <= MAX_DIAGNOSTIC_PAGE_ENTRIES &&
                value.length == 2 + 5 * count;
        }
        default:
            return false;
    }
}

inline bool isValidSuccessfulOperationValue(
    OperationCode operation,
    const TypedValue& value
) {
    switch (operation) {
        case OperationCode::PING:
            return value.type == static_cast<uint8_t>(
                    CapabilityValueType::UNSIGNED_32
                ) && isValidScalarValue(value);
        case OperationCode::GET_DEVICE_INFO:
        case OperationCode::GET_STATUS:
        case OperationCode::GET_CAPABILITIES:
        case OperationCode::DESCRIBE_CAPABILITY:
        case OperationCode::GET_DIAGNOSTICS:
            return isValidStructureValue(operation, value);
        case OperationCode::READ_CAPABILITY:
            return isNonNoneScalarValue(value);
        case OperationCode::SET_INDICATOR:
            return isNoneValue(value);
        case OperationCode::RUN_PROCEDURE:
            return isValidScalarValue(value);
    }
    return false;
}

inline PayloadResult validateOperationResponse(
    const OperationResponse& response
) {
    const OperationClassification classification = classifyCategoryOperation(
        static_cast<uint8_t>(response.category),
        static_cast<uint8_t>(response.operation)
    );
    if (classification == OperationClassification::MALFORMED) {
        return !isKnownOperationCategory(response.category)
            ? PayloadResult::UNKNOWN_CATEGORY
            : PayloadResult::UNKNOWN_OPERATION;
    }
    if (classification == OperationClassification::UNSUPPORTED) {
        return PayloadResult::UNSUPPORTED_CATEGORY_OPERATION;
    }
    if (!isValidOperationTarget(
            response.category, response.operation, response.targetId)) {
        return PayloadResult::INVALID_TARGET;
    }
    if (!isKnownResultClass(response.resultClass)) {
        return PayloadResult::INVALID_RESULT_CLASS;
    }
    if (!isValidResultCode(response.resultClass, response.resultCode)) {
        return PayloadResult::INVALID_RESULT_CODE;
    }
    const bool isSuccessfulTargetResult =
        response.resultClass == ResultClass::OPERATION_RESULT &&
        response.resultCode == static_cast<uint8_t>(OperationStatus::OK);
    if (isSuccessfulTargetResult) {
        return isValidSuccessfulOperationValue(
            response.operation, response.value
        ) ? PayloadResult::OK : PayloadResult::INVALID_RESPONSE_VALUE;
    }
    return isNoneValue(response.value)
        ? PayloadResult::OK
        : PayloadResult::INVALID_RESPONSE_VALUE;
}

inline PayloadResult encodeOperationResponse(
    const OperationResponse& response,
    uint8_t* output,
    size_t outputCapacity,
    size_t& outputLength
) {
    outputLength = 0;
    if (output == nullptr) return PayloadResult::NULL_ARGUMENT;
    const PayloadResult validation = validateOperationResponse(response);
    if (validation != PayloadResult::OK) return validation;
    const size_t required = OPERATION_RESPONSE_FIXED_SIZE + response.value.length;
    if (required > MAX_PAYLOAD_SIZE) return PayloadResult::INVALID_LENGTH;
    if (outputCapacity < required) return PayloadResult::OUTPUT_TOO_SMALL;
    output[0] = static_cast<uint8_t>(response.category);
    output[1] = static_cast<uint8_t>(response.operation);
    output[2] = response.targetDeviceId;
    writeUint16Le(output + 3, response.targetId);
    output[5] = static_cast<uint8_t>(response.resultClass);
    output[6] = response.resultCode;
    output[7] = response.value.type;
    output[8] = response.value.length;
    for (size_t index = 0; index < response.value.length; ++index) {
        output[9 + index] = response.value.bytes[index];
    }
    outputLength = required;
    return PayloadResult::OK;
}

inline PayloadResult decodeOperationResponse(
    const uint8_t* input,
    size_t inputLength,
    OperationResponse& response
) {
    if (input == nullptr) return PayloadResult::NULL_ARGUMENT;
    if (inputLength < OPERATION_RESPONSE_FIXED_SIZE ||
        inputLength > MAX_PAYLOAD_SIZE) return PayloadResult::INVALID_LENGTH;
    const uint8_t valueLength = input[8];
    if (inputLength != OPERATION_RESPONSE_FIXED_SIZE + valueLength) {
        return PayloadResult::INVALID_LENGTH;
    }
    OperationResponse candidate = {};
    candidate.category = static_cast<OperationCategory>(input[0]);
    candidate.operation = static_cast<OperationCode>(input[1]);
    candidate.targetDeviceId = input[2];
    candidate.targetId = readUint16Le(input + 3);
    candidate.resultClass = static_cast<ResultClass>(input[5]);
    candidate.resultCode = input[6];
    candidate.value.type = input[7];
    candidate.value.length = valueLength;
    for (size_t index = 0; index < valueLength; ++index) {
        candidate.value.bytes[index] = input[9 + index];
    }
    if (candidate.value.type != STRUCTURE_VALUE_TYPE &&
        !isValidScalarValue(candidate.value)) {
        return !isKnownHostValueType(candidate.value.type)
            ? PayloadResult::INVALID_VALUE_TYPE
            : PayloadResult::INVALID_VALUE;
    }
    const PayloadResult validation = validateOperationResponse(candidate);
    if (validation != PayloadResult::OK) return validation;
    response = candidate;
    return PayloadResult::OK;
}

inline bool isValidProtocolError(const ProtocolError& error) {
    return isKnownProtocolError(error.errorCode) && error.detail == 0;
}

inline PayloadResult encodeProtocolError(
    const ProtocolError& error,
    uint8_t* output,
    size_t outputCapacity,
    size_t& outputLength
) {
    outputLength = 0;
    if (output == nullptr) return PayloadResult::NULL_ARGUMENT;
    if (!isValidProtocolError(error)) return PayloadResult::INVALID_FIELD;
    if (outputCapacity < PROTOCOL_ERROR_PAYLOAD_SIZE) {
        return PayloadResult::OUTPUT_TOO_SMALL;
    }
    output[0] = static_cast<uint8_t>(error.errorCode);
    output[1] = error.offendingType;
    writeUint16Le(output + 2, error.detail);
    outputLength = PROTOCOL_ERROR_PAYLOAD_SIZE;
    return PayloadResult::OK;
}

inline PayloadResult decodeProtocolError(
    const uint8_t* input,
    size_t inputLength,
    ProtocolError& error
) {
    if (input == nullptr) return PayloadResult::NULL_ARGUMENT;
    if (inputLength != PROTOCOL_ERROR_PAYLOAD_SIZE) {
        return PayloadResult::INVALID_LENGTH;
    }
    ProtocolError candidate = {};
    candidate.errorCode = static_cast<ProtocolErrorCode>(input[0]);
    candidate.offendingType = input[1];
    candidate.detail = readUint16Le(input + 2);
    if (!isValidProtocolError(candidate)) return PayloadResult::INVALID_FIELD;
    error = candidate;
    return PayloadResult::OK;
}

inline PayloadResult encodeDeviceInfoRecord(
    const DeviceInfoRecord& record, TypedValue& value
) {
    if (!isKnownHardwareProfile(record.hardwareProfile) ||
        !isKnownDeviceRole(record.role)) return PayloadResult::INVALID_FIELD;
    TypedValue candidate = {};
    candidate.type = STRUCTURE_VALUE_TYPE;
    candidate.length = DEVICE_INFO_SIZE;
    candidate.bytes[0] = record.firmwareMajor;
    candidate.bytes[1] = record.firmwareMinor;
    candidate.bytes[2] = record.firmwarePatch;
    candidate.bytes[3] = record.wireProtocol;
    candidate.bytes[4] = record.configurationSchema;
    candidate.bytes[5] = static_cast<uint8_t>(record.hardwareProfile);
    candidate.bytes[6] = static_cast<uint8_t>(record.role);
    candidate.bytes[7] = record.deviceId;
    value = candidate;
    return PayloadResult::OK;
}

inline PayloadResult decodeDeviceInfoRecord(
    const TypedValue& value, DeviceInfoRecord& record
) {
    if (value.type != STRUCTURE_VALUE_TYPE || value.length != DEVICE_INFO_SIZE) {
        return PayloadResult::INVALID_VALUE;
    }
    DeviceInfoRecord candidate = {};
    candidate.firmwareMajor = value.bytes[0];
    candidate.firmwareMinor = value.bytes[1];
    candidate.firmwarePatch = value.bytes[2];
    candidate.wireProtocol = value.bytes[3];
    candidate.configurationSchema = value.bytes[4];
    candidate.hardwareProfile = static_cast<HardwareProfile>(value.bytes[5]);
    candidate.role = static_cast<DeviceRole>(value.bytes[6]);
    candidate.deviceId = value.bytes[7];
    if (!isKnownHardwareProfile(candidate.hardwareProfile) ||
        !isKnownDeviceRole(candidate.role)) return PayloadResult::INVALID_FIELD;
    record = candidate;
    return PayloadResult::OK;
}

inline PayloadResult encodeStatusRecord(
    const StatusRecord& record, TypedValue& value
) {
    if ((record.statusFlags & RESERVED_STATUS_FLAGS) != 0) {
        return PayloadResult::INVALID_FIELD;
    }
    TypedValue candidate = {};
    candidate.type = STRUCTURE_VALUE_TYPE;
    candidate.length = STATUS_SIZE;
    writeUint16Le(candidate.bytes, record.statusFlags);
    writeUint32Le(candidate.bytes + 2, record.uptimeSeconds);
    writeUint16Le(candidate.bytes + 6, record.retryCount);
    writeUint16Le(candidate.bytes + 8, record.timeoutCount);
    value = candidate;
    return PayloadResult::OK;
}

inline PayloadResult decodeStatusRecord(
    const TypedValue& value, StatusRecord& record
) {
    if (value.type != STRUCTURE_VALUE_TYPE || value.length != STATUS_SIZE) {
        return PayloadResult::INVALID_VALUE;
    }
    StatusRecord candidate = {};
    candidate.statusFlags = readUint16Le(value.bytes);
    candidate.uptimeSeconds = readUint32Le(value.bytes + 2);
    candidate.retryCount = readUint16Le(value.bytes + 6);
    candidate.timeoutCount = readUint16Le(value.bytes + 8);
    if ((candidate.statusFlags & RESERVED_STATUS_FLAGS) != 0) {
        return PayloadResult::INVALID_FIELD;
    }
    record = candidate;
    return PayloadResult::OK;
}

inline PayloadResult encodeCapabilityPageRecord(
    const CapabilityPageRecord& record, TypedValue& value
) {
    if (record.count > MAX_CAPABILITY_PAGE_ENTRIES) {
        return PayloadResult::INVALID_FIELD;
    }
    TypedValue candidate = {};
    candidate.type = STRUCTURE_VALUE_TYPE;
    candidate.length = static_cast<uint8_t>(3 + 2 * record.count);
    writeUint16Le(candidate.bytes, record.nextCursor);
    candidate.bytes[2] = record.count;
    for (size_t index = 0; index < record.count; ++index) {
        if (record.capabilityIds[index] ==
            DeviceCapabilities::INVALID_CAPABILITY_ID) {
            return PayloadResult::INVALID_FIELD;
        }
        writeUint16Le(candidate.bytes + 3 + 2 * index,
            record.capabilityIds[index]);
    }
    value = candidate;
    return PayloadResult::OK;
}

inline PayloadResult decodeCapabilityPageRecord(
    const TypedValue& value, CapabilityPageRecord& record
) {
    if (value.type != STRUCTURE_VALUE_TYPE || value.length < 3 ||
        value.length > 21 || ((value.length - 3) % 2) != 0) {
        return PayloadResult::INVALID_VALUE;
    }
    CapabilityPageRecord candidate = {};
    candidate.nextCursor = readUint16Le(value.bytes);
    candidate.count = value.bytes[2];
    if (candidate.count > MAX_CAPABILITY_PAGE_ENTRIES ||
        value.length != 3 + 2 * candidate.count) {
        return PayloadResult::INVALID_FIELD;
    }
    for (size_t index = 0; index < candidate.count; ++index) {
        candidate.capabilityIds[index] = readUint16Le(
            value.bytes + 3 + 2 * index);
        if (candidate.capabilityIds[index] ==
            DeviceCapabilities::INVALID_CAPABILITY_ID) {
            return PayloadResult::INVALID_FIELD;
        }
    }
    record = candidate;
    return PayloadResult::OK;
}

inline PayloadResult encodeCapabilityDescriptionRecord(
    const CapabilityDescriptionRecord& record, TypedValue& value
) {
    if (!DeviceCapabilities::isKnownCapabilityClass(record.capabilityClass) ||
        !DeviceCapabilities::isKnownValueType(record.valueType) ||
        !DeviceCapabilities::hasKnownOperationFlags(record.operationFlags) ||
        !DeviceCapabilities::isKnownUnitCode(static_cast<uint8_t>(record.unit)) ||
        record.availability > 1 || record.reserved != 0) {
        return PayloadResult::INVALID_FIELD;
    }
    TypedValue candidate = {};
    candidate.type = STRUCTURE_VALUE_TYPE;
    candidate.length = CAPABILITY_DESCRIPTION_SIZE;
    candidate.bytes[0] = static_cast<uint8_t>(record.capabilityClass);
    candidate.bytes[1] = static_cast<uint8_t>(record.valueType);
    candidate.bytes[2] = record.operationFlags;
    candidate.bytes[3] = static_cast<uint8_t>(record.unit);
    candidate.bytes[4] = record.availability;
    candidate.bytes[5] = record.reserved;
    value = candidate;
    return PayloadResult::OK;
}

inline PayloadResult decodeCapabilityDescriptionRecord(
    const TypedValue& value, CapabilityDescriptionRecord& record
) {
    if (value.type != STRUCTURE_VALUE_TYPE ||
        value.length != CAPABILITY_DESCRIPTION_SIZE) {
        return PayloadResult::INVALID_VALUE;
    }
    CapabilityDescriptionRecord candidate = {};
    candidate.capabilityClass = static_cast<DeviceCapabilities::CapabilityClass>(
        value.bytes[0]);
    candidate.valueType = static_cast<CapabilityValueType>(value.bytes[1]);
    candidate.operationFlags = value.bytes[2];
    candidate.unit = static_cast<DeviceCapabilities::UnitCode>(value.bytes[3]);
    candidate.availability = value.bytes[4];
    candidate.reserved = value.bytes[5];
    TypedValue ignored = {};
    if (encodeCapabilityDescriptionRecord(candidate, ignored) !=
        PayloadResult::OK) return PayloadResult::INVALID_FIELD;
    record = candidate;
    return PayloadResult::OK;
}

inline PayloadResult encodeDiagnosticPageRecord(
    const DiagnosticPageRecord& record, TypedValue& value
) {
    if (record.count > MAX_DIAGNOSTIC_PAGE_ENTRIES) {
        return PayloadResult::INVALID_FIELD;
    }
    TypedValue candidate = {};
    candidate.type = STRUCTURE_VALUE_TYPE;
    candidate.length = static_cast<uint8_t>(2 + 5 * record.count);
    candidate.bytes[0] = record.nextCursor;
    candidate.bytes[1] = record.count;
    for (size_t index = 0; index < record.count; ++index) {
        candidate.bytes[2 + 5 * index] = record.entries[index].metricId;
        writeUint32Le(candidate.bytes + 3 + 5 * index,
            record.entries[index].value);
    }
    value = candidate;
    return PayloadResult::OK;
}

inline PayloadResult decodeDiagnosticPageRecord(
    const TypedValue& value, DiagnosticPageRecord& record
) {
    if (value.type != STRUCTURE_VALUE_TYPE || value.length < 2 ||
        value.length > 17 || ((value.length - 2) % 5) != 0) {
        return PayloadResult::INVALID_VALUE;
    }
    DiagnosticPageRecord candidate = {};
    candidate.nextCursor = value.bytes[0];
    candidate.count = value.bytes[1];
    if (candidate.count > MAX_DIAGNOSTIC_PAGE_ENTRIES ||
        value.length != 2 + 5 * candidate.count) {
        return PayloadResult::INVALID_FIELD;
    }
    for (size_t index = 0; index < candidate.count; ++index) {
        candidate.entries[index].metricId = value.bytes[2 + 5 * index];
        candidate.entries[index].value = readUint32Le(
            value.bytes + 3 + 5 * index);
    }
    record = candidate;
    return PayloadResult::OK;
}

static_assert(MAX_REQUEST_VALUE_SIZE == 121,
    "Operation request value bound must be 121 bytes");
static_assert(MAX_RESPONSE_VALUE_SIZE == 119,
    "Operation response value bound must be 119 bytes");
static_assert(3 + 2 * MAX_CAPABILITY_PAGE_ENTRIES == 21,
    "Capability page must remain within the fixed record bound");
static_assert(2 + 5 * MAX_DIAGNOSTIC_PAGE_ENTRIES == 17,
    "Diagnostic page must remain within the fixed record bound");

}  // namespace HostProtocol
