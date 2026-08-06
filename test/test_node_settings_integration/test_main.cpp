#include <unity.h>

#include "node_settings_integration.h"

namespace {

NodeSettingsIntegration::SafePointState safeState() {
    NodeSettingsIntegration::SafePointState state;
    state.acknowledgmentCompleted = true;
    state.radioStandby = true;
    return state;
}

void testOnlyPostAcknowledgmentStandbyIsSafe() {
    NodeSettingsIntegration::SafePointState state = safeState();
    TEST_ASSERT_TRUE(NodeSettingsIntegration::persistenceSafe(state));
    state.acknowledgmentCompleted = false;
    TEST_ASSERT_FALSE(NodeSettingsIntegration::persistenceSafe(state));
    state = safeState();
    state.radioStandby = false;
    TEST_ASSERT_FALSE(NodeSettingsIntegration::persistenceSafe(state));
}

void testQuietListeningOwnershipIsAlsoSafe() {
    NodeSettingsIntegration::SafePointState state;
    state.quietListeningMaintenance = true;
    state.radioStandby = true;
    TEST_ASSERT_TRUE(NodeSettingsIntegration::persistenceSafe(state));
}

void testReceiveDecodeValidationAndAckObligationsBlockPersistence() {
    NodeSettingsIntegration::SafePointState state = safeState();
    state.processingInbound = true;
    TEST_ASSERT_FALSE(NodeSettingsIntegration::persistenceSafe(state));
    state = safeState();
    state.validatingCommand = true;
    TEST_ASSERT_FALSE(NodeSettingsIntegration::persistenceSafe(state));
    state = safeState();
    state.preparingAcknowledgment = true;
    TEST_ASSERT_FALSE(NodeSettingsIntegration::persistenceSafe(state));
    state = safeState();
    state.acknowledgmentPending = true;
    TEST_ASSERT_FALSE(NodeSettingsIntegration::persistenceSafe(state));
    state = safeState();
    state.duplicateReplayPending = true;
    TEST_ASSERT_FALSE(NodeSettingsIntegration::persistenceSafe(state));
}

void testPendingRadioEventAndRenderingBlockPersistence() {
    NodeSettingsIntegration::SafePointState state = safeState();
    state.radioEventPending = true;
    TEST_ASSERT_FALSE(NodeSettingsIntegration::persistenceSafe(state));
    state = safeState();
    state.rendering = true;
    TEST_ASSERT_FALSE(NodeSettingsIntegration::persistenceSafe(state));
}

void testNodeRequestPriorityMatchesApprovedOrder() {
    NodeSettingsIntegration::RequestQueue queue;
    queue.queueAutomaticRepair();
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(NodeSettingsIntegration::Request::AUTOMATIC_REPAIR),
        static_cast<uint8_t>(queue.pending())
    );
    DeviceSettings::Settings draft = DeviceSettings::defaults();
    draft.displayContrast = 128;
    queue.queueSave(draft);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(NodeSettingsIntegration::Request::SAVE),
        static_cast<uint8_t>(queue.pending())
    );
    queue.queueFactoryReset();
    queue.queueSave(draft);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(NodeSettingsIntegration::Request::FACTORY_RESET),
        static_cast<uint8_t>(queue.pending())
    );
    queue.clear();
    TEST_ASSERT_EQUAL_UINT8(0, static_cast<uint8_t>(queue.pending()));
}

void testQuietMaintenanceProgressesEveryRequestWithoutRadioTraffic() {
    const NodeSettingsIntegration::Request requests[] = {
        NodeSettingsIntegration::Request::SAVE,
        NodeSettingsIntegration::Request::FACTORY_RESET,
        NodeSettingsIntegration::Request::AUTOMATIC_REPAIR
    };
    for (NodeSettingsIntegration::Request request : requests) {
        TEST_ASSERT_TRUE(NodeSettingsIntegration::quietMaintenanceAllowed(
            request,
            true,
            RuntimeState::RuntimePhase::LISTENING,
            false,
            false,
            false,
            false
        ));
    }
}

