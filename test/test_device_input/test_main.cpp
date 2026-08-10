#include <unity.h>

#include "device_input.h"

namespace {

constexpr uint32_t DEBOUNCE_MS = 30;
constexpr uint32_t LONG_PRESS_MS = 800;
constexpr uint32_t VERY_LONG_PRESS_MS = 3000;

void assertNoEvents(const DeviceInput::ButtonEvents& events) {
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(DeviceInput::ButtonEvent::NONE),
        static_cast<uint8_t>(events.first)
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(DeviceInput::ButtonEvent::NONE),
        static_cast<uint8_t>(events.second)
    );
}

void assertOneEvent(
    const DeviceInput::ButtonEvents& events,
    DeviceInput::ButtonEvent expected
) {
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(expected),
        static_cast<uint8_t>(events.first)
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(DeviceInput::ButtonEvent::NONE),
        static_cast<uint8_t>(events.second)
    );
}

void assertTwoEvents(
    const DeviceInput::ButtonEvents& events,
    DeviceInput::ButtonEvent first,
    DeviceInput::ButtonEvent second
) {
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(first),
        static_cast<uint8_t>(events.first)
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(second),
        static_cast<uint8_t>(events.second)
    );
}

void establishReleasedStartup(DeviceInput::Button& button) {
    assertNoEvents(button.update(false, 0));
}

void establishPressedState(
    DeviceInput::Button& button,
    uint32_t edgeMs,
    uint32_t stableMs
) {
    assertNoEvents(button.update(true, edgeMs));
    assertOneEvent(
        button.update(true, stableMs),
        DeviceInput::ButtonEvent::PRESS
    );
}

void testInitialReleasedSampleEmitsNothing() {
    DeviceInput::Button button(DEBOUNCE_MS, LONG_PRESS_MS);
    assertNoEvents(button.update(false, 0));
}

void testContinuedReleasedSamplesEmitNothing() {
    DeviceInput::Button button(DEBOUNCE_MS, LONG_PRESS_MS);
    establishReleasedStartup(button);
    assertNoEvents(button.update(false, 100));
    assertNoEvents(button.update(false, 1000));
}

void testFirstNormalPressWorksAfterReleasedStartup() {
    DeviceInput::Button button(DEBOUNCE_MS, LONG_PRESS_MS);
    establishReleasedStartup(button);
    establishPressedState(button, 10, 40);
}

void testPressChatterShorterThanDebounceEmitsNothing() {
    DeviceInput::Button button(DEBOUNCE_MS, LONG_PRESS_MS);
    establishReleasedStartup(button);
    assertNoEvents(button.update(true, 10));
    assertNoEvents(button.update(true, 39));
}

void testStablePressEmitsAtDebounceBoundary() {
    DeviceInput::Button button(DEBOUNCE_MS, LONG_PRESS_MS);
    establishReleasedStartup(button);
    assertNoEvents(button.update(true, 10));
    assertOneEvent(button.update(true, 40), DeviceInput::ButtonEvent::PRESS);
}

void testContinuedHoldDoesNotRepeatPress() {
    DeviceInput::Button button(DEBOUNCE_MS, LONG_PRESS_MS);
    establishReleasedStartup(button);
    establishPressedState(button, 10, 40);
    assertNoEvents(button.update(true, 41));
    assertNoEvents(button.update(true, 839));
}

void testPressBounceBackResetsCandidateTransition() {
    DeviceInput::Button button(DEBOUNCE_MS, LONG_PRESS_MS);
    establishReleasedStartup(button);
    assertNoEvents(button.update(true, 10));
    assertNoEvents(button.update(false, 20));
    assertNoEvents(button.update(true, 21));
    assertNoEvents(button.update(true, 50));
    assertOneEvent(button.update(true, 51), DeviceInput::ButtonEvent::PRESS);
}

void testReleaseChatterEmitsNothingPrematurely() {
    DeviceInput::Button button(DEBOUNCE_MS, LONG_PRESS_MS);
    establishReleasedStartup(button);
    establishPressedState(button, 10, 40);
    assertNoEvents(button.update(false, 100));
    assertNoEvents(button.update(false, 129));
}

