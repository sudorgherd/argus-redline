#pragma once

#include <stddef.h>
#include <stdint.h>

namespace Protocol {

constexpr uint8_t VERSION = 1;

constexpr size_t HEADER_SIZE = 6;
constexpr size_t MAX_PACKET_SIZE = 32;
constexpr size_t MAX_PAYLOAD_SIZE = MAX_PACKET_SIZE - HEADER_SIZE;

constexpr uint8_t HUB_ID = 0x01;
constexpr uint8_t NODE_ID = 0x10;

constexpr uint8_t OPCODE_CORE_MIN = 0x00;
constexpr uint8_t OPCODE_CORE_MAX = 0x1F;

constexpr uint8_t OPCODE_APPLICATION_MIN = 0x20;
constexpr uint8_t OPCODE_APPLICATION_MAX = 0x7F;

constexpr uint8_t OPCODE_RESERVED_MIN = 0x80;
constexpr uint8_t OPCODE_RESERVED_MAX = 0xFF;

enum class Opcode : uint8_t {
    TEST = 0x64
};

constexpr uint8_t OPCODE_TEST =
    static_cast<uint8_t>(Opcode::TEST);

enum class PacketType : uint8_t {
    COMMAND = 0x01,
    ACK = 0x02,
    ERROR = 0x03
};

enum class AckStatus : uint8_t {
    SUCCESS = 0x00,
    UNSUPPORTED_OPCODE = 0x01,
    MALFORMED_PACKET = 0x02
};

inline bool isSupportedCommandOpcode(uint8_t opcode) {
    return opcode == OPCODE_TEST;
}

inline size_t expectedCommandPayloadLength(uint8_t opcode) {
    switch (opcode) {
        case OPCODE_TEST:
            return 0;

        default:
            return MAX_PAYLOAD_SIZE + 1;
    }
}

inline bool isValidCommandPayload(
    uint8_t opcode,
    uint8_t payloadLength
) {
    const size_t expectedLength =
        expectedCommandPayloadLength(opcode);

    return (
        expectedLength <= MAX_PAYLOAD_SIZE &&
        payloadLength == expectedLength
    );
}

inline bool isValidAckStatus(uint8_t value) {
    return (
        value == static_cast<uint8_t>(AckStatus::SUCCESS) ||
        value == static_cast<uint8_t>(
            AckStatus::UNSUPPORTED_OPCODE
        ) ||
        value == static_cast<uint8_t>(
            AckStatus::MALFORMED_PACKET
        )
    );
}

struct Packet {
    PacketType type;
    uint8_t source;
    uint8_t destination;
    uint8_t sequence;
    uint8_t opcode;
    uint8_t payloadLength;
    uint8_t payload[MAX_PAYLOAD_SIZE];
};

inline uint8_t makeControl(PacketType type) {
    return static_cast<uint8_t>(
        (VERSION << 4) |
        (static_cast<uint8_t>(type) & 0x0F)
    );
}

inline uint8_t getVersion(uint8_t control) {
    return control >> 4;
}

inline PacketType getPacketType(uint8_t control) {
    return static_cast<PacketType>(control & 0x0F);
}

inline bool encode(
    const Packet& packet,
    uint8_t* output,
    size_t outputCapacity,
    size_t& encodedLength
) {
    encodedLength = 0;

    if (output == nullptr) {
        return false;
    }

    if (packet.payloadLength > MAX_PAYLOAD_SIZE) {
        return false;
    }

    const size_t requiredLength =
        HEADER_SIZE + packet.payloadLength;

    if (outputCapacity < requiredLength) {
        return false;
    }

    output[0] = makeControl(packet.type);
    output[1] = packet.source;
    output[2] = packet.destination;
    output[3] = packet.sequence;
    output[4] = packet.opcode;
    output[5] = packet.payloadLength;

    for (size_t index = 0; index < packet.payloadLength; ++index) {
        output[HEADER_SIZE + index] = packet.payload[index];
    }

    encodedLength = requiredLength;
    return true;
}

inline bool decode(
    const uint8_t* input,
    size_t inputLength,
    Packet& packet
) {
    if (input == nullptr || inputLength < HEADER_SIZE) {
        return false;
    }

    if (getVersion(input[0]) != VERSION) {
        return false;
    }

    const uint8_t payloadLength = input[5];

    if (payloadLength > MAX_PAYLOAD_SIZE) {
        return false;
    }

    if (inputLength != HEADER_SIZE + payloadLength) {
        return false;
    }

    const PacketType type = getPacketType(input[0]);

    if (
        type != PacketType::COMMAND &&
        type != PacketType::ACK &&
        type != PacketType::ERROR
    ) {
        return false;
    }

    packet.type = type;
    packet.source = input[1];
    packet.destination = input[2];
    packet.sequence = input[3];
    packet.opcode = input[4];
    packet.payloadLength = payloadLength;

    for (size_t index = 0; index < payloadLength; ++index) {
        packet.payload[index] = input[HEADER_SIZE + index];
    }

    return true;
}

inline bool isAddressedTo(
    const Packet& packet,
    uint8_t deviceId
) {
    return packet.destination == deviceId;
}

}  // namespace Protocol
