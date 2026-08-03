#include <unity.h>

#include "device_ui.h"

namespace {

void assertAction(DeviceUi::UiAction expected, DeviceUi::UiAction actual) {
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(expected),
        static_cast<uint8_t>(actual)
    );
}

void assertScreen(DeviceUi::Screen expected, DeviceUi::Screen actual) {
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(expected),
        static_cast<uint8_t>(actual)
    );
}

void renderAndRecord(DeviceUi::Controller& controller, uint32_t nowMs) {
    assertAction(DeviceUi::UiAction::RENDER, controller.update(nowMs));
    controller.recordRendered(nowMs);
}

void shortPress(DeviceUi::Controller& controller, uint32_t nowMs) {
    controller.handle(DeviceInput::ButtonEvent::PRESS, nowMs);
    controller.handle(DeviceInput::ButtonEvent::RELEASE, nowMs);
    controller.handle(DeviceInput::ButtonEvent::SHORT_PRESS, nowMs);
}

void navigateTo(
    DeviceUi::Controller& controller,
    DeviceUi::Screen target,
    uint32_t& nowMs
) {
    while (controller.screen() != target) {
        nowMs += 100;
        shortPress(controller, nowMs);
        renderAndRecord(controller, nowMs);
    }
}

void sleepController(DeviceUi::Controller& controller, uint32_t timeoutAtMs) {
    renderAndRecord(controller, 0);
    assertAction(
        DeviceUi::UiAction::DISPLAY_OFF,
        controller.update(timeoutAtMs)
    );
}

void testDefaultScreenIsHome() {
    DeviceUi::Controller controller(0);
    assertScreen(DeviceUi::Screen::HOME, controller.screen());
}

void testScreenOrderMatchesApprovedSequence() {
    DeviceUi::Controller controller(0);
    const DeviceUi::Screen expected[] = {
        DeviceUi::Screen::RADIO,
        DeviceUi::Screen::DEVICE,
        DeviceUi::Screen::LAST_PACKET,
        DeviceUi::Screen::DIAGNOSTICS,
        DeviceUi::Screen::ABOUT,
        DeviceUi::Screen::HOME
    };

    for (uint8_t index = 0; index < 6; index++) {
        controller.handle(DeviceInput::ButtonEvent::SHORT_PRESS, index + 1);
        assertScreen(expected[index], controller.screen());
    }
}

void testSixShortPressesWrapExactlyHome() {
    DeviceUi::Controller controller(0);
    for (uint8_t index = 0; index < 6; index++) {
        controller.handle(DeviceInput::ButtonEvent::SHORT_PRESS, index + 1);
    }
    assertScreen(DeviceUi::Screen::HOME, controller.screen());
}

void testLongPressReturnsHomeFromEveryNonHomeScreen() {
    for (uint8_t distance = 1; distance < 6; distance++) {
        DeviceUi::Controller controller(0);
        for (uint8_t index = 0; index < distance; index++) {
            controller.handle(
                DeviceInput::ButtonEvent::SHORT_PRESS,
                index + 1
            );
        }
        controller.handle(DeviceInput::ButtonEvent::LONG_PRESS, 10);
        assertScreen(DeviceUi::Screen::HOME, controller.screen());
    }
}

void testLongPressOnHomeLeavesHomeSelected() {
    DeviceUi::Controller controller(0);
    controller.handle(DeviceInput::ButtonEvent::LONG_PRESS, 10);
    assertScreen(DeviceUi::Screen::HOME, controller.screen());
}

void testNonNavigationEventsDoNotNavigateWhileAwake() {
    DeviceUi::Controller controller(0);
    controller.handle(DeviceInput::ButtonEvent::PRESS, 1);
    controller.handle(DeviceInput::ButtonEvent::RELEASE, 2);
    controller.handle(DeviceInput::ButtonEvent::NONE, 3);
    assertScreen(DeviceUi::Screen::HOME, controller.screen());
}

void testNavigationPolicyHasNoRoleInput() {
    DeviceUi::Controller first(0);
    DeviceUi::Controller second(0);
    first.handle(DeviceInput::ButtonEvent::SHORT_PRESS, 1);
    second.handle(DeviceInput::ButtonEvent::SHORT_PRESS, 1);
    assertScreen(first.screen(), second.screen());
}

void testLiveScreenClassificationIsExact() {
    TEST_ASSERT_FALSE(DeviceUi::isLiveScreen(DeviceUi::Screen::HOME));
    TEST_ASSERT_TRUE(DeviceUi::isLiveScreen(DeviceUi::Screen::RADIO));
    TEST_ASSERT_FALSE(DeviceUi::isLiveScreen(DeviceUi::Screen::DEVICE));
    TEST_ASSERT_TRUE(DeviceUi::isLiveScreen(DeviceUi::Screen::LAST_PACKET));
    TEST_ASSERT_TRUE(DeviceUi::isLiveScreen(DeviceUi::Screen::DIAGNOSTICS));
    TEST_ASSERT_FALSE(DeviceUi::isLiveScreen(DeviceUi::Screen::ABOUT));
}

void testInitialStateIsAwakeDirtyAndImmediatelyRenderable() {
    DeviceUi::Controller controller(123);
    TEST_ASSERT_TRUE(controller.displayAwake());
    TEST_ASSERT_TRUE(controller.dirty());
    assertAction(DeviceUi::UiAction::RENDER, controller.update(123));
}

void testDirtyCallsCoalesceAndRenderAcknowledgmentClearsDirty() {
    DeviceUi::Controller controller(0);
    controller.markDirty();
    controller.markDirty();
    TEST_ASSERT_TRUE(controller.dirty());
    renderAndRecord(controller, 0);
    TEST_ASSERT_FALSE(controller.dirty());
}

