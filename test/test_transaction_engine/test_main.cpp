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

Protocol::Packet makeAcknowledgment(
    Protocol::AckStatus status = Protocol::AckStatus::SUCCESS
) {
    return TransactionEngine::makeAcknowledgment(makeCommand(), status);
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

void testExactCanonicalRequestIsDuplicate() {
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

void testDifferentPayloadLengthIsNotDuplicate() {
    TransactionEngine::NodeDuplicateTracker tracker;
    const Protocol::Packet command = makeCommand();
    tracker.remember(command, Protocol::AckStatus::SUCCESS);

    Protocol::Packet different = command;
    different.payloadLength = 1;
    different.payload[0] = 0xA5;
    TEST_ASSERT_FALSE(tracker.isDuplicate(different));
}

void testDifferentPayloadContentIsNotDuplicate() {
    TransactionEngine::NodeDuplicateTracker tracker;
    Protocol::Packet command = makeCommand();
    command.payloadLength = 2;
    command.payload[0] = 0x12;
    command.payload[1] = 0x34;
    tracker.remember(command, Protocol::AckStatus::SUCCESS);

    Protocol::Packet different = command;
    different.payload[1] = 0x35;
    TEST_ASSERT_FALSE(tracker.isDuplicate(different));
}

void testUnusedPayloadBytesDoNotAffectDuplicateIdentity() {
    TransactionEngine::NodeDuplicateTracker tracker;
    Protocol::Packet command = makeCommand();
    command.payloadLength = 1;
    command.payload[0] = 0x5A;
    command.payload[1] = 0x11;
    tracker.remember(command, Protocol::AckStatus::SUCCESS);

    Protocol::Packet equivalent = command;
    equivalent.payload[1] = 0xEE;
    TEST_ASSERT_TRUE(tracker.isDuplicate(equivalent));
}

void testMaximumPayloadComparisonIsBoundedAndExact() {
    TransactionEngine::NodeDuplicateTracker tracker;
    Protocol::Packet command = makeCommand();
    command.payloadLength = Protocol::MAX_PAYLOAD_SIZE;
    for (size_t index = 0; index < command.payloadLength; ++index) {
        command.payload[index] = static_cast<uint8_t>(index);
    }
    tracker.remember(command, Protocol::AckStatus::SUCCESS);

    TEST_ASSERT_TRUE(tracker.isDuplicate(command));
    command.payload[Protocol::MAX_PAYLOAD_SIZE - 1] ^= 0xFF;
    TEST_ASSERT_FALSE(tracker.isDuplicate(command));
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

void testMalformedThenValidSameTupleDoesNotPoisonCache() {
    TransactionEngine::NodeDuplicateTracker tracker;
    Protocol::Packet malformed = makeCommand();
    malformed.payloadLength = 1;
    malformed.payload[0] = 0xA5;

    const TransactionEngine::NodeCommandEvaluation malformedEvaluation =
        TransactionEngine::evaluateNodeCommand(
            malformed,
            NODE_ID,
            HUB_ID,
            tracker
        );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            TransactionEngine::NodeCommandOutcome::ACK_MALFORMED_PACKET
        ),
        static_cast<uint8_t>(malformedEvaluation.outcome)
    );
    TEST_ASSERT_FALSE(tracker.isDuplicate(malformed));

    const Protocol::Packet valid = makeCommand();
    const TransactionEngine::NodeCommandEvaluation validEvaluation =
        TransactionEngine::evaluateNodeCommand(
            valid,
            NODE_ID,
            HUB_ID,
            tracker
        );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            TransactionEngine::NodeCommandOutcome::ACK_SUCCESS
        ),
        static_cast<uint8_t>(validEvaluation.outcome)
    );
    TEST_ASSERT_TRUE(tracker.isDuplicate(valid));
}

