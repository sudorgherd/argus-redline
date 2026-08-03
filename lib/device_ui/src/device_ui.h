#pragma once

#include <stdint.h>

#include "device_input.h"

namespace DeviceUi {

enum class Screen : uint8_t {
    HOME,
    RADIO,
    DEVICE,
    LAST_PACKET,
    DIAGNOSTICS,
    ABOUT
};

inline bool isLiveScreen(Screen screen) {
    return (
        screen == Screen::RADIO ||
        screen == Screen::LAST_PACKET ||
        screen == Screen::DIAGNOSTICS
    );
}

enum class UiAction : uint8_t {
    NONE,
    RENDER,
    DISPLAY_ON,
    DISPLAY_ON_AND_RENDER,
    DISPLAY_OFF
};

class Controller {
public:
    static constexpr uint32_t DEFAULT_INACTIVITY_TIMEOUT_MS = 30000;
    static constexpr uint32_t DEFAULT_MINIMUM_RENDER_INTERVAL_MS = 100;
    static constexpr uint32_t DEFAULT_LIVE_REFRESH_INTERVAL_MS = 1000;

    Controller(
        uint32_t initialTimeMs,
        uint32_t inactivityTimeoutMs = DEFAULT_INACTIVITY_TIMEOUT_MS,
        uint32_t minimumRenderIntervalMs =
            DEFAULT_MINIMUM_RENDER_INTERVAL_MS,
        uint32_t liveRefreshIntervalMs = DEFAULT_LIVE_REFRESH_INTERVAL_MS
    ) :
        inactivityTimeoutMs_(inactivityTimeoutMs),
        minimumRenderIntervalMs_(minimumRenderIntervalMs),
        liveRefreshIntervalMs_(liveRefreshIntervalMs),
        lastInputAtMs_(initialTimeMs),
        lastLiveRefreshAtMs_(initialTimeMs) {}

    Screen screen() const {
        return screen_;
    }

    bool displayAwake() const {
        return displayAwake_;
    }

    bool dirty() const {
        return dirty_;
    }

    void markDirty() {
        invalidate();
    }

    void handle(DeviceInput::ButtonEvent event, uint32_t nowMs) {
        if (!displayAwake_) {
            if (event == DeviceInput::ButtonEvent::PRESS) {
                displayAwake_ = true;
                displayOnPending_ = true;
                suppressWakeGesture_ = true;
                wakeGestureWasLong_ = false;
                lastInputAtMs_ = nowMs;
                invalidate();
            }
            return;
        }

        if (suppressWakeGesture_) {
            handleSuppressedWakeGesture(event);
            return;
        }

        switch (event) {
            case DeviceInput::ButtonEvent::PRESS:
                lastInputAtMs_ = nowMs;
                break;

            case DeviceInput::ButtonEvent::SHORT_PRESS:
                screen_ = nextScreen(screen_);
                lastInputAtMs_ = nowMs;
                invalidate();
                break;

            case DeviceInput::ButtonEvent::LONG_PRESS:
                lastInputAtMs_ = nowMs;
                if (screen_ != Screen::HOME) {
                    screen_ = Screen::HOME;
                    invalidate();
                }
                break;

            case DeviceInput::ButtonEvent::NONE:
            case DeviceInput::ButtonEvent::RELEASE:
                break;
        }
    }

    UiAction update(uint32_t nowMs) {
        if (
            displayAwake_ &&
            elapsed(nowMs, lastInputAtMs_) >= inactivityTimeoutMs_
        ) {
            displayAwake_ = false;
            displayOnPending_ = false;
            renderPending_ = false;
            dirtyWhileRenderPending_ = false;
            return UiAction::DISPLAY_OFF;
        }

        if (!displayAwake_) {
            return UiAction::NONE;
        }

        if (
            !dirty_ &&
            isLiveScreen(screen_) &&
            elapsed(nowMs, lastLiveRefreshAtMs_) >= liveRefreshIntervalMs_
        ) {
            invalidate();
        }

        const bool renderEligible =
            !hasRendered_ ||
            elapsed(nowMs, lastRenderAtMs_) >= minimumRenderIntervalMs_;

        if (displayOnPending_) {
            displayOnPending_ = false;
            if (dirty_ && !renderPending_ && renderEligible) {
                beginRenderRequest();
                return UiAction::DISPLAY_ON_AND_RENDER;
            }
            return UiAction::DISPLAY_ON;
        }

        if (dirty_ && !renderPending_ && renderEligible) {
            beginRenderRequest();
            return UiAction::RENDER;
        }

        return UiAction::NONE;
    }

    // Acknowledges completion of the most recently requested render.
    void recordRendered(uint32_t nowMs) {
        if (!renderPending_) {
            return;
        }

        renderPending_ = false;
        dirty_ = dirtyWhileRenderPending_;
        dirtyWhileRenderPending_ = false;
        hasRendered_ = true;
        lastRenderAtMs_ = nowMs;
        if (isLiveScreen(screen_)) {
            lastLiveRefreshAtMs_ = nowMs;
        }
    }

private:
    static uint32_t elapsed(uint32_t nowMs, uint32_t sinceMs) {
        return nowMs - sinceMs;
    }

    static Screen nextScreen(Screen screen) {
        switch (screen) {
            case Screen::HOME:
                return Screen::RADIO;
            case Screen::RADIO:
                return Screen::DEVICE;
            case Screen::DEVICE:
                return Screen::LAST_PACKET;
            case Screen::LAST_PACKET:
                return Screen::DIAGNOSTICS;
            case Screen::DIAGNOSTICS:
                return Screen::ABOUT;
            case Screen::ABOUT:
                return Screen::HOME;
        }

        return Screen::HOME;
    }

    void invalidate() {
        dirty_ = true;
        if (renderPending_) {
            dirtyWhileRenderPending_ = true;
        }
    }

    void beginRenderRequest() {
        renderPending_ = true;
        dirtyWhileRenderPending_ = false;
    }

    void handleSuppressedWakeGesture(DeviceInput::ButtonEvent event) {
        if (event == DeviceInput::ButtonEvent::LONG_PRESS) {
            wakeGestureWasLong_ = true;
        } else if (event == DeviceInput::ButtonEvent::SHORT_PRESS) {
            suppressWakeGesture_ = false;
        } else if (
            event == DeviceInput::ButtonEvent::RELEASE &&
            wakeGestureWasLong_
        ) {
            suppressWakeGesture_ = false;
        }
    }

    const uint32_t inactivityTimeoutMs_;
    const uint32_t minimumRenderIntervalMs_;
    const uint32_t liveRefreshIntervalMs_;
    Screen screen_ = Screen::HOME;
    bool displayAwake_ = true;
    bool dirty_ = true;
    bool renderPending_ = false;
    bool dirtyWhileRenderPending_ = false;
    bool displayOnPending_ = false;
    bool suppressWakeGesture_ = false;
    bool wakeGestureWasLong_ = false;
    bool hasRendered_ = false;
    uint32_t lastInputAtMs_;
    uint32_t lastRenderAtMs_ = 0;
    uint32_t lastLiveRefreshAtMs_;
};

}  // namespace DeviceUi