void testInvalidationDuringPendingRenderIsNotLost() {
    DeviceUi::Controller controller(0);
    assertAction(DeviceUi::UiAction::RENDER, controller.update(0));
    controller.markDirty();
    assertAction(DeviceUi::UiAction::NONE, controller.update(1));
    controller.recordRendered(1);
    TEST_ASSERT_TRUE(controller.dirty());
}

void testScreenChangesAndOnlyEffectiveHomeReturnMarkDirty() {
    DeviceUi::Controller controller(0);
    renderAndRecord(controller, 0);
    controller.handle(DeviceInput::ButtonEvent::LONG_PRESS, 10);
    TEST_ASSERT_FALSE(controller.dirty());
    controller.handle(DeviceInput::ButtonEvent::SHORT_PRESS, 20);
    TEST_ASSERT_TRUE(controller.dirty());
    renderAndRecord(controller, 100);
    controller.handle(DeviceInput::ButtonEvent::LONG_PRESS, 110);
    TEST_ASSERT_TRUE(controller.dirty());
}

void testExternalInvalidationWhileAsleepDoesNotWake() {
    DeviceUi::Controller controller(0);
    sleepController(controller, 30000);
    controller.markDirty();
    TEST_ASSERT_FALSE(controller.displayAwake());
    TEST_ASSERT_TRUE(controller.dirty());
    assertAction(DeviceUi::UiAction::NONE, controller.update(30001));
}

void testRenderCapBlocksBeforeAndAllowsAtBoundary() {
    DeviceUi::Controller controller(0);
    renderAndRecord(controller, 0);
    controller.markDirty();
    assertAction(DeviceUi::UiAction::NONE, controller.update(99));
    TEST_ASSERT_TRUE(controller.dirty());
    assertAction(DeviceUi::UiAction::RENDER, controller.update(100));
}

void testRepeatedUpdatesDoNotClearDirtyOrDuplicatePendingRender() {
    DeviceUi::Controller controller(0);
    assertAction(DeviceUi::UiAction::RENDER, controller.update(0));
    assertAction(DeviceUi::UiAction::NONE, controller.update(1));
    assertAction(DeviceUi::UiAction::NONE, controller.update(100));
    TEST_ASSERT_TRUE(controller.dirty());
}

void testRenderCapWorksAcrossRollover() {
    DeviceUi::Controller controller(0xFFFFFFF0U);
    renderAndRecord(controller, 0xFFFFFFF0U);
    controller.markDirty();
    assertAction(DeviceUi::UiAction::NONE, controller.update(0x00000053U));
    assertAction(DeviceUi::UiAction::RENDER, controller.update(0x00000054U));
}

void testZeroRenderIntervalAllowsImmediateRenderAfterAcknowledgment() {
    DeviceUi::Controller controller(0, 30000, 0, 1000);
    renderAndRecord(controller, 0);
    controller.markDirty();
    assertAction(DeviceUi::UiAction::RENDER, controller.update(0));
}

void assertStaticScreenDoesNotRefresh(DeviceUi::Screen target) {
    DeviceUi::Controller controller(0);
    renderAndRecord(controller, 0);
    uint32_t nowMs = 0;
    navigateTo(controller, target, nowMs);
    assertAction(DeviceUi::UiAction::NONE, controller.update(nowMs + 1000));
}

void testStaticScreensDoNotPeriodicallyRefresh() {
    assertStaticScreenDoesNotRefresh(DeviceUi::Screen::HOME);
    assertStaticScreenDoesNotRefresh(DeviceUi::Screen::DEVICE);
    assertStaticScreenDoesNotRefresh(DeviceUi::Screen::ABOUT);
}

void assertLiveScreenRefreshesAtBoundary(DeviceUi::Screen target) {
    DeviceUi::Controller controller(0);
    renderAndRecord(controller, 0);
    uint32_t nowMs = 0;
    navigateTo(controller, target, nowMs);
    assertAction(DeviceUi::UiAction::NONE, controller.update(nowMs + 999));
    assertAction(DeviceUi::UiAction::RENDER, controller.update(nowMs + 1000));
}

void testAllLiveScreensRefreshAtExactBoundary() {
    assertLiveScreenRefreshesAtBoundary(DeviceUi::Screen::RADIO);
    assertLiveScreenRefreshesAtBoundary(DeviceUi::Screen::LAST_PACKET);
    assertLiveScreenRefreshesAtBoundary(DeviceUi::Screen::DIAGNOSTICS);
}

void testLiveRefreshObeysRenderCapAndAcknowledgment() {
    DeviceUi::Controller controller(0, 30000, 100, 50);
    renderAndRecord(controller, 0);
    controller.handle(DeviceInput::ButtonEvent::SHORT_PRESS, 100);
    renderAndRecord(controller, 100);
    assertAction(DeviceUi::UiAction::NONE, controller.update(150));
    TEST_ASSERT_TRUE(controller.dirty());
    assertAction(DeviceUi::UiAction::RENDER, controller.update(200));
    assertAction(DeviceUi::UiAction::NONE, controller.update(201));
}

void testLiveRefreshDoesNotWakeSleepingDisplay() {
    DeviceUi::Controller controller(0);
    renderAndRecord(controller, 0);
    controller.handle(DeviceInput::ButtonEvent::SHORT_PRESS, 100);
    renderAndRecord(controller, 100);
    assertAction(DeviceUi::UiAction::DISPLAY_OFF, controller.update(30100));
    assertAction(DeviceUi::UiAction::NONE, controller.update(31100));
    TEST_ASSERT_FALSE(controller.displayAwake());
}

void testSwitchingToLiveScreenRequestsImmediateDirtyRender() {
    DeviceUi::Controller controller(0);
    renderAndRecord(controller, 0);
    controller.handle(DeviceInput::ButtonEvent::SHORT_PRESS, 100);
    assertScreen(DeviceUi::Screen::RADIO, controller.screen());
    TEST_ASSERT_TRUE(controller.dirty());
    assertAction(DeviceUi::UiAction::RENDER, controller.update(100));
}

