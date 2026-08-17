#include <unity.h>

#include "protocol.h"

namespace {

constexpr uint8_t HUB_ID = 0x01;
constexpr uint8_t NODE_ID = 0x10;
constexpr uint8_t SEQUENCE = 0x2A;

Protocol::Packet makeTestCommand() {
    Protocol::Packet packet = {};
    packet.type = Protocol::PacketType::COMMAND;
    packet.source = HUB_ID;
    packet.destination = NODE_ID;
    packet.sequence = SEQUENCE;
    packet.opcode = Protocol::OPCODE_TEST;
    packet.payloadLength = 0;
    return packet;
}

void testEncodesExactZeroPayloadTestCommand() {
    const Protocol::Packet packet = makeTestCommand();
    uint8_t encoded[Protocol::MAX_PACKET_SIZE] = {};
    size_t encodedLength = 0;

    TEST_ASSERT_TRUE(Protocol::encode(
        packet,
        encoded,
        sizeof(encoded),
        encodedLength
    ));

    const uint8_t expected[] = {
        0x11,
        HUB_ID,
        NODE_ID,
        SEQUENCE,
        0x64,
        0x00
    };

    TEST_ASSERT_EQUAL_UINT32(sizeof(expected), encodedLength);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, encoded, sizeof(expected));
}

void testDecodesZeroPayloadTestCommand() {
    const uint8_t encoded[] = {
        0x11,
        HUB_ID,
        NODE_ID,
        SEQUENCE,
        0x64,
        0x00
    };
    Protocol::Packet packet = {};

    TEST_ASSERT_TRUE(Protocol::decode(
        encoded,
        sizeof(encoded),
        packet
    ));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(Protocol::PacketType::COMMAND),
        static_cast<uint8_t>(packet.type)
    );
    TEST_ASSERT_EQUAL_UINT8(HUB_ID, packet.source);
    TEST_ASSERT_EQUAL_UINT8(NODE_ID, packet.destination);
    TEST_ASSERT_EQUAL_UINT8(SEQUENCE, packet.sequence);
    TEST_ASSERT_EQUAL_UINT8(0x64, packet.opcode);
    TEST_ASSERT_EQUAL_UINT8(0, packet.payloadLength);
}

void testRoundTripsOneByteAcknowledgment() {
    Protocol::Packet acknowledgment = {};
    acknowledgment.type = Protocol::PacketType::ACK;
    acknowledgment.source = NODE_ID;
    acknowledgment.destination = HUB_ID;
    acknowledgment.sequence = SEQUENCE;
    acknowledgment.opcode = Protocol::OPCODE_TEST;
    acknowledgment.payloadLength = 1;
    acknowledgment.payload[0] = static_cast<uint8_t>(
        Protocol::AckStatus::SUCCESS
    );

    uint8_t encoded[Protocol::MAX_PACKET_SIZE] = {};
    size_t encodedLength = 0;
    TEST_ASSERT_TRUE(Protocol::encode(
        acknowledgment,
        encoded,
        sizeof(encoded),
        encodedLength
    ));

    const uint8_t expected[] = {
        0x12,
        NODE_ID,
        HUB_ID,
        SEQUENCE,
        0x64,
        0x01,
        0x00
    };
    TEST_ASSERT_EQUAL_UINT32(sizeof(expected), encodedLength);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, encoded, sizeof(expected));

    Protocol::Packet decoded = {};
    TEST_ASSERT_TRUE(Protocol::decode(
        encoded,
        encodedLength,
        decoded
    ));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(Protocol::PacketType::ACK),
        static_cast<uint8_t>(decoded.type)
    );
    TEST_ASSERT_EQUAL_UINT8(NODE_ID, decoded.source);
    TEST_ASSERT_EQUAL_UINT8(HUB_ID, decoded.destination);
    TEST_ASSERT_EQUAL_UINT8(SEQUENCE, decoded.sequence);
    TEST_ASSERT_EQUAL_UINT8(Protocol::OPCODE_TEST, decoded.opcode);
    TEST_ASSERT_EQUAL_UINT8(1, decoded.payloadLength);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(Protocol::AckStatus::SUCCESS),
        decoded.payload[0]
    );
}

void testRejectsInputShorterThanHeader() {
    const uint8_t encoded[Protocol::HEADER_SIZE - 1] = {};
    Protocol::Packet packet = {};
    TEST_ASSERT_FALSE(Protocol::decode(encoded, sizeof(encoded), packet));
}

void testRejectsPayloadLengthAboveMaximum() {
    const uint8_t encoded[] = {
        0x11,
        HUB_ID,
        NODE_ID,
        SEQUENCE,
        0x64,
        static_cast<uint8_t>(Protocol::MAX_PAYLOAD_SIZE + 1)
    };
    Protocol::Packet packet = {};
    TEST_ASSERT_FALSE(Protocol::decode(encoded, sizeof(encoded), packet));
}

void testRejectsEncodedLengthPayloadLengthMismatch() {
    const uint8_t encoded[] = {
        0x11,
        HUB_ID,
        NODE_ID,
        SEQUENCE,
        0x64,
        0x01
    };
    Protocol::Packet packet = {};
    TEST_ASSERT_FALSE(Protocol::decode(encoded, sizeof(encoded), packet));
}

