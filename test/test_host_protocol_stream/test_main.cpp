#include <unity.h>

#include <cstring>

#include <host_protocol_stream.h>

using namespace HostProtocol;

namespace {

const uint8_t HELLO_FRAME[] = {
    0x01, 0x03, 0x01, 0x01, 0x04, 0x34, 0x12,
    0x02, 0x05, 0x01, 0x01, 0x45, 0xEA, 0x00
};

const uint8_t PING_FRAME[] = {
    0x01, 0x03, 0x01, 0x10, 0x02, 0x01, 0x02, 0x07, 0x04,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x03, 0x64, 0x1D, 0x00
};

uint8_t raw(StreamEvent event) {
    return static_cast<uint8_t>(event);
}

uint8_t raw(DecodeResult result) {
    return static_cast<uint8_t>(result);
}

void assertHello(const StreamResult& result) {
    TEST_ASSERT_EQUAL_UINT8(raw(StreamEvent::FRAME_READY), raw(result.event));
    TEST_ASSERT_EQUAL_UINT8(raw(DecodeResult::OK), raw(result.decodeResult));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MessageType::HELLO_REQUEST),
        static_cast<uint8_t>(result.frame.messageType));
    TEST_ASSERT_EQUAL_HEX16(0x1234, result.frame.requestId);
    TEST_ASSERT_EQUAL_UINT16(2, result.frame.payloadLength);
    TEST_ASSERT_EQUAL_UINT8(1, result.frame.payload[0]);
    TEST_ASSERT_EQUAL_UINT8(1, result.frame.payload[1]);
}

void assertPing(const StreamResult& result) {
    TEST_ASSERT_EQUAL_UINT8(raw(StreamEvent::FRAME_READY), raw(result.event));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MessageType::OPERATION_REQUEST),
        static_cast<uint8_t>(result.frame.messageType));
    TEST_ASSERT_EQUAL_HEX16(0x0001, result.frame.requestId);
    TEST_ASSERT_EQUAL_UINT16(7, result.frame.payloadLength);
}

void testEveryHelloSplitPoint() {
    for (size_t split = 0; split <= sizeof(HELLO_FRAME); ++split) {
        StreamParser parser;
        StreamResult first = parser.consume(HELLO_FRAME, split);
        if (split < sizeof(HELLO_FRAME)) {
            TEST_ASSERT_EQUAL_UINT8(raw(StreamEvent::NONE), raw(first.event));
        }
        StreamResult second = parser.consume(
            HELLO_FRAME + split, sizeof(HELLO_FRAME) - split);
        assertHello(split == sizeof(HELLO_FRAME) ? first : second);
    }
}

void testEveryPingSplitPoint() {
    for (size_t split = 0; split <= sizeof(PING_FRAME); ++split) {
        StreamParser parser;
        StreamResult first = parser.consume(PING_FRAME, split);
        if (split < sizeof(PING_FRAME)) {
            TEST_ASSERT_EQUAL_UINT8(raw(StreamEvent::NONE), raw(first.event));
        }
        StreamResult second = parser.consume(
            PING_FRAME + split, sizeof(PING_FRAME) - split);
        assertPing(split == sizeof(PING_FRAME) ? first : second);
    }
}

void testThreeWayAndByteAtATimeInput() {
    StreamParser parser;
    TEST_ASSERT_EQUAL_UINT8(raw(StreamEvent::NONE), raw(
        parser.consume(HELLO_FRAME, 3).event));
    TEST_ASSERT_EQUAL_UINT8(raw(StreamEvent::NONE), raw(
        parser.consume(HELLO_FRAME + 3, 5).event));
    assertHello(parser.consume(HELLO_FRAME + 8, sizeof(HELLO_FRAME) - 8));

    parser.reset();
    StreamResult result = {};
    for (size_t index = 0; index < sizeof(PING_FRAME); ++index) {
        result = parser.consume(PING_FRAME + index, 1);
        TEST_ASSERT_EQUAL_UINT32(1, result.consumed);
        if (index + 1 < sizeof(PING_FRAME)) {
            TEST_ASSERT_EQUAL_UINT8(raw(StreamEvent::NONE), raw(result.event));
        }
    }
    assertPing(result);
}

