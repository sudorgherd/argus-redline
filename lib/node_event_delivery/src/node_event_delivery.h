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

constexpr uint32_t ADMISSION_TIMEOUT_MILLISECONDS = 2500U;
constexpr uint16_t MAX_BACKOFF_JITTER_MILLISECONDS = 250U;

enum class RuntimeState : uint8_t {
    QUEUED, READY, TX_PREPARE, TX, WAIT_ADMISSION, BACKOFF,
    RELEASED, FAILED, EXPIRED
};

enum class RadioActionType : uint8_t { NONE, TRANSMIT, RECEIVE };
enum class ControllerStatus : uint8_t {
    OK, NO_ACTIVE_EVENT, INVALID_CONFIGURATION, INVALID_STATE,
    SEQUENCE_FAILURE, ENCODE_FAILURE, POLICY_FAILURE, STORAGE_FAILURE, DEGRADED
};

struct RadioAction {
    RadioActionType type;
    uint8_t bytes[Protocol::MAX_PACKET_SIZE];
    size_t length;
};

struct ControllerResult {
    ControllerStatus status;
    RadioAction action;
};

class SequenceSource {
public:
    virtual ~SequenceSource() = default;
    virtual bool next(uint8_t& sequence) = 0;
};

class JitterSource {
public:
    virtual ~JitterSource() = default;
    virtual uint16_t nextMilliseconds() = 0;
};

class Controller {
public:
    ControllerResult recover(NodeEventStore::Store& store, uint8_t sourceDeviceId,
                             uint8_t hubDeviceId, SequenceSource& sequences,
                             JitterSource& jitter, uint32_t nowMilliseconds) {
        reset();
        if (!EventProtocol::hasValidEndpoints(sourceDeviceId, hubDeviceId))
            return result(ControllerStatus::INVALID_CONFIGURATION);
        store_ = &store; sequences_ = &sequences; jitter_ = &jitter;
        source_ = sourceDeviceId; destination_ = hubDeviceId;
        if (policy_.recover(store, nowMilliseconds) != Status::READY)
            return degrade(ControllerStatus::POLICY_FAILURE, false);
        initialized_ = true;
        loadHead();
        if (active_ && record()->attemptsUsed >= EventRecords::MAX_ATTEMPTS) {
            if (!markFailed()) return degrade(ControllerStatus::STORAGE_FAILURE, true);
        }
        return result(active_ ? ControllerStatus::OK : ControllerStatus::NO_ACTIVE_EVENT);
    }

    ControllerResult service(uint32_t nowMilliseconds, bool synchronousWork) {
        if (!usable()) return result(ControllerStatus::DEGRADED);
        if (state_ == RuntimeState::RELEASED) { clearAttempt(); active_ = false; loadHead(); }
        if (!active_) { loadHead(); if (!active_) return result(ControllerStatus::NO_ACTIVE_EVENT); }
        if (!serviceLifetime(nowMilliseconds)) return degradedResult_;
        if (state_ == RuntimeState::EXPIRED) return receiveResult();
        if (terminal()) return result(ControllerStatus::OK);
        switch (state_) {
            case RuntimeState::QUEUED:
                if (record()->attemptsUsed >= EventRecords::MAX_ATTEMPTS) {
                    if (!markFailed()) return degrade(ControllerStatus::STORAGE_FAILURE, true);
                } else if (!synchronousWork) state_ = RuntimeState::READY;
                break;
            case RuntimeState::READY:
                if (synchronousWork) state_ = RuntimeState::QUEUED;
                break;
            case RuntimeState::WAIT_ADMISSION:
                if (elapsed(nowMilliseconds, deadlineOrigin_) >= ADMISSION_TIMEOUT_MILLISECONDS)
                    return failedAttempt(nowMilliseconds);
                break;
            case RuntimeState::BACKOFF:
                if (elapsed(nowMilliseconds, deadlineOrigin_) >= deadlineDuration_)
                    state_ = synchronousWork ? RuntimeState::QUEUED : RuntimeState::READY;
                break;
            default: break;
        }
        return result(ControllerStatus::OK);
    }

