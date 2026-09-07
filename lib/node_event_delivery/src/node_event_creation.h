#pragma once

#include "event_producers.h"
#include "node_event_delivery.h"

namespace NodeEventDelivery {

// Shared production sink for every local producer. Sample time AFTER the
// enqueue commit/readback; an idle slot may still carry its old volatile clock.
template <typename Clock>
class TrackedCreationSink final : public EventProducers::CreationSink {
public:
    TrackedCreationSink(NodeEventStore::Store& store, Controller& controller, Clock clock)
        : sink_(store), controller_(controller), clock_(clock) {}

    EventProducers::CreationResult create(const NodeEventStore::EventInput& input) override {
        using EventProducers::CreationResult;
        if (controller_.degraded()) return CreationResult::STORAGE_FAILURE;
        const CreationResult created = sink_.create(input);
        if (created != CreationResult::ENQUEUED) return created;
        if (controller_.trackEnqueued(sink_.lastResult().slot,
                static_cast<uint32_t>(clock_())) != ControllerStatus::OK) {
            // The durable record stays owned. Block delivery/production rather
            // than report creation success with an uninitialized lifetime.
            return CreationResult::STORAGE_FAILURE;
        }
        return CreationResult::ENQUEUED;
    }

    bool lastResultAvailable() const { return sink_.lastResultAvailable(); }
    const NodeEventStore::EnqueueResult& lastResult() const { return sink_.lastResult(); }

private:
    EventProducers::StoreCreationSink sink_;
    Controller& controller_;
    Clock clock_;
};

} // namespace NodeEventDelivery
