#include <unity.h>

#include <host_protocol.h>

using namespace HostProtocol;

namespace {

Frame makeFrame(
    MessageType type = MessageType::OPERATION_REQUEST,
    uint16_t requestId = 0x1234
) {
    Frame frame = {};
    frame.major = VERSION_MAJOR;
    frame.minor = VERSION_MINOR;
    frame.messageType = type;
    frame.flags = FLAGS_NONE;
    frame.requestId = requestId;
    return frame;
}

size_t encodeDecoded(
    const uint8_t* decoded,
    size_t decodedLength,
    uint8_t* encoded,
    size_t encodedCapacity
) {
    size_t candidateLength = 0;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CobsResult::OK),
        static_cast<uint8_t>(cobsEncode(
            decoded,
            decodedLength,
            encoded,
            encodedCapacity - 1,
            candidateLength
        ))
    );
    encoded[candidateLength] = FRAME_DELIMITER;
    return candidateLength + 1;
}

void writeCrc(uint8_t* decoded, size_t decodedLength) {
    const size_t crcIndex = decodedLength - CRC_SIZE;
    const uint16_t crc = crc16CcittFalse(decoded, crcIndex);
    decoded[crcIndex] = static_cast<uint8_t>(crc);
    decoded[crcIndex + 1] = static_cast<uint8_t>(crc >> 8);
}

void testCrc16CcittFalseKnownAndFrameVectors() {
    const uint8_t standard[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    TEST_ASSERT_EQUAL_HEX16(
        0x29B1,
        crc16CcittFalse(standard, sizeof(standard))
    );

    const uint8_t helloWithoutCrc[] = {
        0x00, 0x01, 0x01, 0x00, 0x34, 0x12, 0x02, 0x00, 0x01, 0x01
    };
    TEST_ASSERT_EQUAL_HEX16(
        0xEA45,
        crc16CcittFalse(helloWithoutCrc, sizeof(helloWithoutCrc))
    );

    const uint8_t zeroPayloadWithoutCrc[] = {
        0x00, 0x01, 0x10, 0x00, 0x34, 0x12, 0x00, 0x00
    };
    TEST_ASSERT_EQUAL_HEX16(
        0x58C0,
        crc16CcittFalse(zeroPayloadWithoutCrc, sizeof(zeroPayloadWithoutCrc))
    );
}

void testCrcDetectsOneBitPayloadCorruption() {
    uint8_t bytes[] = {0x00, 0x01, 0x10, 0x00, 0x01, 0x00, 0x01, 0x00, 0xA5};
    const uint16_t original = crc16CcittFalse(bytes, sizeof(bytes));
    bytes[8] ^= 0x01;
    TEST_ASSERT_NOT_EQUAL(original, crc16CcittFalse(bytes, sizeof(bytes)));
}

void assertCobsRoundTrip(const uint8_t* input, size_t inputLength) {
    uint8_t encoded[MAX_COBS_CANDIDATE_SIZE] = {};
    size_t encodedLength = 0;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CobsResult::OK),
        static_cast<uint8_t>(cobsEncode(
            input, inputLength, encoded, sizeof(encoded), encodedLength
        ))
    );
    for (size_t index = 0; index < encodedLength; ++index) {
        TEST_ASSERT_NOT_EQUAL(0, encoded[index]);
    }
    uint8_t decoded[MAX_DECODED_FRAME_SIZE] = {};
    size_t decodedLength = 0;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CobsResult::OK),
        static_cast<uint8_t>(cobsDecode(
            encoded, encodedLength, decoded, sizeof(decoded), decodedLength
        ))
    );
    TEST_ASSERT_EQUAL_UINT32(inputLength, decodedLength);
    if (inputLength != 0) {
        TEST_ASSERT_EQUAL_UINT8_ARRAY(input, decoded, inputLength);
    }
}

