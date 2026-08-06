#include <unity.h>

#include <string.h>

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

void assertCodecResult(
    DeviceSettings::CodecResult expected,
    DeviceSettings::CodecResult actual
) {
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(expected),
        static_cast<uint8_t>(actual)
    );
}

void encodeDefaults(uint8_t* record, uint32_t generation = 0x12345678U) {
    assertCodecResult(
        DeviceSettings::CodecResult::OK,
        DeviceSettings::encodeRecord(
            DeviceSettings::defaults(),
            generation,
            record,
            DeviceSettings::RECORD_SIZE
        )
    );
}

void updateRecordCrc(uint8_t* record) {
    uint32_t crc = 0;
    TEST_ASSERT_TRUE(DeviceSettings::crc32IsoHdlc(
        record,
        DeviceSettings::CRC_INPUT_SIZE,
        crc
    ));
    DeviceSettings::writeUint32Le(record + 20, crc);
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

void testKnownCrcVectorMatchesIsoHdlc() {
    const uint8_t input[] = {
        '1', '2', '3', '4', '5', '6', '7', '8', '9'
    };
    uint32_t crc = 0;

    TEST_ASSERT_TRUE(DeviceSettings::crc32IsoHdlc(
        input,
        sizeof(input),
        crc
    ));
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926U, crc);
}

void testCrcRejectsNullInputWithoutChangingOutput() {
    uint32_t crc = 0xA5A5A5A5U;
    TEST_ASSERT_FALSE(DeviceSettings::crc32IsoHdlc(nullptr, 0, crc));
    TEST_ASSERT_EQUAL_HEX32(0xA5A5A5A5U, crc);
}

void testEncodeProducesExactKnownFixture() {
    uint8_t actual[DeviceSettings::RECORD_SIZE] = {};
    const uint8_t expected[DeviceSettings::RECORD_SIZE] = {
        0x52, 0x4C, 0x43, 0x46,
        0x01, 0x00,
        0x18, 0x00,
        0x78, 0x56, 0x34, 0x12,
        0x1E, 0x00,
        0xCF,
        0x00,
        0x03,
        0x00, 0x00, 0x00,
        0x06, 0x5E, 0x47, 0x26
    };

    encodeDefaults(actual);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, actual, sizeof(expected));
}

void testEncodeDecodeRoundTripPreservesSettingsAndGeneration() {
    DeviceSettings::Settings original;
    original.displayTimeoutSeconds = 120;
    original.displayContrast = 128;
    original.ledEnabled = false;
    original.diagnosticsEnabled = true;
    original.defaultScreen = DeviceSettings::DefaultScreen::LAST_PACKET;
    original.buttonFeedbackEnabled = true;
    uint8_t record[DeviceSettings::RECORD_SIZE] = {};
    DeviceSettings::Settings decoded;
    uint32_t generation = 0;

    assertCodecResult(
        DeviceSettings::CodecResult::OK,
        DeviceSettings::encodeRecord(
            original, 0x89ABCDEFU, record, sizeof(record)
        )
    );
    assertCodecResult(
        DeviceSettings::CodecResult::OK,
        DeviceSettings::decodeRecord(
            record, sizeof(record), decoded, generation
        )
    );
    TEST_ASSERT_TRUE(original == decoded);
    TEST_ASSERT_EQUAL_HEX32(0x89ABCDEFU, generation);
}

void testLittleEndianFieldOrderIsExplicit() {
    DeviceSettings::Settings settings;
    settings.displayTimeoutSeconds = 0x012CU;
    uint8_t record[DeviceSettings::RECORD_SIZE] = {};

    assertCodecResult(
        DeviceSettings::CodecResult::OK,
        DeviceSettings::encodeRecord(
            settings, 0x12345678U, record, sizeof(record)
        )
    );
    TEST_ASSERT_EQUAL_HEX8(0x01, record[4]);
    TEST_ASSERT_EQUAL_HEX8(0x00, record[5]);
    TEST_ASSERT_EQUAL_HEX8(0x18, record[6]);
    TEST_ASSERT_EQUAL_HEX8(0x00, record[7]);
    TEST_ASSERT_EQUAL_HEX8(0x78, record[8]);
    TEST_ASSERT_EQUAL_HEX8(0x56, record[9]);
    TEST_ASSERT_EQUAL_HEX8(0x34, record[10]);
    TEST_ASSERT_EQUAL_HEX8(0x12, record[11]);
    TEST_ASSERT_EQUAL_HEX8(0x2C, record[12]);
    TEST_ASSERT_EQUAL_HEX8(0x01, record[13]);
}

