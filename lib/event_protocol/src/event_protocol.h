#pragma once

#include <stddef.h>
#include <stdint.h>

#include "../../../include/protocol.h"

namespace EventProtocol {

constexpr size_t COMMON_PAYLOAD_SIZE = 14;
constexpr size_t MAX_BODY_SIZE = 12;
constexpr size_t ADMISSION_PAYLOAD_SIZE = 9;

constexpr uint8_t IMPORTANT_FLAG = 0x01;
constexpr uint8_t ALLOWED_FLAGS = IMPORTANT_FLAG;

constexpr uint32_t MIN_LIFETIME_SECONDS = 60;
constexpr uint32_t MAX_LIFETIME_SECONDS = 86400;

enum class Family : uint8_t {
    BUTTON = 0x40,
    SENSOR_THRESHOLD = 0x41,
    MANUAL_CHECK_IN = 0x44
};

enum class ButtonEvent : uint8_t {
    PRESS = 0x01,
    RELEASE = 0x02,
    SHORT_PRESS = 0x03,
    LONG_PRESS = 0x04,
    VERY_LONG_PRESS = 0x05
};

enum class SensorValueType : uint8_t {
    UNSIGNED_32 = 0x02,
    SIGNED_32 = 0x03,
    NORMALIZED_U16 = 0x04,
    FIXED_Q16_16 = 0x05,
    ENUM_U16 = 0x06
};

enum class ThresholdRelation : uint8_t {
    CROSSED_BELOW = 0x01,
    CROSSED_ABOVE = 0x02
};

enum class ManualReason : uint8_t {
    USER_REQUEST = 0x01
};

enum class AdmissionStatus : uint8_t {
    ADMITTED = 0x00,
    CAPACITY = 0x01,
    IDENTITY_CONTENT_MISMATCH = 0x02,
    UNSUPPORTED_EVENT = 0x03,
    MALFORMED_EVENT = 0x04
};

struct Identity {
    uint8_t sourceDeviceId;
    uint32_t epoch;
    uint32_t id;
};

struct Event {
    uint8_t source;
    uint8_t destination;
    uint8_t sequence;
    uint8_t family;
    uint32_t epoch;
    uint32_t id;
    uint8_t flags;
    uint32_t lifetimeBudgetSeconds;
    uint8_t bodyLength;
    uint8_t body[MAX_BODY_SIZE];
};

struct AdmissionResponse {
    uint8_t source;
    uint8_t destination;
    uint8_t sequence;
    uint8_t family;
    AdmissionStatus status;
    uint32_t epoch;
    uint32_t id;
};

inline void writeUint32Le(uint8_t* output, uint32_t value) {
    output[0] = static_cast<uint8_t>(value & 0xFFU);
    output[1] = static_cast<uint8_t>((value >> 8) & 0xFFU);
    output[2] = static_cast<uint8_t>((value >> 16) & 0xFFU);
    output[3] = static_cast<uint8_t>((value >> 24) & 0xFFU);
}

inline uint32_t readUint32Le(const uint8_t* input) {
    return static_cast<uint32_t>(input[0]) |
        (static_cast<uint32_t>(input[1]) << 8) |
        (static_cast<uint32_t>(input[2]) << 16) |
        (static_cast<uint32_t>(input[3]) << 24);
}

inline bool isRegisteredFamily(uint8_t family) {
    return family == static_cast<uint8_t>(Family::BUTTON) ||
        family == static_cast<uint8_t>(Family::SENSOR_THRESHOLD) ||
        family == static_cast<uint8_t>(Family::MANUAL_CHECK_IN);
}

inline size_t expectedBodyLength(uint8_t family) {
    switch (static_cast<Family>(family)) {
        case Family::BUTTON:
        case Family::MANUAL_CHECK_IN:
            return 1;
        case Family::SENSOR_THRESHOLD:
            return 8;
        default:
            return MAX_BODY_SIZE + 1;
    }
}

inline bool isValidButtonEvent(uint8_t value) {
    return value >= static_cast<uint8_t>(ButtonEvent::PRESS) &&
        value <= static_cast<uint8_t>(ButtonEvent::VERY_LONG_PRESS);
}

inline bool isValidSensorValueType(uint8_t value) {
    return value >= static_cast<uint8_t>(SensorValueType::UNSIGNED_32) &&
        value <= static_cast<uint8_t>(SensorValueType::ENUM_U16);
}

inline bool isValidThresholdRelation(uint8_t value) {
    return value == static_cast<uint8_t>(ThresholdRelation::CROSSED_BELOW) ||
        value == static_cast<uint8_t>(ThresholdRelation::CROSSED_ABOVE);
}

inline bool isValidAdmissionStatus(uint8_t value) {
    return value <= static_cast<uint8_t>(AdmissionStatus::MALFORMED_EVENT);
}

inline bool isValidFamilyBody(
    uint8_t family,
    const uint8_t* body,
    uint8_t bodyLength
) {
    if (body == nullptr || bodyLength != expectedBodyLength(family)) {
        return false;
    }

    switch (static_cast<Family>(family)) {
        case Family::BUTTON:
            return isValidButtonEvent(body[0]);

        case Family::SENSOR_THRESHOLD: {
            const uint16_t capabilityId = static_cast<uint16_t>(body[0]) |
                (static_cast<uint16_t>(body[1]) << 8);
            if (capabilityId == 0 || !isValidSensorValueType(body[2]) ||
                !isValidThresholdRelation(body[7])) {
                return false;
            }
            const uint8_t valueType = body[2];
            if (valueType == static_cast<uint8_t>(SensorValueType::NORMALIZED_U16) ||
                valueType == static_cast<uint8_t>(SensorValueType::ENUM_U16)) {
                return body[5] == 0 && body[6] == 0;
            }
            return true;
        }

        case Family::MANUAL_CHECK_IN:
            return body[0] == static_cast<uint8_t>(ManualReason::USER_REQUEST);

        default:
            return false;
    }
}

inline bool hasValidEndpoints(uint8_t source, uint8_t destination) {
    return source != 0 && destination != 0 && source != destination;
}

inline bool isValidEvent(const Event& event) {
    return hasValidEndpoints(event.source, event.destination) &&
        event.epoch != 0 && event.id != 0 &&
        (event.flags & static_cast<uint8_t>(~ALLOWED_FLAGS)) == 0 &&
        event.lifetimeBudgetSeconds >= MIN_LIFETIME_SECONDS &&
        event.lifetimeBudgetSeconds <= MAX_LIFETIME_SECONDS &&
        event.bodyLength <= MAX_BODY_SIZE &&
        isRegisteredFamily(event.family) &&
        isValidFamilyBody(event.family, event.body, event.bodyLength);
}

inline bool sameIdentity(const Identity& left, const Identity& right) {
    return left.sourceDeviceId == right.sourceDeviceId &&
        left.epoch == right.epoch && left.id == right.id;
}

inline bool sameCanonicalContent(const Event& left, const Event& right) {
    if (left.source != right.source || left.family != right.family ||
        left.epoch != right.epoch || left.id != right.id ||
        left.flags != right.flags ||
        left.lifetimeBudgetSeconds != right.lifetimeBudgetSeconds ||
        left.bodyLength != right.bodyLength) {
        return false;
    }
    for (size_t index = 0; index < left.bodyLength; ++index) {
        if (left.body[index] != right.body[index]) {
            return false;
        }
    }
    return true;
}

inline bool encodeEvent(
    const Event& event,
    uint8_t* output,
    size_t outputCapacity,
    size_t& encodedLength
) {
    encodedLength = 0;
    if (!isValidEvent(event)) {
        return false;
    }

    Protocol::Packet packet = {};
    packet.type = Protocol::PacketType::EVENT;
    packet.source = event.source;
    packet.destination = event.destination;
    packet.sequence = event.sequence;
    packet.opcode = event.family;
    packet.payloadLength = static_cast<uint8_t>(
        COMMON_PAYLOAD_SIZE + event.bodyLength
    );
    writeUint32Le(packet.payload, event.epoch);
    writeUint32Le(packet.payload + 4, event.id);
    packet.payload[8] = event.flags;
    writeUint32Le(packet.payload + 9, event.lifetimeBudgetSeconds);
    packet.payload[13] = event.bodyLength;
    for (size_t index = 0; index < event.bodyLength; ++index) {
        packet.payload[COMMON_PAYLOAD_SIZE + index] = event.body[index];
    }
    return Protocol::encode(packet, output, outputCapacity, encodedLength);
}

inline bool decodeEvent(
    const uint8_t* input,
    size_t inputLength,
    Event& event
) {
    Protocol::Packet packet = {};
    if (!Protocol::decode(input, inputLength, packet) ||
        packet.type != Protocol::PacketType::EVENT ||
        packet.payloadLength < COMMON_PAYLOAD_SIZE) {
        return false;
    }

    Event decoded = {};
    decoded.source = packet.source;
    decoded.destination = packet.destination;
    decoded.sequence = packet.sequence;
    decoded.family = packet.opcode;
    decoded.epoch = readUint32Le(packet.payload);
    decoded.id = readUint32Le(packet.payload + 4);
    decoded.flags = packet.payload[8];
    decoded.lifetimeBudgetSeconds = readUint32Le(packet.payload + 9);
    decoded.bodyLength = packet.payload[13];
    if (decoded.bodyLength > MAX_BODY_SIZE ||
        packet.payloadLength != COMMON_PAYLOAD_SIZE + decoded.bodyLength) {
        return false;
    }
    for (size_t index = 0; index < decoded.bodyLength; ++index) {
        decoded.body[index] = packet.payload[COMMON_PAYLOAD_SIZE + index];
    }
    if (!isValidEvent(decoded)) {
        return false;
    }
    event = decoded;
    return true;
}

inline bool isCorrelatableFamily(uint8_t family) {
    return family >= Protocol::OPCODE_APPLICATION_MIN &&
        family <= Protocol::OPCODE_APPLICATION_MAX;
}

inline bool isValidAdmissionResponse(const AdmissionResponse& response) {
    return hasValidEndpoints(response.source, response.destination) &&
        isCorrelatableFamily(response.family) && response.epoch != 0 &&
        response.id != 0 &&
        isValidAdmissionStatus(static_cast<uint8_t>(response.status));
}

inline bool encodeAdmissionResponse(
    const AdmissionResponse& response,
    uint8_t* output,
    size_t outputCapacity,
    size_t& encodedLength
) {
    encodedLength = 0;
    if (!isValidAdmissionResponse(response)) {
        return false;
    }
    Protocol::Packet packet = {};
    packet.type = Protocol::PacketType::ACK;
    packet.source = response.source;
    packet.destination = response.destination;
    packet.sequence = response.sequence;
    packet.opcode = response.family;
    packet.payloadLength = ADMISSION_PAYLOAD_SIZE;
    packet.payload[0] = static_cast<uint8_t>(response.status);
    writeUint32Le(packet.payload + 1, response.epoch);
    writeUint32Le(packet.payload + 5, response.id);
    return Protocol::encode(packet, output, outputCapacity, encodedLength);
}

inline bool decodeAdmissionResponse(
    const uint8_t* input,
    size_t inputLength,
    AdmissionResponse& response
) {
    Protocol::Packet packet = {};
    if (!Protocol::decode(input, inputLength, packet) ||
        packet.type != Protocol::PacketType::ACK ||
        packet.payloadLength != ADMISSION_PAYLOAD_SIZE) {
        return false;
    }
    AdmissionResponse decoded = {};
    decoded.source = packet.source;
    decoded.destination = packet.destination;
    decoded.sequence = packet.sequence;
    decoded.family = packet.opcode;
    decoded.status = static_cast<AdmissionStatus>(packet.payload[0]);
    decoded.epoch = readUint32Le(packet.payload + 1);
    decoded.id = readUint32Le(packet.payload + 5);
    if (!isValidAdmissionResponse(decoded)) {
        return false;
    }
    response = decoded;
    return true;
}

inline AdmissionResponse makeAdmissionResponse(
    const Event& event,
    AdmissionStatus status
) {
    AdmissionResponse response = {};
    response.source = event.destination;
    response.destination = event.source;
    response.sequence = event.sequence;
    response.family = event.family;
    response.status = status;
    response.epoch = event.epoch;
    response.id = event.id;
    return response;
}

inline bool matchesEvent(
    const AdmissionResponse& response,
    const Event& event
) {
    return isValidAdmissionResponse(response) &&
        response.source == event.destination &&
        response.destination == event.source &&
        response.sequence == event.sequence &&
        response.family == event.family &&
        response.epoch == event.epoch && response.id == event.id;
}

}  // namespace EventProtocol