void testRejectsWrongWireProtocolVersion() {
    const uint8_t encoded[] = {
        0x21,
        HUB_ID,
        NODE_ID,
        SEQUENCE,
        0x64,
        0x00
    };
    Protocol::Packet packet = {};
    TEST_ASSERT_FALSE(Protocol::decode(encoded, sizeof(encoded), packet));
}

void testRejectsUnknownPacketType() {
    const uint8_t encoded[] = {
        0x1F,
        HUB_ID,
        NODE_ID,
        SEQUENCE,
        0x64,
        0x00
    };
    Protocol::Packet packet = {};
    TEST_ASSERT_FALSE(Protocol::decode(encoded, sizeof(encoded), packet));
}

void testPacketTypeNumericValues() {
    TEST_ASSERT_EQUAL_HEX8(
        0x01,
        static_cast<uint8_t>(Protocol::PacketType::COMMAND)
    );
    TEST_ASSERT_EQUAL_HEX8(
        0x02,
        static_cast<uint8_t>(Protocol::PacketType::ACK)
    );
    TEST_ASSERT_EQUAL_HEX8(
        0x03,
        static_cast<uint8_t>(Protocol::PacketType::ERROR)
    );
    TEST_ASSERT_EQUAL_HEX8(
        0x04,
        static_cast<uint8_t>(Protocol::PacketType::RESPONSE)
    );
}

void testTestOpcodeNumericValue() {
    TEST_ASSERT_EQUAL_HEX8(0x64, Protocol::OPCODE_TEST);
}

void testAcknowledgmentStatusNumericValues() {
    TEST_ASSERT_EQUAL_HEX8(
        0x00,
        static_cast<uint8_t>(Protocol::AckStatus::SUCCESS)
    );
    TEST_ASSERT_EQUAL_HEX8(
        0x01,
        static_cast<uint8_t>(Protocol::AckStatus::UNSUPPORTED_OPCODE)
    );
    TEST_ASSERT_EQUAL_HEX8(
        0x02,
        static_cast<uint8_t>(Protocol::AckStatus::MALFORMED_PACKET)
    );
}

void testTestCommandAcceptsOnlyZeroBytePayload() {
    TEST_ASSERT_TRUE(Protocol::isValidCommandPayload(
        Protocol::OPCODE_TEST,
        0
    ));
    TEST_ASSERT_FALSE(Protocol::isValidCommandPayload(
        Protocol::OPCODE_TEST,
        1
    ));
}

void testUnsupportedOpcodeFailsCommandValidation() {
    constexpr uint8_t unsupportedOpcode = 0x20;
    TEST_ASSERT_FALSE(Protocol::isSupportedCommandOpcode(
        unsupportedOpcode
    ));
    TEST_ASSERT_FALSE(Protocol::isValidCommandPayload(
        unsupportedOpcode,
        0
    ));
}

void testResponseRoundTripsWithoutChangingGeometry() {
    Protocol::Packet response = {};
    response.type = Protocol::PacketType::RESPONSE;
    response.source = NODE_ID;
    response.destination = HUB_ID;
    response.sequence = SEQUENCE;
    response.opcode = static_cast<uint8_t>(Protocol::Opcode::PING);
    response.payloadLength = Protocol::MAX_PAYLOAD_SIZE;
    for (size_t index = 0; index < response.payloadLength; ++index) {
        response.payload[index] = static_cast<uint8_t>(index);
    }
    uint8_t encoded[Protocol::MAX_PACKET_SIZE] = {};
    size_t length = 0;
    TEST_ASSERT_TRUE(Protocol::encode(response, encoded, sizeof(encoded), length));
    TEST_ASSERT_EQUAL_UINT32(32, length);
    TEST_ASSERT_EQUAL_HEX8(0x14, encoded[0]);
    Protocol::Packet decoded = {};
    TEST_ASSERT_TRUE(Protocol::decode(encoded, length, decoded));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(response.payload, decoded.payload,
        Protocol::MAX_PAYLOAD_SIZE);
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(testEncodesExactZeroPayloadTestCommand);
    RUN_TEST(testDecodesZeroPayloadTestCommand);
    RUN_TEST(testRoundTripsOneByteAcknowledgment);
    RUN_TEST(testRejectsInputShorterThanHeader);
    RUN_TEST(testRejectsPayloadLengthAboveMaximum);
    RUN_TEST(testRejectsEncodedLengthPayloadLengthMismatch);
    RUN_TEST(testRejectsWrongWireProtocolVersion);
    RUN_TEST(testRejectsUnknownPacketType);
    RUN_TEST(testPacketTypeNumericValues);
    RUN_TEST(testTestOpcodeNumericValue);
    RUN_TEST(testAcknowledgmentStatusNumericValues);
    RUN_TEST(testTestCommandAcceptsOnlyZeroBytePayload);
    RUN_TEST(testUnsupportedOpcodeFailsCommandValidation);
    RUN_TEST(testResponseRoundTripsWithoutChangingGeometry);
    return UNITY_END();
}