void testMaximumApprovedValuesRoundTrip() {
    DeviceSettings::Settings settings;
    settings.displayTimeoutSeconds = 600;
    settings.displayContrast = 255;
    settings.ledEnabled = true;
    settings.diagnosticsEnabled = true;
    settings.defaultScreen = DeviceSettings::DefaultScreen::ABOUT;
    settings.buttonFeedbackEnabled = true;
    uint8_t record[DeviceSettings::RECORD_SIZE] = {};
    DeviceSettings::Settings decoded;
    uint32_t generation = 0;

    assertCodecResult(
        DeviceSettings::CodecResult::OK,
        DeviceSettings::encodeRecord(
            settings, UINT32_MAX, record, sizeof(record)
        )
    );
    TEST_ASSERT_EQUAL_HEX8(DeviceSettings::KNOWN_FLAGS, record[16]);
    assertCodecResult(
        DeviceSettings::CodecResult::OK,
        DeviceSettings::decodeRecord(
            record, sizeof(record), decoded, generation
        )
    );
    TEST_ASSERT_TRUE(settings == decoded);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, generation);
}

void testEncodeRejectsNullAndNonExactOutputLengths() {
    const DeviceSettings::Settings settings = DeviceSettings::defaults();
    uint8_t record[DeviceSettings::RECORD_SIZE] = {};

    assertCodecResult(
        DeviceSettings::CodecResult::NULL_POINTER,
        DeviceSettings::encodeRecord(
            settings, 1, nullptr, DeviceSettings::RECORD_SIZE
        )
    );
    assertCodecResult(
        DeviceSettings::CodecResult::WRONG_LENGTH,
        DeviceSettings::encodeRecord(
            settings, 1, record, DeviceSettings::RECORD_SIZE - 1
        )
    );
    assertCodecResult(
        DeviceSettings::CodecResult::WRONG_LENGTH,
        DeviceSettings::encodeRecord(
            settings, 1, record, DeviceSettings::RECORD_SIZE + 1
        )
    );
}

void testEncodeRejectsEveryInvalidSettingClass() {
    uint8_t record[DeviceSettings::RECORD_SIZE] = {};
    DeviceSettings::Settings settings;
    settings.displayTimeoutSeconds = 4;
    assertCodecResult(
        DeviceSettings::CodecResult::INVALID_TIMEOUT,
        DeviceSettings::encodeRecord(settings, 1, record, sizeof(record))
    );
    settings = DeviceSettings::defaults();
    settings.displayContrast = 15;
    assertCodecResult(
        DeviceSettings::CodecResult::INVALID_CONTRAST,
        DeviceSettings::encodeRecord(settings, 1, record, sizeof(record))
    );
    settings = DeviceSettings::defaults();
    settings.defaultScreen = static_cast<DeviceSettings::DefaultScreen>(6);
    assertCodecResult(
        DeviceSettings::CodecResult::INVALID_SCREEN,
        DeviceSettings::encodeRecord(settings, 1, record, sizeof(record))
    );
}

void testDecodeRejectsNullShortAndOversizedInputs() {
    uint8_t record[DeviceSettings::RECORD_SIZE + 1] = {};
    DeviceSettings::Settings settings;
    uint32_t generation = 0;

    assertCodecResult(
        DeviceSettings::CodecResult::NULL_POINTER,
        DeviceSettings::decodeRecord(
            nullptr, DeviceSettings::RECORD_SIZE, settings, generation
        )
    );
    assertCodecResult(
        DeviceSettings::CodecResult::NULL_POINTER,
        DeviceSettings::decodeRecord(
            record,
            DeviceSettings::RECORD_SIZE,
            nullptr,
            &generation
        )
    );
    assertCodecResult(
        DeviceSettings::CodecResult::NULL_POINTER,
        DeviceSettings::decodeRecord(
            record,
            DeviceSettings::RECORD_SIZE,
            &settings,
            nullptr
        )
    );
    assertCodecResult(
        DeviceSettings::CodecResult::WRONG_LENGTH,
        DeviceSettings::decodeRecord(
            record, DeviceSettings::RECORD_SIZE - 1, settings, generation
        )
    );
    assertCodecResult(
        DeviceSettings::CodecResult::WRONG_LENGTH,
        DeviceSettings::decodeRecord(
            record, DeviceSettings::RECORD_SIZE + 1, settings, generation
        )
    );
}