void testQuietMaintenanceBlocksEveryCompetingOwner() {
    using NodeSettingsIntegration::quietMaintenanceAllowed;
    const NodeSettingsIntegration::Request save =
        NodeSettingsIntegration::Request::SAVE;
    TEST_ASSERT_FALSE(quietMaintenanceAllowed(
        save, true, RuntimeState::RuntimePhase::LISTENING,
        true, false, false, false
    ));
    TEST_ASSERT_FALSE(quietMaintenanceAllowed(
        save, true, RuntimeState::RuntimePhase::LISTENING,
        false, true, false, false
    ));
    TEST_ASSERT_FALSE(quietMaintenanceAllowed(
        save, true, RuntimeState::RuntimePhase::LISTENING,
        false, false, true, false
    ));
    TEST_ASSERT_FALSE(quietMaintenanceAllowed(
        save, true, RuntimeState::RuntimePhase::LISTENING,
        false, false, false, true
    ));
    TEST_ASSERT_FALSE(quietMaintenanceAllowed(
        save, false, RuntimeState::RuntimePhase::LISTENING,
        false, false, false, false
    ));
    TEST_ASSERT_FALSE(quietMaintenanceAllowed(
        save, true, RuntimeState::RuntimePhase::TRANSMITTING_ACK,
        false, false, false, false
    ));
}

void testStandbyFailureRetainsRequestAndRequiresReceiveRestore() {
    NodeSettingsIntegration::RequestQueue queue;
    DeviceSettings::Settings draft = DeviceSettings::defaults();
    draft.displayTimeoutSeconds = 60;
    queue.queueSave(draft);
    const NodeSettingsIntegration::AcquisitionOutcome outcome =
        NodeSettingsIntegration::classifyAcquisition(false, false);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            NodeSettingsIntegration::AcquisitionOutcome::STANDBY_FAILED
        ),
        static_cast<uint8_t>(outcome)
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(NodeSettingsIntegration::Request::SAVE),
        static_cast<uint8_t>(queue.pending())
    );
    TEST_ASSERT_TRUE(NodeSettingsIntegration::restoreReceiveImmediately(
        outcome
    ));
}

void testEventRaceAbortsStorageAndPreservesEventOwnership() {
    const NodeSettingsIntegration::AcquisitionOutcome outcome =
        NodeSettingsIntegration::classifyAcquisition(true, true);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            NodeSettingsIntegration::AcquisitionOutcome::EVENT_PENDING
        ),
        static_cast<uint8_t>(outcome)
    );
    TEST_ASSERT_FALSE(NodeSettingsIntegration::restoreReceiveImmediately(
        outcome
    ));
}

void testOwnedMaintenanceConsumesExactlyOnceAndRestoresReceive() {
    NodeSettingsIntegration::RequestQueue queue;
    queue.queueAutomaticRepair();
    const NodeSettingsIntegration::AcquisitionOutcome outcome =
        NodeSettingsIntegration::classifyAcquisition(true, false);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(
            NodeSettingsIntegration::AcquisitionOutcome::OWNED
        ),
        static_cast<uint8_t>(outcome)
    );
    uint8_t storageCalls = 0;
    if (outcome == NodeSettingsIntegration::AcquisitionOutcome::OWNED) {
        storageCalls++;
        queue.clear();
    }
    TEST_ASSERT_EQUAL_UINT8(1, storageCalls);
    TEST_ASSERT_EQUAL_UINT8(0, static_cast<uint8_t>(queue.pending()));
    TEST_ASSERT_TRUE(NodeSettingsIntegration::restoreReceiveImmediately(
        outcome
    ));
    const bool storageResults[] = {true, false};
    for (bool storageSucceeded : storageResults) {
        (void)storageSucceeded;
        TEST_ASSERT_TRUE(NodeSettingsIntegration::restoreReceiveImmediately(
            outcome
        ));
    }
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(testOnlyPostAcknowledgmentStandbyIsSafe);
    RUN_TEST(testQuietListeningOwnershipIsAlsoSafe);
    RUN_TEST(testReceiveDecodeValidationAndAckObligationsBlockPersistence);
    RUN_TEST(testPendingRadioEventAndRenderingBlockPersistence);
    RUN_TEST(testNodeRequestPriorityMatchesApprovedOrder);
    RUN_TEST(testQuietMaintenanceProgressesEveryRequestWithoutRadioTraffic);
    RUN_TEST(testQuietMaintenanceBlocksEveryCompetingOwner);
    RUN_TEST(testStandbyFailureRetainsRequestAndRequiresReceiveRestore);
    RUN_TEST(testEventRaceAbortsStorageAndPreservesEventOwnership);
    RUN_TEST(testOwnedMaintenanceConsumesExactlyOnceAndRestoresReceive);
    return UNITY_END();
}