void testValidThenPayloadDistinctSameTupleIsMalformedNotDuplicate() {
    TransactionEngine::NodeDuplicateTracker tracker;
    const Protocol::Packet valid = makeCommand();
    TransactionEngine::evaluateNodeCommand(valid, NODE_ID, HUB_ID, tracker);

    Protocol::Packet payloadDistinct = valid;
    payloadDistinct.payloadLength = 1;
    payloadDistinct.payload[0] = 0x7E;
    const TransactionEngine::NodeCommandEvaluation evaluation =
        TransactionEngine::evaluateNodeCommand(
            payloadDistinct,
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
    TEST_ASSERT_TRUE(tracker.isDuplicate(valid));
    TEST_ASSERT_FALSE(tracker.isDuplicate(payloadDistinct));
}

void testMalformedRequestCannotOverwriteValidCache() {
    TransactionEngine::NodeDuplicateTracker tracker;
    const Protocol::Packet valid = makeCommand();
    TransactionEngine::evaluateNodeCommand(valid, NODE_ID, HUB_ID, tracker);

    Protocol::Packet malformed = valid;
    malformed.payloadLength = 1;
    malformed.payload[0] = 0x44;
    TransactionEngine::evaluateNodeCommand(
        malformed,
        NODE_ID,
        HUB_ID,
        tracker
    );

    const TransactionEngine::NodeCommandEvaluation evaluation =
        TransactionEngine::evaluateNodeCommand(
            valid,
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
        static_cast<uint8_t>(Protocol::AckStatus::SUCCESS),
        static_cast<uint8_t>(evaluation.status)
    );
}

void testExactRetransmissionReturnsCachedResultWithoutExecutionOutcome() {
    TransactionEngine::NodeDuplicateTracker tracker;
    const Protocol::Packet command = makeCommand();

    const TransactionEngine::NodeCommandEvaluation first =
        TransactionEngine::evaluateNodeCommand(
            command,
            NODE_ID,
            HUB_ID,
            tracker
        );
    const TransactionEngine::NodeCommandEvaluation retransmission =
        TransactionEngine::evaluateNodeCommand(
            command,
            NODE_ID,
            HUB_ID,
            tracker
        );

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            TransactionEngine::NodeCommandOutcome::ACK_SUCCESS
        ),
        static_cast<uint8_t>(first.outcome)
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            TransactionEngine::NodeCommandOutcome::DUPLICATE
        ),
        static_cast<uint8_t>(retransmission.outcome)
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(Protocol::AckStatus::SUCCESS),
        static_cast<uint8_t>(retransmission.status)
    );
}

void testIgnoredRequestsCannotAlterValidCache() {
    TransactionEngine::NodeDuplicateTracker tracker;
    const Protocol::Packet valid = makeCommand();
    TransactionEngine::evaluateNodeCommand(valid, NODE_ID, HUB_ID, tracker);

    Protocol::Packet wrongDestination = valid;
    wrongDestination.destination++;
    Protocol::Packet wrongSender = valid;
    wrongSender.source++;
    Protocol::Packet wrongType = valid;
    wrongType.type = Protocol::PacketType::ACK;

    TransactionEngine::evaluateNodeCommand(
        wrongDestination,
        NODE_ID,
        HUB_ID,
        tracker
    );
    TransactionEngine::evaluateNodeCommand(
        wrongSender,
        NODE_ID,
        HUB_ID,
        tracker
    );
    TransactionEngine::evaluateNodeCommand(
        wrongType,
        NODE_ID,
        HUB_ID,
        tracker
    );

    const TransactionEngine::NodeCommandEvaluation retransmission =
        TransactionEngine::evaluateNodeCommand(
            valid,
            NODE_ID,
            HUB_ID,
            tracker
        );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            TransactionEngine::NodeCommandOutcome::DUPLICATE
        ),
        static_cast<uint8_t>(retransmission.outcome)
    );
}

void testUnsupportedOpcodeCannotCreateOrOverwriteValidCache() {
    TransactionEngine::NodeDuplicateTracker tracker;
    const Protocol::Packet valid = makeCommand();
    TransactionEngine::evaluateNodeCommand(valid, NODE_ID, HUB_ID, tracker);

    Protocol::Packet unsupported = valid;
    unsupported.opcode = 0x20;
    TransactionEngine::evaluateNodeCommand(
        unsupported,
        NODE_ID,
        HUB_ID,
        tracker
    );

    TEST_ASSERT_FALSE(tracker.isDuplicate(unsupported));
    const TransactionEngine::NodeCommandEvaluation retransmission =
        TransactionEngine::evaluateNodeCommand(
            valid,
            NODE_ID,
            HUB_ID,
            tracker
        );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            TransactionEngine::NodeCommandOutcome::DUPLICATE
        ),
        static_cast<uint8_t>(retransmission.outcome)
    );
}

