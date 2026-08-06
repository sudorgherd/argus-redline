#include <unity.h>

#include "device_settings.h"

namespace {

void assertResult(
    DeviceSettings::ValidationResult expected,
    DeviceSettings::ValidationResult actual
) {
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(expected),
        static_cast<uint8_t>(actual)
    );
}

void testSchemaVersionIsOne() {
    TEST_ASSERT_EQUAL_UINT16(1, DeviceSettings::SCHEMA_VERSION);
}

void testDefaultsMatchApprovedValues() {
    const DeviceSettings::Settings settings = DeviceSettings::defaults();

    TEST_ASSERT_EQUAL_UINT16(30, settings.displayTimeoutSeconds);
    TEST_ASSERT_EQUAL_UINT8(207, settings.displayContrast);
    TEST_ASSERT_TRUE(settings.ledEnabled);
    TEST_ASSERT_TRUE(settings.diagnosticsEnabled);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(DeviceSettings::DefaultScreen::HOME),
        static_cast<uint8_t>(settings.defaultScreen)
    );
    TEST_ASSERT_FALSE(settings.buttonFeedbackEnabled);
}

void testFreshSettingsUseApprovedDefaults() {
    const DeviceSettings::Settings direct;
    TEST_ASSERT_TRUE(direct == DeviceSettings::defaults());
}

void testHubAndNodeDefaultsAreIdentical() {
    const DeviceSettings::Settings hub = DeviceSettings::defaults();
    const DeviceSettings::Settings node = DeviceSettings::defaults();

    TEST_ASSERT_TRUE(hub == node);
}

void testEqualityIncludesEveryField() {
    const DeviceSettings::Settings baseline = DeviceSettings::defaults();
    DeviceSettings::Settings changed = baseline;

    TEST_ASSERT_TRUE(baseline == changed);
    TEST_ASSERT_FALSE(baseline != changed);

    changed.displayTimeoutSeconds = 60;
    TEST_ASSERT_TRUE(baseline != changed);
    changed = baseline;
    changed.displayContrast = 128;
    TEST_ASSERT_TRUE(baseline != changed);
    changed = baseline;
    changed.ledEnabled = false;
    TEST_ASSERT_TRUE(baseline != changed);
    changed = baseline;
    changed.diagnosticsEnabled = false;
    TEST_ASSERT_TRUE(baseline != changed);
    changed = baseline;
    changed.defaultScreen = DeviceSettings::DefaultScreen::ABOUT;
    TEST_ASSERT_TRUE(baseline != changed);
    changed = baseline;
    changed.buttonFeedbackEnabled = true;
    TEST_ASSERT_TRUE(baseline != changed);
}

void testTimeoutAcceptsZeroAndBoundaries() {
    TEST_ASSERT_TRUE(DeviceSettings::isValidDisplayTimeoutSeconds(0));
    TEST_ASSERT_TRUE(DeviceSettings::isValidDisplayTimeoutSeconds(5));
    TEST_ASSERT_TRUE(DeviceSettings::isValidDisplayTimeoutSeconds(600));
}

void testTimeoutRejectsInvalidGapAndAboveMaximum() {
    for (uint16_t value = 1; value <= 4; ++value) {
        TEST_ASSERT_FALSE(
            DeviceSettings::isValidDisplayTimeoutSeconds(value)
        );
    }
    TEST_ASSERT_FALSE(DeviceSettings::isValidDisplayTimeoutSeconds(601));
    TEST_ASSERT_FALSE(
        DeviceSettings::isValidDisplayTimeoutSeconds(UINT16_MAX)
    );
}

void testContrastAcceptsBoundaries() {
    TEST_ASSERT_TRUE(DeviceSettings::isValidDisplayContrast(16));
    TEST_ASSERT_TRUE(DeviceSettings::isValidDisplayContrast(255));
}

