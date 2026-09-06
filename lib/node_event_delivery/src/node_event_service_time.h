#pragma once

#include "node_event_delivery.h"

namespace NodeEventDelivery {

struct ServiceTick {
    uint32_t now;
    ControllerResult result;
};

// Sample at the service boundary, AFTER packet processing. Accept a clock,
// not the loop-start timestamp: admissionCandidate may already have serviced
// lifetime using a later RX timestamp. No time correction or policy change.
template <typename Clock>
ServiceTick serviceNow(Controller& controller, bool synchronousWork, Clock clock) {
    const uint32_t now = static_cast<uint32_t>(clock());
    return {now, controller.service(now, synchronousWork)};
}

} // namespace NodeEventDelivery
