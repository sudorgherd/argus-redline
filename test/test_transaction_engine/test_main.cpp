#include <unity.h>

#include "protocol.h"
#include "transaction_engine.h"

namespace {

constexpr uint8_t HUB_ID = 0x01;
constexpr uint8_t NODE_ID = 0x10;
constexpr uint8_t SEQUENCE = 0x2A;

Protocol::Packet makeCommand() {
    Protocol::Packet command = {};
    command.type = Protocol::PacketType::COMMAND;
    command.source = HUB_ID;
    command.destination = NODE_ID;
    command.sequence = SEQUENCE;
    command.opcode = Protocol::OPCODE_TEST;
    command.payloadLength = 0;
    return command;
}

void testDuplicateStateBeginsEmpty() {
    TransactionEngine::NodeDuplicateTracker tracker;
    TEST_ASSERT_FALSE(tracker.isDuplicate(makeCommand()));
}

void testFirstCommandIsNotDuplicate() {
    TransactionEngine::NodeDuplicateTracker tracker;
    const Protocol::Packet command = makeCommand();
    TEST_ASSERT_FALSE(tracker.isDuplicate(command));
}

void testSameSourceSequenceAndOpcodeIsDuplicate() {
    TransactionEngine::NodeDuplicateTracker tracker;
    const Protocol::Packet command = makeCommand();
    tracker.remember(command, Protocol::AckStatus::SUCCESS);
    TEST_ASSERT_TRUE(tracker.isDuplicate(command));
}

void testDifferentSourceIsNotDuplicate() {
    TransactionEngine::NodeDuplicateTracker tracker;
    const Protocol::Packet command = makeCommand();
    tracker.remember(command, Protocol::AckStatus::SUCCESS);

    Protocol::Packet different = command;
    different.source++;
    TEST_ASSERT_FALSE(tracker.isDuplicate(different));
}

void testDifferentSequenceIsNotDuplicate() {
    TransactionEngine::NodeDuplicateTracker tracker;
    const Protocol::Packet command = makeCommand();
    tracker.remember(command, Protocol::AckStatus::SUCCESS);

    Protocol::Packet different = command;
    different.sequence++;
    TEST_ASSERT_FALSE(tracker.isDuplicate(different));
}

void testDifferentOpcodeIsNotDuplicate() {
    TransactionEngine::NodeDuplicateTracker tracker;
    const Protocol::Packet command = makeCommand();
    tracker.remember(command, Protocol::AckStatus::SUCCESS);

    Protocol::Packet different = command;
    different.opcode++;
    TEST_ASSERT_FALSE(tracker.isDuplicate(different));
}

void testRememberedStatusIsReused() {
    TransactionEngine::NodeDuplicateTracker tracker;
    const Protocol::Packet command = makeCommand();
    tracker.remember(
        command,
        Protocol::AckStatus::MALFORMED_PACKET
    );

    TEST_ASSERT_TRUE(tracker.isDuplicate(command));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(Protocol::AckStatus::MALFORMED_PACKET),
        static_cast<uint8_t>(tracker.rememberedStatus())
    );
}

void testAcknowledgmentReversesEndpoints() {
    const Protocol::Packet command = makeCommand();
    const Protocol::Packet acknowledgment =
        TransactionEngine::makeAcknowledgment(
            command,
            Protocol::AckStatus::SUCCESS
        );

    TEST_ASSERT_EQUAL_UINT8(command.destination, acknowledgment.source);
    TEST_ASSERT_EQUAL_UINT8(command.source, acknowledgment.destination);
}

void testAcknowledgmentMatchesSequenceAndOpcode() {
    const Protocol::Packet command = makeCommand();
    const Protocol::Packet acknowledgment =
        TransactionEngine::makeAcknowledgment(
            command,
            Protocol::AckStatus::SUCCESS
        );

    TEST_ASSERT_EQUAL_UINT8(command.sequence, acknowledgment.sequence);
    TEST_ASSERT_EQUAL_UINT8(command.opcode, acknowledgment.opcode);
}

void testAcknowledgmentHasOneStatusByte() {
    const Protocol::Packet command = makeCommand();
    const Protocol::Packet acknowledgment =
        TransactionEngine::makeAcknowledgment(
            command,
            Protocol::AckStatus::UNSUPPORTED_OPCODE
        );

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(Protocol::PacketType::ACK),
        static_cast<uint8_t>(acknowledgment.type)
    );
    TEST_ASSERT_EQUAL_UINT8(1, acknowledgment.payloadLength);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(Protocol::AckStatus::UNSUPPORTED_OPCODE),
        acknowledgment.payload[0]
    );
}

