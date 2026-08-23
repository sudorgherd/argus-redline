#pragma once

#include <stddef.h>
#include <stdint.h>

#include "event_protocol.h"

namespace EventRecords {

constexpr size_t NODE_METADATA_SIZE = 28;
constexpr size_t HUB_METADATA_SIZE = 24;
constexpr size_t NODE_RECORD_SIZE = 56;
constexpr size_t HUB_RECORD_SIZE = 56;
constexpr uint16_t SCHEMA_VERSION = 1;
constexpr uint8_t MAX_ATTEMPTS = 5;

enum class CodecResult : uint8_t {
    OK,
    NULL_POINTER,
    WRONG_LENGTH,
    BAD_MAGIC,
    BAD_SCHEMA,
    BAD_RECORD_LENGTH,
    BAD_CRC,
    NONZERO_RESERVED,
    INVALID_STATE,
    INVALID_METADATA,
    INVALID_EVENT,
    INVALID_ATTEMPTS,
    INVALID_LIFETIME,
    NONZERO_BODY_TAIL
};

enum class NodeState : uint8_t {
    FREE = 0x00,
    QUEUED = 0x01,
    FAILED = 0x02,
    EXPIRED = 0x03
};

enum class HubState : uint8_t {
    EMPTY = 0x00,
    ACTIVE = 0x01,
    CONSUMED = 0x02
};

struct NodeMetadata {
    uint32_t generation;
    uint8_t custodySourceDeviceId;
    uint32_t eventEpoch;
    uint32_t nextUnreservedEventId;
};

struct HubMetadata {
    uint32_t generation;
    uint32_t nextUnreservedAdmissionOrdinal;
};

struct NodeRecord {
    uint32_t generation;
    NodeState state;
    uint8_t flags;
    uint8_t family;
    uint8_t bodyLength;
    uint32_t eventEpoch;
    uint32_t eventId;
    uint32_t lifetimeBudgetSeconds;
    uint32_t remainingActiveSeconds;
    uint8_t attemptsUsed;
    uint8_t body[EventProtocol::MAX_BODY_SIZE];
};

struct HubRecord {
    uint32_t generation;
    HubState state;
    uint8_t sourceDeviceId;
    uint8_t family;
    uint8_t flags;
    uint8_t bodyLength;
    uint32_t eventEpoch;
    uint32_t eventId;
    uint32_t lifetimeBudgetSeconds;
    uint32_t admissionOrdinal;
    uint8_t body[EventProtocol::MAX_BODY_SIZE];
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
        static_cast<uint16_t>(static_cast<uint16_t>(input[1]) << 8);
}

inline uint32_t readUint32Le(const uint8_t* input) {
    return static_cast<uint32_t>(input[0]) |
        (static_cast<uint32_t>(input[1]) << 8) |
        (static_cast<uint32_t>(input[2]) << 16) |
        (static_cast<uint32_t>(input[3]) << 24);
}

inline bool crc32IsoHdlc(
    const uint8_t* input,
    size_t length,
    uint32_t& crc
) {
    if (input == nullptr) {
        return false;
    }
    uint32_t value = 0xFFFFFFFFU;
    for (size_t index = 0; index < length; ++index) {
        value ^= input[index];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            value = (value & 1U) != 0U
                ? (value >> 1) ^ 0xEDB88320U
                : value >> 1;
        }
    }
    crc = value ^ 0xFFFFFFFFU;
    return true;
}

