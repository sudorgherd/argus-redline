#pragma once

#include "node_event_delivery.h"

namespace NodeEventDelivery {

inline bool isTerminalRecord(const EventRecords::NodeRecord* record) {
    return record && (record->state == EventRecords::NodeState::FAILED ||
        record->state == EventRecords::NodeState::EXPIRED);
}

inline bool hasTerminalRecords(const NodeEventStore::Store& store) {
    for (uint8_t slot = 0; slot < NodeEventStore::NODE_EVENT_CAPACITY; ++slot)
        if (isTerminalRecord(store.recordAt(slot))) return true;
    return false;
}

// Production calls this only after store recovery or at acquired radio standby.
// Each terminal proof is already durable; each FREE is a separate commit/readback.
// Scan all eight slots: lifetime can expire records behind the active FIFO head.
inline bool reclaimTerminals(NodeEventStore::Store& store, Controller* controller,
    EventTxDiagnostics::Observer* observer = nullptr, uint32_t now = 0) {
    if (!store.healthy()) return false;
    for (uint8_t slot = 0; slot < NodeEventStore::NODE_EVENT_CAPACITY; ++slot) {
        if (!isTerminalRecord(store.recordAt(slot))) continue;
        if (controller && controller->hasActiveEvent() && controller->activeSlot() == slot) {
            if (controller->reclaimTerminal().status != ControllerStatus::OK) return false;
        } else if (store.reclaim(slot) != NodeEventStore::MutationStatus::OK) {
            return false;
        }
        if (observer) observer->note(EventTxDiagnostics::Counter::TERMINAL_RECLAIMED, now);
    }
    return true;
}

} // namespace NodeEventDelivery
