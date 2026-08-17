#pragma once

#include <stddef.h>
#include <stdint.h>

#include <device_capabilities.h>

namespace HostProtocol {

constexpr uint8_t VERSION_MAJOR = 0;
constexpr uint8_t VERSION_MINOR = 1;
constexpr uint8_t FLAGS_NONE = 0x00;

constexpr uint16_t RESERVED_REQUEST_ID = 0x0000;
constexpr uint16_t MINIMUM_REQUEST_ID = 0x0001;
constexpr uint16_t MAXIMUM_REQUEST_ID = 0xFFFF;

constexpr uint16_t MAX_PAYLOAD_SIZE = 128;
constexpr uint16_t DECODED_HEADER_SIZE = 8;
constexpr uint16_t CRC_SIZE = 2;
constexpr uint16_t MIN_DECODED_FRAME_SIZE = 10;
constexpr uint16_t MAX_DECODED_FRAME_SIZE = 138;
constexpr uint16_t MAX_COBS_CANDIDATE_SIZE = 139;
constexpr uint16_t MAX_ENCODED_FRAME_SIZE = 140;
constexpr uint8_t HELLO_REQUEST_PAYLOAD_SIZE = 2;
constexpr uint8_t HELLO_RESPONSE_PAYLOAD_SIZE = 16;
constexpr uint8_t PROTOCOL_ERROR_PAYLOAD_SIZE = 4;
constexpr uint8_t MAX_OUTSTANDING_OPERATIONS = 1;
constexpr uint8_t HELLO_RESERVED_VALUE = 0;

enum class MessageType : uint8_t {
    HELLO_REQUEST = 0x01,
    HELLO_RESPONSE = 0x02,
    OPERATION_REQUEST = 0x10,
    OPERATION_RESPONSE = 0x11,
    PROTOCOL_ERROR = 0x7F
};

enum class OperationCategory : uint8_t {
    DEVICE = 0x01,
    CAPABILITY = 0x02,
    PROCEDURE = 0x03,
    DIAGNOSTIC = 0x04
};

enum class OperationCode : uint8_t {
    PING = 0x20,
    GET_DEVICE_INFO = 0x21,
    GET_STATUS = 0x22,
    GET_CAPABILITIES = 0x23,
    DESCRIBE_CAPABILITY = 0x24,
    READ_CAPABILITY = 0x25,
    SET_INDICATOR = 0x26,
    RUN_PROCEDURE = 0x27,
    GET_DIAGNOSTICS = 0x28
};

enum class ResultClass : uint8_t {
    SUCCESS = 0x00,
    REQUEST_REJECTED = 0x01,
    OPERATION_RESULT = 0x02,
    RADIO_RESULT = 0x03,
    LOCAL_RUNTIME_RESULT = 0x04
};

enum class SuccessCode : uint8_t {
    OK = 0x00
};

enum class RequestRejectionCode : uint8_t {
    MALFORMED_REQUEST = 0x01,
    UNSUPPORTED_OPERATION = 0x02,
    BAD_TARGET = 0x03,
    BUSY = 0x04,
    MISMATCH = 0x08
};

enum class RadioResultCode : uint8_t {
    TIMEOUT = 0x06,
    REMOTE_REJECTED = 0x07,
    MISMATCH = 0x08
};

enum class LocalRuntimeResultCode : uint8_t {
    BUSY = 0x04,
    OPERATION_FAILED = 0x0A
};

enum class ProtocolErrorCode : uint8_t {
    UNSUPPORTED_MAJOR = 0x01,
    UNSUPPORTED_MINOR = 0x02,
    UNSUPPORTED_MESSAGE_TYPE = 0x03,
    UNSUPPORTED_FLAGS = 0x04,
    MALFORMED_PAYLOAD = 0x05,
    INVALID_REQUEST_ID = 0x06
};

enum class HardwareProfile : uint8_t {
    HELTEC_V4 = 0x01
};

enum class DeviceRole : uint8_t {
    HUB = 0x01,
    NODE = 0x02
};

using CapabilityValueType = DeviceCapabilities::ValueType;
using OperationStatus = DeviceCapabilities::OperationStatus;
constexpr uint8_t STRUCTURE_VALUE_TYPE = 0x7F;

constexpr uint16_t CATEGORY_DEVICE_BIT = 0x0001;
constexpr uint16_t CATEGORY_CAPABILITY_BIT = 0x0002;
constexpr uint16_t CATEGORY_PROCEDURE_BIT = 0x0004;
constexpr uint16_t CATEGORY_DIAGNOSTIC_BIT = 0x0008;
constexpr uint16_t KNOWN_CATEGORY_BITMAP =
    CATEGORY_DEVICE_BIT |
    CATEGORY_CAPABILITY_BIT |
    CATEGORY_PROCEDURE_BIT |
    CATEGORY_DIAGNOSTIC_BIT;
constexpr uint16_t RESERVED_CATEGORY_BITMAP = 0xFFF0;

constexpr uint16_t FEATURE_LOCAL_OPERATIONS = 0x0001;
constexpr uint16_t FEATURE_RADIO_BRIDGE = 0x0002;
constexpr uint16_t KNOWN_FEATURE_BITMAP =
    FEATURE_LOCAL_OPERATIONS | FEATURE_RADIO_BRIDGE;
constexpr uint16_t RESERVED_FEATURE_BITMAP = 0xFFFC;

inline bool isSupportedVersion(uint8_t major, uint8_t minor) {
    return major == VERSION_MAJOR && minor == VERSION_MINOR;
}

inline bool isKnownMessageType(MessageType type) {
    switch (type) {
        case MessageType::HELLO_REQUEST:
        case MessageType::HELLO_RESPONSE:
        case MessageType::OPERATION_REQUEST:
        case MessageType::OPERATION_RESPONSE:
        case MessageType::PROTOCOL_ERROR:
            return true;
    }
    return false;
}

inline bool hasSupportedFlags(uint8_t flags) {
    return flags == FLAGS_NONE;
}

inline bool isReservedRequestId(uint16_t requestId) {
    return requestId == RESERVED_REQUEST_ID;
}

inline bool isValidRequestId(uint16_t requestId) {
    return requestId >= MINIMUM_REQUEST_ID;
}

inline bool isKnownOperationCategory(OperationCategory category) {
    switch (category) {
        case OperationCategory::DEVICE:
        case OperationCategory::CAPABILITY:
        case OperationCategory::PROCEDURE:
        case OperationCategory::DIAGNOSTIC:
            return true;
    }
    return false;
}

inline bool isKnownOperationCode(OperationCode operation) {
    switch (operation) {
        case OperationCode::PING:
        case OperationCode::GET_DEVICE_INFO:
        case OperationCode::GET_STATUS:
        case OperationCode::GET_CAPABILITIES:
        case OperationCode::DESCRIBE_CAPABILITY:
        case OperationCode::READ_CAPABILITY:
        case OperationCode::SET_INDICATOR:
        case OperationCode::RUN_PROCEDURE:
        case OperationCode::GET_DIAGNOSTICS:
            return true;
    }
    return false;
}

inline bool isSupportedCategoryOperation(
    OperationCategory category,
    OperationCode operation
) {
    switch (category) {
        case OperationCategory::DEVICE:
            return operation == OperationCode::PING ||
                operation == OperationCode::GET_DEVICE_INFO ||
                operation == OperationCode::GET_STATUS;
        case OperationCategory::CAPABILITY:
            return operation == OperationCode::GET_CAPABILITIES ||
                operation == OperationCode::DESCRIBE_CAPABILITY ||
                operation == OperationCode::READ_CAPABILITY ||
                operation == OperationCode::SET_INDICATOR;
        case OperationCategory::PROCEDURE:
            return operation == OperationCode::RUN_PROCEDURE;
        case OperationCategory::DIAGNOSTIC:
            return operation == OperationCode::GET_DIAGNOSTICS;
    }
    return false;
}

inline bool isKnownResultClass(ResultClass resultClass) {
    switch (resultClass) {
        case ResultClass::SUCCESS:
        case ResultClass::REQUEST_REJECTED:
        case ResultClass::OPERATION_RESULT:
        case ResultClass::RADIO_RESULT:
        case ResultClass::LOCAL_RUNTIME_RESULT:
            return true;
    }
    return false;
}

inline bool isValidResultCode(ResultClass resultClass, uint8_t code) {
    switch (resultClass) {
        case ResultClass::SUCCESS:
            return code == static_cast<uint8_t>(SuccessCode::OK);
        case ResultClass::REQUEST_REJECTED:
            switch (static_cast<RequestRejectionCode>(code)) {
                case RequestRejectionCode::MALFORMED_REQUEST:
                case RequestRejectionCode::UNSUPPORTED_OPERATION:
                case RequestRejectionCode::BAD_TARGET:
                case RequestRejectionCode::BUSY:
                case RequestRejectionCode::MISMATCH:
                    return true;
            }
            return false;
        case ResultClass::OPERATION_RESULT:
            return DeviceCapabilities::isKnownOperationStatus(
                static_cast<OperationStatus>(code)
            );
        case ResultClass::RADIO_RESULT:
            switch (static_cast<RadioResultCode>(code)) {
                case RadioResultCode::TIMEOUT:
                case RadioResultCode::REMOTE_REJECTED:
                case RadioResultCode::MISMATCH:
                    return true;
            }
            return false;
        case ResultClass::LOCAL_RUNTIME_RESULT:
            return code == static_cast<uint8_t>(LocalRuntimeResultCode::BUSY) ||
                code == static_cast<uint8_t>(
                    LocalRuntimeResultCode::OPERATION_FAILED
                );
    }
    return false;
}

inline bool isKnownProtocolError(ProtocolErrorCode error) {
    switch (error) {
        case ProtocolErrorCode::UNSUPPORTED_MAJOR:
        case ProtocolErrorCode::UNSUPPORTED_MINOR:
        case ProtocolErrorCode::UNSUPPORTED_MESSAGE_TYPE:
        case ProtocolErrorCode::UNSUPPORTED_FLAGS:
        case ProtocolErrorCode::MALFORMED_PAYLOAD:
        case ProtocolErrorCode::INVALID_REQUEST_ID:
            return true;
    }
    return false;
}

inline bool isValidHelloMinorRange(uint8_t minimum, uint8_t maximum) {
    return minimum == VERSION_MINOR && maximum == VERSION_MINOR;
}

inline bool hasValidCategoryBitmap(uint16_t bitmap) {
    return (bitmap & RESERVED_CATEGORY_BITMAP) == 0;
}

inline bool hasValidFeatureBitmap(uint16_t bitmap) {
    return (bitmap & RESERVED_FEATURE_BITMAP) == 0;
}

inline bool isKnownHardwareProfile(HardwareProfile profile) {
    return profile == HardwareProfile::HELTEC_V4;
}

inline bool isKnownDeviceRole(DeviceRole role) {
    return role == DeviceRole::HUB || role == DeviceRole::NODE;
}

inline bool isKnownHostValueType(uint8_t valueType) {
    return valueType == STRUCTURE_VALUE_TYPE ||
        DeviceCapabilities::isKnownValueType(
            static_cast<CapabilityValueType>(valueType)
        );
}

constexpr uint8_t FRAME_DELIMITER = 0x00;
constexpr uint16_t CRC16_CCITT_FALSE_POLYNOMIAL = 0x1021;
constexpr uint16_t CRC16_CCITT_FALSE_INITIAL = 0xFFFF;

struct Frame {
    uint8_t major;
    uint8_t minor;
    MessageType messageType;
    uint8_t flags;
    uint16_t requestId;
    uint16_t payloadLength;
    uint8_t payload[MAX_PAYLOAD_SIZE];
};

enum class CobsResult : uint8_t {
    OK = 0x00,
    NULL_ARGUMENT = 0x01,
    INPUT_TOO_LARGE = 0x02,
    OUTPUT_TOO_SMALL = 0x03,
    MALFORMED_INPUT = 0x04
};

enum class EncodeResult : uint8_t {
    OK = 0x00,
    NULL_ARGUMENT = 0x01,
    PAYLOAD_TOO_LARGE = 0x02,
    UNSUPPORTED_VERSION = 0x03,
    UNSUPPORTED_MESSAGE_TYPE = 0x04,
    UNSUPPORTED_FLAGS = 0x05,
    INVALID_REQUEST_ID = 0x06,
    OUTPUT_TOO_SMALL = 0x07
};

enum class DecodeResult : uint8_t {
    OK = 0x00,
    NULL_ARGUMENT = 0x01,
    EMPTY_FRAME = 0x02,
    ENCODED_FRAME_TOO_LARGE = 0x03,
    MISSING_DELIMITER = 0x04,
    MALFORMED_COBS = 0x05,
    DECODED_LENGTH_INVALID = 0x06,
    PAYLOAD_TOO_LARGE = 0x07,
    LENGTH_MISMATCH = 0x08,
    CRC_MISMATCH = 0x09,
    UNSUPPORTED_VERSION = 0x0A,
    UNSUPPORTED_MESSAGE_TYPE = 0x0B,
    UNSUPPORTED_FLAGS = 0x0C,
    INVALID_REQUEST_ID = 0x0D,
    UNSUPPORTED_MAJOR = 0x0E,
    UNSUPPORTED_MINOR = 0x0F
};

inline uint16_t crc16CcittFalse(const uint8_t* input, size_t inputLength) {
    uint16_t crc = CRC16_CCITT_FALSE_INITIAL;
    if (input == nullptr && inputLength != 0) {
        return crc;
    }

    for (size_t index = 0; index < inputLength; ++index) {
        crc ^= static_cast<uint16_t>(input[index]) << 8;
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000U) != 0
                ? static_cast<uint16_t>((crc << 1) ^
                    CRC16_CCITT_FALSE_POLYNOMIAL)
                : static_cast<uint16_t>(crc << 1);
        }
    }
    return crc;
}