void testLiveRefreshWorksAcrossRollover() {
    DeviceUi::Controller controller(0xFFFFFE00U);
    renderAndRecord(controller, 0xFFFFFE00U);
    controller.handle(
        DeviceInput::ButtonEvent::SHORT_PRESS,
        0xFFFFFF00U
    );
    renderAndRecord(controller, 0xFFFFFF00U);
    assertAction(DeviceUi::UiAction::NONE, controller.update(0x000002E7U));
    assertAction(DeviceUi::UiAction::RENDER, controller.update(0x000002E8U));
}

void testZeroLiveIntervalMakesLiveScreenEligibleEveryUpdate() {
    DeviceUi::Controller controller(0, 30000, 0, 0);
    renderAndRecord(controller, 0);
    controller.handle(DeviceInput::ButtonEvent::SHORT_PRESS, 1);
    renderAndRecord(controller, 1);
    assertAction(DeviceUi::UiAction::RENDER, controller.update(1));
}

void testTimeoutBoundaryAndOneShotOffAction() {
    DeviceUi::Controller controller(0);
    renderAndRecord(controller, 0);
    assertAction(DeviceUi::UiAction::NONE, controller.update(29999));
    TEST_ASSERT_TRUE(controller.displayAwake());
    assertAction(DeviceUi::UiAction::DISPLAY_OFF, controller.update(30000));
    TEST_ASSERT_FALSE(controller.displayAwake());
    assertAction(DeviceUi::UiAction::NONE, controller.update(30001));
}

void testRenderDirtyAndLiveWorkDoNotResetTimeout() {
    DeviceUi::Controller controller(0, 30000, 0, 1000);
    renderAndRecord(controller, 0);
    controller.markDirty();
    renderAndRecord(controller, 100);
    controller.handle(DeviceInput::ButtonEvent::SHORT_PRESS, 1000);
    renderAndRecord(controller, 1000);
    assertAction(DeviceUi::UiAction::RENDER, controller.update(2000));
    controller.recordRendered(2000);
    controller.markDirty();
    renderAndRecord(controller, 29999);
    assertAction(DeviceUi::UiAction::DISPLAY_OFF, controller.update(31000));
}

void testAwakePressAndNavigationInputsResetTimeout() {
    DeviceUi::Controller controller(0);
    renderAndRecord(controller, 0);
    controller.handle(DeviceInput::ButtonEvent::PRESS, 10000);
    assertAction(DeviceUi::UiAction::NONE, controller.update(39999));
    controller.handle(DeviceInput::ButtonEvent::SHORT_PRESS, 40000);
    assertAction(DeviceUi::UiAction::RENDER, controller.update(40000));
    controller.recordRendered(40000);
    assertAction(DeviceUi::UiAction::RENDER, controller.update(69999));
    controller.recordRendered(69999);
    controller.handle(DeviceInput::ButtonEvent::LONG_PRESS, 70000);
    assertAction(DeviceUi::UiAction::NONE, controller.update(70000));
    TEST_ASSERT_TRUE(controller.dirty());
    assertAction(DeviceUi::UiAction::RENDER, controller.update(70099));
    controller.recordRendered(70099);
    assertAction(DeviceUi::UiAction::NONE, controller.update(99999));
    TEST_ASSERT_TRUE(controller.displayAwake());
}

void testReleaseDoesNotResetTimeout() {
    DeviceUi::Controller controller(0);
    renderAndRecord(controller, 0);
    controller.handle(DeviceInput::ButtonEvent::RELEASE, 29999);
    assertAction(DeviceUi::UiAction::DISPLAY_OFF, controller.update(30000));
}

void testTimeoutWorksAcrossRollover() {
    DeviceUi::Controller controller(0xFFFFFFF0U);
    renderAndRecord(controller, 0xFFFFFFF0U);
    assertAction(DeviceUi::UiAction::NONE, controller.update(0x0000751FU));
    assertAction(DeviceUi::UiAction::DISPLAY_OFF, controller.update(0x00007520U));
}

void testZeroTimeoutSleepsOnFirstService() {
    DeviceUi::Controller controller(10, 0, 100, 1000);
    assertAction(DeviceUi::UiAction::DISPLAY_OFF, controller.update(10));
    TEST_ASSERT_FALSE(controller.displayAwake());
}

void testWakePressRequestsOnAndRenderWithoutNavigation() {
    DeviceUi::Controller controller(0);
    sleepController(controller, 30000);
    controller.handle(DeviceInput::ButtonEvent::PRESS, 31000);
    assertScreen(DeviceUi::Screen::HOME, controller.screen());
    TEST_ASSERT_TRUE(controller.displayAwake());
    TEST_ASSERT_TRUE(controller.dirty());
    assertAction(
        DeviceUi::UiAction::DISPLAY_ON_AND_RENDER,
        controller.update(31000)
    );
}

void testWakeShortGestureIsSuppressedThenNextGestureNavigates() {
    DeviceUi::Controller controller(0);
    sleepController(controller, 30000);
    controller.handle(DeviceInput::ButtonEvent::PRESS, 31000);
    controller.handle(DeviceInput::ButtonEvent::RELEASE, 31100);
    controller.handle(DeviceInput::ButtonEvent::SHORT_PRESS, 31100);
    assertScreen(DeviceUi::Screen::HOME, controller.screen());

    shortPress(controller, 32000);
    assertScreen(DeviceUi::Screen::RADIO, controller.screen());
}