void testEncodedAcknowledgmentBytesRemainCorrect() {
    const Protocol::Packet acknowledgment =
        TransactionEngine::makeAcknowledgment(
            makeCommand(),
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
}

void testWrongDestinationIsIgnored() {
    TransactionEngine::NodeDuplicateTracker tracker;
    Protocol::Packet command = makeCommand();
    command.destination++;

    const TransactionEngine::NodeCommandEvaluation evaluation =
        TransactionEngine::evaluateNodeCommand(
            command,
            NODE_ID,
            HUB_ID,
            tracker
        );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            TransactionEngine::NodeCommandOutcome::
                IGNORE_WRONG_DESTINATION
        ),
        static_cast<uint8_t>(evaluation.outcome)
    );
}

void testWrongSenderIsIgnored() {
    TransactionEngine::NodeDuplicateTracker tracker;
    Protocol::Packet command = makeCommand();
    command.source++;

    const TransactionEngine::NodeCommandEvaluation evaluation =
        TransactionEngine::evaluateNodeCommand(
            command,
            NODE_ID,
            HUB_ID,
            tracker
        );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            TransactionEngine::NodeCommandOutcome::IGNORE_WRONG_SENDER
        ),
        static_cast<uint8_t>(evaluation.outcome)
    );
}

void testNonCommandPacketIsIgnored() {
    TransactionEngine::NodeDuplicateTracker tracker;
    Protocol::Packet command = makeCommand();
    command.type = Protocol::PacketType::ACK;

    const TransactionEngine::NodeCommandEvaluation evaluation =
        TransactionEngine::evaluateNodeCommand(
            command,
            NODE_ID,
            HUB_ID,
            tracker
        );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            TransactionEngine::NodeCommandOutcome::
                IGNORE_WRONG_PACKET_TYPE
        ),
        static_cast<uint8_t>(evaluation.outcome)
    );
}

void testValidTestProducesSuccess() {
    TransactionEngine::NodeDuplicateTracker tracker;
    const TransactionEngine::NodeCommandEvaluation evaluation =
        TransactionEngine::evaluateNodeCommand(
            makeCommand(),
            NODE_ID,
            HUB_ID,
            tracker
        );

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            TransactionEngine::NodeCommandOutcome::ACK_SUCCESS
        ),
        static_cast<uint8_t>(evaluation.outcome)
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(Protocol::AckStatus::SUCCESS),
        static_cast<uint8_t>(evaluation.status)
    );
}

void testUnsupportedOpcodeProducesUnsupportedStatus() {
    TransactionEngine::NodeDuplicateTracker tracker;
    Protocol::Packet command = makeCommand();
    command.opcode = 0x20;

    const TransactionEngine::NodeCommandEvaluation evaluation =
        TransactionEngine::evaluateNodeCommand(
            command,
            NODE_ID,
            HUB_ID,
            tracker
        );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            TransactionEngine::NodeCommandOutcome::
                ACK_UNSUPPORTED_OPCODE
        ),
        static_cast<uint8_t>(evaluation.outcome)
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            Protocol::AckStatus::UNSUPPORTED_OPCODE
        ),
        static_cast<uint8_t>(evaluation.status)
    );
}

void testInvalidTestPayloadProducesMalformedStatus() {
    TransactionEngine::NodeDuplicateTracker tracker;
    Protocol::Packet command = makeCommand();
    command.payloadLength = 1;

    const TransactionEngine::NodeCommandEvaluation evaluation =
        TransactionEngine::evaluateNodeCommand(
            command,
            NODE_ID,
            HUB_ID,
            tracker
        );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            TransactionEngine::NodeCommandOutcome::ACK_MALFORMED_PACKET
        ),
        static_cast<uint8_t>(evaluation.outcome)
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(Protocol::AckStatus::MALFORMED_PACKET),
        static_cast<uint8_t>(evaluation.status)
    );
}

void testDuplicateReturnsRememberedStatus() {
    TransactionEngine::NodeDuplicateTracker tracker;
    const Protocol::Packet command = makeCommand();
    tracker.remember(
        command,
        Protocol::AckStatus::MALFORMED_PACKET
    );

    const TransactionEngine::NodeCommandEvaluation evaluation =
        TransactionEngine::evaluateNodeCommand(
            command,
            NODE_ID,
            HUB_ID,
            tracker
        );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            TransactionEngine::NodeCommandOutcome::DUPLICATE
        ),
        static_cast<uint8_t>(evaluation.outcome)
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(Protocol::AckStatus::MALFORMED_PACKET),
        static_cast<uint8_t>(evaluation.status)
    );
}