void testStableReleaseEmitsRelease() {
    DeviceInput::Button button(DEBOUNCE_MS, LONG_PRESS_MS);
    establishReleasedStartup(button);
    establishPressedState(button, 10, 40);
    assertNoEvents(button.update(false, 100));
    const DeviceInput::ButtonEvents events = button.update(false, 130);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(DeviceInput::ButtonEvent::RELEASE),
        static_cast<uint8_t>(events.first)
    );
}

void testShortHoldEmitsOneShortPress() {
    DeviceInput::Button button(DEBOUNCE_MS, LONG_PRESS_MS);
    establishReleasedStartup(button);
    establishPressedState(button, 10, 40);
    assertNoEvents(button.update(false, 100));
    const DeviceInput::ButtonEvents events = button.update(false, 130);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(DeviceInput::ButtonEvent::SHORT_PRESS),
        static_cast<uint8_t>(events.second)
    );
}

void testShortReleaseOrderingIsReleaseThenShortPress() {
    DeviceInput::Button button(DEBOUNCE_MS, LONG_PRESS_MS);
    establishReleasedStartup(button);
    establishPressedState(button, 10, 40);
    assertNoEvents(button.update(false, 100));
    assertTwoEvents(
        button.update(false, 130),
        DeviceInput::ButtonEvent::RELEASE,
        DeviceInput::ButtonEvent::SHORT_PRESS
    );
}

void testContinuedReleaseDoesNotRepeatEvents() {
    DeviceInput::Button button(DEBOUNCE_MS, LONG_PRESS_MS);
    establishReleasedStartup(button);
    establishPressedState(button, 10, 40);
    assertNoEvents(button.update(false, 100));
    assertTwoEvents(
        button.update(false, 130),
        DeviceInput::ButtonEvent::RELEASE,
        DeviceInput::ButtonEvent::SHORT_PRESS
    );
    assertNoEvents(button.update(false, 131));
    assertNoEvents(button.update(false, 500));
}

void testHoldAt799MillisecondsDoesNotEmitLongPress() {
    DeviceInput::Button button(DEBOUNCE_MS, LONG_PRESS_MS);
    establishReleasedStartup(button);
    establishPressedState(button, 10, 40);
    assertNoEvents(button.update(true, 839));
}

void testHoldAt800MillisecondsEmitsLongPress() {
    DeviceInput::Button button(DEBOUNCE_MS, LONG_PRESS_MS);
    establishReleasedStartup(button);
    establishPressedState(button, 10, 40);
    assertOneEvent(
        button.update(true, 840),
        DeviceInput::ButtonEvent::LONG_PRESS
    );
}

void testContinuedLongHoldDoesNotRepeatLongPress() {
    DeviceInput::Button button(DEBOUNCE_MS, LONG_PRESS_MS);
    establishReleasedStartup(button);
    establishPressedState(button, 10, 40);
    assertOneEvent(
        button.update(true, 840),
        DeviceInput::ButtonEvent::LONG_PRESS
    );
    assertNoEvents(button.update(true, 841));
    assertNoEvents(button.update(true, 2000));
}

void testReleaseAfterLongPressHasNoShortPress() {
    DeviceInput::Button button(DEBOUNCE_MS, LONG_PRESS_MS);
    establishReleasedStartup(button);
    establishPressedState(button, 10, 40);
    assertOneEvent(
        button.update(true, 840),
        DeviceInput::ButtonEvent::LONG_PRESS
    );
    assertNoEvents(button.update(false, 900));
    assertOneEvent(
        button.update(false, 930),
        DeviceInput::ButtonEvent::RELEASE
    );
}

void testFirstPressedSampleEmitsNothing() {
    DeviceInput::Button button(DEBOUNCE_MS, LONG_PRESS_MS);
    assertNoEvents(button.update(true, 0));
}