void testCobsRepresentativeZeroLayouts() {
    const uint8_t noZero[] = {0x11, 0x22, 0x33};
    const uint8_t oneZero[] = {0x11, 0x00, 0x22};
    const uint8_t consecutive[] = {0x11, 0x00, 0x00, 0x22};
    const uint8_t zeroBeginning[] = {0x00, 0x11, 0x22};
    const uint8_t zeroEnd[] = {0x11, 0x22, 0x00};
    const uint8_t runs[] = {0x00, 0x00, 0x11, 0x00, 0x22, 0x00, 0x00};
    assertCobsRoundTrip(nullptr, 0);
    assertCobsRoundTrip(noZero, sizeof(noZero));
    assertCobsRoundTrip(oneZero, sizeof(oneZero));
    assertCobsRoundTrip(consecutive, sizeof(consecutive));
    assertCobsRoundTrip(zeroBeginning, sizeof(zeroBeginning));
    assertCobsRoundTrip(zeroEnd, sizeof(zeroEnd));
    assertCobsRoundTrip(runs, sizeof(runs));
}

void testCobsExactRepresentativeEncodings() {
    const uint8_t noZero[] = {0x11, 0x22, 0x33};
    const uint8_t expectedNoZero[] = {0x04, 0x11, 0x22, 0x33};
    const uint8_t withZeros[] = {0x00, 0x11, 0x00, 0x00};
    const uint8_t expectedWithZeros[] = {0x01, 0x02, 0x11, 0x01, 0x01};
    uint8_t output[8] = {};
    size_t outputLength = 0;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CobsResult::OK),
        static_cast<uint8_t>(cobsEncode(
            noZero, sizeof(noZero), output, sizeof(output), outputLength
        ))
    );
    TEST_ASSERT_EQUAL_UINT32(sizeof(expectedNoZero), outputLength);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedNoZero, output, outputLength);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CobsResult::OK),
        static_cast<uint8_t>(cobsEncode(
            withZeros, sizeof(withZeros), output, sizeof(output), outputLength
        ))
    );
    TEST_ASSERT_EQUAL_UINT32(sizeof(expectedWithZeros), outputLength);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedWithZeros, output, outputLength);
}

void testCobsBoundaryAndCapacity() {
    uint8_t input[MAX_DECODED_FRAME_SIZE] = {};
    for (size_t index = 0; index < sizeof(input); ++index) {
        input[index] = static_cast<uint8_t>(index + 1);
    }
    uint8_t encoded[MAX_COBS_CANDIDATE_SIZE] = {};
    size_t encodedLength = 0;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CobsResult::OK),
        static_cast<uint8_t>(cobsEncode(
            input, sizeof(input), encoded, sizeof(encoded), encodedLength
        ))
    );
    TEST_ASSERT_EQUAL_UINT32(MAX_COBS_CANDIDATE_SIZE, encodedLength);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CobsResult::OUTPUT_TOO_SMALL),
        static_cast<uint8_t>(cobsEncode(
            input, sizeof(input), encoded, encodedLength - 1, encodedLength
        ))
    );
    uint8_t decoded[MAX_DECODED_FRAME_SIZE] = {};
    size_t decodedLength = 0;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CobsResult::OUTPUT_TOO_SMALL),
        static_cast<uint8_t>(cobsDecode(
            encoded,
            MAX_COBS_CANDIDATE_SIZE,
            decoded,
            sizeof(decoded) - 1,
            decodedLength
        ))
    );
}

void testCobsMalformedInputsFailClosed() {
    const uint8_t zeroCode[] = {0x00};
    const uint8_t truncatedBlock[] = {0x04, 0x11, 0x22};
    const uint8_t embeddedZero[] = {0x02, 0x00};
    uint8_t output[8] = {};
    size_t outputLength = 99;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CobsResult::MALFORMED_INPUT),
        static_cast<uint8_t>(cobsDecode(
            zeroCode, sizeof(zeroCode), output, sizeof(output), outputLength
        ))
    );
    TEST_ASSERT_EQUAL_UINT32(0, outputLength);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CobsResult::MALFORMED_INPUT),
        static_cast<uint8_t>(cobsDecode(
            truncatedBlock,
            sizeof(truncatedBlock),
            output,
            sizeof(output),
            outputLength
        ))
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CobsResult::MALFORMED_INPUT),
        static_cast<uint8_t>(cobsDecode(
            embeddedZero,
            sizeof(embeddedZero),
            output,
            sizeof(output),
            outputLength
        ))
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CobsResult::MALFORMED_INPUT),
        static_cast<uint8_t>(cobsDecode(
            output, 0, output, sizeof(output), outputLength
        ))
    );
}