void testSequenceRolloverUsesCompleteCanonicalIdentity() {
    TransactionEngine::HubTransactionState hubState(0xFF);
    hubState.completeTransaction();
    TEST_ASSERT_EQUAL_UINT8(0, hubState.currentSequence());

    TransactionEngine::NodeDuplicateTracker tracker;
    Protocol::Packet wrapped = makeCommand();
    wrapped.sequence = hubState.currentSequence();
    TransactionEngine::evaluateNodeCommand(
        wrapped,
        NODE_ID,
        HUB_ID,
        tracker
    );
    TEST_ASSERT_TRUE(tracker.isDuplicate(wrapped));

    Protocol::Packet payloadDistinct = wrapped;
    payloadDistinct.payloadLength = 1;
    payloadDistinct.payload[0] = 0x99;
    TEST_ASSERT_FALSE(tracker.isDuplicate(payloadDistinct));

    const TransactionEngine::NodeCommandEvaluation malformed =
        TransactionEngine::evaluateNodeCommand(
            payloadDistinct,
            NODE_ID,
            HUB_ID,
            tracker
        );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            TransactionEngine::NodeCommandOutcome::ACK_MALFORMED_PACKET
        ),
        static_cast<uint8_t>(malformed.outcome)
    );
    TEST_ASSERT_TRUE(tracker.isDuplicate(wrapped));
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

void testHubIgnoresWrongDestination() {
    const Protocol::Packet command = makeCommand();
    Protocol::Packet acknowledgment = makeAcknowledgment();
    acknowledgment.destination++;

    const TransactionEngine::HubAckEvaluation evaluation =
        TransactionEngine::evaluateHubAcknowledgment(
            acknowledgment,
            command,
            HUB_ID,
            NODE_ID
        );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            TransactionEngine::HubAckOutcome::
                IGNORE_WRONG_DESTINATION
        ),
        static_cast<uint8_t>(evaluation.outcome)
    );
}

void testHubIgnoresWrongSender() {
    const Protocol::Packet command = makeCommand();
    Protocol::Packet acknowledgment = makeAcknowledgment();
    acknowledgment.source++;

    const TransactionEngine::HubAckEvaluation evaluation =
        TransactionEngine::evaluateHubAcknowledgment(
            acknowledgment,
            command,
            HUB_ID,
            NODE_ID
        );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            TransactionEngine::HubAckOutcome::IGNORE_WRONG_SENDER
        ),
        static_cast<uint8_t>(evaluation.outcome)
    );
}

void testHubIgnoresNonAcknowledgmentPacket() {
    const Protocol::Packet command = makeCommand();
    Protocol::Packet acknowledgment = makeAcknowledgment();
    acknowledgment.type = Protocol::PacketType::COMMAND;

    const TransactionEngine::HubAckEvaluation evaluation =
        TransactionEngine::evaluateHubAcknowledgment(
            acknowledgment,
            command,
            HUB_ID,
            NODE_ID
        );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            TransactionEngine::HubAckOutcome::IGNORE_WRONG_PACKET_TYPE
        ),
        static_cast<uint8_t>(evaluation.outcome)
    );
}

void testHubIgnoresWrongSequence() {
    const Protocol::Packet command = makeCommand();
    Protocol::Packet acknowledgment = makeAcknowledgment();
    acknowledgment.sequence++;

    const TransactionEngine::HubAckEvaluation evaluation =
        TransactionEngine::evaluateHubAcknowledgment(
            acknowledgment,
            command,
            HUB_ID,
            NODE_ID
        );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            TransactionEngine::HubAckOutcome::IGNORE_WRONG_SEQUENCE
        ),
        static_cast<uint8_t>(evaluation.outcome)
    );
}

void testHubIgnoresWrongOpcode() {
    const Protocol::Packet command = makeCommand();
    Protocol::Packet acknowledgment = makeAcknowledgment();
    acknowledgment.opcode++;

    const TransactionEngine::HubAckEvaluation evaluation =
        TransactionEngine::evaluateHubAcknowledgment(
            acknowledgment,
            command,
            HUB_ID,
            NODE_ID
        );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            TransactionEngine::HubAckOutcome::IGNORE_WRONG_OPCODE
        ),
        static_cast<uint8_t>(evaluation.outcome)
    );
}

