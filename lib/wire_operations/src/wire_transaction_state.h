#pragma once

#include "transaction_engine.h"
#include "wire_operations.h"

namespace WireOperations {

enum class AdmissionOutcome : uint8_t {
    IGNORE,
    UNSUPPORTED_OPCODE,
    MALFORMED,
    ADMITTED,
    DUPLICATE_PENDING,
    DUPLICATE_COMPLETE,
    ACTIVE_BUSY
};

class NodeRetainedOperation {
public:
    bool isActive() const { return hasEntry_ && !complete_; }
    bool hasEntry() const { return hasEntry_; }
    bool isComplete() const { return hasEntry_ && complete_; }
    uint32_t lookupCount() const { return lookupCount_; }

    bool isDuplicate(const Protocol::Packet& command) {
        ++lookupCount_;
        if (!hasEntry_ || command.source != source_ ||
            command.sequence != sequence_ || command.opcode != opcode_ ||
            command.payloadLength != payloadLength_) return false;
        for (size_t i = 0; i < command.payloadLength; ++i) {
            if (command.payload[i] != payload_[i]) return false;
        }
        return true;
    }

    bool reserve(const Protocol::Packet& command) {
        if (isActive() || command.payloadLength > Protocol::MAX_PAYLOAD_SIZE)
            return false;
        hasEntry_ = true; complete_ = false;
        source_ = command.source; destination_ = command.destination;
        sequence_ = command.sequence;
        opcode_ = command.opcode; payloadLength_ = command.payloadLength;
        for (size_t i = 0; i < Protocol::MAX_PAYLOAD_SIZE; ++i)
            payload_[i] = i < command.payloadLength ? command.payload[i] : 0;
        response_ = {};
        return true;
    }

    bool complete(const Protocol::Packet& response) {
        if (!isActive() || response.type != Protocol::PacketType::RESPONSE ||
            response.source != destination_ || response.destination != source_ ||
            response.sequence != sequence_ || response.opcode != opcode_)
            return false;
        Response decoded = {};
        if (decodeResponse(response.opcode, response.payload,
                response.payloadLength, decoded) != CodecResult::OK) return false;
        Request request = {};
        if (decodeRequest(opcode_, payload_, payloadLength_, request) !=
                CodecResult::OK || decoded.targetId != request.targetId)
            return false;
        response_ = response; complete_ = true;
        return true;
    }

    Protocol::Packet acknowledgment() const {
        Protocol::Packet command = rememberedCommand();
        return TransactionEngine::makeAcknowledgment(
            command, Protocol::AckStatus::SUCCESS);
    }
    const Protocol::Packet& response() const { return response_; }
    void reset() { *this = NodeRetainedOperation{}; }

private:
    Protocol::Packet rememberedCommand() const {
        Protocol::Packet command = {};
        command.type = Protocol::PacketType::COMMAND;
        command.source = source_; command.destination = destination_;
        command.sequence = sequence_; command.opcode = opcode_;
        command.payloadLength = payloadLength_;
        for (size_t i = 0; i < payloadLength_; ++i) command.payload[i] = payload_[i];
        return command;
    }
    bool hasEntry_ = false;
    bool complete_ = false;
    uint8_t source_ = 0;
    uint8_t destination_ = 0;
    uint8_t sequence_ = 0;
    uint8_t opcode_ = 0;
    uint8_t payloadLength_ = 0;
    uint8_t payload_[Protocol::MAX_PAYLOAD_SIZE] = {};
    Protocol::Packet response_ = {};
    uint32_t lookupCount_ = 0;
};

struct AdmissionResult {
    AdmissionOutcome outcome;
    Protocol::AckStatus acknowledgmentStatus;
};

inline AdmissionResult admitNodeCommand(
    const Protocol::Packet& command,
    uint8_t localId,
    uint8_t peerId,
    NodeRetainedOperation& retained
) {
    if (command.destination != localId || command.source != peerId ||
        command.type != Protocol::PacketType::COMMAND)
        return {AdmissionOutcome::IGNORE, Protocol::AckStatus::SUCCESS};
    if (!isStructuredOpcode(command.opcode))
        return {AdmissionOutcome::UNSUPPORTED_OPCODE,
            Protocol::AckStatus::UNSUPPORTED_OPCODE};
    Request decoded = {};
    if (decodeRequest(command.opcode, command.payload,
            command.payloadLength, decoded) != CodecResult::OK)
        return {AdmissionOutcome::MALFORMED,
            Protocol::AckStatus::MALFORMED_PACKET};
    if (retained.isDuplicate(command)) {
        return {retained.isComplete() ? AdmissionOutcome::DUPLICATE_COMPLETE
            : AdmissionOutcome::DUPLICATE_PENDING, Protocol::AckStatus::SUCCESS};
    }
    if (retained.isActive())
        return {AdmissionOutcome::ACTIVE_BUSY, Protocol::AckStatus::SUCCESS};
    retained.reserve(command);
    return {AdmissionOutcome::ADMITTED, Protocol::AckStatus::SUCCESS};
}

enum class PeerSupport : uint8_t { UNKNOWN, SUPPORTED, UNSUPPORTED };

inline bool mayStartStructuredOperation(
    PeerSupport support, uint8_t opcode, bool explicitlyConfigured = false
) {
    return explicitlyConfigured || support == PeerSupport::SUPPORTED ||
        (support == PeerSupport::UNKNOWN &&
         opcode == static_cast<uint8_t>(Protocol::Opcode::PING));
}

enum class StructuredPhase : uint8_t { IDLE, AWAITING_ACK, AWAITING_RESPONSE };
enum class StructuredEvent : uint8_t {
    NONE, ACK_ACCEPTED, REMOTE_REJECTED, RESPONSE_COMPLETE,
    MALFORMED_IGNORED, MISMATCH_IGNORED, RETRANSMIT, TIMEOUT
};

class HubStructuredTransaction {
public:
    bool begin(const Protocol::Packet& command, uint32_t now,
        uint32_t overallTimeout, uint8_t maxRetries = 2) {
        if (phase_ != StructuredPhase::IDLE ||
            command.type != Protocol::PacketType::COMMAND ||
            !isStructuredOpcode(command.opcode)) return false;
        Request request = {};
        if (decodeRequest(command.opcode, command.payload,
                command.payloadLength, request) != CodecResult::OK) return false;
        command_ = command; requestTarget_ = request.targetId;
        phase_ = StructuredPhase::AWAITING_ACK;
        deadline_ = now + overallTimeout; retries_ = 0; maxRetries_ = maxRetries;
        return true;
    }
    StructuredPhase phase() const { return phase_; }
    uint32_t deadline() const { return deadline_; }
    uint8_t retryCount() const { return retries_; }
    const Protocol::Packet& command() const { return command_; }
    PeerSupport peerSupport() const { return support_; }
    const Response& result() const { return result_; }