void testContrastRejectsValuesBelowMinimum() {
    TEST_ASSERT_FALSE(DeviceSettings::isValidDisplayContrast(0));
    TEST_ASSERT_FALSE(DeviceSettings::isValidDisplayContrast(15));
}

void testEveryApprovedDefaultScreenIsValid() {
    const DeviceSettings::DefaultScreen screens[] = {
        DeviceSettings::DefaultScreen::HOME,
        DeviceSettings::DefaultScreen::RADIO,
        DeviceSettings::DefaultScreen::DEVICE,
        DeviceSettings::DefaultScreen::LAST_PACKET,
        DeviceSettings::DefaultScreen::DIAGNOSTICS,
        DeviceSettings::DefaultScreen::ABOUT
    };

    for (const DeviceSettings::DefaultScreen screen : screens) {
        TEST_ASSERT_TRUE(DeviceSettings::isValidDefaultScreen(screen));
    }
}

void testUnknownDefaultScreensAreInvalid() {
    TEST_ASSERT_FALSE(DeviceSettings::isValidDefaultScreen(
        static_cast<DeviceSettings::DefaultScreen>(6)
    ));
    TEST_ASSERT_FALSE(DeviceSettings::isValidDefaultScreen(
        static_cast<DeviceSettings::DefaultScreen>(UINT8_MAX)
    ));
}

void testValidSettingsRemainUnchanged() {
    DeviceSettings::Settings settings;
    settings.displayTimeoutSeconds = 600;
    settings.displayContrast = 16;
    settings.ledEnabled = false;
    settings.diagnosticsEnabled = false;
    settings.defaultScreen = DeviceSettings::DefaultScreen::ABOUT;
    settings.buttonFeedbackEnabled = true;
    const DeviceSettings::Settings before = settings;

    TEST_ASSERT_TRUE(DeviceSettings::isValid(settings));
    assertResult(
        DeviceSettings::ValidationResult::UNCHANGED,
        DeviceSettings::validateAndRepair(settings)
    );
    TEST_ASSERT_TRUE(settings == before);
}

void testInvalidTimeoutRepairsOnlyTimeout() {
    DeviceSettings::Settings settings;
    settings.displayTimeoutSeconds = 4;
    settings.displayContrast = 128;
    settings.ledEnabled = false;
    settings.diagnosticsEnabled = false;
    settings.defaultScreen = DeviceSettings::DefaultScreen::RADIO;
    settings.buttonFeedbackEnabled = true;

    assertResult(
        DeviceSettings::ValidationResult::REPAIRED,
        DeviceSettings::validateAndRepair(settings)
    );
    TEST_ASSERT_EQUAL_UINT16(30, settings.displayTimeoutSeconds);
    TEST_ASSERT_EQUAL_UINT8(128, settings.displayContrast);
    TEST_ASSERT_FALSE(settings.ledEnabled);
    TEST_ASSERT_FALSE(settings.diagnosticsEnabled);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(DeviceSettings::DefaultScreen::RADIO),
        static_cast<uint8_t>(settings.defaultScreen)
    );
    TEST_ASSERT_TRUE(settings.buttonFeedbackEnabled);
}

void testInvalidContrastRepairsOnlyContrast() {
    DeviceSettings::Settings settings;
    settings.displayTimeoutSeconds = 0;
    settings.displayContrast = 15;
    settings.ledEnabled = false;
    settings.diagnosticsEnabled = false;
    settings.defaultScreen = DeviceSettings::DefaultScreen::DEVICE;
    settings.buttonFeedbackEnabled = true;

    assertResult(
        DeviceSettings::ValidationResult::REPAIRED,
        DeviceSettings::validateAndRepair(settings)
    );
    TEST_ASSERT_EQUAL_UINT16(0, settings.displayTimeoutSeconds);
    TEST_ASSERT_EQUAL_UINT8(207, settings.displayContrast);
    TEST_ASSERT_FALSE(settings.ledEnabled);
    TEST_ASSERT_FALSE(settings.diagnosticsEnabled);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(DeviceSettings::DefaultScreen::DEVICE),
        static_cast<uint8_t>(settings.defaultScreen)
    );
    TEST_ASSERT_TRUE(settings.buttonFeedbackEnabled);
}