void testBackToBackFramesAndConsumedCounts() {
    uint8_t input[sizeof(HELLO_FRAME) + sizeof(PING_FRAME) * 2] = {};
    size_t offset = 0;
    for (size_t i = 0; i < sizeof(HELLO_FRAME); ++i) input[offset++] = HELLO_FRAME[i];
    for (size_t i = 0; i < sizeof(PING_FRAME); ++i) input[offset++] = PING_FRAME[i];
    for (size_t i = 0; i < sizeof(PING_FRAME); ++i) input[offset++] = PING_FRAME[i];

    StreamParser parser;
    StreamResult first = parser.consume(input, sizeof(input));
    assertHello(first);
    TEST_ASSERT_EQUAL_UINT32(sizeof(HELLO_FRAME), first.consumed);
    StreamResult second = parser.consume(input + first.consumed,
        sizeof(input) - first.consumed);
    assertPing(second);
    TEST_ASSERT_EQUAL_UINT32(sizeof(PING_FRAME), second.consumed);
    StreamResult third = parser.consume(input + first.consumed + second.consumed,
        sizeof(input) - first.consumed - second.consumed);
    assertPing(third);
}

void testValidThenPartialNextRetainsOnlyConsumedBytes() {
    uint8_t input[sizeof(HELLO_FRAME) + 5] = {};
    for (size_t i = 0; i < sizeof(HELLO_FRAME); ++i) input[i] = HELLO_FRAME[i];
    for (size_t i = 0; i < 5; ++i) input[sizeof(HELLO_FRAME) + i] = PING_FRAME[i];
    StreamParser parser;
    StreamResult first = parser.consume(input, sizeof(input));
    assertHello(first);
    TEST_ASSERT_EQUAL_UINT32(sizeof(HELLO_FRAME), first.consumed);
    TEST_ASSERT_EQUAL_UINT32(0, parser.candidateLength());
    StreamResult partial = parser.consume(input + first.consumed,
        sizeof(input) - first.consumed);
    TEST_ASSERT_EQUAL_UINT8(raw(StreamEvent::NONE), raw(partial.event));
    TEST_ASSERT_EQUAL_UINT32(5, parser.candidateLength());
    assertPing(parser.consume(PING_FRAME + 5, sizeof(PING_FRAME) - 5));
}

void testEmptyInputAndDelimitersAreIgnored() {
    const uint8_t delimiters[] = {0, 0, 0};
    StreamParser parser;
    StreamResult empty = parser.consume(nullptr, 0);
    TEST_ASSERT_EQUAL_UINT8(raw(StreamEvent::NONE), raw(empty.event));
    TEST_ASSERT_EQUAL_UINT32(0, empty.consumed);
    StreamResult ignored = parser.consume(delimiters, sizeof(delimiters));
    TEST_ASSERT_EQUAL_UINT8(raw(StreamEvent::NONE), raw(ignored.event));
    TEST_ASSERT_EQUAL_UINT32(sizeof(delimiters), ignored.consumed);

    uint8_t input[sizeof(delimiters) + sizeof(HELLO_FRAME)] = {};
    for (size_t i = 0; i < sizeof(HELLO_FRAME); ++i) input[3 + i] = HELLO_FRAME[i];
    StreamResult hello = parser.consume(input, sizeof(input));
    assertHello(hello);
    TEST_ASSERT_EQUAL_UINT32(sizeof(input), hello.consumed);
}

void assertRejectThenRecover(
    const uint8_t* malformed,
    size_t malformedLength,
    DecodeResult expected
) {
    uint8_t input[MAX_ENCODED_FRAME_SIZE * 2] = {};
    for (size_t i = 0; i < malformedLength; ++i) input[i] = malformed[i];
    for (size_t i = 0; i < sizeof(HELLO_FRAME); ++i) {
        input[malformedLength + i] = HELLO_FRAME[i];
    }
    StreamParser parser;
    StreamResult rejected = parser.consume(
        input, malformedLength + sizeof(HELLO_FRAME));
    TEST_ASSERT_EQUAL_UINT8(raw(StreamEvent::FRAME_REJECTED), raw(rejected.event));
    TEST_ASSERT_EQUAL_UINT8(raw(expected), raw(rejected.decodeResult));
    TEST_ASSERT_EQUAL_UINT32(malformedLength, rejected.consumed);
    assertHello(parser.consume(input + rejected.consumed,
        malformedLength + sizeof(HELLO_FRAME) - rejected.consumed));
}

