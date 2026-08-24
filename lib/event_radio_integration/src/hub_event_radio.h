#pragma once

#include <stddef.h>
#include <stdint.h>

#include "event_protocol.h"
#include "hub_event_ledger.h"

namespace EventRadioIntegration {

enum class HubOwner : uint8_t {
    IDLE_RECEIVE,
    COMMAND_TX,
    COMMAND_WAIT_ACK,
    COMMAND_WAIT_RESPONSE,
    EVENT_ACK_TX
};

enum class HubAction : uint8_t {
    DISCARD,
    START_EVENT_ACK,
    RESUME_PRIOR_OWNER
};

struct HubResult {
    HubAction action;
    bool ledgerMutated;
    EventProtocol::AdmissionStatus status;
    uint8_t bytes[Protocol::MAX_PACKET_SIZE];
    size_t length;
};

class HubAdapter {
public:
    HubResult process(const uint8_t* bytes, size_t length,
                      uint8_t hubDeviceId, uint8_t nodeDeviceId,
                      HubEventLedger::Ledger& ledger) const {
        HubResult out = {};
        out.action = HubAction::DISCARD;
        Protocol::Packet packet = {};
        if (!Protocol::decode(bytes, length, packet) ||
            packet.type != Protocol::PacketType::EVENT ||
            packet.source != nodeDeviceId || packet.destination != hubDeviceId ||
            packet.source == 0 || packet.destination == 0 ||
            packet.payloadLength < EventProtocol::COMMON_PAYLOAD_SIZE) return out;

        const uint32_t epoch = EventProtocol::readUint32Le(packet.payload);
        const uint32_t id = EventProtocol::readUint32Le(packet.payload + 4);
        const uint8_t bodyLength = packet.payload[13];
        if (epoch == 0 || id == 0 || bodyLength > EventProtocol::MAX_BODY_SIZE ||
            packet.payloadLength != EventProtocol::COMMON_PAYLOAD_SIZE + bodyLength ||
            !EventProtocol::isCorrelatableFamily(packet.opcode)) return out;

        EventProtocol::Event event = {};
        event.source = packet.source; event.destination = packet.destination;
        event.sequence = packet.sequence; event.family = packet.opcode;
        event.epoch = epoch; event.id = id; event.flags = packet.payload[8];
        event.lifetimeBudgetSeconds = EventProtocol::readUint32Le(packet.payload + 9);
        event.bodyLength = bodyLength;
        for (uint8_t i = 0; i < bodyLength; ++i)
            event.body[i] = packet.payload[EventProtocol::COMMON_PAYLOAD_SIZE + i];

        EventProtocol::AdmissionStatus wireStatus;
        bool mutated = false;
        if (!EventProtocol::isRegisteredFamily(event.family)) {
            wireStatus = EventProtocol::AdmissionStatus::UNSUPPORTED_EVENT;
        } else if (!EventProtocol::isValidEvent(event)) {
            wireStatus = EventProtocol::AdmissionStatus::MALFORMED_EVENT;
        } else {
            const size_t before = ledger.activeCount();
            const HubEventLedger::AdmissionResult admitted = ledger.admit(event);
            switch (admitted.status) {
                case HubEventLedger::AdmissionStatus::DURABLE_NEW_ADMISSION:
                    wireStatus = EventProtocol::AdmissionStatus::ADMITTED;
                    mutated = ledger.activeCount() != before;
                    break;
                case HubEventLedger::AdmissionStatus::EXACT_DUPLICATE:
                    wireStatus = EventProtocol::AdmissionStatus::ADMITTED;
                    break;
                case HubEventLedger::AdmissionStatus::CAPACITY:
                    wireStatus = EventProtocol::AdmissionStatus::CAPACITY;
                    break;
                case HubEventLedger::AdmissionStatus::IDENTITY_CONTENT_MISMATCH:
                    wireStatus = EventProtocol::AdmissionStatus::IDENTITY_CONTENT_MISMATCH;
                    break;
                default:
                    return out;  // Local storage failure has no Wire success status.
            }
        }

        const EventProtocol::AdmissionResponse response =
            EventProtocol::makeAdmissionResponse(event, wireStatus);
        if (!EventProtocol::encodeAdmissionResponse(response, out.bytes,
                sizeof(out.bytes), out.length)) return HubResult{};
        out.action = HubAction::START_EVENT_ACK;
        out.ledgerMutated = mutated;
        out.status = wireStatus;
        return out;
    }
};

class HubArbiter {
public:
    void setOwner(HubOwner owner, uint32_t deadline = 0) {
        owner_ = owner; deadline_ = deadline;
    }
    bool beginEventAck() {
        if (owner_ == HubOwner::EVENT_ACK_TX || owner_ == HubOwner::COMMAND_TX) return false;
        savedOwner_ = owner_; savedDeadline_ = deadline_;
        owner_ = HubOwner::EVENT_ACK_TX;
        return true;
    }
    HubOwner finishEventAck() {
        if (owner_ == HubOwner::EVENT_ACK_TX) {
            owner_ = savedOwner_; deadline_ = savedDeadline_;
        }
        return owner_;
    }
    bool commandMayAcquire(bool isrPending) const {
        return owner_ == HubOwner::IDLE_RECEIVE && !isrPending;
    }
    HubOwner owner() const { return owner_; }
    uint32_t deadline() const { return deadline_; }
private:
    HubOwner owner_ = HubOwner::IDLE_RECEIVE;
    HubOwner savedOwner_ = HubOwner::IDLE_RECEIVE;
    uint32_t deadline_ = 0;
    uint32_t savedDeadline_ = 0;
};

}  // namespace EventRadioIntegration