void testWakeLongGestureIsFullySuppressed() {
    DeviceUi::Controller controller(0);
    sleepController(controller, 30000);
    controller.handle(DeviceInput::ButtonEvent::PRESS, 31000);
    controller.handle(DeviceInput::ButtonEvent::LONG_PRESS, 31800);
    controller.handle(DeviceInput::ButtonEvent::RELEASE, 31900);
    assertScreen(DeviceUi::Screen::HOME, controller.screen());
    shortPress(controller, 32000);
    assertScreen(DeviceUi::Screen::RADIO, controller.screen());
}

void testWakeResetsTimeoutAndPreservesSelectedScreen() {
    DeviceUi::Controller controller(0);
    renderAndRecord(controller, 0);
    controller.handle(DeviceInput::ButtonEvent::SHORT_PRESS, 100);
    renderAndRecord(controller, 100);
    assertAction(DeviceUi::UiAction::DISPLAY_OFF, controller.update(30100));
    controller.handle(DeviceInput::ButtonEvent::PRESS, 31000);
    assertScreen(DeviceUi::Screen::RADIO, controller.screen());
    assertAction(
        DeviceUi::UiAction::DISPLAY_ON_AND_RENDER,
        controller.update(31000)
    );
    controller.recordRendered(31000);
    assertAction(DeviceUi::UiAction::RENDER, controller.update(60999));
    controller.recordRendered(60999);
    assertAction(DeviceUi::UiAction::DISPLAY_OFF, controller.update(61000));
}

void testTimeoutCancelsPendingRenderWithoutLosingDirtyState() {
    DeviceUi::Controller controller(0);
    assertAction(DeviceUi::UiAction::RENDER, controller.update(0));
    assertAction(DeviceUi::UiAction::DISPLAY_OFF, controller.update(30000));
    TEST_ASSERT_TRUE(controller.dirty());

    controller.handle(DeviceInput::ButtonEvent::PRESS, 31000);
    assertAction(
        DeviceUi::UiAction::DISPLAY_ON_AND_RENDER,
        controller.update(31000)
    );
}

void testWakeDoesNotRepeatDisplayOnRequest() {
    DeviceUi::Controller controller(0);
    sleepController(controller, 30000);
    controller.handle(DeviceInput::ButtonEvent::PRESS, 31000);
    assertAction(
        DeviceUi::UiAction::DISPLAY_ON_AND_RENDER,
        controller.update(31000)
    );
    assertAction(DeviceUi::UiAction::NONE, controller.update(31001));
}

void testWakeRespectsRenderCapWithSeparateDisplayOnAction() {
    DeviceUi::Controller controller(0, 50, 100, 1000);
    renderAndRecord(controller, 0);
    assertAction(DeviceUi::UiAction::DISPLAY_OFF, controller.update(50));
    controller.handle(DeviceInput::ButtonEvent::PRESS, 60);
    assertAction(DeviceUi::UiAction::DISPLAY_ON, controller.update(60));
    TEST_ASSERT_TRUE(controller.dirty());
    assertAction(DeviceUi::UiAction::RENDER, controller.update(100));
}

void testWakeBehaviorWorksAcrossRollover() {
    DeviceUi::Controller controller(0xFFFFFF00U, 100, 0, 1000);
    assertAction(
        DeviceUi::UiAction::DISPLAY_OFF,
        controller.update(0xFFFFFF64U)
    );
    controller.handle(DeviceInput::ButtonEvent::PRESS, 0xFFFFFFF0U);
    assertAction(
        DeviceUi::UiAction::DISPLAY_ON_AND_RENDER,
        controller.update(0xFFFFFFF0U)
    );
    controller.recordRendered(0xFFFFFFF0U);
    assertAction(DeviceUi::UiAction::NONE, controller.update(0x00000053U));
    assertAction(
        DeviceUi::UiAction::DISPLAY_OFF,
        controller.update(0x00000054U)
    );
}

void testControllersRemainIndependent() {
    DeviceUi::Controller first(0);
    DeviceUi::Controller second(0);
    first.handle(DeviceInput::ButtonEvent::SHORT_PRESS, 1);
    first.markDirty();
    assertScreen(DeviceUi::Screen::RADIO, first.screen());
    assertScreen(DeviceUi::Screen::HOME, second.screen());
    TEST_ASSERT_TRUE(second.displayAwake());
}

DeviceUi::PresentationInput makePresentationInput(
    RuntimeState::DeviceRole role = RuntimeState::DeviceRole::HUB
) {
    DeviceUi::PresentationInput input;
    input.role = role;
    input.localId = role == RuntimeState::DeviceRole::HUB ? 0x01 : 0x10;
    input.peerId = role == RuntimeState::DeviceRole::HUB ? 0x10 : 0x01;
    input.ready = true;
    input.health = RuntimeState::Health::READY;
    input.phase = role == RuntimeState::DeviceRole::HUB
        ? RuntimeState::RuntimePhase::IDLE
        : RuntimeState::RuntimePhase::LISTENING;
    input.firmwareVersion = "v0.2.0";
    input.wireProtocolVersion = 1;
    input.hardwareProfile = "HELTEC_V4";
    return input;
}

void assertRow(
    const DeviceUi::PresentationSnapshot& snapshot,
    uint8_t index,
    const char* label,
    const char* value
) {
    TEST_ASSERT_LESS_THAN_UINT8(snapshot.rowCount, index);
    TEST_ASSERT_EQUAL_STRING(label, snapshot.rows[index].label);
    TEST_ASSERT_EQUAL_STRING(value, snapshot.rows[index].value);
}

void testEveryScreenBuildsWithMatchingIdAndBoundedRows() {
    const DeviceUi::Screen screens[] = {
        DeviceUi::Screen::HOME,
        DeviceUi::Screen::RADIO,
        DeviceUi::Screen::DEVICE,
        DeviceUi::Screen::LAST_PACKET,
        DeviceUi::Screen::DIAGNOSTICS,
        DeviceUi::Screen::ABOUT
    };
    const DeviceUi::PresentationInput input = makePresentationInput();
    for (DeviceUi::Screen screen : screens) {
        const DeviceUi::PresentationSnapshot snapshot =
            DeviceUi::buildPresentation(screen, input);
        assertScreen(screen, snapshot.screen);
        TEST_ASSERT_LESS_OR_EQUAL_UINT8(
            DeviceUi::MAX_PRESENTATION_ROWS,
            snapshot.rowCount
        );
        TEST_ASSERT_NOT_EQUAL('\0', snapshot.title[0]);
    }
}