inline CobsResult cobsEncode(
    const uint8_t* input,
    size_t inputLength,
    uint8_t* output,
    size_t outputCapacity,
    size_t& outputLength
) {
    outputLength = 0;
    if ((input == nullptr && inputLength != 0) || output == nullptr) {
        return CobsResult::NULL_ARGUMENT;
    }
    if (inputLength > MAX_DECODED_FRAME_SIZE) {
        return CobsResult::INPUT_TOO_LARGE;
    }
    if (outputCapacity == 0) {
        return CobsResult::OUTPUT_TOO_SMALL;
    }

    size_t codeIndex = 0;
    size_t writeIndex = 1;
    uint8_t code = 1;
    while (inputLength-- != 0) {
        const uint8_t value = *input++;
        if (value == 0) {
            if (codeIndex >= outputCapacity) {
                return CobsResult::OUTPUT_TOO_SMALL;
            }
            output[codeIndex] = code;
            codeIndex = writeIndex;
            if (writeIndex >= outputCapacity) {
                return CobsResult::OUTPUT_TOO_SMALL;
            }
            ++writeIndex;
            code = 1;
        } else {
            if (writeIndex >= outputCapacity) {
                return CobsResult::OUTPUT_TOO_SMALL;
            }
            output[writeIndex++] = value;
            ++code;
            if (code == 0xFF) {
                output[codeIndex] = code;
                codeIndex = writeIndex;
                if (writeIndex >= outputCapacity) {
                    return CobsResult::OUTPUT_TOO_SMALL;
                }
                ++writeIndex;
                code = 1;
            }
        }
    }
    if (codeIndex >= outputCapacity) {
        return CobsResult::OUTPUT_TOO_SMALL;
    }
    output[codeIndex] = code;
    outputLength = writeIndex;
    return CobsResult::OK;
}