void testContinuedStartupHoldNeverEmitsPressOrLongPress() {
    DeviceInput::Button button(DEBOUNCE_MS, LONG_PRESS_MS);
    assertNoEvents(button.update(true, 0));
    assertNoEvents(button.update(true, 800));
    assertNoEvents(button.update(true, 100000));
}

void testShortStartupReleaseDoesNotArmInput() {
    DeviceInput::Button button(DEBOUNCE_MS, LONG_PRESS_MS);
    assertNoEvents(button.update(true, 0));
    assertNoEvents(button.update(false, 100));
    assertNoEvents(button.update(false, 129));
    assertNoEvents(button.update(true, 130));
    assertNoEvents(button.update(true, 1000));
}

void testStableStartupReleaseArmsWithoutSyntheticEvents() {
    DeviceInput::Button button(DEBOUNCE_MS, LONG_PRESS_MS);
    assertNoEvents(button.update(true, 0));
    assertNoEvents(button.update(false, 100));
    assertNoEvents(button.update(false, 130));
    assertNoEvents(button.update(false, 131));
}

void testNextPressWorksAfterStartupHeldRelease() {
    DeviceInput::Button button(DEBOUNCE_MS, LONG_PRESS_MS);
    assertNoEvents(button.update(true, 0));
    assertNoEvents(button.update(false, 100));
    assertNoEvents(button.update(false, 130));
    establishPressedState(button, 200, 230);
}

void testButtonInstancesRemainIndependent() {
    DeviceInput::Button first(DEBOUNCE_MS, LONG_PRESS_MS);
    DeviceInput::Button second(DEBOUNCE_MS, LONG_PRESS_MS);
    establishReleasedStartup(first);
    establishReleasedStartup(second);

    establishPressedState(first, 10, 40);
    assertNoEvents(second.update(false, 40));
    assertNoEvents(second.update(true, 50));
    assertOneEvent(second.update(true, 80), DeviceInput::ButtonEvent::PRESS);
}

void testPressDebounceWorksAcrossRollover() {
    DeviceInput::Button button(DEBOUNCE_MS, LONG_PRESS_MS);
    assertNoEvents(button.update(false, 0xFFFFFF00U));
    assertNoEvents(button.update(true, 0xFFFFFFF0U));
    assertOneEvent(
        button.update(true, 0x0000000EU),
        DeviceInput::ButtonEvent::PRESS
    );
}

void testReleaseDebounceWorksAcrossRollover() {
    DeviceInput::Button button(DEBOUNCE_MS, LONG_PRESS_MS);
    assertNoEvents(button.update(false, 0xFFFFFE00U));
    establishPressedState(button, 0xFFFFFE10U, 0xFFFFFE2EU);
    assertNoEvents(button.update(false, 0xFFFFFFF0U));
    assertTwoEvents(
        button.update(false, 0x0000000EU),
        DeviceInput::ButtonEvent::RELEASE,
        DeviceInput::ButtonEvent::SHORT_PRESS
    );
}

void testLongPressThresholdWorksAcrossRollover() {
    DeviceInput::Button button(DEBOUNCE_MS, LONG_PRESS_MS);
    assertNoEvents(button.update(false, 0xFFFFFE00U));
    establishPressedState(button, 0xFFFFFEE2U, 0xFFFFFF00U);
    assertNoEvents(button.update(true, 0x0000021FU));
    assertOneEvent(
        button.update(true, 0x00000220U),
        DeviceInput::ButtonEvent::LONG_PRESS
    );
}

void testStartupHeldReleaseArmsAcrossRollover() {
    DeviceInput::Button button(DEBOUNCE_MS, LONG_PRESS_MS);
    assertNoEvents(button.update(true, 0xFFFFFF00U));
    assertNoEvents(button.update(false, 0xFFFFFFF0U));
    assertNoEvents(button.update(false, 0x0000000EU));
    establishPressedState(button, 0x00000020U, 0x0000003EU);
}