void testBuilderFullyReplacesReusedSnapshotAndDoesNotMutateInput() {
    DeviceUi::PresentationInput input = makePresentationInput();
    const uint8_t originalLocalId = input.localId;
    DeviceUi::PresentationSnapshot snapshot = DeviceUi::buildPresentation(
        DeviceUi::Screen::DIAGNOSTICS,
        input
    );
    TEST_ASSERT_EQUAL_UINT8(5, snapshot.rowCount);
    snapshot = DeviceUi::buildPresentation(DeviceUi::Screen::HOME, input);
    TEST_ASSERT_EQUAL_UINT8(1, snapshot.rowCount);
    TEST_ASSERT_EQUAL_CHAR('\0', snapshot.rows[1].label[0]);
    TEST_ASSERT_EQUAL_CHAR('\0', snapshot.rows[1].value[0]);
    TEST_ASSERT_EQUAL_UINT8(originalLocalId, input.localId);
}

void testFixedStringsAreBoundedAndNullTerminated() {
    DeviceUi::PresentationInput input = makePresentationInput();
    input.hardwareProfile = "123456789012345678901234567890";
    input.firmwareVersion = "abcdefghijklmnopqrstuvwxyz";
    const DeviceUi::PresentationSnapshot device =
        DeviceUi::buildPresentation(DeviceUi::Screen::DEVICE, input);
    const DeviceUi::PresentationSnapshot about =
        DeviceUi::buildPresentation(DeviceUi::Screen::ABOUT, input);
    TEST_ASSERT_EQUAL_CHAR(
        '\0',
        device.rows[4].value[DeviceUi::PRESENTATION_VALUE_CAPACITY - 1]
    );
    TEST_ASSERT_EQUAL_CHAR(
        '\0',
        about.rows[0].value[DeviceUi::PRESENTATION_VALUE_CAPACITY - 1]
    );
}

void testHomeRoleAndAllHealthLabelsAreDeterministic() {
    DeviceUi::PresentationInput hub = makePresentationInput();
    assertRow(
        DeviceUi::buildPresentation(DeviceUi::Screen::HOME, hub),
        0,
        "TX",
        "READY"
    );
    DeviceUi::PresentationInput node =
        makePresentationInput(RuntimeState::DeviceRole::NODE);
    const RuntimeState::Health healthValues[] = {
        RuntimeState::Health::STARTING,
        RuntimeState::Health::READY,
        RuntimeState::Health::DEGRADED,
        RuntimeState::Health::ERROR
    };
    const char* labels[] = {"START", "READY", "DEGRADED", "ERROR"};
    for (uint8_t index = 0; index < 4; ++index) {
        node.health = healthValues[index];
        const DeviceUi::PresentationSnapshot snapshot =
            DeviceUi::buildPresentation(DeviceUi::Screen::HOME, node);
        assertRow(snapshot, 0, "RX", labels[index]);
        TEST_ASSERT_EQUAL_UINT8(1, snapshot.rowCount);
    }
}

void testUnknownRoleAndHealthUseFallbacks() {
    DeviceUi::PresentationInput input = makePresentationInput();
    input.role = static_cast<RuntimeState::DeviceRole>(0xFF);
    input.health = static_cast<RuntimeState::Health>(0xFF);
    assertRow(
        DeviceUi::buildPresentation(DeviceUi::Screen::HOME, input),
        0,
        "UNKNOWN",
        "UNKNOWN"
    );
}

void testRadioRetainsMetricsIncludingRealZero() {
    DeviceUi::PresentationInput input = makePresentationInput();
    input.radioMetricsAvailable = true;
    input.rssi = 0.0F;
    input.snr = -1.25F;
    input.peerState = DeviceUi::PeerState::REACHABLE;
    const DeviceUi::PresentationSnapshot snapshot =
        DeviceUi::buildPresentation(DeviceUi::Screen::RADIO, input);
    assertRow(snapshot, 2, "RSSI", "0.0");
    assertRow(snapshot, 3, "SNR", "-1.2");
    assertRow(snapshot, 4, "PEER", "REACHABLE");
}

void testRadioUnavailableMetricsAreExplicit() {
    const DeviceUi::PresentationSnapshot snapshot =
        DeviceUi::buildPresentation(
            DeviceUi::Screen::RADIO,
            makePresentationInput()
        );
    assertRow(snapshot, 2, "RSSI", "--");
    assertRow(snapshot, 3, "SNR", "--");
}

void testAllRuntimePhasesAndUnknownFallbackMapDeterministically() {
    DeviceUi::PresentationInput input = makePresentationInput();
    const RuntimeState::RuntimePhase phases[] = {
        RuntimeState::RuntimePhase::IDLE,
        RuntimeState::RuntimePhase::TRANSMITTING,
        RuntimeState::RuntimePhase::WAITING_FOR_ACK,
        RuntimeState::RuntimePhase::LISTENING,
        RuntimeState::RuntimePhase::TRANSMITTING_ACK,
        static_cast<RuntimeState::RuntimePhase>(0xFF)
    };
    const char* labels[] = {
        "IDLE", "TX", "WAIT ACK", "LISTEN", "TX ACK", "UNKNOWN"
    };
    for (uint8_t index = 0; index < 6; ++index) {
        input.phase = phases[index];
        assertRow(
            DeviceUi::buildPresentation(DeviceUi::Screen::RADIO, input),
            1,
            "PHASE",
            labels[index]
        );
    }
}