void testNormativeHelloRequestVector() {
    Frame frame = makeFrame(MessageType::HELLO_REQUEST, 0x1234);
    frame.payloadLength = 2;
    frame.payload[0] = 0x01;
    frame.payload[1] = 0x01;
    uint8_t encoded[MAX_ENCODED_FRAME_SIZE] = {};
    size_t encodedLength = 0;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(EncodeResult::OK),
        static_cast<uint8_t>(encodeFrame(
            frame, encoded, sizeof(encoded), encodedLength
        ))
    );
    const uint8_t expected[] = {
        0x01, 0x03, 0x01, 0x01, 0x04, 0x34, 0x12,
        0x02, 0x05, 0x01, 0x01, 0x45, 0xEA, 0x00
    };
    TEST_ASSERT_EQUAL_UINT32(sizeof(expected), encodedLength);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, encoded, sizeof(expected));
    Frame decoded = {};
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(DecodeResult::OK),
        static_cast<uint8_t>(decodeFrame(encoded, encodedLength, decoded))
    );
    TEST_ASSERT_EQUAL_HEX16(0x1234, decoded.requestId);
    TEST_ASSERT_EQUAL_UINT16(2, decoded.payloadLength);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(frame.payload, decoded.payload, 2);
}

void testNormativePingRequestVectorIsOpaquePayload() {
    Frame frame = makeFrame(MessageType::OPERATION_REQUEST, 0x0001);
    const uint8_t payload[] = {0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00};
    frame.payloadLength = sizeof(payload);
    for (size_t index = 0; index < sizeof(payload); ++index) {
        frame.payload[index] = payload[index];
    }
    uint8_t encoded[MAX_ENCODED_FRAME_SIZE] = {};
    size_t encodedLength = 0;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(EncodeResult::OK),
        static_cast<uint8_t>(encodeFrame(
            frame, encoded, sizeof(encoded), encodedLength
        ))
    );
    const uint8_t expected[] = {
        0x01, 0x03, 0x01, 0x10, 0x02, 0x01, 0x02, 0x07, 0x04,
        0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x03, 0x64, 0x1D, 0x00
    };
    TEST_ASSERT_EQUAL_UINT32(sizeof(expected), encodedLength);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, encoded, sizeof(expected));
}

void testMinimumAndMaximumFramesRoundTrip() {
    Frame minimum = makeFrame();
    uint8_t encoded[MAX_ENCODED_FRAME_SIZE] = {};
    size_t encodedLength = 0;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(EncodeResult::OK),
        static_cast<uint8_t>(encodeFrame(
            minimum, encoded, sizeof(encoded), encodedLength
        ))
    );
    TEST_ASSERT_EQUAL_UINT32(MIN_DECODED_FRAME_SIZE + 2, encodedLength);
    Frame decoded = {};
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(DecodeResult::OK),
        static_cast<uint8_t>(decodeFrame(encoded, encodedLength, decoded))
    );
    TEST_ASSERT_EQUAL_UINT16(0, decoded.payloadLength);

    Frame maximum = makeFrame();
    maximum.payloadLength = MAX_PAYLOAD_SIZE;
    for (size_t index = 0; index < MAX_PAYLOAD_SIZE; ++index) {
        maximum.payload[index] = static_cast<uint8_t>(index);
    }
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(EncodeResult::OK),
        static_cast<uint8_t>(encodeFrame(
            maximum, encoded, sizeof(encoded), encodedLength
        ))
    );
    TEST_ASSERT_EQUAL_UINT32(MAX_ENCODED_FRAME_SIZE, encodedLength);
    TEST_ASSERT_EQUAL_UINT8(FRAME_DELIMITER, encoded[encodedLength - 1]);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(DecodeResult::OK),
        static_cast<uint8_t>(decodeFrame(encoded, encodedLength, decoded))
    );
    TEST_ASSERT_EQUAL_UINT16(MAX_PAYLOAD_SIZE, decoded.payloadLength);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(
        maximum.payload, decoded.payload, MAX_PAYLOAD_SIZE
    );
}

