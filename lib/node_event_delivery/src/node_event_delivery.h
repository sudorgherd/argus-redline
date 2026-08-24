#pragma once

#include <stddef.h>
#include <stdint.h>

#include "node_event_store.h"

namespace NodeEventDelivery {

constexpr uint32_t CHECKPOINT_SECONDS = 60U;
constexpr uint32_t CHECKPOINT_MILLISECONDS = 60000U;

enum class Status : uint8_t {
    READY,
    STORE_DEGRADED,
    DEBIT_FAILURE,
    CHECKPOINT_FAILURE,
    EXPIRY_FAILURE,
    INVALID_SLOT,
    NOT_QUEUED
};

enum class AttemptResult : uint8_t {
    ARMED,
    EXHAUSTED,
    EXPIRED,
    NOT_QUEUED,
    INVALID_SLOT,
    STORAGE_FAILURE,
    DEGRADED
};

class Policy {
public:
    Status recover(NodeEventStore::Store& store, uint32_t nowMilliseconds) {
        reset();
        store_ = &store;
        if (!store.healthy()) return fail(Status::STORE_DEGRADED);

        for (uint8_t slot = 0; slot < NodeEventStore::NODE_EVENT_CAPACITY; ++slot) {
            const EventRecords::NodeRecord* record = store.recordAt(slot);
            if (record == nullptr || record->state != EventRecords::NodeState::QUEUED) continue;
            const NodeEventStore::MutationStatus mutation =
                record->remainingActiveSeconds <= CHECKPOINT_SECONDS
                    ? store.markExpired(slot)
                    : store.checkpointRemaining(
                        slot, record->remainingActiveSeconds - CHECKPOINT_SECONDS);
            if (mutation != NodeEventStore::MutationStatus::OK) {
                return fail(Status::DEBIT_FAILURE);
            }
        }

        initializeTrackedSlots(nowMilliseconds);
        ready_ = true;
        status_ = Status::READY;
        return status_;
    }

    Status trackEnqueued(uint8_t slot, uint32_t nowMilliseconds) {
        if (!ready_ || store_ == nullptr || !store_->healthy()) {
            return fail(Status::STORE_DEGRADED);
        }
        if (slot >= NodeEventStore::NODE_EVENT_CAPACITY) return Status::INVALID_SLOT;
        const EventRecords::NodeRecord* record = store_->recordAt(slot);
        if (record == nullptr || record->state != EventRecords::NodeState::QUEUED) {
            return Status::NOT_QUEUED;
        }
        SlotRuntime& runtime = runtime_[slot];
        runtime.tracked = true;
        runtime.uncommittedMilliseconds = 0;
        runtime.baselineMilliseconds = nowMilliseconds;
        return Status::READY;
    }

    Status service(uint32_t nowMilliseconds) {
        if (!ready_ || store_ == nullptr || !store_->healthy()) {
            return fail(Status::STORE_DEGRADED);
        }
        for (uint8_t slot = 0; slot < NodeEventStore::NODE_EVENT_CAPACITY; ++slot) {
            SlotRuntime& runtime = runtime_[slot];
            const EventRecords::NodeRecord* record = store_->recordAt(slot);
            if (record == nullptr || record->state != EventRecords::NodeState::QUEUED) {
                runtime = {};
                continue;
            }
            if (!runtime.tracked) {
                runtime.tracked = true;
                runtime.baselineMilliseconds = nowMilliseconds;
                runtime.uncommittedMilliseconds = 0;
                continue;
            }
            const uint32_t elapsed = nowMilliseconds - runtime.baselineMilliseconds;
            runtime.baselineMilliseconds = nowMilliseconds;
            runtime.uncommittedMilliseconds += static_cast<uint64_t>(elapsed);
            while (true) {
                record = store_->recordAt(slot);
                if (record == nullptr || record->state != EventRecords::NodeState::QUEUED) {
                    runtime = {};
                    break;
                }
                if (record->remainingActiveSeconds > CHECKPOINT_SECONDS &&
                    runtime.uncommittedMilliseconds >= CHECKPOINT_MILLISECONDS) {
                    if (store_->checkpointRemaining(
                            slot, record->remainingActiveSeconds - CHECKPOINT_SECONDS) !=
                        NodeEventStore::MutationStatus::OK) {
                        return fail(Status::CHECKPOINT_FAILURE);
                    }
                    runtime.uncommittedMilliseconds -= CHECKPOINT_MILLISECONDS;
                    continue;
                }
                if (runtime.uncommittedMilliseconds >=
                    static_cast<uint64_t>(record->remainingActiveSeconds) * 1000U) {
                    if (store_->markExpired(slot) != NodeEventStore::MutationStatus::OK) {
                        return fail(Status::EXPIRY_FAILURE);
                    }
                    runtime = {};
                    break;
                }
                break;
            }
        }
        return Status::READY;
    }

