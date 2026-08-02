#include <unity.h>

#include "runtime_state.h"
#include "transaction_engine.h"

namespace {

constexpr uint8_t HUB_ID = 0x01;
constexpr uint8_t NODE_ID = 0x10;

void testHubDefaultsAreDeterministic() {
    const RuntimeState::State state(
        RuntimeState::DeviceRole::HUB,
        HUB_ID,
        NODE_ID
    );

    TEST_ASSERT_FALSE(state.isReady());
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(RuntimeState::RuntimePhase::IDLE),
        static_cast<uint8_t>(state.phase())
    );
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 0.0F, state.latestRssi());
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 0.0F, state.latestSnr());
}

void testRolesInitializeCorrectly() {
    const RuntimeState::State hub(
        RuntimeState::DeviceRole::HUB,
        HUB_ID,
        NODE_ID
    );
    const RuntimeState::State node(
        RuntimeState::DeviceRole::NODE,
        NODE_ID,
        HUB_ID
    );

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(RuntimeState::DeviceRole::HUB),
        static_cast<uint8_t>(hub.role())
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(RuntimeState::DeviceRole::NODE),
        static_cast<uint8_t>(node.role())
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(RuntimeState::RuntimePhase::LISTENING),
        static_cast<uint8_t>(node.phase())
    );
}

void testIdentityIsExposed() {
    const RuntimeState::State state(
        RuntimeState::DeviceRole::HUB,
        HUB_ID,
        NODE_ID
    );

    TEST_ASSERT_EQUAL_UINT8(HUB_ID, state.localId());
    TEST_ASSERT_EQUAL_UINT8(NODE_ID, state.peerId());
}

void testReadinessTransitions() {
    RuntimeState::State state(
        RuntimeState::DeviceRole::HUB,
        HUB_ID,
        NODE_ID
    );

    state.setReady(true);
    TEST_ASSERT_TRUE(state.isReady());
    state.setReady(false);
    TEST_ASSERT_FALSE(state.isReady());
}

void testRadioMetricsUpdateTogether() {
    RuntimeState::State state(
        RuntimeState::DeviceRole::NODE,
        NODE_ID,
        HUB_ID
    );

    state.updateRadioMetrics(-91.5F, 7.25F);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, -91.5F, state.latestRssi());
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 7.25F, state.latestSnr());
}

void testTransactionFacingPhaseUpdates() {
    RuntimeState::State state(
        RuntimeState::DeviceRole::HUB,
        HUB_ID,
        NODE_ID
    );

    state.setPhase(RuntimeState::RuntimePhase::TRANSMITTING);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(RuntimeState::RuntimePhase::TRANSMITTING),
        static_cast<uint8_t>(state.phase())
    );
    state.setPhase(RuntimeState::RuntimePhase::WAITING_FOR_ACK);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(RuntimeState::RuntimePhase::WAITING_FOR_ACK),
        static_cast<uint8_t>(state.phase())
    );
}

void testHubAndNodeStateRemainIndependent() {
    RuntimeState::State hub(
        RuntimeState::DeviceRole::HUB,
        HUB_ID,
        NODE_ID
    );
    RuntimeState::State node(
        RuntimeState::DeviceRole::NODE,
        NODE_ID,
        HUB_ID
    );

    hub.setReady(true);
    hub.updateRadioMetrics(-80.0F, 9.0F);

    TEST_ASSERT_FALSE(node.isReady());
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 0.0F, node.latestRssi());
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 0.0F, node.latestSnr());
}

void testRuntimeUpdatesDoNotModifyTransactionEngineState() {
    RuntimeState::State runtime(
        RuntimeState::DeviceRole::HUB,
        HUB_ID,
        NODE_ID
    );
    TransactionEngine::HubTransactionState transaction;

    runtime.setReady(true);
    runtime.setPhase(RuntimeState::RuntimePhase::WAITING_FOR_ACK);
    runtime.updateRadioMetrics(-85.0F, 6.0F);

    TEST_ASSERT_EQUAL_UINT8(1, transaction.currentSequence());
    TEST_ASSERT_EQUAL_UINT8(0, transaction.retryCount());
    TEST_ASSERT_FALSE(transaction.isAwaitingAcknowledgment());
}

void testNewStateRestoresVolatileDefaults() {
    RuntimeState::State first(
        RuntimeState::DeviceRole::NODE,
        NODE_ID,
        HUB_ID
    );
    first.setReady(true);
    first.setPhase(RuntimeState::RuntimePhase::TRANSMITTING_ACK);
    first.updateRadioMetrics(-72.0F, 11.0F);

    const RuntimeState::State restarted(
        RuntimeState::DeviceRole::NODE,
        NODE_ID,
        HUB_ID
    );
    TEST_ASSERT_FALSE(restarted.isReady());
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(RuntimeState::RuntimePhase::LISTENING),
        static_cast<uint8_t>(restarted.phase())
    );
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 0.0F, restarted.latestRssi());
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 0.0F, restarted.latestSnr());
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(testHubDefaultsAreDeterministic);
    RUN_TEST(testRolesInitializeCorrectly);
    RUN_TEST(testIdentityIsExposed);
    RUN_TEST(testReadinessTransitions);
    RUN_TEST(testRadioMetricsUpdateTogether);
    RUN_TEST(testTransactionFacingPhaseUpdates);
    RUN_TEST(testHubAndNodeStateRemainIndependent);
    RUN_TEST(testRuntimeUpdatesDoNotModifyTransactionEngineState);
    RUN_TEST(testNewStateRestoresVolatileDefaults);
    return UNITY_END();
}