    ControllerResult grantTransmit(uint32_t nowMilliseconds) {
        if (!usable()) return result(ControllerStatus::DEGRADED);
        if (!active_ || state_ != RuntimeState::READY)
            return result(ControllerStatus::INVALID_STATE);
        state_ = RuntimeState::TX_PREPARE;
        if (!serviceLifetime(nowMilliseconds)) return degradedResult_;
        if (state_ == RuntimeState::EXPIRED) return receiveResult();
        if (!headStillAuthoritative()) return degrade(ControllerStatus::POLICY_FAILURE, true);
        const AttemptResult armed = policy_.armAttempt(activeSlot_, nowMilliseconds);
        if (armed != AttemptResult::ARMED) {
            if (armed == AttemptResult::EXPIRED) { state_ = RuntimeState::EXPIRED; return receiveResult(); }
            if (armed == AttemptResult::EXHAUSTED && markFailed()) return receiveResult();
            return degrade(ControllerStatus::STORAGE_FAILURE, true);
        }
        uint8_t sequence = 0;
        if (!sequences_->next(sequence)) return degrade(ControllerStatus::SEQUENCE_FAILURE, true);
        const EventRecords::NodeRecord* current = record();
        attemptEvent_ = {};
        attemptEvent_.source = source_; attemptEvent_.destination = destination_;
        attemptEvent_.sequence = sequence; attemptEvent_.family = current->family;
        attemptEvent_.epoch = current->eventEpoch; attemptEvent_.id = current->eventId;
        attemptEvent_.flags = current->flags;
        attemptEvent_.lifetimeBudgetSeconds = current->lifetimeBudgetSeconds;
        attemptEvent_.bodyLength = current->bodyLength;
        for (uint8_t i = 0; i < current->bodyLength; ++i) attemptEvent_.body[i] = current->body[i];
        RadioAction action = {};
        action.type = RadioActionType::TRANSMIT;
        if (!EventProtocol::encodeEvent(attemptEvent_, action.bytes, sizeof(action.bytes), action.length))
            return degrade(ControllerStatus::ENCODE_FAILURE, true);
        correlationValid_ = true; state_ = RuntimeState::TX;
        return {ControllerStatus::OK, action};
    }

    ControllerResult txStarted(uint32_t nowMilliseconds) {
        if (!usable() || state_ != RuntimeState::TX) return result(ControllerStatus::INVALID_STATE);
        if (!serviceLifetime(nowMilliseconds)) return degradedResult_;
        return state_ == RuntimeState::EXPIRED ? receiveResult() : result(ControllerStatus::OK);
    }
    ControllerResult txStartFailed(uint32_t nowMilliseconds) {
        if (!usable() || state_ != RuntimeState::TX) return result(ControllerStatus::INVALID_STATE);
        if (!serviceLifetime(nowMilliseconds)) return degradedResult_;
        if (state_ != RuntimeState::EXPIRED) return failedAttempt(nowMilliseconds);
        return receiveResult();
    }
    ControllerResult txCompleted(uint32_t nowMilliseconds) {
        if (!usable() || state_ != RuntimeState::TX) return result(ControllerStatus::INVALID_STATE);
        if (!serviceLifetime(nowMilliseconds)) return degradedResult_;
        if (state_ != RuntimeState::EXPIRED) {
            state_ = RuntimeState::WAIT_ADMISSION;
            deadlineOrigin_ = nowMilliseconds; deadlineDuration_ = ADMISSION_TIMEOUT_MILLISECONDS;
        }
        return receiveResult();
    }

    ControllerResult admissionCandidate(const uint8_t* bytes, size_t length,
                                        uint32_t nowMilliseconds) {
        if (!usable() || state_ != RuntimeState::WAIT_ADMISSION)
            return result(ControllerStatus::INVALID_STATE);
        if (!serviceLifetime(nowMilliseconds)) return degradedResult_;
        if (state_ == RuntimeState::EXPIRED) return receiveResult();
        EventProtocol::AdmissionResponse response = {};
        if (!EventProtocol::decodeAdmissionResponse(bytes, length, response) ||
            !correlationValid_ || !EventProtocol::matchesEvent(response, attemptEvent_))
            return receiveResult();
        switch (response.status) {
            case EventProtocol::AdmissionStatus::ADMITTED:
                if (store_->releaseQueued(activeSlot_) != NodeEventStore::MutationStatus::OK)
                    return degrade(ControllerStatus::STORAGE_FAILURE, true);
                state_ = RuntimeState::RELEASED; clearAttempt(); return receiveResult();
            case EventProtocol::AdmissionStatus::CAPACITY:
                return failedAttempt(nowMilliseconds);
            case EventProtocol::AdmissionStatus::IDENTITY_CONTENT_MISMATCH:
            case EventProtocol::AdmissionStatus::UNSUPPORTED_EVENT:
            case EventProtocol::AdmissionStatus::MALFORMED_EVENT:
                if (!markFailed()) return degrade(ControllerStatus::STORAGE_FAILURE, true);
                return receiveResult();
        }
        return receiveResult();
    }

    ControllerResult reclaimTerminal() {
        if (!usable() || !active_ ||
            (state_ != RuntimeState::FAILED && state_ != RuntimeState::EXPIRED))
            return result(ControllerStatus::INVALID_STATE);
        if (store_->reclaim(activeSlot_) != NodeEventStore::MutationStatus::OK)
            return degrade(ControllerStatus::STORAGE_FAILURE, true);
        active_ = false; clearAttempt();
        return result(ControllerStatus::OK);
    }