void testHubRejectsZeroLengthAcknowledgmentPayload() {
    const Protocol::Packet command = makeCommand();
    Protocol::Packet acknowledgment = makeAcknowledgment();
    acknowledgment.payloadLength = 0;

    const TransactionEngine::HubAckEvaluation evaluation =
        TransactionEngine::evaluateHubAcknowledgment(
            acknowledgment,
            command,
            HUB_ID,
            NODE_ID
        );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            TransactionEngine::HubAckOutcome::
                IGNORE_MALFORMED_PAYLOAD
        ),
        static_cast<uint8_t>(evaluation.outcome)
    );
}

void testHubRejectsLongAcknowledgmentPayload() {
    const Protocol::Packet command = makeCommand();
    Protocol::Packet acknowledgment = makeAcknowledgment();
    acknowledgment.payloadLength = 2;

    const TransactionEngine::HubAckEvaluation evaluation =
        TransactionEngine::evaluateHubAcknowledgment(
            acknowledgment,
            command,
            HUB_ID,
            NODE_ID
        );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            TransactionEngine::HubAckOutcome::
                IGNORE_MALFORMED_PAYLOAD
        ),
        static_cast<uint8_t>(evaluation.outcome)
    );
}

void testHubMatchingAcknowledgmentReturnsEncodedStatus() {
    const Protocol::Packet command = makeCommand();
    Protocol::Packet acknowledgment = makeAcknowledgment();
    acknowledgment.payload[0] = 0x7F;

    const TransactionEngine::HubAckEvaluation evaluation =
        TransactionEngine::evaluateHubAcknowledgment(
            acknowledgment,
            command,
            HUB_ID,
            NODE_ID
        );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            TransactionEngine::HubAckOutcome::MATCHING_ACK
        ),
        static_cast<uint8_t>(evaluation.outcome)
    );
    TEST_ASSERT_EQUAL_HEX8(0x7F, evaluation.rawStatus);
}

void testHubExtractsSuccessStatus() {
    const Protocol::Packet command = makeCommand();
    const Protocol::Packet acknowledgment = makeAcknowledgment(
        Protocol::AckStatus::SUCCESS
    );

    const TransactionEngine::HubAckEvaluation evaluation =
        TransactionEngine::evaluateHubAcknowledgment(
            acknowledgment,
            command,
            HUB_ID,
            NODE_ID
        );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(Protocol::AckStatus::SUCCESS),
        evaluation.rawStatus
    );
}

void testHubExtractsNonSuccessStatus() {
    const Protocol::Packet command = makeCommand();
    const Protocol::Packet acknowledgment = makeAcknowledgment(
        Protocol::AckStatus::MALFORMED_PACKET
    );

    const TransactionEngine::HubAckEvaluation evaluation =
        TransactionEngine::evaluateHubAcknowledgment(
            acknowledgment,
            command,
            HUB_ID,
            NODE_ID
        );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(Protocol::AckStatus::MALFORMED_PACKET),
        evaluation.rawStatus
    );
}

void testHubAcknowledgmentEvaluationOrderMatchesFirmware() {
    const Protocol::Packet command = makeCommand();
    Protocol::Packet acknowledgment = makeAcknowledgment();
    acknowledgment.type = Protocol::PacketType::COMMAND;
    acknowledgment.source++;
    acknowledgment.destination++;

    TransactionEngine::HubAckEvaluation evaluation =
        TransactionEngine::evaluateHubAcknowledgment(
            acknowledgment,
            command,
            HUB_ID,
            NODE_ID
        );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            TransactionEngine::HubAckOutcome::IGNORE_WRONG_PACKET_TYPE
        ),
        static_cast<uint8_t>(evaluation.outcome)
    );

    acknowledgment.type = Protocol::PacketType::ACK;
    evaluation = TransactionEngine::evaluateHubAcknowledgment(
        acknowledgment,
        command,
        HUB_ID,
        NODE_ID
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            TransactionEngine::HubAckOutcome::IGNORE_WRONG_SENDER
        ),
        static_cast<uint8_t>(evaluation.outcome)
    );

    acknowledgment.source = NODE_ID;
    evaluation = TransactionEngine::evaluateHubAcknowledgment(
        acknowledgment,
        command,
        HUB_ID,
        NODE_ID
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            TransactionEngine::HubAckOutcome::
                IGNORE_WRONG_DESTINATION
        ),
        static_cast<uint8_t>(evaluation.outcome)
    );
}