void testInvalidDefaultScreenRepairsOnlyScreenToHome() {
    DeviceSettings::Settings settings;
    settings.displayTimeoutSeconds = 5;
    settings.displayContrast = 255;
    settings.ledEnabled = false;
    settings.diagnosticsEnabled = false;
    settings.defaultScreen =
        static_cast<DeviceSettings::DefaultScreen>(UINT8_MAX);
    settings.buttonFeedbackEnabled = true;

    assertResult(
        DeviceSettings::ValidationResult::REPAIRED,
        DeviceSettings::validateAndRepair(settings)
    );
    TEST_ASSERT_EQUAL_UINT16(5, settings.displayTimeoutSeconds);
    TEST_ASSERT_EQUAL_UINT8(255, settings.displayContrast);
    TEST_ASSERT_FALSE(settings.ledEnabled);
    TEST_ASSERT_FALSE(settings.diagnosticsEnabled);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(DeviceSettings::DefaultScreen::HOME),
        static_cast<uint8_t>(settings.defaultScreen)
    );
    TEST_ASSERT_TRUE(settings.buttonFeedbackEnabled);
}

void testMultipleInvalidFieldsRepairIndependently() {
    DeviceSettings::Settings settings;
    settings.displayTimeoutSeconds = 601;
    settings.displayContrast = 0;
    settings.ledEnabled = false;
    settings.diagnosticsEnabled = false;
    settings.defaultScreen = static_cast<DeviceSettings::DefaultScreen>(6);
    settings.buttonFeedbackEnabled = true;

    assertResult(
        DeviceSettings::ValidationResult::REPAIRED,
        DeviceSettings::validateAndRepair(settings)
    );
    DeviceSettings::Settings expected;
    expected.ledEnabled = false;
    expected.diagnosticsEnabled = false;
    expected.buttonFeedbackEnabled = true;
    TEST_ASSERT_TRUE(settings == expected);
    TEST_ASSERT_TRUE(DeviceSettings::isValid(settings));
}

void testRepairIsIdempotent() {
    DeviceSettings::Settings settings;
    settings.displayTimeoutSeconds = 1;
    settings.displayContrast = 1;
    settings.defaultScreen = static_cast<DeviceSettings::DefaultScreen>(99);

    assertResult(
        DeviceSettings::ValidationResult::REPAIRED,
        DeviceSettings::validateAndRepair(settings)
    );
    assertResult(
        DeviceSettings::ValidationResult::UNCHANGED,
        DeviceSettings::validateAndRepair(settings)
    );
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(testSchemaVersionIsOne);
    RUN_TEST(testDefaultsMatchApprovedValues);
    RUN_TEST(testFreshSettingsUseApprovedDefaults);
    RUN_TEST(testHubAndNodeDefaultsAreIdentical);
    RUN_TEST(testEqualityIncludesEveryField);
    RUN_TEST(testTimeoutAcceptsZeroAndBoundaries);
    RUN_TEST(testTimeoutRejectsInvalidGapAndAboveMaximum);
    RUN_TEST(testContrastAcceptsBoundaries);
    RUN_TEST(testContrastRejectsValuesBelowMinimum);
    RUN_TEST(testEveryApprovedDefaultScreenIsValid);
    RUN_TEST(testUnknownDefaultScreensAreInvalid);
    RUN_TEST(testValidSettingsRemainUnchanged);
    RUN_TEST(testInvalidTimeoutRepairsOnlyTimeout);
    RUN_TEST(testInvalidContrastRepairsOnlyContrast);
    RUN_TEST(testInvalidDefaultScreenRepairsOnlyScreenToHome);
    RUN_TEST(testMultipleInvalidFieldsRepairIndependently);
    RUN_TEST(testRepairIsIdempotent);
    return UNITY_END();
}