void testAllMessageTypesAndRequestIdRules() {
    const MessageType types[] = {
        MessageType::HELLO_REQUEST,
        MessageType::HELLO_RESPONSE,
        MessageType::OPERATION_REQUEST,
        MessageType::OPERATION_RESPONSE,
        MessageType::PROTOCOL_ERROR
    };
    uint8_t encoded[MAX_ENCODED_FRAME_SIZE] = {};
    for (size_t index = 0; index < sizeof(types) / sizeof(types[0]); ++index) {
        Frame frame = makeFrame(types[index], 0xFFFF);
        size_t encodedLength = 0;
        TEST_ASSERT_EQUAL_UINT8(
            static_cast<uint8_t>(EncodeResult::OK),
            static_cast<uint8_t>(encodeFrame(
                frame, encoded, sizeof(encoded), encodedLength
            ))
        );
        Frame decoded = {};
        TEST_ASSERT_EQUAL_UINT8(
            static_cast<uint8_t>(DecodeResult::OK),
            static_cast<uint8_t>(decodeFrame(encoded, encodedLength, decoded))
        );
        TEST_ASSERT_EQUAL_UINT8(
            static_cast<uint8_t>(types[index]),
            static_cast<uint8_t>(decoded.messageType)
        );
    }
    Frame reserved = makeFrame(MessageType::PROTOCOL_ERROR, 0x0000);
    size_t encodedLength = 0;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(EncodeResult::OK),
        static_cast<uint8_t>(encodeFrame(
            reserved, encoded, sizeof(encoded), encodedLength
        ))
    );
    reserved.messageType = MessageType::HELLO_REQUEST;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(EncodeResult::INVALID_REQUEST_ID),
        static_cast<uint8_t>(encodeFrame(
            reserved, encoded, sizeof(encoded), encodedLength
        ))
    );
}

void testExplicitLittleEndianEnvelopeFields() {
    Frame frame = makeFrame(MessageType::HELLO_REQUEST, 0x1234);
    frame.payloadLength = 2;
    frame.payload[0] = 0xA5;
    frame.payload[1] = 0x5A;
    uint8_t encoded[MAX_ENCODED_FRAME_SIZE] = {};
    size_t encodedLength = 0;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(EncodeResult::OK),
        static_cast<uint8_t>(encodeFrame(
            frame, encoded, sizeof(encoded), encodedLength
        ))
    );
    uint8_t decoded[MAX_DECODED_FRAME_SIZE] = {};
    size_t decodedLength = 0;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CobsResult::OK),
        static_cast<uint8_t>(cobsDecode(
            encoded,
            encodedLength - 1,
            decoded,
            sizeof(decoded),
            decodedLength
        ))
    );
    TEST_ASSERT_EQUAL_UINT32(12, decodedLength);
    TEST_ASSERT_EQUAL_HEX8(0x34, decoded[4]);
    TEST_ASSERT_EQUAL_HEX8(0x12, decoded[5]);
    TEST_ASSERT_EQUAL_HEX8(0x02, decoded[6]);
    TEST_ASSERT_EQUAL_HEX8(0x00, decoded[7]);
    const uint16_t crc = crc16CcittFalse(decoded, 10);
    TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(crc), decoded[10]);
    TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(crc >> 8), decoded[11]);
}