    uint32_t effectiveRemainingSeconds(uint8_t slot) const {
        if (!ready_ || store_ == nullptr || slot >= NodeEventStore::NODE_EVENT_CAPACITY) return 0;
        const EventRecords::NodeRecord* record = store_->recordAt(slot);
        if (record == nullptr || record->state != EventRecords::NodeState::QUEUED ||
            !runtime_[slot].tracked) return 0;
        const uint64_t elapsedSeconds = runtime_[slot].uncommittedMilliseconds / 1000U;
        return elapsedSeconds >= record->remainingActiveSeconds
            ? 0U
            : record->remainingActiveSeconds - static_cast<uint32_t>(elapsedSeconds);
    }

    bool eligible(uint8_t slot) const {
        if (!ready_ || store_ == nullptr || !store_->healthy() ||
            slot >= NodeEventStore::NODE_EVENT_CAPACITY || !runtime_[slot].tracked) {
            return false;
        }
        const EventRecords::NodeRecord* record = store_->recordAt(slot);
        return record != nullptr && record->state == EventRecords::NodeState::QUEUED &&
            runtime_[slot].uncommittedMilliseconds <
                static_cast<uint64_t>(record->remainingActiveSeconds) * 1000U;
    }

    AttemptResult armAttempt(uint8_t slot, uint32_t nowMilliseconds) {
        if (!ready_) return AttemptResult::DEGRADED;
        const Status serviced = service(nowMilliseconds);
        if (serviced != Status::READY) return AttemptResult::STORAGE_FAILURE;
        if (slot >= NodeEventStore::NODE_EVENT_CAPACITY) return AttemptResult::INVALID_SLOT;
        const EventRecords::NodeRecord* record = store_->recordAt(slot);
        if (record == nullptr || record->state == EventRecords::NodeState::EXPIRED) {
            return AttemptResult::EXPIRED;
        }
        if (record->state != EventRecords::NodeState::QUEUED) return AttemptResult::NOT_QUEUED;
        if (!eligible(slot)) return AttemptResult::EXPIRED;
        switch (store_->armAttempt(slot)) {
            case NodeEventStore::AttemptStatus::ARMED: return AttemptResult::ARMED;
            case NodeEventStore::AttemptStatus::EXHAUSTED: return AttemptResult::EXHAUSTED;
            case NodeEventStore::AttemptStatus::INVALID_SLOT: return AttemptResult::INVALID_SLOT;
            case NodeEventStore::AttemptStatus::NOT_QUEUED: return AttemptResult::NOT_QUEUED;
            case NodeEventStore::AttemptStatus::STORAGE_FAILURE:
                fail(Status::STORE_DEGRADED);
                return AttemptResult::STORAGE_FAILURE;
            case NodeEventStore::AttemptStatus::DEGRADED:
            default:
                fail(Status::STORE_DEGRADED);
                return AttemptResult::DEGRADED;
        }
    }

    bool ready() const { return ready_; }
    Status status() const { return status_; }

private:
    struct SlotRuntime {
        bool tracked;
        uint32_t baselineMilliseconds;
        uint64_t uncommittedMilliseconds;
    };

    void initializeTrackedSlots(uint32_t nowMilliseconds) {
        for (uint8_t slot = 0; slot < NodeEventStore::NODE_EVENT_CAPACITY; ++slot) {
            const EventRecords::NodeRecord* record = store_->recordAt(slot);
            if (record != nullptr && record->state == EventRecords::NodeState::QUEUED) {
                runtime_[slot].tracked = true;
                runtime_[slot].baselineMilliseconds = nowMilliseconds;
                runtime_[slot].uncommittedMilliseconds = 0;
            }
        }
    }

    Status fail(Status status) {
        ready_ = false;
        status_ = status;
        return status;
    }
    void reset() {
        store_ = nullptr;
        for (size_t i = 0; i < NodeEventStore::NODE_EVENT_CAPACITY; ++i) runtime_[i] = {};
        ready_ = false;
        status_ = Status::STORE_DEGRADED;
    }

    NodeEventStore::Store* store_ = nullptr;
    SlotRuntime runtime_[NodeEventStore::NODE_EVENT_CAPACITY] = {};
    bool ready_ = false;
    Status status_ = Status::STORE_DEGRADED;
};

}  // namespace NodeEventDelivery