void testSecondCompleteShortPressWorks() {
    DeviceInput::Button button(DEBOUNCE_MS, LONG_PRESS_MS);
    establishReleasedStartup(button);
    establishPressedState(button, 10, 40);
    assertNoEvents(button.update(false, 100));
    assertTwoEvents(
        button.update(false, 130),
        DeviceInput::ButtonEvent::RELEASE,
        DeviceInput::ButtonEvent::SHORT_PRESS
    );
    establishPressedState(button, 200, 230);
    assertNoEvents(button.update(false, 300));
    assertTwoEvents(
        button.update(false, 330),
        DeviceInput::ButtonEvent::RELEASE,
        DeviceInput::ButtonEvent::SHORT_PRESS
    );
}

void testLongPressThenShortPressClassifyIndependently() {
    DeviceInput::Button button(DEBOUNCE_MS, LONG_PRESS_MS);
    establishReleasedStartup(button);
    establishPressedState(button, 10, 40);
    assertOneEvent(
        button.update(true, 840),
        DeviceInput::ButtonEvent::LONG_PRESS
    );
    assertNoEvents(button.update(false, 900));
    assertOneEvent(
        button.update(false, 930),
        DeviceInput::ButtonEvent::RELEASE
    );
    establishPressedState(button, 1000, 1030);
    assertNoEvents(button.update(false, 1100));
    assertTwoEvents(
        button.update(false, 1130),
        DeviceInput::ButtonEvent::RELEASE,
        DeviceInput::ButtonEvent::SHORT_PRESS
    );
}

void testZeroTimestampIsHandledNormally() {
    DeviceInput::Button button(DEBOUNCE_MS, LONG_PRESS_MS);
    assertNoEvents(button.update(false, 0xFFFFFF00U));
    assertNoEvents(button.update(true, 0));
    assertOneEvent(button.update(true, 30), DeviceInput::ButtonEvent::PRESS);
}

void testZeroThresholdsApplyOnObservedTransition() {
    DeviceInput::Button button(0, 0);
    establishReleasedStartup(button);
    assertTwoEvents(
        button.update(true, 10),
        DeviceInput::ButtonEvent::PRESS,
        DeviceInput::ButtonEvent::LONG_PRESS
    );
    assertOneEvent(
        button.update(false, 11),
        DeviceInput::ButtonEvent::RELEASE
    );
}

void testLongThenVeryLongAtExactThresholds() {
    DeviceInput::Button button(
        DEBOUNCE_MS,
        LONG_PRESS_MS,
        VERY_LONG_PRESS_MS
    );
    establishReleasedStartup(button);
    establishPressedState(button, 10, 40);
    assertOneEvent(button.update(true, 840), DeviceInput::ButtonEvent::LONG_PRESS);
    assertNoEvents(button.update(true, 3039));
    assertOneEvent(
        button.update(true, 3040),
        DeviceInput::ButtonEvent::VERY_LONG_PRESS
    );
}

void testVeryLongDoesNotRepeatAndReleaseHasNoShortPress() {
    DeviceInput::Button button(DEBOUNCE_MS, LONG_PRESS_MS, VERY_LONG_PRESS_MS);
    establishReleasedStartup(button);
    establishPressedState(button, 10, 40);
    assertOneEvent(button.update(true, 840), DeviceInput::ButtonEvent::LONG_PRESS);
    assertOneEvent(button.update(true, 3040), DeviceInput::ButtonEvent::VERY_LONG_PRESS);
    assertNoEvents(button.update(true, 3041));
    assertNoEvents(button.update(true, 10000));
    assertNoEvents(button.update(false, 10100));
    assertOneEvent(button.update(false, 10130), DeviceInput::ButtonEvent::RELEASE);
}

void testStartupHeldSuppressesVeryLongPress() {
    DeviceInput::Button button(DEBOUNCE_MS, LONG_PRESS_MS, VERY_LONG_PRESS_MS);
    assertNoEvents(button.update(true, 0));
    assertNoEvents(button.update(true, 800));
    assertNoEvents(button.update(true, 3000));
    assertNoEvents(button.update(true, 100000));
}