void testEncodeValidationAndOutputCapacity() {
    Frame frame = makeFrame();
    uint8_t encoded[MAX_ENCODED_FRAME_SIZE] = {};
    size_t encodedLength = 99;
    frame.payloadLength = MAX_PAYLOAD_SIZE + 1;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(EncodeResult::PAYLOAD_TOO_LARGE),
        static_cast<uint8_t>(encodeFrame(
            frame, encoded, sizeof(encoded), encodedLength
        ))
    );
    frame = makeFrame();
    frame.major = 1;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(EncodeResult::UNSUPPORTED_VERSION),
        static_cast<uint8_t>(encodeFrame(
            frame, encoded, sizeof(encoded), encodedLength
        ))
    );
    frame = makeFrame();
    frame.minor = 0;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(EncodeResult::UNSUPPORTED_VERSION),
        static_cast<uint8_t>(encodeFrame(
            frame, encoded, sizeof(encoded), encodedLength
        ))
    );
    frame = makeFrame(static_cast<MessageType>(0x12));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(EncodeResult::UNSUPPORTED_MESSAGE_TYPE),
        static_cast<uint8_t>(encodeFrame(
            frame, encoded, sizeof(encoded), encodedLength
        ))
    );
    frame = makeFrame();
    frame.flags = 1;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(EncodeResult::UNSUPPORTED_FLAGS),
        static_cast<uint8_t>(encodeFrame(
            frame, encoded, sizeof(encoded), encodedLength
        ))
    );
    frame = makeFrame();
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(EncodeResult::OUTPUT_TOO_SMALL),
        static_cast<uint8_t>(encodeFrame(frame, encoded, 11, encodedLength))
    );
    TEST_ASSERT_EQUAL_UINT32(0, encodedLength);
}

void testBinaryPayloadWithZerosRoundTrips() {
    Frame frame = makeFrame();
    const uint8_t payload[] = {0x00, 0x01, 0x00, 0x00, 0xFE, 0x00, 0xFF};
    frame.payloadLength = sizeof(payload);
    for (size_t index = 0; index < sizeof(payload); ++index) {
        frame.payload[index] = payload[index];
    }
    uint8_t encoded[MAX_ENCODED_FRAME_SIZE] = {};
    size_t encodedLength = 0;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(EncodeResult::OK),
        static_cast<uint8_t>(encodeFrame(
            frame, encoded, sizeof(encoded), encodedLength
        ))
    );
    for (size_t index = 0; index + 1 < encodedLength; ++index) {
        TEST_ASSERT_NOT_EQUAL(0, encoded[index]);
    }
    Frame decoded = {};
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(DecodeResult::OK),
        static_cast<uint8_t>(decodeFrame(encoded, encodedLength, decoded))
    );
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, decoded.payload, sizeof(payload));
}

void testDecodeRejectsMalformedFramingAndBounds() {
    Frame frame = {};
    const uint8_t emptyDelimiter[] = {0x00};
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(DecodeResult::EMPTY_FRAME),
        static_cast<uint8_t>(decodeFrame(
            emptyDelimiter, sizeof(emptyDelimiter), frame
        ))
    );
    const uint8_t missingDelimiter[] = {0x01};
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(DecodeResult::MISSING_DELIMITER),
        static_cast<uint8_t>(decodeFrame(
            missingDelimiter, sizeof(missingDelimiter), frame
        ))
    );
    const uint8_t malformed[] = {0x04, 0x11, 0x00};
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(DecodeResult::MALFORMED_COBS),
        static_cast<uint8_t>(decodeFrame(malformed, sizeof(malformed), frame))
    );
    uint8_t oversized[MAX_ENCODED_FRAME_SIZE + 1] = {};
    oversized[sizeof(oversized) - 1] = FRAME_DELIMITER;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(DecodeResult::ENCODED_FRAME_TOO_LARGE),
        static_cast<uint8_t>(decodeFrame(oversized, sizeof(oversized), frame))
    );
}

void testDecodeRejectsShortLengthAndTruncatedCrc() {
    const uint8_t tooShortDecoded[] = {0x00, 0x01, 0x10, 0x00, 0x01};
    uint8_t encoded[MAX_ENCODED_FRAME_SIZE] = {};
    size_t encodedLength = encodeDecoded(
        tooShortDecoded, sizeof(tooShortDecoded), encoded, sizeof(encoded)
    );
    Frame frame = {};
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(DecodeResult::DECODED_LENGTH_INVALID),
        static_cast<uint8_t>(decodeFrame(encoded, encodedLength, frame))
    );

    uint8_t truncatedCrc[] = {
        0x00, 0x01, 0x10, 0x00, 0x01, 0x00, 0x00, 0x00, 0xAA
    };
    encodedLength = encodeDecoded(
        truncatedCrc, sizeof(truncatedCrc), encoded, sizeof(encoded)
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(DecodeResult::DECODED_LENGTH_INVALID),
        static_cast<uint8_t>(decodeFrame(encoded, encodedLength, frame))
    );
}

