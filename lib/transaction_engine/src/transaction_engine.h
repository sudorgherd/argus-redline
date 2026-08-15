#pragma once

#include "protocol.h"

namespace TransactionEngine {

class NodeDuplicateTracker {
public:
    // Compare only after command admission and opcode-specific validation.
    bool isDuplicate(const Protocol::Packet& command) const {
        if (
            !hasRememberedCommand_ ||
            command.source != source_ ||
            command.sequence != sequence_ ||
            command.opcode != opcode_ ||
            command.payloadLength != payloadLength_ ||
            command.payloadLength > Protocol::MAX_PAYLOAD_SIZE
        ) {
            return false;
        }

        for (size_t index = 0; index < command.payloadLength; ++index) {
            if (command.payload[index] != payload_[index]) {
                return false;
            }
        }

        return true;
    }

    void remember(
        const Protocol::Packet& command,
        Protocol::AckStatus status
    ) {
        // Precondition: command is admissible and its payload is validated.
        hasRememberedCommand_ = true;
        source_ = command.source;
        sequence_ = command.sequence;
        opcode_ = command.opcode;
        payloadLength_ = command.payloadLength;
        for (size_t index = 0; index < Protocol::MAX_PAYLOAD_SIZE; ++index) {
            payload_[index] = index < command.payloadLength
                ? command.payload[index]
                : 0;
        }
        status_ = status;
    }

    // Precondition: isDuplicate() returned true for the current command.
    Protocol::AckStatus rememberedStatus() const {
        return status_;
    }

private:
    bool hasRememberedCommand_ = false;
    uint8_t source_ = 0;
    uint8_t sequence_ = 0;
    uint8_t opcode_ = 0;
    uint8_t payloadLength_ = 0;
    uint8_t payload_[Protocol::MAX_PAYLOAD_SIZE] = {};
    Protocol::AckStatus status_ = Protocol::AckStatus::SUCCESS;
};

enum class NodeCommandOutcome : uint8_t {
    IGNORE_WRONG_DESTINATION,
    IGNORE_WRONG_SENDER,
    IGNORE_WRONG_PACKET_TYPE,
    DUPLICATE,
    ACK_SUCCESS,
    ACK_UNSUPPORTED_OPCODE,
    ACK_MALFORMED_PACKET
};

struct NodeCommandEvaluation {
    NodeCommandOutcome outcome;
    // Used only for DUPLICATE and ACK_* outcomes.
    Protocol::AckStatus status;
};

inline NodeCommandEvaluation evaluateNodeCommand(
    const Protocol::Packet& command,
    uint8_t localDeviceId,
    uint8_t peerDeviceId,
    NodeDuplicateTracker& duplicateTracker
) {
    if (!Protocol::isAddressedTo(command, localDeviceId)) {
        return {
            NodeCommandOutcome::IGNORE_WRONG_DESTINATION,
            Protocol::AckStatus::SUCCESS
        };
    }

    if (command.type != Protocol::PacketType::COMMAND) {
        return {
            NodeCommandOutcome::IGNORE_WRONG_PACKET_TYPE,
            Protocol::AckStatus::SUCCESS
        };
    }

    if (command.source != peerDeviceId) {
        return {
            NodeCommandOutcome::IGNORE_WRONG_SENDER,
            Protocol::AckStatus::SUCCESS
        };
    }

    if (!Protocol::isSupportedCommandOpcode(command.opcode)) {
        return {
            NodeCommandOutcome::ACK_UNSUPPORTED_OPCODE,
            Protocol::AckStatus::UNSUPPORTED_OPCODE
        };
    }

    if (
        !Protocol::isValidCommandPayload(
            command.opcode,
            command.payloadLength
        )
    ) {
        return {
            NodeCommandOutcome::ACK_MALFORMED_PACKET,
            Protocol::AckStatus::MALFORMED_PACKET
        };
    }

    if (duplicateTracker.isDuplicate(command)) {
        return {
            NodeCommandOutcome::DUPLICATE,
            duplicateTracker.rememberedStatus()
        };
    }

    duplicateTracker.remember(command, Protocol::AckStatus::SUCCESS);
    return {
        NodeCommandOutcome::ACK_SUCCESS,
        Protocol::AckStatus::SUCCESS
    };
}

enum class HubAckOutcome : uint8_t {
    IGNORE_WRONG_DESTINATION,
    IGNORE_WRONG_SENDER,
    IGNORE_WRONG_PACKET_TYPE,
    IGNORE_WRONG_SEQUENCE,
    IGNORE_WRONG_OPCODE,
    IGNORE_MALFORMED_PAYLOAD,
    MATCHING_ACK
};

enum class HubTransactionAction : uint8_t {
    NO_ACTION,
    TRANSMIT_INITIAL,
    RETRANSMIT,
    TRANSACTION_SUCCEEDED,
    TRANSACTION_REMOTE_ERROR,
    TRANSACTION_FAILED
};

class HubTransactionState {
public:
    static constexpr uint8_t DEFAULT_INITIAL_SEQUENCE = 1;
    static constexpr uint8_t DEFAULT_MAX_RETRIES = 2;
    static constexpr uint32_t DEFAULT_ACK_TIMEOUT_MS = 2500;

