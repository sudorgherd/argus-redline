#include <unity.h>

#include "capability_characterization.h"

namespace {

using CapabilityCharacterization::Action;

void testRaw12NormalizationEndpointsAndMidpoint() {
    TEST_ASSERT_EQUAL_UINT16(0, CapabilityCharacterization::normalizeRaw12ToU16(0));
    TEST_ASSERT_EQUAL_UINT16(65535, CapabilityCharacterization::normalizeRaw12ToU16(4095));
    TEST_ASSERT_EQUAL_UINT16(32775, CapabilityCharacterization::normalizeRaw12ToU16(2048));
}

void testRaw12NormalizationIsMonotonicAndClampsInvalidHighInput() {
    TEST_ASSERT_LESS_THAN_UINT16(
        CapabilityCharacterization::normalizeRaw12ToU16(2048),
        CapabilityCharacterization::normalizeRaw12ToU16(2047)
    );
    TEST_ASSERT_EQUAL_UINT16(65535, CapabilityCharacterization::normalizeRaw12ToU16(65535));
}

void testExactBoundedCommandVocabulary() {
    const char* commands[] = {
        "help", "caps", "digital", "indicator-on", "indicator-off",
        "analog", "deny-remote", "deny-interlock", "status"
    };
    const Action actions[] = {
        Action::HELP, Action::CAPS, Action::DIGITAL,
        Action::INDICATOR_ON, Action::INDICATOR_OFF, Action::ANALOG_INPUT,
        Action::DENY_REMOTE, Action::DENY_INTERLOCK, Action::STATUS
    };
    for (uint8_t index = 0; index < 9; ++index) {
        TEST_ASSERT_EQUAL_UINT8(
            static_cast<uint8_t>(actions[index]),
            static_cast<uint8_t>(
                CapabilityCharacterization::classifyCommand(commands[index])
            )
        );
    }
}

void testGenericOrMalformedCommandsAreRejected() {
    const char* rejected[] = {
        "", "read 0x0201", "set 0x0101 1", "gpio 4", "adc 4",
        "DIGITAL", "indicator-on ", "unknown"
    };
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(Action::NONE),
        static_cast<uint8_t>(CapabilityCharacterization::classifyCommand(nullptr))
    );
    for (const char* command : rejected) {
        TEST_ASSERT_EQUAL_UINT8(
            static_cast<uint8_t>(Action::NONE),
            static_cast<uint8_t>(
                CapabilityCharacterization::classifyCommand(command)
            )
        );
    }
    TEST_ASSERT_EQUAL_UINT8(24, CapabilityCharacterization::COMMAND_BUFFER_CAPACITY);
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(testRaw12NormalizationEndpointsAndMidpoint);
    RUN_TEST(testRaw12NormalizationIsMonotonicAndClampsInvalidHighInput);
    RUN_TEST(testExactBoundedCommandVocabulary);
    RUN_TEST(testGenericOrMalformedCommandsAreRejected);
    return UNITY_END();
}
