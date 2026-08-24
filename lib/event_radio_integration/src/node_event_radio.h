#pragma once

#include <stdint.h>

#include "node_event_delivery.h"

namespace EventRadioIntegration {

enum class NodeOwner : uint8_t {
    LISTENING,
    STANDBY_ACQUIRED,
    EVENT_TX,
    COMMAND_PRE_ACK,
    COMMAND_ACK_TX,
    COMMAND_RESPONSE_TX,
    MAINTENANCE
};

struct NodeSafePoint {
    bool runtimeReady;
    bool listening;
    bool isrPending;
    bool synchronousWork;
    bool maintenance;
    bool rendering;
};

enum class NodeAcquireResult : uint8_t {
    DENIED,
    ACQUIRE_STANDBY,
    RECEIVED_PACKET_WON,
    GRANT_EVENT_TX
};

class NodeArbiter {
public:
    NodeAcquireResult requestEvent(const NodeSafePoint& point,
                                   NodeEventDelivery::RuntimeState state) {
        if (owner_ != NodeOwner::LISTENING || !point.runtimeReady ||
            !point.listening || point.isrPending || point.synchronousWork ||
            point.maintenance || point.rendering ||
            state != NodeEventDelivery::RuntimeState::READY) {
            return NodeAcquireResult::DENIED;
        }
        owner_ = NodeOwner::STANDBY_ACQUIRED;
        return NodeAcquireResult::ACQUIRE_STANDBY;
    }

    NodeAcquireResult finishStandby(bool standbySucceeded, bool isrPending) {
        if (owner_ != NodeOwner::STANDBY_ACQUIRED) return NodeAcquireResult::DENIED;
        if (!standbySucceeded || isrPending) {
            owner_ = NodeOwner::LISTENING;
            return isrPending ? NodeAcquireResult::RECEIVED_PACKET_WON
                              : NodeAcquireResult::DENIED;
        }
        owner_ = NodeOwner::EVENT_TX;
        return NodeAcquireResult::GRANT_EVENT_TX;
    }

    void restoreListening() { owner_ = NodeOwner::LISTENING; }
    void beginCommandPreAck() { owner_ = NodeOwner::COMMAND_PRE_ACK; }
    void beginCommandAckTx() { owner_ = NodeOwner::COMMAND_ACK_TX; }
    void beginCommandResponseTx() { owner_ = NodeOwner::COMMAND_RESPONSE_TX; }
    NodeOwner owner() const { return owner_; }
    bool eventOwnsRadio() const { return owner_ == NodeOwner::EVENT_TX; }
    bool eventMayAcquire() const { return owner_ == NodeOwner::LISTENING; }

private:
    NodeOwner owner_ = NodeOwner::LISTENING;
};

constexpr uint32_t PRE_ACK_DELAY_MILLISECONDS = 100U;

class CommandPreAckTimer {
public:
    void begin(uint32_t now) { active_ = true; origin_ = now; fired_ = false; }
    bool due(uint32_t now) {
        if (!active_ || fired_ || static_cast<uint32_t>(now - origin_) <
                PRE_ACK_DELAY_MILLISECONDS) return false;
        fired_ = true;
        return true;
    }
    void clear() { active_ = false; fired_ = false; }
    bool active() const { return active_; }
private:
    bool active_ = false;
    bool fired_ = false;
    uint32_t origin_ = 0;
};

}  // namespace EventRadioIntegration
