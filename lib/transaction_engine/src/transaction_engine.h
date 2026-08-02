#pragma once

#include "protocol.h"

namespace TransactionEngine {

class NodeDuplicateTracker {
public:
    bool isDuplicate(const Protocol::Packet& command) const {
        return (
            hasRememberedCommand_ &&
            command.source == source_ &&
            command.sequence == sequence_ &&
            command.opcode == opcode_
        );
    }

    void remember(
        const Protocol::Packet& command,
        Protocol::AckStatus status
    ) {
        hasRememberedCommand_ = true;
        source_ = command.source;
        sequence_ = command.sequence;
        opcode_ = command.opcode;
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

    if (duplicateTracker.isDuplicate(command)) {
        return {
            NodeCommandOutcome::DUPLICATE,
            duplicateTracker.rememberedStatus()
        };
    }

    Protocol::AckStatus status;
    NodeCommandOutcome outcome;

    if (!Protocol::isSupportedCommandOpcode(command.opcode)) {
        status = Protocol::AckStatus::UNSUPPORTED_OPCODE;
        outcome = NodeCommandOutcome::ACK_UNSUPPORTED_OPCODE;
    } else if (
        !Protocol::isValidCommandPayload(
            command.opcode,
            command.payloadLength
        )
    ) {
        status = Protocol::AckStatus::MALFORMED_PACKET;
        outcome = NodeCommandOutcome::ACK_MALFORMED_PACKET;
    } else {
        status = Protocol::AckStatus::SUCCESS;
        outcome = NodeCommandOutcome::ACK_SUCCESS;
    }

    duplicateTracker.remember(command, status);
    return {outcome, status};
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