inline CobsResult cobsDecode(
    const uint8_t* input,
    size_t inputLength,
    uint8_t* output,
    size_t outputCapacity,
    size_t& outputLength
) {
    outputLength = 0;
    if (input == nullptr || output == nullptr) {
        return CobsResult::NULL_ARGUMENT;
    }
    if (inputLength == 0) {
        return CobsResult::MALFORMED_INPUT;
    }
    if (inputLength > MAX_COBS_CANDIDATE_SIZE) {
        return CobsResult::INPUT_TOO_LARGE;
    }

    size_t readIndex = 0;
    size_t writeIndex = 0;
    while (readIndex < inputLength) {
        const uint8_t code = input[readIndex++];
        if (code == 0) {
            return CobsResult::MALFORMED_INPUT;
        }
        const size_t blockLength = static_cast<size_t>(code - 1);
        if (blockLength > inputLength - readIndex) {
            return CobsResult::MALFORMED_INPUT;
        }
        if (blockLength > outputCapacity - writeIndex) {
            return CobsResult::OUTPUT_TOO_SMALL;
        }
        for (size_t index = 0; index < blockLength; ++index) {
            const uint8_t value = input[readIndex++];
            if (value == 0) {
                return CobsResult::MALFORMED_INPUT;
            }
            output[writeIndex++] = value;
        }
        if (code != 0xFF && readIndex < inputLength) {
            if (writeIndex >= outputCapacity) {
                return CobsResult::OUTPUT_TOO_SMALL;
            }
            output[writeIndex++] = 0;
        }
    }
    outputLength = writeIndex;
    return CobsResult::OK;
}