void testHubEvaluatorDoesNotMutateOutstandingCommand() {
    Protocol::Packet command = makeCommand();
    const Protocol::Packet original = command;
    TransactionEngine::evaluateHubAcknowledgment(
        makeAcknowledgment(),
        command,
        HUB_ID,
        NODE_ID
    );

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(original.type),
        static_cast<uint8_t>(command.type)
    );
    TEST_ASSERT_EQUAL_UINT8(original.source, command.source);
    TEST_ASSERT_EQUAL_UINT8(original.destination, command.destination);
    TEST_ASSERT_EQUAL_UINT8(original.sequence, command.sequence);
    TEST_ASSERT_EQUAL_UINT8(original.opcode, command.opcode);
    TEST_ASSERT_EQUAL_UINT8(
        original.payloadLength,
        command.payloadLength
    );
}

void testHubInitialTransactionRequestsFirstTransmission() {
    TransactionEngine::HubTransactionState state;

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            TransactionEngine::HubTransactionAction::TRANSMIT_INITIAL
        ),
        static_cast<uint8_t>(state.requestedTransmission())
    );
    TEST_ASSERT_EQUAL_UINT8(1, state.currentSequence());
    TEST_ASSERT_EQUAL_UINT8(0, state.retryCount());
}

void testHubBeginningAcknowledgmentWaitStoresDeadline() {
    TransactionEngine::HubTransactionState state;
    state.beginAcknowledgmentWait(1000);

    TEST_ASSERT_TRUE(state.isAwaitingAcknowledgment());
    TEST_ASSERT_EQUAL_UINT32(3500, state.acknowledgmentDeadline());
}

void testHubBeforeDeadlineContinuesWaiting() {
    TransactionEngine::HubTransactionState state;
    state.beginAcknowledgmentWait(1000);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            TransactionEngine::HubTransactionAction::NO_ACTION
        ),
        static_cast<uint8_t>(state.acknowledgmentWaitAction(3499))
    );
    TEST_ASSERT_TRUE(state.isAwaitingAcknowledgment());
}

void testHubAtDeadlineRequestsRetransmission() {
    TransactionEngine::HubTransactionState state;
    state.beginAcknowledgmentWait(1000);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            TransactionEngine::HubTransactionAction::RETRANSMIT
        ),
        static_cast<uint8_t>(state.acknowledgmentWaitAction(3500))
    );
    TEST_ASSERT_TRUE(state.isAwaitingAcknowledgment());
    state.attemptFailed();
    TEST_ASSERT_FALSE(state.isAwaitingAcknowledgment());
}

void testHubAfterTimeoutRequestsRetransmission() {
    TransactionEngine::HubTransactionState state;
    state.beginAcknowledgmentWait(1000);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            TransactionEngine::HubTransactionAction::RETRANSMIT
        ),
        static_cast<uint8_t>(state.acknowledgmentWaitAction(3501))
    );
}

void testHubRetryCountIncrementsExactlyAsBefore() {
    TransactionEngine::HubTransactionState state;

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            TransactionEngine::HubTransactionAction::RETRANSMIT
        ),
        static_cast<uint8_t>(state.attemptFailed())
    );
    TEST_ASSERT_EQUAL_UINT8(1, state.retryCount());
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            TransactionEngine::HubTransactionAction::RETRANSMIT
        ),
        static_cast<uint8_t>(state.attemptFailed())
    );
    TEST_ASSERT_EQUAL_UINT8(2, state.retryCount());
}

void testHubRetryExhaustionProducesFailure() {
    TransactionEngine::HubTransactionState state;
    state.attemptFailed();
    state.attemptFailed();

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            TransactionEngine::HubTransactionAction::TRANSACTION_FAILED
        ),
        static_cast<uint8_t>(state.attemptFailed())
    );
}