void testDecodeRejectsWrongMagic() {
    uint8_t record[DeviceSettings::RECORD_SIZE] = {};
    encodeDefaults(record);
    record[0] ^= 0x01;
    DeviceSettings::Settings settings;
    uint32_t generation = 0;

    assertCodecResult(
        DeviceSettings::CodecResult::CORRUPT_MAGIC,
        DeviceSettings::decodeRecord(
            record, sizeof(record), settings, generation
        )
    );
}

void testDecodeRejectsWrongEmbeddedRecordLength() {
    uint8_t record[DeviceSettings::RECORD_SIZE] = {};
    encodeDefaults(record);
    DeviceSettings::writeUint16Le(record + 6, 23);
    updateRecordCrc(record);
    DeviceSettings::Settings settings;
    uint32_t generation = 0;

    assertCodecResult(
        DeviceSettings::CodecResult::CORRUPT_RECORD_LENGTH,
        DeviceSettings::decodeRecord(
            record, sizeof(record), settings, generation
        )
    );
}

void testDecodeRejectsBadCrc() {
    uint8_t record[DeviceSettings::RECORD_SIZE] = {};
    encodeDefaults(record);
    record[20] ^= 0x01;
    DeviceSettings::Settings settings;
    uint32_t generation = 0;

    assertCodecResult(
        DeviceSettings::CodecResult::CORRUPT_CRC,
        DeviceSettings::decodeRecord(
            record, sizeof(record), settings, generation
        )
    );
}

void testDecodeDistinguishesUnsupportedSchema() {
    uint8_t record[DeviceSettings::RECORD_SIZE] = {};
    encodeDefaults(record);
    DeviceSettings::writeUint16Le(record + 4, 2);
    updateRecordCrc(record);
    DeviceSettings::Settings settings;
    uint32_t generation = 0;

    assertCodecResult(
        DeviceSettings::CodecResult::UNSUPPORTED_SCHEMA,
        DeviceSettings::decodeRecord(
            record, sizeof(record), settings, generation
        )
    );
}

void testDecodeRejectsUnknownFlagBits() {
    uint8_t record[DeviceSettings::RECORD_SIZE] = {};
    encodeDefaults(record);
    record[16] |= 0x08;
    updateRecordCrc(record);
    DeviceSettings::Settings settings;
    uint32_t generation = 0;

    assertCodecResult(
        DeviceSettings::CodecResult::UNKNOWN_FLAGS,
        DeviceSettings::decodeRecord(
            record, sizeof(record), settings, generation
        )
    );
}

void testDecodeRejectsEveryNonzeroReservedByte() {
    for (size_t offset = 17; offset <= 19; ++offset) {
        uint8_t record[DeviceSettings::RECORD_SIZE] = {};
        encodeDefaults(record);
        record[offset] = 1;
        updateRecordCrc(record);
        DeviceSettings::Settings settings;
        uint32_t generation = 0;
        assertCodecResult(
            DeviceSettings::CodecResult::NONZERO_RESERVED,
            DeviceSettings::decodeRecord(
                record, sizeof(record), settings, generation
            )
        );
    }
}

void testDecodeRejectsInvalidTimeoutContrastAndScreen() {
    const size_t offsets[] = {12, 14, 15};
    const DeviceSettings::CodecResult expected[] = {
        DeviceSettings::CodecResult::INVALID_TIMEOUT,
        DeviceSettings::CodecResult::INVALID_CONTRAST,
        DeviceSettings::CodecResult::INVALID_SCREEN
    };

    for (size_t index = 0; index < 3; ++index) {
        uint8_t record[DeviceSettings::RECORD_SIZE] = {};
        encodeDefaults(record);
        record[offsets[index]] = index == 0 ? 1 : (index == 1 ? 15 : 6);
        if (index == 0) {
            record[13] = 0;
        }
        updateRecordCrc(record);
        DeviceSettings::Settings settings;
        uint32_t generation = 0;
        assertCodecResult(
            expected[index],
            DeviceSettings::decodeRecord(
                record, sizeof(record), settings, generation
            )
        );
    }
}