void testDecodeRejectsPayloadAndGeometryMismatches() {
    uint8_t decoded[MIN_DECODED_FRAME_SIZE + 1] = {
        0x00, 0x01, 0x10, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    uint8_t encoded[MAX_ENCODED_FRAME_SIZE] = {};
    Frame frame = {};

    decoded[6] = 129;
    writeCrc(decoded, sizeof(decoded));
    size_t encodedLength = encodeDecoded(
        decoded, sizeof(decoded), encoded, sizeof(encoded)
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(DecodeResult::PAYLOAD_TOO_LARGE),
        static_cast<uint8_t>(decodeFrame(encoded, encodedLength, frame))
    );

    decoded[6] = 0;
    writeCrc(decoded, sizeof(decoded));
    encodedLength = encodeDecoded(decoded, sizeof(decoded), encoded, sizeof(encoded));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(DecodeResult::LENGTH_MISMATCH),
        static_cast<uint8_t>(decodeFrame(encoded, encodedLength, frame))
    );

    decoded[6] = 2;
    writeCrc(decoded, sizeof(decoded));
    encodedLength = encodeDecoded(decoded, sizeof(decoded), encoded, sizeof(encoded));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(DecodeResult::LENGTH_MISMATCH),
        static_cast<uint8_t>(decodeFrame(encoded, encodedLength, frame))
    );
}

void testDecodeRejectsBadCrcAndEnvelopeVocabulary() {
    Frame source = makeFrame();
    source.payloadLength = 1;
    source.payload[0] = 0xA5;
    uint8_t encoded[MAX_ENCODED_FRAME_SIZE] = {};
    size_t encodedLength = 0;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(EncodeResult::OK),
        static_cast<uint8_t>(encodeFrame(
            source, encoded, sizeof(encoded), encodedLength
        ))
    );
    uint8_t corruptDecoded[MAX_DECODED_FRAME_SIZE] = {};
    size_t corruptDecodedLength = 0;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(CobsResult::OK),
        static_cast<uint8_t>(cobsDecode(
            encoded,
            encodedLength - 1,
            corruptDecoded,
            sizeof(corruptDecoded),
            corruptDecodedLength
        ))
    );
    corruptDecoded[DECODED_HEADER_SIZE] ^= 0x01;
    encodedLength = encodeDecoded(
        corruptDecoded,
        corruptDecodedLength,
        encoded,
        sizeof(encoded)
    );
    Frame frame = {};
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(DecodeResult::CRC_MISMATCH),
        static_cast<uint8_t>(decodeFrame(encoded, encodedLength, frame))
    );

    uint8_t decoded[MIN_DECODED_FRAME_SIZE] = {
        0x00, 0x01, 0x10, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    struct Case {
        size_t offset;
        uint8_t value;
        DecodeResult expected;
    };
    const Case cases[] = {
        {0, 0x01, DecodeResult::UNSUPPORTED_MAJOR},
        {1, 0x00, DecodeResult::UNSUPPORTED_MINOR},
        {2, 0x12, DecodeResult::UNSUPPORTED_MESSAGE_TYPE},
        {3, 0x01, DecodeResult::UNSUPPORTED_FLAGS},
        {4, 0x00, DecodeResult::INVALID_REQUEST_ID}
    };
    for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        decoded[0] = 0x00;
        decoded[1] = 0x01;
        decoded[2] = 0x10;
        decoded[3] = 0x00;
        decoded[4] = 0x01;
        decoded[cases[index].offset] = cases[index].value;
        writeCrc(decoded, sizeof(decoded));
        encodedLength = encodeDecoded(
            decoded, sizeof(decoded), encoded, sizeof(encoded)
        );
        TEST_ASSERT_EQUAL_UINT8(
            static_cast<uint8_t>(cases[index].expected),
            static_cast<uint8_t>(decodeFrame(encoded, encodedLength, frame))
        );
    }
}

