#include <unity.h>

#include <host_transport.h>

using namespace HostProtocol;
using namespace HostTransport;

namespace {

struct FakeStream {
    uint8_t input[512];
    size_t inputLength;
    size_t inputOffset;
    uint8_t output[512];
    size_t outputLength;
    size_t writable;
    size_t maximumWrite;
    size_t availableCalls;
    size_t readCalls;
    size_t writableCalls;
    size_t writeCalls;
    bool failRead;

    FakeStream() : inputLength(0), inputOffset(0), outputLength(0),
        writable(0), maximumWrite(512), availableCalls(0), readCalls(0),
        writableCalls(0), writeCalls(0), failRead(false) {}

    int available() {
        ++availableCalls;
        return static_cast<int>(inputLength - inputOffset);
    }

    int read() {
        ++readCalls;
        if (failRead || inputOffset >= inputLength) return -1;
        return input[inputOffset++];
    }

    int availableForWrite() {
        ++writableCalls;
        return static_cast<int>(writable);
    }

    size_t write(const uint8_t* bytes, size_t length) {
        ++writeCalls;
        size_t accepted = length;
        if (accepted > writable) accepted = writable;
        if (accepted > maximumWrite) accepted = maximumWrite;
        for (size_t index = 0; index < accepted; ++index) {
            output[outputLength++] = bytes[index];
        }
        return accepted;
    }

    void appendInput(const uint8_t* bytes, size_t length) {
        for (size_t index = 0; index < length; ++index) {
            input[inputLength++] = bytes[index];
        }
    }
};

Frame makeFrame(size_t payloadLength = 0, uint16_t requestId = 1) {
    Frame frame = {};
    frame.major = VERSION_MAJOR;
    frame.minor = VERSION_MINOR;
    frame.messageType = MessageType::OPERATION_REQUEST;
    frame.flags = FLAGS_NONE;
    frame.requestId = requestId;
    frame.payloadLength = static_cast<uint16_t>(payloadLength);
    for (size_t index = 0; index < payloadLength; ++index) {
        frame.payload[index] = static_cast<uint8_t>(index);
    }
    return frame;
}

size_t encode(const Frame& frame, uint8_t* output) {
    size_t length = 0;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EncodeResult::OK),
        static_cast<uint8_t>(encodeFrame(
            frame, output, MAX_ENCODED_FRAME_SIZE, length)));
    return length;
}

void testRxNoInputAndZeroBudgetPerformNoIo() {
    FakeStream stream;
    Adapter<FakeStream> adapter(stream);
    RxResult zero = adapter.serviceRx(0);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RxStatus::NO_ACTIVITY),
        static_cast<uint8_t>(zero.status));
    TEST_ASSERT_EQUAL_UINT32(0, stream.availableCalls);
    TEST_ASSERT_EQUAL_UINT32(0, stream.readCalls);
    RxResult empty = adapter.serviceRx(8);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RxStatus::NO_ACTIVITY),
        static_cast<uint8_t>(empty.status));
    TEST_ASSERT_EQUAL_UINT32(0, stream.readCalls);
}

void testRxCompletePartialAndByteAtATime() {
    uint8_t encoded[MAX_ENCODED_FRAME_SIZE] = {};
    const size_t length = encode(makeFrame(7), encoded);
    FakeStream stream;
    Adapter<FakeStream> adapter(stream);
    stream.appendInput(encoded, length);
    RxResult complete = adapter.serviceRx(length);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RxStatus::PARSER_EVENT),
        static_cast<uint8_t>(complete.status));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StreamEvent::FRAME_READY),
        static_cast<uint8_t>(complete.parserResult.event));
    TEST_ASSERT_EQUAL_UINT32(length, complete.bytesRead);

    FakeStream splitStream;
    Adapter<FakeStream> split(splitStream);
    splitStream.appendInput(encoded, length);
    for (size_t index = 0; index < length; ++index) {
        RxResult result = split.serviceRx(1);
        TEST_ASSERT_EQUAL_UINT32(1, result.bytesRead);
        if (index + 1 == length) {
            TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RxStatus::PARSER_EVENT),
                static_cast<uint8_t>(result.status));
        } else {
            TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RxStatus::PROGRESS),
                static_cast<uint8_t>(result.status));
        }
    }
}

