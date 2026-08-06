#pragma once

#include "settings_integration.h"
#include "runtime_state.h"

namespace NodeSettingsIntegration {

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

struct SafePointState {
    bool acknowledgmentCompleted = false;
    bool quietListeningMaintenance = false;
    bool processingInbound = false;
    bool validatingCommand = false;
    bool preparingAcknowledgment = false;
    bool acknowledgmentPending = false;
    bool duplicateReplayPending = false;
    bool radioEventPending = false;
    bool radioStandby = false;
    bool rendering = false;
};

inline bool persistenceSafe(const SafePointState& state) {
    return (state.acknowledgmentCompleted ||
            state.quietListeningMaintenance) &&
        !state.processingInbound &&
        !state.validatingCommand &&
        !state.preparingAcknowledgment &&
        !state.acknowledgmentPending &&
        !state.duplicateReplayPending &&
        !state.radioEventPending &&
        state.radioStandby &&
        !state.rendering;
}

inline bool quietMaintenanceAllowed(
    Request request,
    bool attemptArmed,
    RuntimeState::RuntimePhase phase,
    bool radioEventPending,
    bool acknowledgmentObligation,
    bool duplicateReplayObligation,
    bool rendering
) {
    return request != Request::NONE &&
        attemptArmed &&
        phase == RuntimeState::RuntimePhase::LISTENING &&
        !radioEventPending &&
        !acknowledgmentObligation &&
        !duplicateReplayObligation &&
        !rendering;
}

enum class AcquisitionOutcome : uint8_t {
    OWNED,
    EVENT_PENDING,
    STANDBY_FAILED
};

inline AcquisitionOutcome classifyAcquisition(
    bool standbySucceeded,
    bool radioEventPending
) {
    if (radioEventPending) {
        return AcquisitionOutcome::EVENT_PENDING;
    }
    return standbySucceeded
        ? AcquisitionOutcome::OWNED
        : AcquisitionOutcome::STANDBY_FAILED;
}

inline bool restoreReceiveImmediately(AcquisitionOutcome outcome) {
    return outcome != AcquisitionOutcome::EVENT_PENDING;
}

}  // namespace NodeSettingsIntegration
