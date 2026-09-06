#pragma once

#include "hub_event_radio.h"

namespace EventRadioIntegration {

enum class ReleaseReceiveResult : uint8_t {
    RESTORED, PENDING_IRQ, EVENT_OWNED, STANDBY_FAILED, RECEIVE_FAILED
};

// One-shot transaction-release boundary, not an idle-loop service. The caller
// has released its command owner and scheduled IDLE. Never consume an IRQ here:
// the completion that caused release was already consumed by the dispatcher;
// another pending IRQ must reach idle RX dispatch before any receive staging.
// restoreReceive must preserve the flag, including IRQs raised during RX start.
template <typename Radio, typename RestoreReceive>
ReleaseReceiveResult restoreReleasedReceive(Radio& radio, volatile bool& irqPending,
    bool eventAckActive, const HubArbiter& arbiter, RestoreReceive restoreReceive) {
    if (eventAckActive || arbiter.owner() == HubOwner::EVENT_ACK_TX)
        return ReleaseReceiveResult::EVENT_OWNED;
    if (irqPending) return ReleaseReceiveResult::PENDING_IRQ;
    const int standbyResult = radio.standby();
    // Standby stops new RX activity; recheck the existing ISR flag afterwards.
    if (standbyResult != 0) return ReleaseReceiveResult::STANDBY_FAILED;
    if (irqPending) return ReleaseReceiveResult::PENDING_IRQ;
    return restoreReceive() ? ReleaseReceiveResult::RESTORED
                            : ReleaseReceiveResult::RECEIVE_FAILED;
}

} // namespace EventRadioIntegration
