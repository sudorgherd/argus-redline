#pragma once

#include <stdint.h>

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
    TRANSMITTING_ACK
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

    float latestRssi() const {
        return latestRssi_;
    }

    float latestSnr() const {
        return latestSnr_;
    }

    void updateRadioMetrics(float rssi, float snr) {
        latestRssi_ = rssi;
        latestSnr_ = snr;
    }

private:
    DeviceRole role_;
    uint8_t localId_;
    uint8_t peerId_;
    bool ready_ = false;
    RuntimePhase phase_;
    float latestRssi_ = 0.0F;
    float latestSnr_ = 0.0F;
};

}  // namespace RuntimeState