inline bool isValidEnvelopeRequestId(
    MessageType messageType,
    uint16_t requestId
) {
    return isValidRequestId(requestId) ||
        (messageType == MessageType::PROTOCOL_ERROR &&
            isReservedRequestId(requestId));
}

inline EncodeResult encodeFrame(
    const Frame& frame,
    uint8_t* output,
    size_t outputCapacity,
    size_t& outputLength
) {
    outputLength = 0;
    if (output == nullptr) {
        return EncodeResult::NULL_ARGUMENT;
    }
    if (frame.payloadLength > MAX_PAYLOAD_SIZE) {
        return EncodeResult::PAYLOAD_TOO_LARGE;
    }
    if (!isSupportedVersion(frame.major, frame.minor)) {
        return EncodeResult::UNSUPPORTED_VERSION;
    }
    if (!isKnownMessageType(frame.messageType)) {
        return EncodeResult::UNSUPPORTED_MESSAGE_TYPE;
    }
    if (!hasSupportedFlags(frame.flags)) {
        return EncodeResult::UNSUPPORTED_FLAGS;
    }
    if (!isValidEnvelopeRequestId(frame.messageType, frame.requestId)) {
        return EncodeResult::INVALID_REQUEST_ID;
    }

    const size_t decodedLength = MIN_DECODED_FRAME_SIZE + frame.payloadLength;
    uint8_t decoded[MAX_DECODED_FRAME_SIZE] = {};
    decoded[0] = frame.major;
    decoded[1] = frame.minor;
    decoded[2] = static_cast<uint8_t>(frame.messageType);
    decoded[3] = frame.flags;
    decoded[4] = static_cast<uint8_t>(frame.requestId);
    decoded[5] = static_cast<uint8_t>(frame.requestId >> 8);
    decoded[6] = static_cast<uint8_t>(frame.payloadLength);
    decoded[7] = static_cast<uint8_t>(frame.payloadLength >> 8);
    for (size_t index = 0; index < frame.payloadLength; ++index) {
        decoded[DECODED_HEADER_SIZE + index] = frame.payload[index];
    }
    const uint16_t crc = crc16CcittFalse(
        decoded,
        DECODED_HEADER_SIZE + frame.payloadLength
    );
    decoded[DECODED_HEADER_SIZE + frame.payloadLength] =
        static_cast<uint8_t>(crc);
    decoded[DECODED_HEADER_SIZE + frame.payloadLength + 1] =
        static_cast<uint8_t>(crc >> 8);

    if (outputCapacity == 0) {
        return EncodeResult::OUTPUT_TOO_SMALL;
    }
    size_t candidateLength = 0;
    const CobsResult cobsResult = cobsEncode(
        decoded,
        decodedLength,
        output,
        outputCapacity - 1,
        candidateLength
    );
    if (cobsResult != CobsResult::OK) {
        return cobsResult == CobsResult::OUTPUT_TOO_SMALL
            ? EncodeResult::OUTPUT_TOO_SMALL
            : EncodeResult::NULL_ARGUMENT;
    }
    if (candidateLength >= outputCapacity) {
        return EncodeResult::OUTPUT_TOO_SMALL;
    }
    output[candidateLength] = FRAME_DELIMITER;
    outputLength = candidateLength + 1;
    return EncodeResult::OK;
}