void testMalformedCandidatesRecoverAtNextDelimiter() {
    const uint8_t badCobs[] = {0x03, 0x11, 0x00};
    assertRejectThenRecover(badCobs, sizeof(badCobs), DecodeResult::MALFORMED_COBS);

    uint8_t badCrc[sizeof(HELLO_FRAME)] = {};
    for (size_t i = 0; i < sizeof(HELLO_FRAME); ++i) badCrc[i] = HELLO_FRAME[i];
    badCrc[sizeof(badCrc) - 2] ^= 1;
    assertRejectThenRecover(badCrc, sizeof(badCrc), DecodeResult::CRC_MISMATCH);

    uint8_t decodedBadLength[12] = {
        0, 1, 1, 0, 0x34, 0x12, 3, 0, 1, 1, 0, 0
    };
    const uint16_t lengthCrc = crc16CcittFalse(decodedBadLength, 10);
    decodedBadLength[10] = static_cast<uint8_t>(lengthCrc);
    decodedBadLength[11] = static_cast<uint8_t>(lengthCrc >> 8);
    uint8_t badLength[MAX_ENCODED_FRAME_SIZE] = {};
    size_t badLengthCandidate = 0;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CobsResult::OK),
        static_cast<uint8_t>(cobsEncode(decodedBadLength,
            sizeof(decodedBadLength), badLength, sizeof(badLength) - 1,
            badLengthCandidate)));
    badLength[badLengthCandidate] = 0;
    assertRejectThenRecover(badLength, badLengthCandidate + 1,
        DecodeResult::LENGTH_MISMATCH);

    const uint8_t truncated[] = {0x01, 0x03, 0x01, 0x00};
    assertRejectThenRecover(truncated, sizeof(truncated),
        DecodeResult::MALFORMED_COBS);
}

void testTrustworthyEnvelopeFailuresRemainClassified() {
    const struct { size_t decodedIndex; uint8_t value; DecodeResult expected; } cases[] = {
        {0, 1, DecodeResult::UNSUPPORTED_MAJOR},
        {1, 3, DecodeResult::UNSUPPORTED_MINOR},
        {2, 0x55, DecodeResult::UNSUPPORTED_MESSAGE_TYPE},
        {3, 1, DecodeResult::UNSUPPORTED_FLAGS},
        {4, 0, DecodeResult::INVALID_REQUEST_ID}
    };
    for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); ++c) {
        uint8_t decoded[12] = {0, 1, 1, 0, 0x34, 0x12, 2, 0, 1, 1, 0, 0};
        decoded[cases[c].decodedIndex] = cases[c].value;
        if (cases[c].expected == DecodeResult::INVALID_REQUEST_ID) {
            decoded[5] = 0;
        }
        const uint16_t crc = crc16CcittFalse(decoded, 10);
        decoded[10] = static_cast<uint8_t>(crc);
        decoded[11] = static_cast<uint8_t>(crc >> 8);
        uint8_t encoded[MAX_ENCODED_FRAME_SIZE] = {};
        size_t candidateLength = 0;
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CobsResult::OK),
            static_cast<uint8_t>(cobsEncode(decoded, sizeof(decoded), encoded,
                sizeof(encoded) - 1, candidateLength)));
        encoded[candidateLength] = 0;
        StreamParser parser;
        StreamResult result = parser.consume(encoded, candidateLength + 1);
        TEST_ASSERT_EQUAL_UINT8(raw(StreamEvent::FRAME_REJECTED), raw(result.event));
        TEST_ASSERT_EQUAL_UINT8(raw(cases[c].expected), raw(result.decodeResult));
    }
}