template <size_t D, size_t E>
void assertMinorTwoVector(const uint8_t (&decoded)[D], const uint8_t (&encoded)[E]) {
    TEST_ASSERT_EQUAL_UINT32(D, MIN_DECODED_FRAME_SIZE +
        static_cast<uint16_t>(decoded[6] | (decoded[7] << 8)));
    TEST_ASSERT_EQUAL_HEX16(static_cast<uint16_t>(decoded[D - 2] |
        (decoded[D - 1] << 8)), crc16CcittFalse(decoded, D - 2));
    Frame frame = {};
    frame.major = decoded[0]; frame.minor = decoded[1];
    frame.messageType = static_cast<MessageType>(decoded[2]);
    frame.flags = decoded[3]; frame.requestId = static_cast<uint16_t>(
        decoded[4] | (decoded[5] << 8));
    frame.payloadLength = static_cast<uint16_t>(decoded[6] | (decoded[7] << 8));
    for (size_t i = 0; i < frame.payloadLength; ++i) frame.payload[i] = decoded[8 + i];
    uint8_t actual[MAX_ENCODED_FRAME_SIZE] = {}; size_t actualLength = 0;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EncodeResult::OK),
        static_cast<uint8_t>(encodeFrame(frame, actual, sizeof(actual), actualLength)));
    TEST_ASSERT_EQUAL_UINT32(E, actualLength);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(encoded, actual, E);
    Frame roundTrip = {};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DecodeResult::OK),
        static_cast<uint8_t>(decodeFrame(encoded, E, roundTrip)));
    TEST_ASSERT_EQUAL_UINT8(VERSION_MINOR_0_2, roundTrip.minor);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(decoded + 8, roundTrip.payload, frame.payloadLength);
}

void testEveryNormativeHostProtocol02Vector() {
    const uint8_t hReqD[] = {0,2,1,0,0x34,0x12,2,0,1,2,0x62,0xF7};
    const uint8_t hReqE[] = {1,3,2,1,4,0x34,0x12,2,5,1,2,0x62,0xF7,0};
    assertMinorTwoVector(hReqD, hReqE);
    const uint8_t hResD[] = {0,2,2,0,0x34,0x12,0x10,0,2,0,7,0,1,1,1,1,1,0x80,0x1F,0,7,0,1,0,0x2C,0xC5};
    const uint8_t hResE[] = {1,3,2,2,4,0x34,0x12,0x10,2,2,2,7,8,1,1,1,1,1,0x80,0x1F,2,7,2,1,3,0x2C,0xC5,0};
    assertMinorTwoVector(hResD, hResE);
    const uint8_t pollD[] = {0,2,0x10,0,1,0x10,7,0,5,0x29,1,0,0,0,0,0xDD,0xFD};
    const uint8_t pollE[] = {1,3,2,0x10,4,1,0x10,7,4,5,0x29,1,1,1,1,3,0xDD,0xFD,0};
    assertMinorTwoVector(pollD, pollE);
    const uint8_t noneD[] = {0,2,0x11,0,1,0x10,0x26,0,5,0x29,1,0,0,0,0,0x7F,0x1D,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,0x36};
    const uint8_t noneE[] = {1,3,2,0x11,4,1,0x10,0x26,4,5,0x29,1,1,1,1,3,0x7F,0x1D,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,3,2,0x36,0};
    assertMinorTwoVector(noneD, noneE);
    const uint8_t oneD[] = {0,2,0x11,0,1,0x10,0x26,0,5,0x29,1,0,0,0,0,0x7F,0x1D,
        1,2,0x40,1,0x44,0x33,0x22,0x11,4,3,2,1,0x10,0x0E,0,0,1,2,0,0,0,0,0,0,0,0,0,0,0,0x0B,0x49};
    const uint8_t oneE[] = {1,3,2,0x11,4,1,0x10,0x26,4,5,0x29,1,1,1,1,0x11,0x7F,0x1D,
        1,2,0x40,1,0x44,0x33,0x22,0x11,4,3,2,1,0x10,0x0E,1,3,1,2,1,1,1,1,1,1,1,1,1,1,3,0x0B,0x49,0};
    assertMinorTwoVector(oneD, oneE);
    const uint8_t consumeD[] = {0,2,0x10,0,2,0x10,0x10,0,5,0x2A,1,0,0,0x7F,9,2,0x44,0x33,0x22,0x11,4,3,2,1,0xDA,0xCB};
    const uint8_t consumeE[] = {1,3,2,0x10,4,2,0x10,0x10,4,5,0x2A,1,1,0x0E,0x7F,9,2,0x44,0x33,0x22,0x11,4,3,2,1,0xDA,0xCB,0};
    assertMinorTwoVector(consumeD, consumeE);
    const uint8_t successD[] = {0,2,0x11,0,2,0x10,9,0,5,0x2A,1,0,0,0,0,0,0,0x1B,0x85};
    const uint8_t successE[] = {1,3,2,0x11,4,2,0x10,9,4,5,0x2A,1,1,1,1,1,1,3,0x1B,0x85,0};
    assertMinorTwoVector(successD, successE);
    const uint8_t notFoundD[] = {0,2,0x11,0,2,0x10,9,0,5,0x2A,1,0,0,5,1,0,0,0x6E,0x0E};
    const uint8_t notFoundE[] = {1,3,2,0x11,4,2,0x10,9,4,5,0x2A,1,1,3,5,1,1,3,0x6E,0x0E,0};
    assertMinorTwoVector(notFoundD, notFoundE);
}

