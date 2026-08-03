#pragma once

#include <stdint.h>

namespace DeviceInput {

enum class ButtonEvent : uint8_t {
    NONE,
    PRESS,
    RELEASE,
    SHORT_PRESS,
    LONG_PRESS
};

// At most two semantic events can result from one sample. When two occur,
// first precedes second; a short release is RELEASE then SHORT_PRESS.
struct ButtonEvents {
    ButtonEvent first = ButtonEvent::NONE;
    ButtonEvent second = ButtonEvent::NONE;
};

class Button {
public:
    Button(uint32_t debounceMs, uint32_t longPressMs) :
        debounceMs_(debounceMs),
        longPressMs_(longPressMs) {}

    ButtonEvents update(bool pressed, uint32_t nowMs) {
        ButtonEvents events;

        if (!initialized_) {
            initialized_ = true;
            stablePressed_ = pressed;
            candidatePressed_ = pressed;
            candidateSinceMs_ = nowMs;
            armed_ = !pressed;
            return events;
        }

        if (!armed_) {
            updateStartupHeldState(pressed, nowMs);
            return events;
        }

        if (pressed == stablePressed_) {
            candidatePressed_ = stablePressed_;
            candidateSinceMs_ = nowMs;
        } else {
            if (candidatePressed_ != pressed) {
                candidatePressed_ = pressed;
                candidateSinceMs_ = nowMs;
            }

            if (elapsed(nowMs, candidateSinceMs_) >= debounceMs_) {
                stablePressed_ = pressed;
                if (stablePressed_) {
                    pressSinceMs_ = nowMs;
                    longPressEmitted_ = false;
                    events.first = ButtonEvent::PRESS;
                } else {
                    events.first = ButtonEvent::RELEASE;
                    if (!longPressEmitted_) {
                        events.second = ButtonEvent::SHORT_PRESS;
                    }
                }
            }
        }

        if (
            stablePressed_ &&
            !longPressEmitted_ &&
            elapsed(nowMs, pressSinceMs_) >= longPressMs_
        ) {
            longPressEmitted_ = true;
            append(events, ButtonEvent::LONG_PRESS);
        }

        return events;
    }

private:
    static uint32_t elapsed(uint32_t nowMs, uint32_t sinceMs) {
        return nowMs - sinceMs;
    }

    static void append(ButtonEvents& events, ButtonEvent event) {
        if (events.first == ButtonEvent::NONE) {
            events.first = event;
        } else {
            events.second = event;
        }
    }

    void updateStartupHeldState(bool pressed, uint32_t nowMs) {
        if (pressed) {
            candidatePressed_ = true;
            candidateSinceMs_ = nowMs;
            return;
        }

        if (candidatePressed_) {
            candidatePressed_ = false;
            candidateSinceMs_ = nowMs;
        }

        if (elapsed(nowMs, candidateSinceMs_) >= debounceMs_) {
            stablePressed_ = false;
            armed_ = true;
        }
    }

    const uint32_t debounceMs_;
    const uint32_t longPressMs_;
    bool initialized_ = false;
    bool armed_ = false;
    bool stablePressed_ = false;
    bool candidatePressed_ = false;
    bool longPressEmitted_ = false;
    uint32_t candidateSinceMs_ = 0;
    uint32_t pressSinceMs_ = 0;
};

}  // namespace DeviceInput