inline DecodeResult decodeFrame(
    const uint8_t* input,
    size_t inputLength,
    Frame& frame
) {
    if (input == nullptr) {
        return DecodeResult::NULL_ARGUMENT;
    }
    if (inputLength == 0) {
        return DecodeResult::EMPTY_FRAME;
    }
    if (inputLength > MAX_ENCODED_FRAME_SIZE) {
        return DecodeResult::ENCODED_FRAME_TOO_LARGE;
    }
    if (input[inputLength - 1] != FRAME_DELIMITER) {
        return DecodeResult::MISSING_DELIMITER;
    }
    const size_t candidateLength = inputLength - 1;
    if (candidateLength == 0) {
        return DecodeResult::EMPTY_FRAME;
    }

    uint8_t decoded[MAX_DECODED_FRAME_SIZE] = {};
    size_t decodedLength = 0;
    const CobsResult cobsResult = cobsDecode(
        input,
        candidateLength,
        decoded,
        sizeof(decoded),
        decodedLength
    );
    if (cobsResult != CobsResult::OK) {
        return cobsResult == CobsResult::INPUT_TOO_LARGE
            ? DecodeResult::ENCODED_FRAME_TOO_LARGE
            : DecodeResult::MALFORMED_COBS;
    }
    if (decodedLength < MIN_DECODED_FRAME_SIZE ||
        decodedLength > MAX_DECODED_FRAME_SIZE) {
        return DecodeResult::DECODED_LENGTH_INVALID;
    }

    const uint16_t payloadLength = static_cast<uint16_t>(decoded[6]) |
        (static_cast<uint16_t>(decoded[7]) << 8);
    if (payloadLength > MAX_PAYLOAD_SIZE) {
        return DecodeResult::PAYLOAD_TOO_LARGE;
    }
    const size_t expectedLength = MIN_DECODED_FRAME_SIZE + payloadLength;
    if (decodedLength != expectedLength) {
        return DecodeResult::LENGTH_MISMATCH;
    }
    const size_t crcIndex = DECODED_HEADER_SIZE + payloadLength;
    const uint16_t storedCrc = static_cast<uint16_t>(decoded[crcIndex]) |
        (static_cast<uint16_t>(decoded[crcIndex + 1]) << 8);
    if (crc16CcittFalse(decoded, crcIndex) != storedCrc) {
        return DecodeResult::CRC_MISMATCH;
    }

    const uint8_t major = decoded[0];
    const uint8_t minor = decoded[1];
    const MessageType messageType = static_cast<MessageType>(decoded[2]);
    const uint8_t flags = decoded[3];
    const uint16_t requestId = static_cast<uint16_t>(decoded[4]) |
        (static_cast<uint16_t>(decoded[5]) << 8);
    Frame candidate = {};
    candidate.major = major;
    candidate.minor = minor;
    candidate.messageType = messageType;
    candidate.flags = flags;
    candidate.requestId = requestId;
    candidate.payloadLength = payloadLength;
    for (size_t index = 0; index < payloadLength; ++index) {
        candidate.payload[index] = decoded[DECODED_HEADER_SIZE + index];
    }
    // Preserve the trustworthy CRC-checked envelope for deterministic error
    // reporting even when a later vocabulary check rejects it.
    frame = candidate;
    if (major != VERSION_MAJOR) {
        return DecodeResult::UNSUPPORTED_MAJOR;
    }
    if (minor != VERSION_MINOR) {
        return DecodeResult::UNSUPPORTED_MINOR;
    }
    if (!isKnownMessageType(messageType)) {
        return DecodeResult::UNSUPPORTED_MESSAGE_TYPE;
    }
    if (!hasSupportedFlags(flags)) {
        return DecodeResult::UNSUPPORTED_FLAGS;
    }
    if (!isValidEnvelopeRequestId(messageType, requestId)) {
        return DecodeResult::INVALID_REQUEST_ID;
    }

    return DecodeResult::OK;
}