void testPeerStateMappingIsRoleConstrained() {
    DeviceUi::PresentationInput hub = makePresentationInput();
    hub.peerState = DeviceUi::PeerState::DEGRADED;
    assertRow(
        DeviceUi::buildPresentation(DeviceUi::Screen::RADIO, hub),
        4,
        "PEER",
        "DEGRADED"
    );
    DeviceUi::PresentationInput node =
        makePresentationInput(RuntimeState::DeviceRole::NODE);
    node.peerState = DeviceUi::PeerState::SEEN;
    assertRow(
        DeviceUi::buildPresentation(DeviceUi::Screen::RADIO, node),
        4,
        "PEER",
        "SEEN"
    );
    node.peerState = DeviceUi::PeerState::DEGRADED;
    assertRow(
        DeviceUi::buildPresentation(DeviceUi::Screen::RADIO, node),
        4,
        "PEER",
        "UNKNOWN"
    );
}

void testDeviceShowsIdentityReadinessHealthAndProfile() {
    DeviceUi::PresentationInput input = makePresentationInput();
    const DeviceUi::PresentationSnapshot ready =
        DeviceUi::buildPresentation(DeviceUi::Screen::DEVICE, input);
    assertRow(ready, 0, "ROLE", "TX");
    assertRow(ready, 1, "LOCAL", "0x01");
    assertRow(ready, 2, "PEER", "0x10");
    assertRow(ready, 3, "STATUS", "READY");
    assertRow(ready, 4, "HW", "HELTEC_V4");
    input.ready = false;
    assertRow(
        DeviceUi::buildPresentation(DeviceUi::Screen::DEVICE, input),
        3,
        "STATUS",
        "NOT READY"
    );
}

void testUnavailableLastPacketShowsNoPacketOnly() {
    const DeviceUi::PresentationSnapshot snapshot =
        DeviceUi::buildPresentation(
            DeviceUi::Screen::LAST_PACKET,
            makePresentationInput()
        );
    TEST_ASSERT_EQUAL_UINT8(1, snapshot.rowCount);
    assertRow(snapshot, 0, "RX", "NO PACKET");
}

void testHubAckLastPacketRetainsAllFieldsAndStatus() {
    DeviceUi::PresentationInput input = makePresentationInput();
    input.lastInboundPacket = {
        true,
        static_cast<uint8_t>(Protocol::PacketType::ACK),
        0x10,
        0x01,
        31,
        Protocol::OPCODE_TEST,
        1,
        true,
        static_cast<uint8_t>(Protocol::AckStatus::SUCCESS),
        1234
    };
    const DeviceUi::PresentationSnapshot snapshot =
        DeviceUi::buildPresentation(DeviceUi::Screen::LAST_PACKET, input);
    assertRow(snapshot, 0, "RX", "ACK");
    assertRow(snapshot, 1, "SRC>DST", "10>01");
    assertRow(snapshot, 2, "SEQ/OP", "31/100");
    assertRow(snapshot, 3, "LEN", "1");
    assertRow(snapshot, 4, "ACK", "SUCCESS");
}

void testNodeCommandLastPacketUsesUnavailableAckStatus() {
    DeviceUi::PresentationInput input =
        makePresentationInput(RuntimeState::DeviceRole::NODE);
    input.lastInboundPacket = {
        true,
        static_cast<uint8_t>(Protocol::PacketType::COMMAND),
        0x01,
        0x10,
        0,
        Protocol::OPCODE_TEST,
        0,
        false,
        99,
        0
    };
    const DeviceUi::PresentationSnapshot snapshot =
        DeviceUi::buildPresentation(DeviceUi::Screen::LAST_PACKET, input);
    assertRow(snapshot, 0, "RX", "COMMAND");
    assertRow(snapshot, 1, "SRC>DST", "01>10");
    assertRow(snapshot, 2, "SEQ/OP", "0/100");
    assertRow(snapshot, 3, "LEN", "0");
    assertRow(snapshot, 4, "ACK", "--");
}

void testPacketAndAckUnknownRawValuesUseNumericFallbacks() {
    DeviceUi::PresentationInput input = makePresentationInput();
    input.lastInboundPacket.available = true;
    input.lastInboundPacket.rawType = 0xFE;
    input.lastInboundPacket.ackStatusAvailable = true;
    input.lastInboundPacket.rawAckStatus = 0xFD;
    const DeviceUi::PresentationSnapshot snapshot =
        DeviceUi::buildPresentation(DeviceUi::Screen::LAST_PACKET, input);
    assertRow(snapshot, 0, "RX", "TYPE 254");
    assertRow(snapshot, 4, "ACK", "STATUS 253");
}

void testEveryRecognizedPacketAndAckStatusMapsDeterministically() {
    DeviceUi::PresentationInput input = makePresentationInput();
    input.lastInboundPacket.available = true;
    input.lastInboundPacket.ackStatusAvailable = true;
    const uint8_t types[] = {1, 2, 3};
    const char* typeLabels[] = {"COMMAND", "ACK", "ERROR"};
    for (uint8_t index = 0; index < 3; ++index) {
        input.lastInboundPacket.rawType = types[index];
        assertRow(
            DeviceUi::buildPresentation(DeviceUi::Screen::LAST_PACKET, input),
            0,
            "RX",
            typeLabels[index]
        );
    }
    const uint8_t statuses[] = {0, 1, 2};
    const char* statusLabels[] = {"SUCCESS", "UNSUPPORTED", "MALFORMED"};
    for (uint8_t index = 0; index < 3; ++index) {
        input.lastInboundPacket.rawAckStatus = statuses[index];
        assertRow(
            DeviceUi::buildPresentation(DeviceUi::Screen::LAST_PACKET, input),
            4,
            "ACK",
            statusLabels[index]
        );
    }
}