void testEveryProtectedByteDetectsUnrecomputedCorruption() {
    for (size_t offset = 0; offset < DeviceSettings::CRC_INPUT_SIZE; ++offset) {
        uint8_t record[DeviceSettings::RECORD_SIZE] = {};
        encodeDefaults(record);
        record[offset] ^= 0x80;
        DeviceSettings::Settings settings;
        uint32_t generation = 0;
        const DeviceSettings::CodecResult result =
            DeviceSettings::decodeRecord(
                record, sizeof(record), settings, generation
            );
        TEST_ASSERT_TRUE(
            result == DeviceSettings::CodecResult::CORRUPT_MAGIC ||
            result == DeviceSettings::CodecResult::CORRUPT_RECORD_LENGTH ||
            result == DeviceSettings::CodecResult::CORRUPT_CRC
        );
    }
}

void testDecodeFailureDoesNotModifyCallerOutputs() {
    uint8_t record[DeviceSettings::RECORD_SIZE] = {};
    encodeDefaults(record);
    record[20] ^= 1;
    DeviceSettings::Settings settings;
    settings.displayTimeoutSeconds = 60;
    settings.defaultScreen = DeviceSettings::DefaultScreen::ABOUT;
    const DeviceSettings::Settings before = settings;
    uint32_t generation = 0xA5A5A5A5U;

    assertCodecResult(
        DeviceSettings::CodecResult::CORRUPT_CRC,
        DeviceSettings::decodeRecord(
            record, sizeof(record), settings, generation
        )
    );
    TEST_ASSERT_TRUE(settings == before);
    TEST_ASSERT_EQUAL_HEX32(0xA5A5A5A5U, generation);
}

void testEncodingIgnoresCompilerPadding() {
    DeviceSettings::Settings first;
    DeviceSettings::Settings second;
    memset(&first, 0xAA, sizeof(first));
    memset(&second, 0x55, sizeof(second));

    first.displayTimeoutSeconds = second.displayTimeoutSeconds = 300;
    first.displayContrast = second.displayContrast = 64;
    first.ledEnabled = second.ledEnabled = true;
    first.diagnosticsEnabled = second.diagnosticsEnabled = false;
    first.defaultScreen = second.defaultScreen =
        DeviceSettings::DefaultScreen::DIAGNOSTICS;
    first.buttonFeedbackEnabled = second.buttonFeedbackEnabled = true;

    uint8_t firstRecord[DeviceSettings::RECORD_SIZE] = {};
    uint8_t secondRecord[DeviceSettings::RECORD_SIZE] = {};
    assertCodecResult(
        DeviceSettings::CodecResult::OK,
        DeviceSettings::encodeRecord(
            first, 7, firstRecord, sizeof(firstRecord)
        )
    );
    assertCodecResult(
        DeviceSettings::CodecResult::OK,
        DeviceSettings::encodeRecord(
            second, 7, secondRecord, sizeof(secondRecord)
        )
    );
    TEST_ASSERT_EQUAL_UINT8_ARRAY(
        firstRecord, secondRecord, DeviceSettings::RECORD_SIZE
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
    RUN_TEST(testKnownCrcVectorMatchesIsoHdlc);
    RUN_TEST(testCrcRejectsNullInputWithoutChangingOutput);
    RUN_TEST(testEncodeProducesExactKnownFixture);
    RUN_TEST(testEncodeDecodeRoundTripPreservesSettingsAndGeneration);
    RUN_TEST(testLittleEndianFieldOrderIsExplicit);
    RUN_TEST(testMaximumApprovedValuesRoundTrip);
    RUN_TEST(testEncodeRejectsNullAndNonExactOutputLengths);
    RUN_TEST(testEncodeRejectsEveryInvalidSettingClass);
    RUN_TEST(testDecodeRejectsNullShortAndOversizedInputs);
    RUN_TEST(testDecodeRejectsWrongMagic);
    RUN_TEST(testDecodeRejectsWrongEmbeddedRecordLength);
    RUN_TEST(testDecodeRejectsBadCrc);
    RUN_TEST(testDecodeDistinguishesUnsupportedSchema);
    RUN_TEST(testDecodeRejectsUnknownFlagBits);
    RUN_TEST(testDecodeRejectsEveryNonzeroReservedByte);
    RUN_TEST(testDecodeRejectsInvalidTimeoutContrastAndScreen);
    RUN_TEST(testEveryProtectedByteDetectsUnrecomputedCorruption);
    RUN_TEST(testDecodeFailureDoesNotModifyCallerOutputs);
    RUN_TEST(testEncodingIgnoresCompilerPadding);
    return UNITY_END();
}