void testRxBudgetIsExactAndPartialPersists() {
    uint8_t encoded[MAX_ENCODED_FRAME_SIZE] = {};
    const size_t length = encode(makeFrame(20), encoded);
    FakeStream stream;
    stream.appendInput(encoded, length);
    Adapter<FakeStream> adapter(stream);
    RxResult first = adapter.serviceRx(3);
    TEST_ASSERT_EQUAL_UINT32(3, first.bytesRead);
    TEST_ASSERT_EQUAL_UINT32(3, stream.inputOffset);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RxStatus::PROGRESS),
        static_cast<uint8_t>(first.status));
    RxResult second = adapter.serviceRx(length);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RxStatus::PARSER_EVENT),
        static_cast<uint8_t>(second.status));
    TEST_ASSERT_EQUAL_UINT32(length, stream.inputOffset);
}

void testRxStopsBeforeSecondFrame() {
    uint8_t first[MAX_ENCODED_FRAME_SIZE] = {};
    uint8_t second[MAX_ENCODED_FRAME_SIZE] = {};
    const size_t firstLength = encode(makeFrame(0, 1), first);
    const size_t secondLength = encode(makeFrame(1, 2), second);
    FakeStream stream;
    stream.appendInput(first, firstLength);
    stream.appendInput(second, secondLength);
    Adapter<FakeStream> adapter(stream);
    RxResult firstResult = adapter.serviceRx(firstLength + secondLength);
    TEST_ASSERT_EQUAL_UINT32(firstLength, firstResult.bytesRead);
    TEST_ASSERT_EQUAL_UINT32(firstLength, stream.inputOffset);
    TEST_ASSERT_EQUAL_HEX16(1, firstResult.parserResult.frame.requestId);
    RxResult secondResult = adapter.serviceRx(firstLength + secondLength);
    TEST_ASSERT_EQUAL_HEX16(2, secondResult.parserResult.frame.requestId);
    TEST_ASSERT_EQUAL_UINT32(firstLength + secondLength, stream.inputOffset);
}

void testRxPreservesMalformedAndOversizeEvents() {
    const uint8_t malformed[] = {3, 0x11, 0};
    FakeStream stream;
    stream.appendInput(malformed, sizeof(malformed));
    Adapter<FakeStream> adapter(stream);
    RxResult rejected = adapter.serviceRx(10);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StreamEvent::FRAME_REJECTED),
        static_cast<uint8_t>(rejected.parserResult.event));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DecodeResult::MALFORMED_COBS),
        static_cast<uint8_t>(rejected.parserResult.decodeResult));

    for (size_t index = 0; index < MAX_COBS_CANDIDATE_SIZE + 1; ++index) {
        stream.input[stream.inputLength++] = 1;
    }
    stream.input[stream.inputLength++] = 0;
    RxResult overflow = adapter.serviceRx(MAX_COBS_CANDIDATE_SIZE + 2);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(StreamEvent::OVERSIZED_CANDIDATE),
        static_cast<uint8_t>(overflow.parserResult.event));
}

void testRxInputErrorIsSeparate() {
    FakeStream stream;
    stream.inputLength = 1;
    stream.failRead = true;
    Adapter<FakeStream> adapter(stream);
    RxResult result = adapter.serviceRx(1);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RxStatus::INPUT_ERROR),
        static_cast<uint8_t>(result.status));
    TEST_ASSERT_EQUAL_UINT32(0, result.bytesRead);
}

void testTxSubmissionValidationAndBusy() {
    FakeStream stream;
    Adapter<FakeStream> adapter(stream);
    uint8_t encoded[MAX_ENCODED_FRAME_SIZE + 1] = {};
    const size_t length = encode(makeFrame(), encoded);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SubmitStatus::INVALID_ARGUMENT),
        static_cast<uint8_t>(adapter.submit(nullptr, length)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SubmitStatus::INVALID_ARGUMENT),
        static_cast<uint8_t>(adapter.submit(encoded, 0)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SubmitStatus::INVALID_ARGUMENT),
        static_cast<uint8_t>(adapter.submit(encoded, sizeof(encoded))));
    encoded[length - 1] = 1;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SubmitStatus::INVALID_FRAME),
        static_cast<uint8_t>(adapter.submit(encoded, length)));
    encoded[length - 1] = 0;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SubmitStatus::OK),
        static_cast<uint8_t>(adapter.submit(encoded, length)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SubmitStatus::BUSY),
        static_cast<uint8_t>(adapter.submit(encoded, length)));
}