void testIgnoredPacketDoesNotMutateDuplicateState() {
    TransactionEngine::NodeDuplicateTracker tracker;
    Protocol::Packet ignored = makeCommand();
    ignored.destination++;
    TransactionEngine::evaluateNodeCommand(
        ignored,
        NODE_ID,
        HUB_ID,
        tracker
    );

    TEST_ASSERT_FALSE(tracker.isDuplicate(ignored));
    const TransactionEngine::NodeCommandEvaluation evaluation =
        TransactionEngine::evaluateNodeCommand(
            makeCommand(),
            NODE_ID,
            HUB_ID,
            tracker
        );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            TransactionEngine::NodeCommandOutcome::ACK_SUCCESS
        ),
        static_cast<uint8_t>(evaluation.outcome)
    );
}

void testDuplicatePacketDoesNotReplaceRememberedState() {
    TransactionEngine::NodeDuplicateTracker tracker;
    Protocol::Packet command = makeCommand();
    tracker.remember(
        command,
        Protocol::AckStatus::UNSUPPORTED_OPCODE
    );

    Protocol::Packet duplicate = command;
    duplicate.payloadLength = 1;
    TransactionEngine::evaluateNodeCommand(
        duplicate,
        NODE_ID,
        HUB_ID,
        tracker
    );

    TEST_ASSERT_TRUE(tracker.isDuplicate(command));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            Protocol::AckStatus::UNSUPPORTED_OPCODE
        ),
        static_cast<uint8_t>(tracker.rememberedStatus())
    );
}

void testNewCommandIsRememberedWithSelectedStatus() {
    TransactionEngine::NodeDuplicateTracker tracker;
    Protocol::Packet command = makeCommand();
    command.payloadLength = 1;
    TransactionEngine::evaluateNodeCommand(
        command,
        NODE_ID,
        HUB_ID,
        tracker
    );

    TEST_ASSERT_TRUE(tracker.isDuplicate(command));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(Protocol::AckStatus::MALFORMED_PACKET),
        static_cast<uint8_t>(tracker.rememberedStatus())
    );
}

void testEvaluationOrderMatchesCurrentFirmware() {
    TransactionEngine::NodeDuplicateTracker tracker;
    Protocol::Packet command = makeCommand();
    command.destination++;
    command.source++;
    command.type = Protocol::PacketType::ACK;

    TransactionEngine::NodeCommandEvaluation evaluation =
        TransactionEngine::evaluateNodeCommand(
            command,
            NODE_ID,
            HUB_ID,
            tracker
        );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            TransactionEngine::NodeCommandOutcome::
                IGNORE_WRONG_DESTINATION
        ),
        static_cast<uint8_t>(evaluation.outcome)
    );

    command.destination = NODE_ID;
    evaluation = TransactionEngine::evaluateNodeCommand(
        command,
        NODE_ID,
        HUB_ID,
        tracker
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            TransactionEngine::NodeCommandOutcome::
                IGNORE_WRONG_PACKET_TYPE
        ),
        static_cast<uint8_t>(evaluation.outcome)
    );

    command.type = Protocol::PacketType::COMMAND;
    evaluation = TransactionEngine::evaluateNodeCommand(
        command,
        NODE_ID,
        HUB_ID,
        tracker
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            TransactionEngine::NodeCommandOutcome::IGNORE_WRONG_SENDER
        ),
        static_cast<uint8_t>(evaluation.outcome)
    );
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(testDuplicateStateBeginsEmpty);
    RUN_TEST(testFirstCommandIsNotDuplicate);
    RUN_TEST(testSameSourceSequenceAndOpcodeIsDuplicate);
    RUN_TEST(testDifferentSourceIsNotDuplicate);
    RUN_TEST(testDifferentSequenceIsNotDuplicate);
    RUN_TEST(testDifferentOpcodeIsNotDuplicate);
    RUN_TEST(testRememberedStatusIsReused);
    RUN_TEST(testAcknowledgmentReversesEndpoints);
    RUN_TEST(testAcknowledgmentMatchesSequenceAndOpcode);
    RUN_TEST(testAcknowledgmentHasOneStatusByte);
    RUN_TEST(testEncodedAcknowledgmentBytesRemainCorrect);
    RUN_TEST(testWrongDestinationIsIgnored);
    RUN_TEST(testWrongSenderIsIgnored);
    RUN_TEST(testNonCommandPacketIsIgnored);
    RUN_TEST(testValidTestProducesSuccess);
    RUN_TEST(testUnsupportedOpcodeProducesUnsupportedStatus);
    RUN_TEST(testInvalidTestPayloadProducesMalformedStatus);
    RUN_TEST(testDuplicateReturnsRememberedStatus);
    RUN_TEST(testIgnoredPacketDoesNotMutateDuplicateState);
    RUN_TEST(testDuplicatePacketDoesNotReplaceRememberedState);
    RUN_TEST(testNewCommandIsRememberedWithSelectedStatus);
    RUN_TEST(testEvaluationOrderMatchesCurrentFirmware);
    return UNITY_END();
}
