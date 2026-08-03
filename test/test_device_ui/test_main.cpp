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
    return UNITY_END();
}
