#pragma once

#include "runtime_state.h"
#include "settings_integration.h"

namespace HubSettingsIntegration {

using Request = SettingsIntegration::Request;
using RequestQueue = SettingsIntegration::RequestQueue;
using ConfigurationState = SettingsIntegration::ConfigurationState;

using SettingsIntegration::feedbackAllowed;
using SettingsIntegration::fromLoad;
using SettingsIntegration::fromReset;
using SettingsIntegration::fromSave;
using SettingsIntegration::saveSucceeded;
using SettingsIntegration::screen;
using SettingsIntegration::timeoutMs;

inline bool persistenceSafe(
    RuntimeState::RuntimePhase phase,
    bool awaitingAcknowledgment,
    bool retryScheduled,
    bool radioEventPending,
    bool rendering
) {
    return phase == RuntimeState::RuntimePhase::IDLE &&
        !awaitingAcknowledgment &&
        !retryScheduled &&
        !radioEventPending &&
        !rendering;
}

}  // namespace HubSettingsIntegration
