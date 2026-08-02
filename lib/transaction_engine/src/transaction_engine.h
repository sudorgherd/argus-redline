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