void testTxZeroBudgetAndBlockedCapacityPerformNoWrite() {
    FakeStream stream;
    Adapter<FakeStream> adapter(stream);
    uint8_t encoded[MAX_ENCODED_FRAME_SIZE] = {};
    const size_t length = encode(makeFrame(), encoded);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SubmitStatus::OK),
        static_cast<uint8_t>(adapter.submit(encoded, length)));
    stream.writable = length;
    TxResult zero = adapter.serviceTx(0);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TxStatus::BLOCKED),
        static_cast<uint8_t>(zero.status));
    TEST_ASSERT_EQUAL_UINT32(0, stream.writableCalls);
    TEST_ASSERT_EQUAL_UINT32(0, stream.writeCalls);
    stream.writable = 0;
    TxResult blocked = adapter.serviceTx(length);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TxStatus::BLOCKED),
        static_cast<uint8_t>(blocked.status));
    TEST_ASSERT_EQUAL_UINT32(0, stream.writeCalls);
}

void testTxMinimumAndMaximumFramesAreExact() {
    const size_t payloads[] = {0, MAX_PAYLOAD_SIZE};
    for (size_t test = 0; test < 2; ++test) {
        FakeStream stream;
        Adapter<FakeStream> adapter(stream);
        uint8_t encoded[MAX_ENCODED_FRAME_SIZE] = {};
        const size_t length = encode(makeFrame(payloads[test]), encoded);
        if (test == 1) TEST_ASSERT_EQUAL_UINT32(MAX_ENCODED_FRAME_SIZE, length);
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SubmitStatus::OK),
            static_cast<uint8_t>(adapter.submit(encoded, length)));
        stream.writable = MAX_ENCODED_FRAME_SIZE;
        TxResult sent = adapter.serviceTx(MAX_ENCODED_FRAME_SIZE);
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TxStatus::COMPLETE),
            static_cast<uint8_t>(sent.status));
        TEST_ASSERT_EQUAL_UINT8_ARRAY(encoded, stream.output, length);
        TEST_ASSERT_FALSE(adapter.hasPendingTx());
    }
}

void testTxPartialWritesResumeWithoutDuplicateOrSkip() {
    uint8_t encoded[MAX_ENCODED_FRAME_SIZE] = {};
    const size_t length = encode(makeFrame(MAX_PAYLOAD_SIZE), encoded);
    FakeStream stream;
    Adapter<FakeStream> adapter(stream);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SubmitStatus::OK),
        static_cast<uint8_t>(adapter.submit(encoded, length)));

    const size_t accepts[] = {3, 0, 17, MAX_ENCODED_FRAME_SIZE};
    size_t expectedOffset = 0;
    for (size_t index = 0; index < 4; ++index) {
        stream.writable = MAX_ENCODED_FRAME_SIZE;
        stream.maximumWrite = accepts[index];
        TxResult result = adapter.serviceTx(MAX_ENCODED_FRAME_SIZE);
        const size_t accepted = accepts[index] > length - expectedOffset
            ? length - expectedOffset : accepts[index];
        expectedOffset += accepted;
        TEST_ASSERT_EQUAL_UINT32(expectedOffset, stream.outputLength);
        TEST_ASSERT_EQUAL_UINT32(length - expectedOffset, result.remaining);
    }
    TEST_ASSERT_EQUAL_UINT8_ARRAY(encoded, stream.output, length);
    TEST_ASSERT_FALSE(adapter.hasPendingTx());
}

void testTxBudgetCapsOneWriteAndAllowsNextFrame() {
    uint8_t encoded[MAX_ENCODED_FRAME_SIZE] = {};
    const size_t length = encode(makeFrame(10), encoded);
    FakeStream stream;
    stream.writable = MAX_ENCODED_FRAME_SIZE;
    Adapter<FakeStream> adapter(stream);
    adapter.submit(encoded, length);
    TxResult first = adapter.serviceTx(2);
    TEST_ASSERT_EQUAL_UINT32(2, first.bytesWritten);
    TEST_ASSERT_EQUAL_UINT32(1, stream.writeCalls);
    TxResult rest = adapter.serviceTx(MAX_ENCODED_FRAME_SIZE);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TxStatus::COMPLETE),
        static_cast<uint8_t>(rest.status));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SubmitStatus::OK),
        static_cast<uint8_t>(adapter.submit(encoded, length)));
}