static_assert(UINT8_MAX == 0xFF, "Host Protocol requires 8-bit uint8_t");
static_assert(UINT16_MAX == 0xFFFF, "Host Protocol requires 16-bit uint16_t");
static_assert(sizeof(MessageType) == 1, "MessageType must be 8 bits");
static_assert(sizeof(OperationCategory) == 1,
    "OperationCategory must be 8 bits");
static_assert(sizeof(OperationCode) == 1, "OperationCode must be 8 bits");
static_assert(sizeof(ResultClass) == 1, "ResultClass must be 8 bits");
static_assert(sizeof(ProtocolErrorCode) == 1,
    "ProtocolErrorCode must be 8 bits");
static_assert(MAX_DECODED_FRAME_SIZE ==
    MIN_DECODED_FRAME_SIZE + MAX_PAYLOAD_SIZE,
    "Decoded frame bound must cover the maximum payload");
static_assert(MAX_COBS_CANDIDATE_SIZE == MAX_DECODED_FRAME_SIZE + 1,
    "COBS candidate bound must cover worst-case expansion");
static_assert(MAX_ENCODED_FRAME_SIZE == MAX_COBS_CANDIDATE_SIZE + 1,
    "Encoded frame bound must include exactly one delimiter");
static_assert(sizeof(Frame::payload) == MAX_PAYLOAD_SIZE,
    "Frame must own exactly one maximum Host payload");

}  // namespace HostProtocol
