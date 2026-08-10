#include <unity.h>

#include "capability_role_integration.h"
#include "heltec_v4_capabilities.h"

namespace {

using namespace CapabilityRoleIntegration;
using namespace DeviceCapabilities;

void testHubSafePointRequiresEveryOwnershipCondition() {
    HubSafePointState state;
    state.runtimeReady = true;
    state.registryValid = true;
    TEST_ASSERT_TRUE(hubSafe(state));

    state.phase = RuntimeState::RuntimePhase::TRANSMITTING;
    TEST_ASSERT_FALSE(hubSafe(state));
    state.phase = RuntimeState::RuntimePhase::WAITING_FOR_ACK;
    TEST_ASSERT_FALSE(hubSafe(state));
    state.phase = RuntimeState::RuntimePhase::IDLE;
    state.awaitingAcknowledgment = true;
    TEST_ASSERT_FALSE(hubSafe(state));
    state.awaitingAcknowledgment = false;
    state.retryCount = 1;
    TEST_ASSERT_FALSE(hubSafe(state));
    state.retryCount = 0;
    state.radioEventPending = true;
    TEST_ASSERT_FALSE(hubSafe(state));
    state.radioEventPending = false;
    state.rendering = true;
    TEST_ASSERT_FALSE(hubSafe(state));
    state.rendering = false;
    state.persistencePending = true;
    TEST_ASSERT_FALSE(hubSafe(state));
    state.persistencePending = false;
    state.transmissionDue = true;
    TEST_ASSERT_FALSE(hubSafe(state));
    state.transmissionDue = false;
    state.registryValid = false;
    TEST_ASSERT_FALSE(hubSafe(state));
    state.registryValid = true;
    state.runtimeReady = false;
    TEST_ASSERT_FALSE(hubSafe(state));
}

void testNodeSafePointRequiresExplicitStandbyOwnership() {
    NodeSafePointState state;
    state.runtimeReady = true;
    state.registryValid = true;
    state.radioStandby = true;
    state.ownership = NodeOwnership::POST_ACK;
    TEST_ASSERT_TRUE(nodeSafe(state));

    state.ownership = NodeOwnership::QUIET_MAINTENANCE;
    TEST_ASSERT_TRUE(nodeSafe(state));
    state.ownership = NodeOwnership::NONE;
    TEST_ASSERT_FALSE(nodeSafe(state));
    state.phase = RuntimeState::RuntimePhase::TRANSMITTING_ACK;
    TEST_ASSERT_FALSE(nodeSafe(state));
    state.phase = RuntimeState::RuntimePhase::LISTENING;
    state.radioStandby = false;
    TEST_ASSERT_FALSE(nodeSafe(state));
    state.radioStandby = true;
    state.radioEventPending = true;
    TEST_ASSERT_FALSE(nodeSafe(state));
    state.radioEventPending = false;
    state.rendering = true;
    TEST_ASSERT_FALSE(nodeSafe(state));
    state.rendering = false;
    state.registryValid = false;
    TEST_ASSERT_FALSE(nodeSafe(state));
    state.registryValid = true;
    state.runtimeReady = false;
    TEST_ASSERT_FALSE(nodeSafe(state));

    state.runtimeReady = true;
    state.ownership = NodeOwnership::QUIET_MAINTENANCE;
    state.phase = RuntimeState::RuntimePhase::TRANSMITTING_ACK;
    TEST_ASSERT_FALSE(nodeSafe(state));
}

void testLedArbitrationIsMasterGatedOrOfAllRequests() {
    TEST_ASSERT_FALSE(ledOutputRequested(false, true, true, true));
    TEST_ASSERT_FALSE(ledOutputRequested(true, false, false, false));
    TEST_ASSERT_TRUE(ledOutputRequested(true, true, false, false));
    TEST_ASSERT_TRUE(ledOutputRequested(true, false, true, false));
    TEST_ASSERT_TRUE(ledOutputRequested(true, false, false, true));
    TEST_ASSERT_TRUE(ledOutputRequested(true, true, false, true));
    TEST_ASSERT_TRUE(ledOutputRequested(true, false, true, true));
    TEST_ASSERT_TRUE(ledOutputRequested(true, true, true, true));
}

void testExecutionSeamDispatchesOnceAndCopiesDiagnostics() {
    HeltecV4Capabilities::ProfileState profileState;
    HeltecV4Capabilities::HeltecV4CapabilityHandler handler(profileState);
    CapabilityDiagnostics diagnostics;
    RuntimeState::State runtime(
        RuntimeState::DeviceRole::HUB,
        0x01,
        0x10
    );
    CapabilityValue input = {};
    input.type = ValueType::BOOLEAN;
    input.bits = 1;
    CallerContext caller = {};
    caller.callerClass = CallerClass::FIRMWARE_LOCAL;

    OperationResult result = executeLocalCapabilityNow(
        true,
        HeltecV4Capabilities::registryView(),
        handler,
        HeltecV4Capabilities::APPLICATION_INDICATOR_ID,
        Operation::SET,
        input,
        caller,
        InterlockState::CLEAR,
        diagnostics,
        runtime
    );
    TEST_ASSERT_EQUAL_HEX8(
        static_cast<uint8_t>(OperationStatus::OK),
        static_cast<uint8_t>(result.status)
    );
    TEST_ASSERT_TRUE(profileState.indicatorRequested);
    TEST_ASSERT_TRUE(runtime.hasCapabilityDiagnostics());
    TEST_ASSERT_EQUAL_UINT32(
        1,
        runtime.capabilityDiagnostics().counters.lookupAttempts
    );
    TEST_ASSERT_EQUAL_UINT32(
        1,
        runtime.capabilityDiagnostics().counters.acceptedOperations
    );

    result = executeLocalCapabilityNow(
        false,
        HeltecV4Capabilities::registryView(),
        handler,
        HeltecV4Capabilities::APPLICATION_INDICATOR_ID,
        Operation::SET,
        input,
        caller,
        InterlockState::CLEAR,
        diagnostics,
        runtime
    );
    TEST_ASSERT_EQUAL_HEX8(
        static_cast<uint8_t>(OperationStatus::INVALID_DESCRIPTOR),
        static_cast<uint8_t>(result.status)
    );
    TEST_ASSERT_EQUAL_UINT32(
        1,
        runtime.capabilityDiagnostics().counters.lookupAttempts
    );
    TEST_ASSERT_EQUAL_UINT32(
        1,
        runtime.capabilityDiagnostics().counters.validationFailures
    );
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(testHubSafePointRequiresEveryOwnershipCondition);
    RUN_TEST(testNodeSafePointRequiresExplicitStandbyOwnership);
    RUN_TEST(testLedArbitrationIsMasterGatedOrOfAllRequests);
    RUN_TEST(testExecutionSeamDispatchesOnceAndCopiesDiagnostics);
    return UNITY_END();
}