void testResetClearsPartialRxAndTx() {
    uint8_t encoded[MAX_ENCODED_FRAME_SIZE] = {};
    const size_t length = encode(makeFrame(8), encoded);
    FakeStream stream;
    stream.appendInput(encoded, 4);
    stream.writable = 2;
    Adapter<FakeStream> adapter(stream);
    adapter.serviceRx(4);
    adapter.submit(encoded, length);
    adapter.serviceTx(2);
    TEST_ASSERT_TRUE(adapter.hasPendingTx());
    adapter.reset();
    TEST_ASSERT_FALSE(adapter.hasPendingTx());
    stream.writable = MAX_ENCODED_FRAME_SIZE;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TxStatus::IDLE),
        static_cast<uint8_t>(adapter.serviceTx(length).status));
    stream.appendInput(encoded, length);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RxStatus::PARSER_EVENT),
        static_cast<uint8_t>(adapter.serviceRx(length).status));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SubmitStatus::OK),
        static_cast<uint8_t>(adapter.submit(encoded, length)));
    stream.writable = MAX_ENCODED_FRAME_SIZE;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TxStatus::COMPLETE),
        static_cast<uint8_t>(adapter.serviceTx(length).status));
    TEST_ASSERT_EQUAL_UINT32(length + 2, stream.outputLength);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(encoded, stream.output + 2, length);
}

void testResetClearsOversizeDiscardState() {
    FakeStream stream;
    for (size_t index = 0; index < MAX_COBS_CANDIDATE_SIZE + 1; ++index) {
        stream.input[stream.inputLength++] = 1;
    }
    Adapter<FakeStream> adapter(stream);
    RxResult discarded = adapter.serviceRx(MAX_COBS_CANDIDATE_SIZE + 1);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RxStatus::PROGRESS),
        static_cast<uint8_t>(discarded.status));
    adapter.reset();
    uint8_t encoded[MAX_ENCODED_FRAME_SIZE] = {};
    const size_t length = encode(makeFrame(), encoded);
    stream.appendInput(encoded, length);
    RxResult recovered = adapter.serviceRx(length);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StreamEvent::FRAME_READY),
        static_cast<uint8_t>(recovered.parserResult.event));
}

void testRxAndTxStateRemainIndependent() {
    uint8_t encoded[MAX_ENCODED_FRAME_SIZE] = {};
    const size_t length = encode(makeFrame(12), encoded);
    FakeStream stream;
    stream.appendInput(encoded, 5);
    stream.writable = 3;
    Adapter<FakeStream> adapter(stream);
    adapter.submit(encoded, length);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RxStatus::PROGRESS),
        static_cast<uint8_t>(adapter.serviceRx(5).status));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TxStatus::PROGRESS),
        static_cast<uint8_t>(adapter.serviceTx(3).status));
    TEST_ASSERT_EQUAL_UINT32(length - 3, adapter.remainingTx());
    stream.appendInput(encoded + 5, length - 5);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RxStatus::PARSER_EVENT),
        static_cast<uint8_t>(adapter.serviceRx(length).status));
    TEST_ASSERT_EQUAL_UINT32(length - 3, adapter.remainingTx());
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(testRxNoInputAndZeroBudgetPerformNoIo);
    RUN_TEST(testRxCompletePartialAndByteAtATime);
    RUN_TEST(testRxBudgetIsExactAndPartialPersists);
    RUN_TEST(testRxStopsBeforeSecondFrame);
    RUN_TEST(testRxPreservesMalformedAndOversizeEvents);
    RUN_TEST(testRxInputErrorIsSeparate);
    RUN_TEST(testTxSubmissionValidationAndBusy);
    RUN_TEST(testTxZeroBudgetAndBlockedCapacityPerformNoWrite);
    RUN_TEST(testTxMinimumAndMaximumFramesAreExact);
    RUN_TEST(testTxPartialWritesResumeWithoutDuplicateOrSkip);
    RUN_TEST(testTxBudgetCapsOneWriteAndAllowsNextFrame);
    RUN_TEST(testResetClearsPartialRxAndTx);
    RUN_TEST(testResetClearsOversizeDiscardState);
    RUN_TEST(testRxAndTxStateRemainIndependent);
    return UNITY_END();
}