void testHubSuccessfulAcknowledgmentCompletesTransaction() {
    TransactionEngine::HubTransactionState state;
    state.beginAcknowledgmentWait(1000);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            TransactionEngine::HubTransactionAction::
                TRANSACTION_SUCCEEDED
        ),
        static_cast<uint8_t>(state.acknowledgmentCompletionAction(
            Protocol::AckStatus::SUCCESS
        ))
    );
    state.completeTransaction();
    TEST_ASSERT_FALSE(state.isAwaitingAcknowledgment());
}

void testHubNonSuccessAcknowledgmentCompletesWithRemoteError() {
    TransactionEngine::HubTransactionState state;

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            TransactionEngine::HubTransactionAction::
                TRANSACTION_REMOTE_ERROR
        ),
        static_cast<uint8_t>(state.acknowledgmentCompletionAction(
            Protocol::AckStatus::UNSUPPORTED_OPCODE
        ))
    );
    state.completeTransaction();
}

void testHubSequenceAdvancesOnceAfterSuccess() {
    TransactionEngine::HubTransactionState state;
    state.acknowledgmentCompletionAction(Protocol::AckStatus::SUCCESS);
    state.completeTransaction();

    TEST_ASSERT_EQUAL_UINT8(2, state.currentSequence());
}

void testHubSequenceAdvancesOnceAfterRemoteError() {
    TransactionEngine::HubTransactionState state;
    state.acknowledgmentCompletionAction(
        Protocol::AckStatus::MALFORMED_PACKET
    );
    state.completeTransaction();

    TEST_ASSERT_EQUAL_UINT8(2, state.currentSequence());
}

void testHubSequenceAdvancesOnceAfterRetryExhaustion() {
    TransactionEngine::HubTransactionState state;
    state.attemptFailed();
    state.attemptFailed();
    state.attemptFailed();
    state.completeTransaction();

    TEST_ASSERT_EQUAL_UINT8(2, state.currentSequence());
}

void testHubSequenceDoesNotAdvanceWhileRetrying() {
    TransactionEngine::HubTransactionState state;
    state.attemptFailed();
    TEST_ASSERT_EQUAL_UINT8(1, state.currentSequence());
    state.attemptFailed();
    TEST_ASSERT_EQUAL_UINT8(1, state.currentSequence());
}

void testHubRetryStateResetsForNextTransaction() {
    TransactionEngine::HubTransactionState state;
    state.attemptFailed();
    state.acknowledgmentCompletionAction(Protocol::AckStatus::SUCCESS);
    state.completeTransaction();

    TEST_ASSERT_EQUAL_UINT8(0, state.retryCount());
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            TransactionEngine::HubTransactionAction::TRANSMIT_INITIAL
        ),
        static_cast<uint8_t>(state.requestedTransmission())
    );
}

void testHubSequenceWrapsExactlyAsBefore() {
    TransactionEngine::HubTransactionState state(0xFF);
    state.acknowledgmentCompletionAction(Protocol::AckStatus::SUCCESS);
    state.completeTransaction();

    TEST_ASSERT_EQUAL_UINT8(0, state.currentSequence());
}

void testHubAcknowledgmentDeadlineWrapsCorrectly() {
    TransactionEngine::HubTransactionState state;
    state.beginAcknowledgmentWait(0xFFFFFFF0);

    TEST_ASSERT_EQUAL_HEX32(0x000009B4, state.acknowledgmentDeadline());
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            TransactionEngine::HubTransactionAction::NO_ACTION
        ),
        static_cast<uint8_t>(state.acknowledgmentWaitAction(0x000009B3))
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            TransactionEngine::HubTransactionAction::RETRANSMIT
        ),
        static_cast<uint8_t>(state.acknowledgmentWaitAction(0x000009B4))
    );
}