void testExactCandidateBoundAndOversizeRecovery() {
    uint8_t exact[MAX_COBS_CANDIDATE_SIZE + 1] = {};
    for (size_t i = 0; i < MAX_COBS_CANDIDATE_SIZE; ++i) exact[i] = 1;
    StreamParser parser;
    StreamResult exactResult = parser.consume(exact, sizeof(exact));
    TEST_ASSERT_EQUAL_UINT8(raw(StreamEvent::FRAME_REJECTED), raw(exactResult.event));
    TEST_ASSERT_NOT_EQUAL(raw(DecodeResult::ENCODED_FRAME_TOO_LARGE),
        raw(exactResult.decodeResult));

    uint8_t oversized[401] = {};
    for (size_t i = 0; i < sizeof(oversized) - 1; ++i) oversized[i] = 1;
    StreamResult partial = parser.consume(oversized, sizeof(oversized) - 1);
    TEST_ASSERT_EQUAL_UINT8(raw(StreamEvent::NONE), raw(partial.event));
    TEST_ASSERT_TRUE(parser.isDiscardingOversize());
    TEST_ASSERT_EQUAL_UINT32(0, parser.candidateLength());
    StreamResult overflow = parser.consume(oversized + sizeof(oversized) - 1, 1);
    TEST_ASSERT_EQUAL_UINT8(raw(StreamEvent::OVERSIZED_CANDIDATE), raw(overflow.event));
    TEST_ASSERT_FALSE(parser.isDiscardingOversize());
    assertHello(parser.consume(HELLO_FRAME, sizeof(HELLO_FRAME)));
}

void testResetClearsEveryFramingState() {
    StreamParser parser;
    parser.reset();
    assertHello(parser.consume(HELLO_FRAME, sizeof(HELLO_FRAME)));
    parser.consume(PING_FRAME, 5);
    TEST_ASSERT_EQUAL_UINT32(5, parser.candidateLength());
    parser.reset();
    TEST_ASSERT_EQUAL_UINT32(0, parser.candidateLength());
    assertHello(parser.consume(HELLO_FRAME, sizeof(HELLO_FRAME)));

    uint8_t oversized[MAX_COBS_CANDIDATE_SIZE + 1];
    for (size_t i = 0; i < sizeof(oversized); ++i) oversized[i] = 1;
    parser.consume(oversized, sizeof(oversized));
    TEST_ASSERT_TRUE(parser.isDiscardingOversize());
    parser.reset();
    TEST_ASSERT_FALSE(parser.isDiscardingOversize());
    assertHello(parser.consume(HELLO_FRAME, sizeof(HELLO_FRAME)));
}

void testZeroHeavyBinaryPayloadRoundTrips() {
    Frame frame = {};
    frame.major = VERSION_MAJOR;
    frame.minor = VERSION_MINOR;
    frame.messageType = MessageType::OPERATION_REQUEST;
    frame.requestId = 7;
    frame.payloadLength = MAX_PAYLOAD_SIZE;
    for (size_t i = 0; i < MAX_PAYLOAD_SIZE; ++i) {
        frame.payload[i] = i % 3 == 0 ? 0 : static_cast<uint8_t>(i);
    }
    uint8_t encoded[MAX_ENCODED_FRAME_SIZE] = {};
    size_t encodedLength = 0;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EncodeResult::OK),
        static_cast<uint8_t>(encodeFrame(frame, encoded, sizeof(encoded), encodedLength)));
    StreamParser parser;
    StreamResult result = parser.consume(encoded, encodedLength);
    TEST_ASSERT_EQUAL_UINT8(raw(StreamEvent::FRAME_READY), raw(result.event));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(frame.payload, result.frame.payload, MAX_PAYLOAD_SIZE);
}

void testDeterministicMalformedCorpus() {
    for (uint16_t value = 1; value <= 255; ++value) {
        const uint8_t candidate[] = {static_cast<uint8_t>(value), 0};
        StreamParser parser;
        StreamResult result = parser.consume(candidate, sizeof(candidate));
        TEST_ASSERT_EQUAL_UINT8(raw(StreamEvent::FRAME_REJECTED), raw(result.event));
    }
    for (size_t length = 1; length < sizeof(HELLO_FRAME) - 1; ++length) {
        uint8_t candidate[sizeof(HELLO_FRAME)] = {};
        for (size_t i = 0; i < length; ++i) candidate[i] = HELLO_FRAME[i];
        candidate[length] = 0;
        StreamParser parser;
        StreamResult rejected = parser.consume(candidate, length + 1);
        TEST_ASSERT_EQUAL_UINT8(raw(StreamEvent::FRAME_REJECTED), raw(rejected.event));
        assertHello(parser.consume(HELLO_FRAME, sizeof(HELLO_FRAME)));
    }
}

