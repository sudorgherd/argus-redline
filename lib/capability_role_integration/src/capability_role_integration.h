#pragma once

#include <stdint.h>

#include "device_capabilities.h"
#include "runtime_state.h"

namespace CapabilityRoleIntegration {

struct HubSafePointState {
    bool runtimeReady = false;
    bool registryValid = false;
    RuntimeState::RuntimePhase phase = RuntimeState::RuntimePhase::IDLE;
    bool awaitingAcknowledgment = false;
    uint8_t retryCount = 0;
    bool radioEventPending = false;
    bool rendering = false;
    bool persistencePending = false;
    bool transmissionDue = false;
};

inline bool hubSafe(const HubSafePointState& state) {
    return state.runtimeReady &&
        state.registryValid &&
        state.phase == RuntimeState::RuntimePhase::IDLE &&
        !state.awaitingAcknowledgment &&
        state.retryCount == 0 &&
        !state.radioEventPending &&
        !state.rendering &&
        !state.persistencePending &&
        !state.transmissionDue;
}

enum class NodeOwnership : uint8_t {
    NONE = 0,
    POST_ACK = 1,
    QUIET_MAINTENANCE = 2
};

struct NodeSafePointState {
    bool runtimeReady = false;
    bool registryValid = false;
    RuntimeState::RuntimePhase phase = RuntimeState::RuntimePhase::LISTENING;
    NodeOwnership ownership = NodeOwnership::NONE;
    bool radioStandby = false;
    bool radioEventPending = false;
    bool rendering = false;
};

inline bool nodeSafe(const NodeSafePointState& state) {
    if (!state.runtimeReady ||
        !state.registryValid ||
        !state.radioStandby ||
        state.radioEventPending ||
        state.rendering) {
        return false;
    }

    if (state.ownership == NodeOwnership::POST_ACK) {
        return true;
    }

    return state.ownership == NodeOwnership::QUIET_MAINTENANCE &&
        state.phase == RuntimeState::RuntimePhase::LISTENING;
}

inline bool ledOutputRequested(
    bool masterEnabled,
    bool radioActive,
    bool feedbackActive,
    bool capabilityRequested
) {
    return masterEnabled &&
        (radioActive || feedbackActive || capabilityRequested);
}

inline DeviceCapabilities::OperationResult executeLocalCapabilityNow(
    bool registryValid,
    const DeviceCapabilities::CapabilityRegistryView& registry,
    DeviceCapabilities::LocalCapabilityHandler& handler,
    DeviceCapabilities::CapabilityId capabilityId,
    DeviceCapabilities::Operation operation,
    const DeviceCapabilities::CapabilityValue& input,
    const DeviceCapabilities::CallerContext& caller,
    DeviceCapabilities::InterlockState interlock,
    DeviceCapabilities::CapabilityDiagnostics& diagnostics,
    RuntimeState::State& runtimeState
) {
    const DeviceCapabilities::CapabilityRegistryView invalidRegistry = {
        nullptr,
        1
    };
    const DeviceCapabilities::OperationResult result =
        DeviceCapabilities::dispatchCapabilityOperationObserved(
            registryValid ? registry : invalidRegistry,
            handler,
            capabilityId,
            operation,
            input,
            caller,
            interlock,
            diagnostics
        );
    runtimeState.updateCapabilityDiagnostics(diagnostics.snapshot());
    return result;
}

}  // namespace CapabilityRoleIntegration