    HubTransactionState(
        uint8_t initialSequence = DEFAULT_INITIAL_SEQUENCE,
        uint8_t maximumRetries = DEFAULT_MAX_RETRIES,
        uint32_t acknowledgmentTimeoutMs = DEFAULT_ACK_TIMEOUT_MS
    ) :
        sequence_(initialSequence),
        maximumRetries_(maximumRetries),
        acknowledgmentTimeoutMs_(acknowledgmentTimeoutMs) {}

    uint8_t currentSequence() const {
        return sequence_;
    }

    uint8_t retryCount() const {
        return retryCount_;
    }

    uint8_t maximumRetries() const {
        return maximumRetries_;
    }

    bool isAwaitingAcknowledgment() const {
        return awaitingAcknowledgment_;
    }

    uint32_t acknowledgmentDeadline() const {
        return acknowledgmentDeadline_;
    }

    HubTransactionAction requestedTransmission() const {
        return retryCount_ == 0
            ? HubTransactionAction::TRANSMIT_INITIAL
            : HubTransactionAction::RETRANSMIT;
    }

    void beginAcknowledgmentWait(uint32_t currentTimeMs) {
        awaitingAcknowledgment_ = true;
        acknowledgmentDeadline_ =
            currentTimeMs + acknowledgmentTimeoutMs_;
    }

    HubTransactionAction acknowledgmentWaitAction(
        uint32_t currentTimeMs
    ) const {
        if (
            !awaitingAcknowledgment_ ||
            static_cast<int32_t>(
                currentTimeMs - acknowledgmentDeadline_
            ) < 0
        ) {
            return HubTransactionAction::NO_ACTION;
        }

        return retryCount_ < maximumRetries_
            ? HubTransactionAction::RETRANSMIT
            : HubTransactionAction::TRANSACTION_FAILED;
    }

    HubTransactionAction attemptFailed() {
        awaitingAcknowledgment_ = false;

        if (retryCount_ < maximumRetries_) {
            retryCount_++;
            return HubTransactionAction::RETRANSMIT;
        }

        return HubTransactionAction::TRANSACTION_FAILED;
    }

    // Precondition: status was validated with Protocol::isValidAckStatus().
    HubTransactionAction acknowledgmentCompletionAction(
        Protocol::AckStatus status
    ) const {
        return status == Protocol::AckStatus::SUCCESS
            ? HubTransactionAction::TRANSACTION_SUCCEEDED
            : HubTransactionAction::TRANSACTION_REMOTE_ERROR;
    }

    // Apply exactly once after a terminal action, never while waiting/retrying.
    void completeTransaction() {
        sequence_++;
        retryCount_ = 0;
        awaitingAcknowledgment_ = false;
    }

private:
    uint8_t sequence_ = DEFAULT_INITIAL_SEQUENCE;
    uint8_t retryCount_ = 0;
    uint8_t maximumRetries_ = DEFAULT_MAX_RETRIES;
    bool awaitingAcknowledgment_ = false;
    uint32_t acknowledgmentDeadline_ = 0;
    uint32_t acknowledgmentTimeoutMs_ = DEFAULT_ACK_TIMEOUT_MS;
};

struct HubAckEvaluation {
    HubAckOutcome outcome;
    // Used only for MATCHING_ACK.
    uint8_t rawStatus;
};

// Precondition: outstandingCommand is the command for the active ACK wait.
inline HubAckEvaluation evaluateHubAcknowledgment(
    const Protocol::Packet& acknowledgment,
    const Protocol::Packet& outstandingCommand,
    uint8_t localHubId,
    uint8_t peerNodeId
) {
    if (acknowledgment.type != Protocol::PacketType::ACK) {
        return {HubAckOutcome::IGNORE_WRONG_PACKET_TYPE, 0};
    }

    if (acknowledgment.source != peerNodeId) {
        return {HubAckOutcome::IGNORE_WRONG_SENDER, 0};
    }

    if (!Protocol::isAddressedTo(acknowledgment, localHubId)) {
        return {HubAckOutcome::IGNORE_WRONG_DESTINATION, 0};
    }

    if (acknowledgment.sequence != outstandingCommand.sequence) {
        return {HubAckOutcome::IGNORE_WRONG_SEQUENCE, 0};
    }

    if (acknowledgment.opcode != outstandingCommand.opcode) {
        return {HubAckOutcome::IGNORE_WRONG_OPCODE, 0};
    }

    if (acknowledgment.payloadLength != 1) {
        return {HubAckOutcome::IGNORE_MALFORMED_PAYLOAD, 0};
    }

    return {HubAckOutcome::MATCHING_ACK, acknowledgment.payload[0]};
}

// Precondition: command was validated as addressed to the local Node.
inline Protocol::Packet makeAcknowledgment(
    const Protocol::Packet& command,
    Protocol::AckStatus status
) {
    Protocol::Packet acknowledgment = {};
    acknowledgment.type = Protocol::PacketType::ACK;
    acknowledgment.source = command.destination;
    acknowledgment.destination = command.source;
    acknowledgment.sequence = command.sequence;
    acknowledgment.opcode = command.opcode;
    acknowledgment.payloadLength = 1;
    acknowledgment.payload[0] = static_cast<uint8_t>(status);
    return acknowledgment;
}

}  // namespace TransactionEngine