void testLaterPacketPresentationCannotRetainAckStatus() {
    DeviceUi::PresentationInput input = makePresentationInput();
    input.lastInboundPacket.available = true;
    input.lastInboundPacket.ackStatusAvailable = true;
    input.lastInboundPacket.rawAckStatus = 0;
    DeviceUi::PresentationSnapshot snapshot = DeviceUi::buildPresentation(
        DeviceUi::Screen::LAST_PACKET,
        input
    );
    assertRow(snapshot, 4, "ACK", "SUCCESS");
    input.lastInboundPacket.ackStatusAvailable = false;
    snapshot = DeviceUi::buildPresentation(
        DeviceUi::Screen::LAST_PACKET,
        input
    );
    assertRow(snapshot, 4, "ACK", "--");
}

void testHubDiagnosticsUsesBoundedRoleSpecificRows() {
    DeviceUi::PresentationInput input = makePresentationInput();
    input.counters.transmissionsCompleted = 10;
    input.counters.successfulTransactions = 8;
    input.counters.retransmissions = 2;
    input.counters.acknowledgmentTimeouts = 3;
    input.counters.malformedPackets = 4;
    input.counters.ignoredPackets = 5;
    input.lastError = RuntimeState::ErrorClass::ACK_TIMEOUT;
    const DeviceUi::PresentationSnapshot snapshot =
        DeviceUi::buildPresentation(DeviceUi::Screen::DIAGNOSTICS, input);
    assertRow(snapshot, 0, "TX", "10");
    assertRow(snapshot, 1, "SUCCESS", "8");
    assertRow(snapshot, 2, "RETRY/TO", "2/3");
    assertRow(snapshot, 3, "BAD/IGN", "9");
    assertRow(snapshot, 4, "ERROR", "ACK TIMEOUT");
}

void testNodeDiagnosticsUsesBoundedRoleSpecificRows() {
    DeviceUi::PresentationInput input =
        makePresentationInput(RuntimeState::DeviceRole::NODE);
    input.counters.decodedPacketsReceived = 11;
    input.counters.acceptedCommands = 9;
    input.counters.transmissionsCompleted = 10;
    input.counters.duplicates = 1;
    const DeviceUi::PresentationSnapshot snapshot =
        DeviceUi::buildPresentation(DeviceUi::Screen::DIAGNOSTICS, input);
    assertRow(snapshot, 0, "RX", "11");
    assertRow(snapshot, 1, "CMD/ACK", "9/10");
    assertRow(snapshot, 2, "DUP", "1");
    assertRow(snapshot, 3, "BAD/IGN", "0");
    assertRow(snapshot, 4, "ERROR", "NONE");
}

void testDiagnosticsCombinedCountSaturatesAndMaximumFormats() {
    DeviceUi::PresentationInput input = makePresentationInput();
    input.counters.transmissionsCompleted = UINT32_MAX;
    input.counters.malformedPackets = UINT32_MAX;
    input.counters.ignoredPackets = 1;
    const DeviceUi::PresentationSnapshot snapshot =
        DeviceUi::buildPresentation(DeviceUi::Screen::DIAGNOSTICS, input);
    assertRow(snapshot, 0, "TX", "4294967295");
    assertRow(snapshot, 3, "BAD/IGN", "4294967295");
}

void testEveryErrorClassAndUnknownFallbackMapsDeterministically() {
    DeviceUi::PresentationInput input = makePresentationInput();
    const RuntimeState::ErrorClass errors[] = {
        RuntimeState::ErrorClass::NONE,
        RuntimeState::ErrorClass::RADIO_INITIALIZATION,
        RuntimeState::ErrorClass::RADIO_START_RECEIVE,
        RuntimeState::ErrorClass::RADIO_START_TRANSMIT,
        RuntimeState::ErrorClass::RADIO_READ,
        RuntimeState::ErrorClass::PACKET_LENGTH,
        RuntimeState::ErrorClass::PACKET_DECODE,
        RuntimeState::ErrorClass::PACKET_IGNORED,
        RuntimeState::ErrorClass::ACK_TIMEOUT,
        RuntimeState::ErrorClass::REMOTE_ACK,
        RuntimeState::ErrorClass::ACK_STATUS,
        static_cast<RuntimeState::ErrorClass>(0xFF)
    };
    const char* labels[] = {
        "NONE", "RADIO INIT", "START RECEIVE", "START TRANSMIT",
        "RADIO READ", "PACKET LENGTH", "PACKET DECODE",
        "PACKET IGNORED", "ACK TIMEOUT", "REMOTE ACK", "ACK STATUS",
        "UNKNOWN"
    };
    for (uint8_t index = 0; index < 12; ++index) {
        input.lastError = errors[index];
        assertRow(
            DeviceUi::buildPresentation(
                DeviceUi::Screen::DIAGNOSTICS,
                input
            ),
            4,
            "ERROR",
            labels[index]
        );
    }
}

void testAboutUsesSuppliedMetadataExactlyWithoutFutureVersion() {
    DeviceUi::PresentationInput input = makePresentationInput();
    input.firmwareVersion = "custom-build";
    input.wireProtocolVersion = 1;
    input.hardwareProfile = "HELTEC_V4";
    const DeviceUi::PresentationSnapshot snapshot =
        DeviceUi::buildPresentation(DeviceUi::Screen::ABOUT, input);
    TEST_ASSERT_EQUAL_STRING("ARGUS REDLINE", snapshot.title);
    assertRow(snapshot, 0, "FW", "custom-build");
    assertRow(snapshot, 1, "WIRE", "1");
    assertRow(snapshot, 2, "HW", "HELTEC_V4");
    assertRow(snapshot, 3, "ROLE", "TX");
}