inline bool allZero(const uint8_t* bytes, size_t length) {
    for (size_t index = 0; index < length; ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

inline void clearBytes(uint8_t* bytes, size_t length) {
    for (size_t index = 0; index < length; ++index) {
        bytes[index] = 0;
    }
}

inline bool sameMagic(const uint8_t* input, const char magic[5]) {
    return input[0] == static_cast<uint8_t>(magic[0]) &&
        input[1] == static_cast<uint8_t>(magic[1]) &&
        input[2] == static_cast<uint8_t>(magic[2]) &&
        input[3] == static_cast<uint8_t>(magic[3]);
}

inline CodecResult validateEnvelope(
    const uint8_t* input,
    size_t inputLength,
    size_t expectedLength,
    const char magic[5]
) {
    if (input == nullptr) return CodecResult::NULL_POINTER;
    if (inputLength != expectedLength) return CodecResult::WRONG_LENGTH;
    if (!sameMagic(input, magic)) return CodecResult::BAD_MAGIC;
    if (readUint16Le(input + 6) != expectedLength) {
        return CodecResult::BAD_RECORD_LENGTH;
    }
    uint32_t crc = 0;
    crc32IsoHdlc(input, expectedLength - 4, crc);
    if (readUint32Le(input + expectedLength - 4) != crc) {
        return CodecResult::BAD_CRC;
    }
    if (readUint16Le(input + 4) != SCHEMA_VERSION) {
        return CodecResult::BAD_SCHEMA;
    }
    return CodecResult::OK;
}

inline void encodeEnvelope(
    uint8_t* output,
    size_t length,
    const char magic[5],
    uint32_t generation
) {
    clearBytes(output, length);
    output[0] = static_cast<uint8_t>(magic[0]);
    output[1] = static_cast<uint8_t>(magic[1]);
    output[2] = static_cast<uint8_t>(magic[2]);
    output[3] = static_cast<uint8_t>(magic[3]);
    writeUint16Le(output + 4, SCHEMA_VERSION);
    writeUint16Le(output + 6, static_cast<uint16_t>(length));
    writeUint32Le(output + 8, generation);
}

inline void finishCrc(uint8_t* output, size_t length) {
    uint32_t crc = 0;
    crc32IsoHdlc(output, length - 4, crc);
    writeUint32Le(output + length - 4, crc);
}

inline CodecResult encodeNodeMetadata(
    const NodeMetadata& value,
    uint8_t* output,
    size_t outputLength
) {
    if (output == nullptr) return CodecResult::NULL_POINTER;
    if (outputLength != NODE_METADATA_SIZE) return CodecResult::WRONG_LENGTH;
    if (value.custodySourceDeviceId == 0 || value.eventEpoch == 0 ||
        value.nextUnreservedEventId == 0) {
        return CodecResult::INVALID_METADATA;
    }
    encodeEnvelope(output, outputLength, "EID1", value.generation);
    output[12] = value.custodySourceDeviceId;
    writeUint32Le(output + 16, value.eventEpoch);
    writeUint32Le(output + 20, value.nextUnreservedEventId);
    finishCrc(output, outputLength);
    return CodecResult::OK;
}

inline CodecResult decodeNodeMetadata(
    const uint8_t* input,
    size_t inputLength,
    NodeMetadata& value
) {
    CodecResult result = validateEnvelope(
        input, inputLength, NODE_METADATA_SIZE, "EID1");
    if (result != CodecResult::OK) return result;
    if (!allZero(input + 13, 3)) return CodecResult::NONZERO_RESERVED;
    NodeMetadata decoded = {};
    decoded.generation = readUint32Le(input + 8);
    decoded.custodySourceDeviceId = input[12];
    decoded.eventEpoch = readUint32Le(input + 16);
    decoded.nextUnreservedEventId = readUint32Le(input + 20);
    if (decoded.custodySourceDeviceId == 0 || decoded.eventEpoch == 0 ||
        decoded.nextUnreservedEventId == 0) {
        return CodecResult::INVALID_METADATA;
    }
    value = decoded;
    return CodecResult::OK;
}

inline CodecResult encodeHubMetadata(
    const HubMetadata& value,
    uint8_t* output,
    size_t outputLength
) {
    if (output == nullptr) return CodecResult::NULL_POINTER;
    if (outputLength != HUB_METADATA_SIZE) return CodecResult::WRONG_LENGTH;
    if (value.nextUnreservedAdmissionOrdinal == 0) {
        return CodecResult::INVALID_METADATA;
    }
    encodeEnvelope(output, outputLength, "HAO1", value.generation);
    writeUint32Le(output + 12, value.nextUnreservedAdmissionOrdinal);
    finishCrc(output, outputLength);
    return CodecResult::OK;
}

inline CodecResult decodeHubMetadata(
    const uint8_t* input,
    size_t inputLength,
    HubMetadata& value
) {
    CodecResult result = validateEnvelope(
        input, inputLength, HUB_METADATA_SIZE, "HAO1");
    if (result != CodecResult::OK) return result;
    if (!allZero(input + 16, 4)) return CodecResult::NONZERO_RESERVED;
    HubMetadata decoded = {};
    decoded.generation = readUint32Le(input + 8);
    decoded.nextUnreservedAdmissionOrdinal = readUint32Le(input + 12);
    if (decoded.nextUnreservedAdmissionOrdinal == 0) {
        return CodecResult::INVALID_METADATA;
    }
    value = decoded;
    return CodecResult::OK;
}

inline bool validNodeState(NodeState state) {
    return static_cast<uint8_t>(state) <= static_cast<uint8_t>(NodeState::EXPIRED);
}

inline bool validHubState(HubState state) {
    return static_cast<uint8_t>(state) <= static_cast<uint8_t>(HubState::CONSUMED);
}

inline bool validCanonicalEvent(
    uint8_t source,
    uint8_t family,
    uint8_t flags,
    uint8_t bodyLength,
    uint32_t epoch,
    uint32_t id,
    uint32_t lifetime,
    const uint8_t* body
) {
    return source != 0 && epoch != 0 && id != 0 &&
        (flags & static_cast<uint8_t>(~EventProtocol::ALLOWED_FLAGS)) == 0 &&
        lifetime >= EventProtocol::MIN_LIFETIME_SECONDS &&
        lifetime <= EventProtocol::MAX_LIFETIME_SECONDS &&
        bodyLength <= EventProtocol::MAX_BODY_SIZE &&
        EventProtocol::isRegisteredFamily(family) &&
        EventProtocol::isValidFamilyBody(family, body, bodyLength);
}

inline bool canonicalFree(const NodeRecord& value) {
    return value.flags == 0 && value.family == 0 && value.bodyLength == 0 &&
        value.eventEpoch == 0 && value.eventId == 0 &&
        value.lifetimeBudgetSeconds == 0 &&
        value.remainingActiveSeconds == 0 && value.attemptsUsed == 0 &&
        allZero(value.body, EventProtocol::MAX_BODY_SIZE);
}

inline CodecResult validateNodeRecord(const NodeRecord& value) {
    if (!validNodeState(value.state)) return CodecResult::INVALID_STATE;
    if (value.state == NodeState::FREE) {
        return canonicalFree(value) ? CodecResult::OK : CodecResult::INVALID_EVENT;
    }
    if (value.attemptsUsed > MAX_ATTEMPTS) return CodecResult::INVALID_ATTEMPTS;
    if (value.remainingActiveSeconds > value.lifetimeBudgetSeconds) {
        return CodecResult::INVALID_LIFETIME;
    }
    if (!validCanonicalEvent(
        1, value.family, value.flags, value.bodyLength, value.eventEpoch,
        value.eventId, value.lifetimeBudgetSeconds, value.body)) {
        return CodecResult::INVALID_EVENT;
    }
    if (!allZero(
        value.body + value.bodyLength,
        EventProtocol::MAX_BODY_SIZE - value.bodyLength
    )) {
        return CodecResult::NONZERO_BODY_TAIL;
    }
    return CodecResult::OK;
}

inline CodecResult encodeNodeRecord(
    const NodeRecord& value,
    uint8_t* output,
    size_t outputLength
) {
    if (output == nullptr) return CodecResult::NULL_POINTER;
    if (outputLength != NODE_RECORD_SIZE) return CodecResult::WRONG_LENGTH;
    CodecResult result = validateNodeRecord(value);
    if (result != CodecResult::OK) return result;
    encodeEnvelope(output, outputLength, "EVT1", value.generation);
    output[12] = static_cast<uint8_t>(value.state);
    output[13] = value.flags;
    output[14] = value.family;
    output[15] = value.bodyLength;
    writeUint32Le(output + 16, value.eventEpoch);
    writeUint32Le(output + 20, value.eventId);
    writeUint32Le(output + 24, value.lifetimeBudgetSeconds);
    writeUint32Le(output + 28, value.remainingActiveSeconds);
    output[32] = value.attemptsUsed;
    for (size_t index = 0; index < value.bodyLength; ++index) {
        output[36 + index] = value.body[index];
    }
    finishCrc(output, outputLength);
    return CodecResult::OK;
}

inline CodecResult decodeNodeRecord(
    const uint8_t* input,
    size_t inputLength,
    NodeRecord& value
) {
    CodecResult result = validateEnvelope(
        input, inputLength, NODE_RECORD_SIZE, "EVT1");
    if (result != CodecResult::OK) return result;
    if (!allZero(input + 33, 3) || !allZero(input + 48, 4)) {
        return CodecResult::NONZERO_RESERVED;
    }
    NodeRecord decoded = {};
    decoded.generation = readUint32Le(input + 8);
    decoded.state = static_cast<NodeState>(input[12]);
    decoded.flags = input[13];
    decoded.family = input[14];
    decoded.bodyLength = input[15];
    decoded.eventEpoch = readUint32Le(input + 16);
    decoded.eventId = readUint32Le(input + 20);
    decoded.lifetimeBudgetSeconds = readUint32Le(input + 24);
    decoded.remainingActiveSeconds = readUint32Le(input + 28);
    decoded.attemptsUsed = input[32];
    if (decoded.bodyLength > EventProtocol::MAX_BODY_SIZE) {
        return CodecResult::INVALID_EVENT;
    }
    for (size_t index = 0; index < EventProtocol::MAX_BODY_SIZE; ++index) {
        decoded.body[index] = input[36 + index];
    }
    if (!allZero(input + 36 + decoded.bodyLength,
        EventProtocol::MAX_BODY_SIZE - decoded.bodyLength)) {
        return CodecResult::NONZERO_BODY_TAIL;
    }
    result = validateNodeRecord(decoded);
    if (result != CodecResult::OK) return result;
    value = decoded;
    return CodecResult::OK;
}

inline bool canonicalEmpty(const HubRecord& value) {
    return value.sourceDeviceId == 0 && value.family == 0 && value.flags == 0 &&
        value.bodyLength == 0 && value.eventEpoch == 0 && value.eventId == 0 &&
        value.lifetimeBudgetSeconds == 0 && value.admissionOrdinal == 0 &&
        allZero(value.body, EventProtocol::MAX_BODY_SIZE);
}

inline CodecResult validateHubRecord(const HubRecord& value) {
    if (!validHubState(value.state)) return CodecResult::INVALID_STATE;
    if (value.state == HubState::EMPTY) {
        return canonicalEmpty(value) ? CodecResult::OK : CodecResult::INVALID_EVENT;
    }
    if (value.admissionOrdinal == 0) return CodecResult::INVALID_EVENT;
    if (!validCanonicalEvent(
        value.sourceDeviceId, value.family, value.flags, value.bodyLength,
        value.eventEpoch, value.eventId, value.lifetimeBudgetSeconds,
        value.body)) {
        return CodecResult::INVALID_EVENT;
    }
    if (!allZero(
        value.body + value.bodyLength,
        EventProtocol::MAX_BODY_SIZE - value.bodyLength
    )) {
        return CodecResult::NONZERO_BODY_TAIL;
    }
    return CodecResult::OK;
}

inline CodecResult encodeHubRecord(
    const HubRecord& value,
    uint8_t* output,
    size_t outputLength
) {
    if (output == nullptr) return CodecResult::NULL_POINTER;
    if (outputLength != HUB_RECORD_SIZE) return CodecResult::WRONG_LENGTH;
    CodecResult result = validateHubRecord(value);
    if (result != CodecResult::OK) return result;
    encodeEnvelope(output, outputLength, "HEV1", value.generation);
    output[12] = static_cast<uint8_t>(value.state);
    output[13] = value.sourceDeviceId;
    output[14] = value.family;
    output[15] = value.flags;
    output[16] = value.bodyLength;
    writeUint32Le(output + 20, value.eventEpoch);
    writeUint32Le(output + 24, value.eventId);
    writeUint32Le(output + 28, value.lifetimeBudgetSeconds);
    writeUint32Le(output + 32, value.admissionOrdinal);
    for (size_t index = 0; index < value.bodyLength; ++index) {
        output[36 + index] = value.body[index];
    }
    finishCrc(output, outputLength);
    return CodecResult::OK;
}

inline CodecResult decodeHubRecord(
    const uint8_t* input,
    size_t inputLength,
    HubRecord& value
) {
    CodecResult result = validateEnvelope(
        input, inputLength, HUB_RECORD_SIZE, "HEV1");
    if (result != CodecResult::OK) return result;
    if (!allZero(input + 17, 3) || !allZero(input + 48, 4)) {
        return CodecResult::NONZERO_RESERVED;
    }
    HubRecord decoded = {};
    decoded.generation = readUint32Le(input + 8);
    decoded.state = static_cast<HubState>(input[12]);
    decoded.sourceDeviceId = input[13];
    decoded.family = input[14];
    decoded.flags = input[15];
    decoded.bodyLength = input[16];
    decoded.eventEpoch = readUint32Le(input + 20);
    decoded.eventId = readUint32Le(input + 24);
    decoded.lifetimeBudgetSeconds = readUint32Le(input + 28);
    decoded.admissionOrdinal = readUint32Le(input + 32);
    if (decoded.bodyLength > EventProtocol::MAX_BODY_SIZE) {
        return CodecResult::INVALID_EVENT;
    }
    for (size_t index = 0; index < EventProtocol::MAX_BODY_SIZE; ++index) {
        decoded.body[index] = input[36 + index];
    }
    if (!allZero(input + 36 + decoded.bodyLength,
        EventProtocol::MAX_BODY_SIZE - decoded.bodyLength)) {
        return CodecResult::NONZERO_BODY_TAIL;
    }
    result = validateHubRecord(decoded);
    if (result != CodecResult::OK) return result;
    value = decoded;
    return CodecResult::OK;
}

inline bool decodeNodeMetadataForSelection(
    const uint8_t* input, size_t length, NodeMetadata& value) {
    return decodeNodeMetadata(input, length, value) == CodecResult::OK;
}
inline bool decodeHubMetadataForSelection(
    const uint8_t* input, size_t length, HubMetadata& value) {
    return decodeHubMetadata(input, length, value) == CodecResult::OK;
}
inline bool decodeNodeRecordForSelection(
    const uint8_t* input, size_t length, NodeRecord& value) {
    return decodeNodeRecord(input, length, value) == CodecResult::OK;
}
inline bool decodeHubRecordForSelection(
    const uint8_t* input, size_t length, HubRecord& value) {
    return decodeHubRecord(input, length, value) == CodecResult::OK;
}

}  // namespace EventRecords