void testHubOutstandingCommandSequenceRemainsAligned() {
    TransactionEngine::HubTransactionState state;
    Protocol::Packet outstandingCommand = makeCommand();
    outstandingCommand.sequence = state.currentSequence();
    state.beginAcknowledgmentWait(1000);

    TEST_ASSERT_EQUAL_UINT8(
        state.currentSequence(),
        outstandingCommand.sequence
    );
    state.acknowledgmentWaitAction(3500);
    state.attemptFailed();
    TEST_ASSERT_EQUAL_UINT8(
        state.currentSequence(),
        outstandingCommand.sequence
    );

    outstandingCommand.sequence = state.currentSequence();
    TEST_ASSERT_EQUAL_UINT8(
        state.currentSequence(),
        outstandingCommand.sequence
    );
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(testDuplicateStateBeginsEmpty);
    RUN_TEST(testFirstCommandIsNotDuplicate);
    RUN_TEST(testExactCanonicalRequestIsDuplicate);
    RUN_TEST(testDifferentSourceIsNotDuplicate);
    RUN_TEST(testDifferentSequenceIsNotDuplicate);
    RUN_TEST(testDifferentOpcodeIsNotDuplicate);
    RUN_TEST(testDifferentPayloadLengthIsNotDuplicate);
    RUN_TEST(testDifferentPayloadContentIsNotDuplicate);
    RUN_TEST(testUnusedPayloadBytesDoNotAffectDuplicateIdentity);
    RUN_TEST(testMaximumPayloadComparisonIsBoundedAndExact);
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
    RUN_TEST(testMalformedThenValidSameTupleDoesNotPoisonCache);
    RUN_TEST(testValidThenPayloadDistinctSameTupleIsMalformedNotDuplicate);
    RUN_TEST(testMalformedRequestCannotOverwriteValidCache);
    RUN_TEST(testExactRetransmissionReturnsCachedResultWithoutExecutionOutcome);
    RUN_TEST(testIgnoredRequestsCannotAlterValidCache);
    RUN_TEST(testUnsupportedOpcodeCannotCreateOrOverwriteValidCache);
    RUN_TEST(testSequenceRolloverUsesCompleteCanonicalIdentity);
    RUN_TEST(testEvaluationOrderMatchesCurrentFirmware);
    RUN_TEST(testHubIgnoresWrongDestination);
    RUN_TEST(testHubIgnoresWrongSender);
    RUN_TEST(testHubIgnoresNonAcknowledgmentPacket);
    RUN_TEST(testHubIgnoresWrongSequence);
    RUN_TEST(testHubIgnoresWrongOpcode);
    RUN_TEST(testHubRejectsZeroLengthAcknowledgmentPayload);
    RUN_TEST(testHubRejectsLongAcknowledgmentPayload);
    RUN_TEST(testHubMatchingAcknowledgmentReturnsEncodedStatus);
    RUN_TEST(testHubExtractsSuccessStatus);
    RUN_TEST(testHubExtractsNonSuccessStatus);
    RUN_TEST(testHubAcknowledgmentEvaluationOrderMatchesFirmware);
    RUN_TEST(testHubEvaluatorDoesNotMutateOutstandingCommand);
    RUN_TEST(testHubInitialTransactionRequestsFirstTransmission);
    RUN_TEST(testHubBeginningAcknowledgmentWaitStoresDeadline);
    RUN_TEST(testHubBeforeDeadlineContinuesWaiting);
    RUN_TEST(testHubAtDeadlineRequestsRetransmission);
    RUN_TEST(testHubAfterTimeoutRequestsRetransmission);
    RUN_TEST(testHubRetryCountIncrementsExactlyAsBefore);
    RUN_TEST(testHubRetryExhaustionProducesFailure);
    RUN_TEST(testHubSuccessfulAcknowledgmentCompletesTransaction);
    RUN_TEST(testHubNonSuccessAcknowledgmentCompletesWithRemoteError);
    RUN_TEST(testHubSequenceAdvancesOnceAfterSuccess);
    RUN_TEST(testHubSequenceAdvancesOnceAfterRemoteError);
    RUN_TEST(testHubSequenceAdvancesOnceAfterRetryExhaustion);
    RUN_TEST(testHubSequenceDoesNotAdvanceWhileRetrying);
    RUN_TEST(testHubRetryStateResetsForNextTransaction);
    RUN_TEST(testHubSequenceWrapsExactlyAsBefore);
    RUN_TEST(testHubAcknowledgmentDeadlineWrapsCorrectly);
    RUN_TEST(testHubOutstandingCommandSequenceRemainsAligned);
    return UNITY_END();
}