void testHubAndNodePresentationInputsRemainIndependent() {
    DeviceUi::PresentationInput hub = makePresentationInput();
    DeviceUi::PresentationInput node =
        makePresentationInput(RuntimeState::DeviceRole::NODE);
    hub.counters.transmissionsCompleted = 7;
    node.counters.transmissionsCompleted = 3;
    assertRow(
        DeviceUi::buildPresentation(DeviceUi::Screen::DIAGNOSTICS, hub),
        0,
        "TX",
        "7"
    );
    assertRow(
        DeviceUi::buildPresentation(DeviceUi::Screen::DIAGNOSTICS, node),
        1,
        "CMD/ACK",
        "0/3"
    );
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(testDefaultScreenIsHome);
    RUN_TEST(testScreenOrderMatchesApprovedSequence);
    RUN_TEST(testSixShortPressesWrapExactlyHome);
    RUN_TEST(testLongPressReturnsHomeFromEveryNonHomeScreen);
    RUN_TEST(testLongPressOnHomeLeavesHomeSelected);
    RUN_TEST(testNonNavigationEventsDoNotNavigateWhileAwake);
    RUN_TEST(testNavigationPolicyHasNoRoleInput);
    RUN_TEST(testLiveScreenClassificationIsExact);
    RUN_TEST(testInitialStateIsAwakeDirtyAndImmediatelyRenderable);
    RUN_TEST(testDirtyCallsCoalesceAndRenderAcknowledgmentClearsDirty);
    RUN_TEST(testInvalidationDuringPendingRenderIsNotLost);
    RUN_TEST(testScreenChangesAndOnlyEffectiveHomeReturnMarkDirty);
    RUN_TEST(testExternalInvalidationWhileAsleepDoesNotWake);
    RUN_TEST(testRenderCapBlocksBeforeAndAllowsAtBoundary);
    RUN_TEST(testRepeatedUpdatesDoNotClearDirtyOrDuplicatePendingRender);
    RUN_TEST(testRenderCapWorksAcrossRollover);
    RUN_TEST(testZeroRenderIntervalAllowsImmediateRenderAfterAcknowledgment);
    RUN_TEST(testStaticScreensDoNotPeriodicallyRefresh);
    RUN_TEST(testAllLiveScreensRefreshAtExactBoundary);
    RUN_TEST(testLiveRefreshObeysRenderCapAndAcknowledgment);
    RUN_TEST(testLiveRefreshDoesNotWakeSleepingDisplay);
    RUN_TEST(testSwitchingToLiveScreenRequestsImmediateDirtyRender);
    RUN_TEST(testLiveRefreshWorksAcrossRollover);
    RUN_TEST(testZeroLiveIntervalMakesLiveScreenEligibleEveryUpdate);
    RUN_TEST(testTimeoutBoundaryAndOneShotOffAction);
    RUN_TEST(testRenderDirtyAndLiveWorkDoNotResetTimeout);
    RUN_TEST(testAwakePressAndNavigationInputsResetTimeout);
    RUN_TEST(testReleaseDoesNotResetTimeout);
    RUN_TEST(testTimeoutWorksAcrossRollover);
    RUN_TEST(testZeroTimeoutSleepsOnFirstService);
    RUN_TEST(testWakePressRequestsOnAndRenderWithoutNavigation);
    RUN_TEST(testWakeShortGestureIsSuppressedThenNextGestureNavigates);
    RUN_TEST(testWakeLongGestureIsFullySuppressed);
    RUN_TEST(testWakeResetsTimeoutAndPreservesSelectedScreen);
    RUN_TEST(testTimeoutCancelsPendingRenderWithoutLosingDirtyState);
    RUN_TEST(testWakeDoesNotRepeatDisplayOnRequest);
    RUN_TEST(testWakeRespectsRenderCapWithSeparateDisplayOnAction);
    RUN_TEST(testWakeBehaviorWorksAcrossRollover);
    RUN_TEST(testControllersRemainIndependent);
    RUN_TEST(testEveryScreenBuildsWithMatchingIdAndBoundedRows);
    RUN_TEST(testBuilderFullyReplacesReusedSnapshotAndDoesNotMutateInput);
    RUN_TEST(testFixedStringsAreBoundedAndNullTerminated);
    RUN_TEST(testHomeRoleAndAllHealthLabelsAreDeterministic);
    RUN_TEST(testUnknownRoleAndHealthUseFallbacks);
    RUN_TEST(testRadioRetainsMetricsIncludingRealZero);
    RUN_TEST(testRadioUnavailableMetricsAreExplicit);
    RUN_TEST(testAllRuntimePhasesAndUnknownFallbackMapDeterministically);
    RUN_TEST(testPeerStateMappingIsRoleConstrained);
    RUN_TEST(testDeviceShowsIdentityReadinessHealthAndProfile);
    RUN_TEST(testUnavailableLastPacketShowsNoPacketOnly);
    RUN_TEST(testHubAckLastPacketRetainsAllFieldsAndStatus);
    RUN_TEST(testNodeCommandLastPacketUsesUnavailableAckStatus);
    RUN_TEST(testPacketAndAckUnknownRawValuesUseNumericFallbacks);
    RUN_TEST(testEveryRecognizedPacketAndAckStatusMapsDeterministically);
    RUN_TEST(testLaterPacketPresentationCannotRetainAckStatus);
    RUN_TEST(testHubDiagnosticsUsesBoundedRoleSpecificRows);
    RUN_TEST(testNodeDiagnosticsUsesBoundedRoleSpecificRows);
    RUN_TEST(testDiagnosticsCombinedCountSaturatesAndMaximumFormats);
    RUN_TEST(testEveryErrorClassAndUnknownFallbackMapsDeterministically);
    RUN_TEST(testAboutUsesSuppliedMetadataExactlyWithoutFutureVersion);
    RUN_TEST(testHubAndNodePresentationInputsRemainIndependent);
    return UNITY_END();
}
