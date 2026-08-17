#pragma once

#include <stddef.h>
#include <stdint.h>

#include "host_protocol_messages.h"
#include "protocol.h"

namespace WireOperations {

constexpr uint8_t REQUEST_FIXED_SIZE = 4;
constexpr uint8_t RESPONSE_FIXED_SIZE = 5;
constexpr uint8_t MAX_REQUEST_VALUE_SIZE =
    Protocol::MAX_PAYLOAD_SIZE - REQUEST_FIXED_SIZE;
constexpr uint8_t MAX_RESPONSE_VALUE_SIZE =
    Protocol::MAX_PAYLOAD_SIZE - RESPONSE_FIXED_SIZE;
constexpr uint8_t STRUCTURE_VALUE_TYPE = HostProtocol::STRUCTURE_VALUE_TYPE;

using ValueType = DeviceCapabilities::ValueType;
using OperationStatus = DeviceCapabilities::OperationStatus;

struct Value {
    uint8_t type;
    uint8_t length;
    uint8_t bytes[MAX_REQUEST_VALUE_SIZE];
};

struct Request {
    uint16_t targetId;
    Value value;
};

struct Response {
    OperationStatus status;
    uint16_t targetId;
    Value value;
};

enum class CodecResult : uint8_t {
    OK,
    NULL_ARGUMENT,
    UNSUPPORTED_OPCODE,
    INVALID_LENGTH,
    INVALID_TARGET,
    INVALID_VALUE_TYPE,
    INVALID_VALUE,
    INVALID_STATUS,
    OUTPUT_TOO_SMALL
};

inline bool isStructuredOpcode(uint8_t opcode) {
    return opcode >= static_cast<uint8_t>(Protocol::Opcode::PING) &&
        opcode <= static_cast<uint8_t>(Protocol::Opcode::GET_DIAGNOSTICS);
}

inline uint8_t scalarLength(uint8_t type) {
    return HostProtocol::expectedScalarValueLength(type);
}

inline bool isScalarValue(const Value& value) {
    if (value.type == STRUCTURE_VALUE_TYPE ||
        !DeviceCapabilities::isKnownValueType(
            static_cast<ValueType>(value.type))) return false;
    if (value.length != scalarLength(value.type)) return false;
    return value.type != static_cast<uint8_t>(ValueType::BOOLEAN) ||
        value.bytes[0] <= 1;
}

inline bool isNone(const Value& value) {
    return value.type == static_cast<uint8_t>(ValueType::NONE) &&
        value.length == 0;
}

inline bool isUint32(const Value& value) {
    return value.type == static_cast<uint8_t>(ValueType::UNSIGNED_32) &&
        isScalarValue(value);
}

inline bool isValidRequestSchema(uint8_t opcode, const Request& request) {
    if (!isStructuredOpcode(opcode) || !isScalarValue(request.value)) {
        return false;
    }
    switch (static_cast<Protocol::Opcode>(opcode)) {
        case Protocol::Opcode::PING:
        case Protocol::Opcode::GET_DEVICE_INFO:
        case Protocol::Opcode::GET_STATUS:
            return request.targetId == 0 && isNone(request.value);
        case Protocol::Opcode::GET_CAPABILITIES:
        case Protocol::Opcode::GET_DIAGNOSTICS:
            return request.targetId == 0 &&
                (isNone(request.value) || isUint32(request.value));
        case Protocol::Opcode::DESCRIBE_CAPABILITY:
        case Protocol::Opcode::READ_CAPABILITY:
            return request.targetId != 0 && isNone(request.value);
        case Protocol::Opcode::SET_INDICATOR:
            return request.targetId != 0 &&
                request.value.type == static_cast<uint8_t>(ValueType::BOOLEAN);
        case Protocol::Opcode::RUN_PROCEDURE:
            return request.targetId != 0;
        case Protocol::Opcode::TEST:
            break;
    }
    return false;
}

inline CodecResult encodeRequest(
    uint8_t opcode,
    const Request& request,
    uint8_t* output,
    size_t capacity,
    size_t& length
) {
    length = 0;
    if (output == nullptr) return CodecResult::NULL_ARGUMENT;
    if (!isStructuredOpcode(opcode)) return CodecResult::UNSUPPORTED_OPCODE;
    if (!isValidRequestSchema(opcode, request)) return CodecResult::INVALID_VALUE;
    const size_t required = REQUEST_FIXED_SIZE + request.value.length;
    if (required > Protocol::MAX_PAYLOAD_SIZE) return CodecResult::INVALID_LENGTH;
    if (capacity < required) return CodecResult::OUTPUT_TOO_SMALL;
    HostProtocol::writeUint16Le(output, request.targetId);
    output[2] = request.value.type;
    output[3] = request.value.length;
    for (size_t i = 0; i < request.value.length; ++i) output[4 + i] = request.value.bytes[i];
    length = required;
    return CodecResult::OK;
}

inline CodecResult decodeRequest(
    uint8_t opcode,
    const uint8_t* input,
    size_t inputLength,
    Request& request
) {
    if (input == nullptr) return CodecResult::NULL_ARGUMENT;
    if (!isStructuredOpcode(opcode)) return CodecResult::UNSUPPORTED_OPCODE;
    if (inputLength < REQUEST_FIXED_SIZE || inputLength > Protocol::MAX_PAYLOAD_SIZE)
        return CodecResult::INVALID_LENGTH;
    const uint8_t valueLength = input[3];
    if (valueLength > MAX_REQUEST_VALUE_SIZE ||
        inputLength != REQUEST_FIXED_SIZE + valueLength)
        return CodecResult::INVALID_LENGTH;
    Request candidate = {};
    candidate.targetId = HostProtocol::readUint16Le(input);
    candidate.value.type = input[2];
    candidate.value.length = valueLength;
    for (size_t i = 0; i < valueLength; ++i) candidate.value.bytes[i] = input[4 + i];
    if (!DeviceCapabilities::isKnownValueType(
            static_cast<ValueType>(candidate.value.type)) ||
        candidate.value.type == STRUCTURE_VALUE_TYPE)
        return CodecResult::INVALID_VALUE_TYPE;
    if (!isValidRequestSchema(opcode, candidate)) return CodecResult::INVALID_VALUE;
    request = candidate;
    return CodecResult::OK;
}

inline bool toHostValue(const Value& input, HostProtocol::TypedValue& output) {
    if (input.length > MAX_REQUEST_VALUE_SIZE) return false;
    output = {};
    output.type = input.type;
    output.length = input.length;
    for (size_t i = 0; i < input.length; ++i) output.bytes[i] = input.bytes[i];
    return true;
}

inline bool isValidSuccessValue(uint8_t opcode, const Value& value) {
    HostProtocol::TypedValue host = {};
    if (!toHostValue(value, host)) return false;
    return HostProtocol::isValidSuccessfulOperationValue(
        static_cast<HostProtocol::OperationCode>(opcode), host);
}

inline bool isValidResponseSchema(uint8_t opcode, const Response& response) {
    if (!isStructuredOpcode(opcode) ||
        !DeviceCapabilities::isKnownOperationStatus(response.status)) return false;
    const bool zeroTargetOperation =
        opcode == static_cast<uint8_t>(Protocol::Opcode::PING) ||
        opcode == static_cast<uint8_t>(Protocol::Opcode::GET_DEVICE_INFO) ||
        opcode == static_cast<uint8_t>(Protocol::Opcode::GET_STATUS) ||
        opcode == static_cast<uint8_t>(Protocol::Opcode::GET_CAPABILITIES) ||
        opcode == static_cast<uint8_t>(Protocol::Opcode::GET_DIAGNOSTICS);
    if ((zeroTargetOperation && response.targetId != 0) ||
        (!zeroTargetOperation && response.targetId == 0)) return false;
    if (response.status != OperationStatus::OK) return isNone(response.value);
    return isValidSuccessValue(opcode, response.value);
}

inline CodecResult encodeResponse(
    uint8_t opcode,
    const Response& response,
    uint8_t* output,
    size_t capacity,
    size_t& length
) {
    length = 0;
    if (output == nullptr) return CodecResult::NULL_ARGUMENT;
    if (!isStructuredOpcode(opcode)) return CodecResult::UNSUPPORTED_OPCODE;
    if (!isValidResponseSchema(opcode, response)) return CodecResult::INVALID_VALUE;
    const size_t required = RESPONSE_FIXED_SIZE + response.value.length;
    if (required > Protocol::MAX_PAYLOAD_SIZE) return CodecResult::INVALID_LENGTH;
    if (capacity < required) return CodecResult::OUTPUT_TOO_SMALL;
    output[0] = static_cast<uint8_t>(response.status);
    HostProtocol::writeUint16Le(output + 1, response.targetId);
    output[3] = response.value.type;
    output[4] = response.value.length;
    for (size_t i = 0; i < response.value.length; ++i) output[5 + i] = response.value.bytes[i];
    length = required;
    return CodecResult::OK;
}

inline CodecResult decodeResponse(
    uint8_t opcode,
    const uint8_t* input,
    size_t inputLength,
    Response& response
) {
    if (input == nullptr) return CodecResult::NULL_ARGUMENT;
    if (!isStructuredOpcode(opcode)) return CodecResult::UNSUPPORTED_OPCODE;
    if (inputLength < RESPONSE_FIXED_SIZE || inputLength > Protocol::MAX_PAYLOAD_SIZE)
        return CodecResult::INVALID_LENGTH;
    const uint8_t valueLength = input[4];
    if (valueLength > MAX_RESPONSE_VALUE_SIZE ||
        inputLength != RESPONSE_FIXED_SIZE + valueLength)
        return CodecResult::INVALID_LENGTH;
    Response candidate = {};
    candidate.status = static_cast<OperationStatus>(input[0]);
    candidate.targetId = HostProtocol::readUint16Le(input + 1);
    candidate.value.type = input[3];
    candidate.value.length = valueLength;
    for (size_t i = 0; i < valueLength; ++i) candidate.value.bytes[i] = input[5 + i];
    if (!DeviceCapabilities::isKnownOperationStatus(candidate.status))
        return CodecResult::INVALID_STATUS;
    if (!isValidResponseSchema(opcode, candidate)) return CodecResult::INVALID_VALUE;
    response = candidate;
    return CodecResult::OK;
}

inline Protocol::Packet makeCommand(
    uint8_t source, uint8_t destination, uint8_t sequence,
    uint8_t opcode, const Request& request
) {
    Protocol::Packet packet = {};
    packet.type = Protocol::PacketType::COMMAND;
    packet.source = source; packet.destination = destination;
    packet.sequence = sequence; packet.opcode = opcode;
    size_t length = 0;
    if (encodeRequest(opcode, request, packet.payload,
            sizeof(packet.payload), length) == CodecResult::OK)
        packet.payloadLength = static_cast<uint8_t>(length);
    return packet;
}

inline Protocol::Packet makeResponsePacket(
    const Protocol::Packet& command, const Response& response
) {
    Protocol::Packet packet = {};
    packet.type = Protocol::PacketType::RESPONSE;
    packet.source = command.destination; packet.destination = command.source;
    packet.sequence = command.sequence; packet.opcode = command.opcode;
    size_t length = 0;
    if (encodeResponse(command.opcode, response, packet.payload,
            sizeof(packet.payload), length) == CodecResult::OK)
        packet.payloadLength = static_cast<uint8_t>(length);
    return packet;
}

inline DeviceCapabilities::OperationResult processRemoteCapability(
    const DeviceCapabilities::CapabilityRegistryView& registry,
    DeviceCapabilities::LocalCapabilityHandler& handler,
    DeviceCapabilities::CapabilityId capabilityId,
    DeviceCapabilities::Operation operation,
    const DeviceCapabilities::CapabilityValue& input,
    DeviceCapabilities::InterlockState interlock,
    DeviceCapabilities::CapabilityDiagnostics& diagnostics
) {
    using namespace DeviceCapabilities;
    OperationResult result = {};
    if (!isValidCapabilityRegistry(registry)) {
        result = makeCanonicalFailureResult(OperationStatus::INVALID_DESCRIPTOR);
    } else {
        CapabilityDescriptor descriptor = {};
        if (!findCapability(registry, capabilityId, descriptor)) {
            result = makeCanonicalFailureResult(OperationStatus::CAPABILITY_NOT_FOUND);
        } else {
            CallerContext caller = {};
            caller.callerClass = CallerClass::FUTURE_REMOTE;
            if (operation == Operation::SET ||
                operation == Operation::RUN_LOCAL_PROCEDURE) {
                result = makeCanonicalFailureResult(OperationStatus::UNAUTHORIZED);
            } else if (!isHandlerDispatchableOperation(operation) ||
                !supportsOperation(descriptor, operation)) {
                result = makeCanonicalFailureResult(OperationStatus::UNSUPPORTED_OPERATION);
            } else if (!isOperationAuthorized(descriptor, operation, caller)) {
                result = makeCanonicalFailureResult(OperationStatus::UNAUTHORIZED);
            } else if (operation == Operation::SET &&
                (!isValidCapabilityValue(input) || input.type != descriptor.valueType)) {
                result = makeCanonicalFailureResult(OperationStatus::INVALID_VALUE_TYPE);
            } else if (operation == Operation::SET &&
                !isValueWithinBounds(descriptor, input)) {
                result = makeCanonicalFailureResult(OperationStatus::VALUE_OUT_OF_RANGE);
            } else if (operation != Operation::SET && !isCanonicalNoneValue(input)) {
                result = makeCanonicalFailureResult(OperationStatus::INVALID_VALUE_TYPE);
            } else if (!isOperationSafe(descriptor, operation, interlock)) {
                result = makeCanonicalFailureResult(OperationStatus::INTERLOCK_ACTIVE);
            } else {
                result = handler.execute(descriptor, operation, input);
                if (!isValidOperationResult(result) ||
                    !isHandlerOwnedStatus(result.status) ||
                    (result.status == OperationStatus::OK && operation == Operation::READ &&
                     !isValueWithinBounds(descriptor, result.value)) ||
                    (result.status == OperationStatus::OK && operation != Operation::READ &&
                     !isCanonicalNoneValue(result.value))) {
                    result = makeCanonicalFailureResult(OperationStatus::OPERATION_FAILED);
                }
            }
        }
    }
    diagnostics.recordOutcome(result.status);
    return result;
}

static_assert(MAX_REQUEST_VALUE_SIZE == 22, "Wire request value bound");
static_assert(MAX_RESPONSE_VALUE_SIZE == 21, "Wire response value bound");
static_assert(5 + 3 + 2 * HostProtocol::MAX_CAPABILITY_PAGE_ENTRIES == 26,
    "Nine capability IDs must exactly fit Wire payload");
static_assert(5 + 2 + 5 * HostProtocol::MAX_DIAGNOSTIC_PAGE_ENTRIES <= 26,
    "Three diagnostics must fit Wire payload");
static_assert(static_cast<uint8_t>(Protocol::Opcode::PING) ==
    static_cast<uint8_t>(HostProtocol::OperationCode::PING), "Shared opcode registry");
static_assert(static_cast<uint8_t>(Protocol::Opcode::GET_DIAGNOSTICS) ==
    static_cast<uint8_t>(HostProtocol::OperationCode::GET_DIAGNOSTICS), "Shared opcode registry");

}  // namespace WireOperations