void testInvalidArgumentDoesNotAlterPartialState() {
    StreamParser parser;
    parser.consume(HELLO_FRAME, 4);
    StreamResult invalid = parser.consume(nullptr, 1);
    TEST_ASSERT_EQUAL_UINT8(raw(StreamEvent::INVALID_ARGUMENT), raw(invalid.event));
    TEST_ASSERT_EQUAL_UINT32(0, invalid.consumed);
    TEST_ASSERT_EQUAL_UINT32(4, parser.candidateLength());
    assertHello(parser.consume(HELLO_FRAME + 4, sizeof(HELLO_FRAME) - 4));
}

void testMixedMinorFramesRemainFrameAuthoritative() {
    Frame minorTwo = {};
    minorTwo.major = VERSION_MAJOR;
    minorTwo.minor = VERSION_MINOR_0_2;
    minorTwo.messageType = MessageType::HELLO_REQUEST;
    minorTwo.requestId = 0x4567;
    minorTwo.payloadLength = 2;
    minorTwo.payload[0] = 1;
    minorTwo.payload[1] = 2;
    uint8_t encodedTwo[MAX_ENCODED_FRAME_SIZE] = {};
    size_t encodedTwoLength = 0;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EncodeResult::OK),
        static_cast<uint8_t>(encodeFrame(minorTwo, encodedTwo,
            sizeof(encodedTwo), encodedTwoLength)));

    for (size_t split = 0; split <= encodedTwoLength; ++split) {
        StreamParser splitParser;
        StreamResult first = splitParser.consume(encodedTwo, split);
        if (split < encodedTwoLength) {
            TEST_ASSERT_EQUAL_UINT8(raw(StreamEvent::NONE), raw(first.event));
        }
        StreamResult second = splitParser.consume(encodedTwo + split,
            encodedTwoLength - split);
        const StreamResult& ready = split == encodedTwoLength ? first : second;
        TEST_ASSERT_EQUAL_UINT8(raw(StreamEvent::FRAME_READY), raw(ready.event));
        TEST_ASSERT_EQUAL_UINT8(VERSION_MINOR_0_2, ready.frame.minor);
    }

    uint8_t mixed[sizeof(HELLO_FRAME) + MAX_ENCODED_FRAME_SIZE] = {};
    memcpy(mixed, HELLO_FRAME, sizeof(HELLO_FRAME));
    memcpy(mixed + sizeof(HELLO_FRAME), encodedTwo, encodedTwoLength);
    StreamParser parser;
    StreamResult one = parser.consume(mixed, sizeof(HELLO_FRAME) + encodedTwoLength);
    TEST_ASSERT_EQUAL_UINT8(VERSION_MINOR_0_1, one.frame.minor);
    StreamResult two = parser.consume(mixed + one.consumed,
        sizeof(HELLO_FRAME) + encodedTwoLength - one.consumed);
    TEST_ASSERT_EQUAL_UINT8(raw(StreamEvent::FRAME_READY), raw(two.event));
    TEST_ASSERT_EQUAL_UINT8(VERSION_MINOR_0_2, two.frame.minor);
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(testEveryHelloSplitPoint);
    RUN_TEST(testEveryPingSplitPoint);
    RUN_TEST(testThreeWayAndByteAtATimeInput);
    RUN_TEST(testBackToBackFramesAndConsumedCounts);
    RUN_TEST(testValidThenPartialNextRetainsOnlyConsumedBytes);
    RUN_TEST(testEmptyInputAndDelimitersAreIgnored);
    RUN_TEST(testMalformedCandidatesRecoverAtNextDelimiter);
    RUN_TEST(testTrustworthyEnvelopeFailuresRemainClassified);
    RUN_TEST(testExactCandidateBoundAndOversizeRecovery);
    RUN_TEST(testResetClearsEveryFramingState);
    RUN_TEST(testZeroHeavyBinaryPayloadRoundTrips);
    RUN_TEST(testDeterministicMalformedCorpus);
    RUN_TEST(testInvalidArgumentDoesNotAlterPartialState);
    RUN_TEST(testMixedMinorFramesRemainFrameAuthoritative);
    return UNITY_END();
}