    StructuredEvent receive(const Protocol::Packet& packet) {
        if (phase_ == StructuredPhase::IDLE) return StructuredEvent::MISMATCH_IGNORED;
        if (packet.source != command_.destination ||
            packet.destination != command_.source ||
            packet.sequence != command_.sequence || packet.opcode != command_.opcode)
            return StructuredEvent::MISMATCH_IGNORED;
        if (packet.type == Protocol::PacketType::ACK) {
            if (packet.payloadLength != 1 || !Protocol::isValidAckStatus(packet.payload[0]))
                return StructuredEvent::MALFORMED_IGNORED;
            const Protocol::AckStatus status =
                static_cast<Protocol::AckStatus>(packet.payload[0]);
            if (status != Protocol::AckStatus::SUCCESS) {
                if (command_.opcode == static_cast<uint8_t>(Protocol::Opcode::PING) &&
                    status == Protocol::AckStatus::UNSUPPORTED_OPCODE)
                    support_ = PeerSupport::UNSUPPORTED;
                phase_ = StructuredPhase::IDLE;
                return StructuredEvent::REMOTE_REJECTED;
            }
            phase_ = StructuredPhase::AWAITING_RESPONSE;
            return StructuredEvent::ACK_ACCEPTED;
        }
        if (packet.type != Protocol::PacketType::RESPONSE)
            return StructuredEvent::MISMATCH_IGNORED;
        Response decoded = {};
        if (decodeResponse(packet.opcode, packet.payload,
                packet.payloadLength, decoded) != CodecResult::OK ||
            decoded.targetId != requestTarget_)
            return StructuredEvent::MALFORMED_IGNORED;
        result_ = decoded;
        support_ = PeerSupport::SUPPORTED;
        phase_ = StructuredPhase::IDLE;
        return StructuredEvent::RESPONSE_COMPLETE;
    }

    StructuredEvent service(uint32_t now) {
        if (phase_ == StructuredPhase::IDLE ||
            static_cast<int32_t>(now - deadline_) < 0) return StructuredEvent::NONE;
        if (retries_ < maxRetries_) {
            ++retries_;
            return StructuredEvent::RETRANSMIT;
        }
        phase_ = StructuredPhase::IDLE;
        return StructuredEvent::TIMEOUT;
    }
    void configureSupport(PeerSupport support) { support_ = support; }

private:
    StructuredPhase phase_ = StructuredPhase::IDLE;
    PeerSupport support_ = PeerSupport::UNKNOWN;
    Protocol::Packet command_ = {};
    Response result_ = {};
    uint16_t requestTarget_ = 0;
    uint32_t deadline_ = 0;
    uint8_t retries_ = 0;
    uint8_t maxRetries_ = 0;
};

}  // namespace WireOperations