void testFrameMinorOneAndTwoCoexistWithoutGeometryChange() {
    Frame one = makeFrame();
    Frame two = one; two.minor = VERSION_MINOR_0_2; two.requestId = 0x2345;
    uint8_t bytes[MAX_ENCODED_FRAME_SIZE] = {}; size_t length = 0;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EncodeResult::OK),
        static_cast<uint8_t>(encodeFrame(two, bytes, sizeof(bytes), length)));
    Frame decoded = {};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DecodeResult::OK),
        static_cast<uint8_t>(decodeFrame(bytes, length, decoded)));
    TEST_ASSERT_EQUAL_UINT8(2, decoded.minor);
    two.minor = 3;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EncodeResult::UNSUPPORTED_VERSION),
        static_cast<uint8_t>(encodeFrame(two, bytes, sizeof(bytes), length)));
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(testCrc16CcittFalseKnownAndFrameVectors);
    RUN_TEST(testCrcDetectsOneBitPayloadCorruption);
    RUN_TEST(testCobsRepresentativeZeroLayouts);
    RUN_TEST(testCobsExactRepresentativeEncodings);
    RUN_TEST(testCobsBoundaryAndCapacity);
    RUN_TEST(testCobsMalformedInputsFailClosed);
    RUN_TEST(testNormativeHelloRequestVector);
    RUN_TEST(testNormativePingRequestVectorIsOpaquePayload);
    RUN_TEST(testMinimumAndMaximumFramesRoundTrip);
    RUN_TEST(testAllMessageTypesAndRequestIdRules);
    RUN_TEST(testExplicitLittleEndianEnvelopeFields);
    RUN_TEST(testEncodeValidationAndOutputCapacity);
    RUN_TEST(testBinaryPayloadWithZerosRoundTrips);
    RUN_TEST(testDecodeRejectsMalformedFramingAndBounds);
    RUN_TEST(testDecodeRejectsShortLengthAndTruncatedCrc);
    RUN_TEST(testDecodeRejectsPayloadAndGeometryMismatches);
    RUN_TEST(testDecodeRejectsBadCrcAndEnvelopeVocabulary);
    RUN_TEST(testEveryNormativeHostProtocol02Vector);
    RUN_TEST(testFrameMinorOneAndTwoCoexistWithoutGeometryChange);
    return UNITY_END();
}