void testReleaseAllowsSecondVeryLongHold() {
    DeviceInput::Button button(DEBOUNCE_MS, LONG_PRESS_MS, VERY_LONG_PRESS_MS);
    establishReleasedStartup(button);
    establishPressedState(button, 10, 40);
    assertOneEvent(button.update(true, 840), DeviceInput::ButtonEvent::LONG_PRESS);
    assertOneEvent(button.update(true, 3040), DeviceInput::ButtonEvent::VERY_LONG_PRESS);
    assertNoEvents(button.update(false, 3100));
    assertOneEvent(button.update(false, 3130), DeviceInput::ButtonEvent::RELEASE);
    establishPressedState(button, 4000, 4030);
    assertOneEvent(button.update(true, 4830), DeviceInput::ButtonEvent::LONG_PRESS);
    assertOneEvent(button.update(true, 7030), DeviceInput::ButtonEvent::VERY_LONG_PRESS);
}

void testVeryLongThresholdWorksAcrossRollover() {
    DeviceInput::Button button(DEBOUNCE_MS, LONG_PRESS_MS, VERY_LONG_PRESS_MS);
    assertNoEvents(button.update(false, 0xFFFFE000U));
    establishPressedState(button, 0xFFFFFFD0U, 0xFFFFFFEEU);
    assertOneEvent(button.update(true, 0x0000030EU), DeviceInput::ButtonEvent::LONG_PRESS);
    assertNoEvents(button.update(true, 0x00000BA5U));
    assertOneEvent(
        button.update(true, 0x00000BA6U),
        DeviceInput::ButtonEvent::VERY_LONG_PRESS
    );
}

void testSnapshotIsUnavailableBeforeFirstSampleAndReleasedStartupIsAvailable() {
    DeviceInput::Button button(DEBOUNCE_MS, LONG_PRESS_MS);
    DeviceInput::ButtonSnapshot snapshot = button.snapshot();
    TEST_ASSERT_FALSE(snapshot.available);
    TEST_ASSERT_FALSE(snapshot.pressed);

    assertNoEvents(button.update(false, 0));
    snapshot = button.snapshot();
    TEST_ASSERT_TRUE(snapshot.available);
    TEST_ASSERT_FALSE(snapshot.pressed);
}

void testSnapshotTracksOnlyDebouncedStableState() {
    DeviceInput::Button button(DEBOUNCE_MS, LONG_PRESS_MS);
    establishReleasedStartup(button);
    assertNoEvents(button.update(true, 10));
    TEST_ASSERT_FALSE(button.snapshot().pressed);
    assertOneEvent(button.update(true, 40), DeviceInput::ButtonEvent::PRESS);
    TEST_ASSERT_TRUE(button.snapshot().available);
    TEST_ASSERT_TRUE(button.snapshot().pressed);
    assertNoEvents(button.update(false, 100));
    TEST_ASSERT_TRUE(button.snapshot().pressed);
    assertTwoEvents(
        button.update(false, 130),
        DeviceInput::ButtonEvent::RELEASE,
        DeviceInput::ButtonEvent::SHORT_PRESS
    );
    TEST_ASSERT_TRUE(button.snapshot().available);
    TEST_ASSERT_FALSE(button.snapshot().pressed);
}

void testStartupHeldSnapshotRemainsUnavailableUntilStableRelease() {
    DeviceInput::Button button(DEBOUNCE_MS, LONG_PRESS_MS);
    assertNoEvents(button.update(true, 0));
    TEST_ASSERT_FALSE(button.snapshot().available);
    assertNoEvents(button.update(true, 1000));
    TEST_ASSERT_FALSE(button.snapshot().available);
    assertNoEvents(button.update(false, 1100));
    TEST_ASSERT_FALSE(button.snapshot().available);
    assertNoEvents(button.update(false, 1129));
    TEST_ASSERT_FALSE(button.snapshot().available);
    assertNoEvents(button.update(false, 1130));
    TEST_ASSERT_TRUE(button.snapshot().available);
    TEST_ASSERT_FALSE(button.snapshot().pressed);
}

