#pragma once

#include <stdint.h>

#include "device_capabilities.h"

namespace RuntimeState {

enum class DeviceRole : uint8_t {
    HUB,
    NODE
};

enum class RuntimePhase : uint8_t {
    IDLE,
    TRANSMITTING,
    WAITING_FOR_ACK,
    LISTENING,
    TRANSMITTING_ACK,
    WAITING_FOR_RESPONSE,
    TRANSMITTING_RESPONSE
};

enum class Health : uint8_t {
    STARTING,
    READY,
    DEGRADED,
    ERROR
};

enum class ErrorClass : uint8_t {
    NONE,
    RADIO_INITIALIZATION,
    RADIO_START_RECEIVE,
    RADIO_START_TRANSMIT,
    RADIO_READ,
    PACKET_LENGTH,
    PACKET_DECODE,
    PACKET_IGNORED,
    ACK_TIMEOUT,
    REMOTE_ACK,
    ACK_STATUS
};

struct LastInboundPacket {
    bool available = false;
    uint8_t rawType = 0;
    uint8_t source = 0;
    uint8_t destination = 0;
    uint8_t sequence = 0;
    uint8_t opcode = 0;
    uint8_t payloadLength = 0;
    bool ackStatusAvailable = false;
    uint8_t rawAckStatus = 0;
    uint32_t observedAtMs = 0;
};

struct DiagnosticCounters {
    uint32_t transmissionsCompleted = 0;
    uint32_t decodedPacketsReceived = 0;
    uint32_t successfulTransactions = 0;
    uint32_t acceptedCommands = 0;
    uint32_t retransmissions = 0;
    uint32_t acknowledgmentTimeouts = 0;
    uint32_t duplicates = 0;
    uint32_t malformedPackets = 0;
    uint32_t ignoredPackets = 0;
    uint32_t radioErrors = 0;
};

class State {
public:
    // localId and peerId are immutable snapshots supplied by DeviceConfig.
    State(DeviceRole role, uint8_t localId, uint8_t peerId) :
        role_(role),
        localId_(localId),
        peerId_(peerId),
        phase_(
            role == DeviceRole::HUB
                ? RuntimePhase::IDLE
                : RuntimePhase::LISTENING
        ) {}

    DeviceRole role() const {
        return role_;
    }

    uint8_t localId() const {
        return localId_;
    }

    uint8_t peerId() const {
        return peerId_;
    }

    bool isReady() const {
        return ready_;
    }

    void setReady(bool ready) {
        ready_ = ready;
    }

    RuntimePhase phase() const {
        return phase_;
    }

    void setPhase(RuntimePhase phase) {
        phase_ = phase;
    }

    Health health() const {
        return health_;
    }

    void setHealth(Health health) {
        health_ = health;
    }

    ErrorClass lastError() const {
        return lastError_;
    }

    void recordError(ErrorClass error) {
        lastError_ = error;
    }

    bool hasRadioMetrics() const {
        return radioMetricsAvailable_;
    }

    float latestRssi() const {
        return latestRssi_;
    }

    float latestSnr() const {
        return latestSnr_;
    }

    void updateRadioMetrics(float rssi, float snr) {
        latestRssi_ = rssi;
        latestSnr_ = snr;
        radioMetricsAvailable_ = true;
    }

    const LastInboundPacket& lastInboundPacket() const {
        return lastInboundPacket_;
    }

    void recordInboundPacket(
        uint8_t rawType,
        uint8_t source,
        uint8_t destination,
        uint8_t sequence,
        uint8_t opcode,
        uint8_t payloadLength,
        bool ackStatusAvailable,
        uint8_t rawAckStatus,
        uint32_t observedAtMs
    ) {
        LastInboundPacket packet;
        packet.available = true;
        packet.rawType = rawType;
        packet.source = source;
        packet.destination = destination;
        packet.sequence = sequence;
        packet.opcode = opcode;
        packet.payloadLength = payloadLength;
        packet.ackStatusAvailable = ackStatusAvailable;
        packet.rawAckStatus = ackStatusAvailable ? rawAckStatus : 0;
        packet.observedAtMs = observedAtMs;
        lastInboundPacket_ = packet;
    }

    bool hasLastActivity() const {
        return lastActivityAvailable_;
    }

    uint32_t lastActivityAtMs() const {
        return lastActivityAtMs_;
    }

    void recordActivity(uint32_t observedAtMs) {
        lastActivityAvailable_ = true;
        lastActivityAtMs_ = observedAtMs;
    }

    const DiagnosticCounters& counters() const {
        return counters_;
    }

    bool hasCapabilityDiagnostics() const {
        return capabilityDiagnosticsAvailable_;
    }

    const DeviceCapabilities::CapabilityDiagnosticsSnapshot&
    capabilityDiagnostics() const {
        return capabilityDiagnostics_;
    }

    bool updateCapabilityDiagnostics(
        const DeviceCapabilities::CapabilityDiagnosticsSnapshot& snapshot
    ) {
        if (!DeviceCapabilities::isValidCapabilityDiagnosticsSnapshot(
                snapshot
            )) {
            return false;
        }

        capabilityDiagnostics_ = snapshot;
        capabilityDiagnosticsAvailable_ = true;
        return true;
    }

    void incrementTransmissionsCompleted(uint32_t amount = 1) {
        saturatingIncrement(counters_.transmissionsCompleted, amount);
    }

    void incrementDecodedPacketsReceived(uint32_t amount = 1) {
        saturatingIncrement(counters_.decodedPacketsReceived, amount);
    }

    void incrementSuccessfulTransactions(uint32_t amount = 1) {
        saturatingIncrement(counters_.successfulTransactions, amount);
    }

    void incrementAcceptedCommands(uint32_t amount = 1) {
        saturatingIncrement(counters_.acceptedCommands, amount);
    }

    void incrementRetransmissions(uint32_t amount = 1) {
        saturatingIncrement(counters_.retransmissions, amount);
    }

    void incrementAcknowledgmentTimeouts(uint32_t amount = 1) {
        saturatingIncrement(counters_.acknowledgmentTimeouts, amount);
    }

    void incrementDuplicates(uint32_t amount = 1) {
        saturatingIncrement(counters_.duplicates, amount);
    }

    void incrementMalformedPackets(uint32_t amount = 1) {
        saturatingIncrement(counters_.malformedPackets, amount);
    }

    void incrementIgnoredPackets(uint32_t amount = 1) {
        saturatingIncrement(counters_.ignoredPackets, amount);
    }

    void incrementRadioErrors(uint32_t amount = 1) {
        saturatingIncrement(counters_.radioErrors, amount);
    }

private:
    static void saturatingIncrement(uint32_t& value, uint32_t amount) {
        value = amount > UINT32_MAX - value
            ? UINT32_MAX
            : value + amount;
    }

    DeviceRole role_;
    uint8_t localId_;
    uint8_t peerId_;
    bool ready_ = false;
    RuntimePhase phase_;
    Health health_ = Health::STARTING;
    ErrorClass lastError_ = ErrorClass::NONE;
    bool radioMetricsAvailable_ = false;
    float latestRssi_ = 0.0F;
    float latestSnr_ = 0.0F;
    LastInboundPacket lastInboundPacket_;
    bool lastActivityAvailable_ = false;
    uint32_t lastActivityAtMs_ = 0;
    DiagnosticCounters counters_;
    bool capabilityDiagnosticsAvailable_ = false;
    DeviceCapabilities::CapabilityDiagnosticsSnapshot
        capabilityDiagnostics_ = {};
};

}  // namespace RuntimeState
