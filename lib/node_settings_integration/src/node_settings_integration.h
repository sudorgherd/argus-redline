#pragma once

#include "hub_settings_integration.h"

namespace NodeSettingsIntegration {

using Request = HubSettingsIntegration::Request;
using RequestQueue = HubSettingsIntegration::RequestQueue;
using ConfigurationState = HubSettingsIntegration::ConfigurationState;

using HubSettingsIntegration::feedbackAllowed;
using HubSettingsIntegration::fromLoad;
using HubSettingsIntegration::fromReset;
using HubSettingsIntegration::fromSave;
using HubSettingsIntegration::saveSucceeded;
using HubSettingsIntegration::screen;
using HubSettingsIntegration::timeoutMs;

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