void testSnapshotQueryDoesNotConsumeOrAlterSemanticEvents() {
    DeviceInput::Button button(DEBOUNCE_MS, LONG_PRESS_MS);
    establishReleasedStartup(button);
    assertNoEvents(button.update(true, 10));
    TEST_ASSERT_FALSE(button.snapshot().pressed);
    TEST_ASSERT_FALSE(button.snapshot().pressed);
    assertOneEvent(button.update(true, 40), DeviceInput::ButtonEvent::PRESS);
    TEST_ASSERT_TRUE(button.snapshot().pressed);
    assertNoEvents(button.update(false, 100));
    TEST_ASSERT_TRUE(button.snapshot().pressed);
    assertTwoEvents(
        button.update(false, 130),
        DeviceInput::ButtonEvent::RELEASE,
        DeviceInput::ButtonEvent::SHORT_PRESS
    );
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(testInitialReleasedSampleEmitsNothing);
    RUN_TEST(testContinuedReleasedSamplesEmitNothing);
    RUN_TEST(testFirstNormalPressWorksAfterReleasedStartup);
    RUN_TEST(testPressChatterShorterThanDebounceEmitsNothing);
    RUN_TEST(testStablePressEmitsAtDebounceBoundary);
    RUN_TEST(testContinuedHoldDoesNotRepeatPress);
    RUN_TEST(testPressBounceBackResetsCandidateTransition);
    RUN_TEST(testReleaseChatterEmitsNothingPrematurely);
    RUN_TEST(testStableReleaseEmitsRelease);
    RUN_TEST(testShortHoldEmitsOneShortPress);
    RUN_TEST(testShortReleaseOrderingIsReleaseThenShortPress);
    RUN_TEST(testContinuedReleaseDoesNotRepeatEvents);
    RUN_TEST(testHoldAt799MillisecondsDoesNotEmitLongPress);
    RUN_TEST(testHoldAt800MillisecondsEmitsLongPress);
    RUN_TEST(testContinuedLongHoldDoesNotRepeatLongPress);
    RUN_TEST(testReleaseAfterLongPressHasNoShortPress);
    RUN_TEST(testFirstPressedSampleEmitsNothing);
    RUN_TEST(testContinuedStartupHoldNeverEmitsPressOrLongPress);
    RUN_TEST(testShortStartupReleaseDoesNotArmInput);
    RUN_TEST(testStableStartupReleaseArmsWithoutSyntheticEvents);
    RUN_TEST(testNextPressWorksAfterStartupHeldRelease);
    RUN_TEST(testButtonInstancesRemainIndependent);
    RUN_TEST(testPressDebounceWorksAcrossRollover);
    RUN_TEST(testReleaseDebounceWorksAcrossRollover);
    RUN_TEST(testLongPressThresholdWorksAcrossRollover);
    RUN_TEST(testStartupHeldReleaseArmsAcrossRollover);
    RUN_TEST(testSecondCompleteShortPressWorks);
    RUN_TEST(testLongPressThenShortPressClassifyIndependently);
    RUN_TEST(testZeroTimestampIsHandledNormally);
    RUN_TEST(testZeroThresholdsApplyOnObservedTransition);
    RUN_TEST(testLongThenVeryLongAtExactThresholds);
    RUN_TEST(testVeryLongDoesNotRepeatAndReleaseHasNoShortPress);
    RUN_TEST(testStartupHeldSuppressesVeryLongPress);
    RUN_TEST(testReleaseAllowsSecondVeryLongHold);
    RUN_TEST(testVeryLongThresholdWorksAcrossRollover);
    RUN_TEST(testSnapshotIsUnavailableBeforeFirstSampleAndReleasedStartupIsAvailable);
    RUN_TEST(testSnapshotTracksOnlyDebouncedStableState);
    RUN_TEST(testStartupHeldSnapshotRemainsUnavailableUntilStableRelease);
    RUN_TEST(testSnapshotQueryDoesNotConsumeOrAlterSemanticEvents);
    return UNITY_END();
}
