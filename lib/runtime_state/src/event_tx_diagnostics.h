#pragma once

#include <stdint.h>

// Volatile observations only. No observer method returns scheduling decisions.
namespace EventTxDiagnostics {

enum class Counter : uint8_t {
    READY_SELECTED, ARBITRATION_GRANTED, START_CALLED, START_ACCEPTED,
    START_FAILED, TX_ENTERED, DIO_EVENT, DIO_OTHER, DIO_CLASSIFIED_EVENT,
    DIO_OTHER_PATH_DURING_TX, TX_COMPLETED_CALLED, WAIT_ENTERED,
    ACK_DECODED, ACK_MATCHED, ADMISSION_TIMEOUT, BACKOFF_ENTERED,
    BACKOFF_DUE, RETRY_READY, EXPIRED_QUEUED, EXPIRED_READY, EXPIRED_TX,
    EXPIRED_WAIT, EXPIRED_BACKOFF, EXPIRED_PREPARE, CLOCK_REVERSED,
    RECEIVE_DURING_TX, HOST_DURING_TX, MAINTENANCE_DURING_TX,
    PENDING_FLAG_CLEARED_DURING_TX, TERMINAL_RECLAIMED, COUNT
};
constexpr uint8_t COUNTER_COUNT = static_cast<uint8_t>(Counter::COUNT);

struct Snapshot {
    bool available = false;
    uint8_t lastStep = 0; // 0=none, otherwise Counter+1 (ISR counts don't set it)
    uint8_t controllerState = 0; // NodeEventDelivery::RuntimeState
    uint8_t radioOwner = 0; // EventRadioIntegration::NodeOwner
    uint8_t attempt = 0;
    uint8_t source = 0;
    uint32_t epoch = 0;
    uint32_t id = 0;
    uint32_t lastAt = 0;
    uint32_t startAt = 0;
    uint32_t completedAt = 0;
    uint32_t previousClock = 0;
    uint32_t inputClock = 0;
    int16_t startResult = 0;
    uint8_t admissionStatus = 0xFF; // none decoded
    uint16_t counters[COUNTER_COUNT] = {};
};

class Observer {
public:
    void note(Counter counter, uint32_t now) {
        const uint8_t index = static_cast<uint8_t>(counter);
        if (snapshot_.counters[index] != UINT16_MAX) ++snapshot_.counters[index];
        snapshot_.lastStep = index + 1;
        snapshot_.lastAt = now;
    }
    void attempt(uint8_t source, uint32_t epoch, uint32_t id, uint8_t used) {
        snapshot_.source = source; snapshot_.epoch = epoch; snapshot_.id = id;
        snapshot_.attempt = used;
    }
    void start(uint32_t now) { snapshot_.startAt = now; note(Counter::START_CALLED, now); }
    void startResult(int16_t code, uint32_t now) {
        snapshot_.startResult = code;
        note(code == 0 ? Counter::START_ACCEPTED : Counter::START_FAILED, now);
    }
    void completed(uint32_t now) {
        snapshot_.completedAt = now; note(Counter::TX_COMPLETED_CALLED, now);
    }
    void dispatchDio(bool eventOwner, bool controllerTx, uint32_t now) {
        if (eventOwner) note(Counter::DIO_CLASSIFIED_EVENT, now);
        else if (controllerTx) note(Counter::DIO_OTHER_PATH_DURING_TX, now);
    }
    void clearingFlag(bool pending, bool controllerTx, uint32_t now) {
        if (pending && controllerTx) note(Counter::PENDING_FLAG_CLEARED_DURING_TX, now);
    }
    void admission(uint8_t status, uint32_t now) {
        snapshot_.admissionStatus = status; note(Counter::ACK_DECODED, now);
    }
    void clock(uint32_t now) {
        if (clockSeen_ && static_cast<uint32_t>(now - lastClock_) > 0x7FFFFFFFU) {
            snapshot_.previousClock = lastClock_; snapshot_.inputClock = now;
            note(Counter::CLOCK_REVERSED, now);
        }
        lastClock_ = now; clockSeen_ = true;
    }
    void enable() { snapshot_.available = true; }
    const Snapshot& snapshot() const { return snapshot_; }
private:
    Snapshot snapshot_;
    uint32_t lastClock_ = 0;
    bool clockSeen_ = false;
};

} // namespace EventTxDiagnostics