    RuntimeState state() const { return state_; }
    bool hasActiveEvent() const { return active_; }
    uint8_t activeSlot() const { return activeSlot_; }
    bool degraded() const { return degraded_; }
    uint32_t deadlineOrigin() const { return deadlineOrigin_; }
    uint32_t deadlineDuration() const { return deadlineDuration_; }
    const EventProtocol::Event& attemptEvent() const { return attemptEvent_; }

private:
    static uint32_t elapsed(uint32_t now, uint32_t then) { return now - then; }
    const EventRecords::NodeRecord* record() const {
        return active_ && store_ != nullptr ? store_->recordAt(activeSlot_) : nullptr;
    }
    bool usable() const { return initialized_ && !degraded_ && store_ && store_->healthy() && policy_.ready(); }
    bool terminal() const { return state_ == RuntimeState::RELEASED || state_ == RuntimeState::FAILED || state_ == RuntimeState::EXPIRED; }
    bool headStillAuthoritative() const {
        uint8_t slot = 0xFF; const EventRecords::NodeRecord* head = store_->queuedAt(0, &slot);
        return head && slot == activeSlot_ && record() && record()->state == EventRecords::NodeState::QUEUED;
    }
    void loadHead() {
        uint8_t slot = 0xFF;
        if (store_ && store_->queuedAt(0, &slot)) { active_ = true; activeSlot_ = slot; state_ = RuntimeState::QUEUED; }
    }
    bool serviceLifetime(uint32_t now) {
        if (policy_.service(now) != Status::READY) { degrade(ControllerStatus::POLICY_FAILURE, true); return false; }
        const EventRecords::NodeRecord* current = record();
        if (current && current->state == EventRecords::NodeState::EXPIRED) {
            state_ = RuntimeState::EXPIRED; clearAttempt();
        }
        return true;
    }
    bool markFailed() {
        if (store_->markFailed(activeSlot_) != NodeEventStore::MutationStatus::OK) return false;
        state_ = RuntimeState::FAILED; clearAttempt(); return true;
    }
    ControllerResult failedAttempt(uint32_t now) {
        const EventRecords::NodeRecord* current = record();
        if (!current || current->attemptsUsed >= EventRecords::MAX_ATTEMPTS) {
            if (!markFailed()) return degrade(ControllerStatus::STORAGE_FAILURE, true);
        } else {
            const uint16_t jitter = jitter_->nextMilliseconds();
            if (jitter > MAX_BACKOFF_JITTER_MILLISECONDS)
                return degrade(ControllerStatus::INVALID_CONFIGURATION, true);
            static const uint32_t base[] = {1000U, 2000U, 4000U, 8000U};
            deadlineOrigin_ = now; deadlineDuration_ = base[current->attemptsUsed - 1U] + jitter;
            state_ = RuntimeState::BACKOFF;
        }
        return receiveResult();
    }
    void clearAttempt() { correlationValid_ = false; attemptEvent_ = {}; deadlineOrigin_ = deadlineDuration_ = 0; }
    ControllerResult receiveResult() const {
        ControllerResult output = result(ControllerStatus::OK); output.action.type = RadioActionType::RECEIVE; return output;
    }
    ControllerResult result(ControllerStatus status) const { ControllerResult output = {}; output.status = status; return output; }
    ControllerResult degrade(ControllerStatus status, bool receive) {
        degraded_ = true; degradedResult_ = result(status);
        if (receive) degradedResult_.action.type = RadioActionType::RECEIVE;
        return degradedResult_;
    }
    void reset() {
        store_ = nullptr; sequences_ = nullptr; jitter_ = nullptr; source_ = destination_ = 0;
        activeSlot_ = 0xFF; active_ = initialized_ = degraded_ = correlationValid_ = false;
        state_ = RuntimeState::QUEUED; clearAttempt(); degradedResult_ = {};
    }

    NodeEventStore::Store* store_ = nullptr;
    SequenceSource* sequences_ = nullptr;
    JitterSource* jitter_ = nullptr;
    Policy policy_;
    uint8_t source_ = 0, destination_ = 0, activeSlot_ = 0xFF;
    bool active_ = false, initialized_ = false, degraded_ = false, correlationValid_ = false;
    RuntimeState state_ = RuntimeState::QUEUED;
    uint32_t deadlineOrigin_ = 0, deadlineDuration_ = 0;
    EventProtocol::Event attemptEvent_ = {};
    ControllerResult degradedResult_ = {};
};

}  // namespace NodeEventDelivery
