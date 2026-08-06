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

enum class FakeOperation : uint8_t {
    READ_A,
    READ_B,
    WRITE_A,
    WRITE_B,
    REMOVE_A,
    REMOVE_B,
    READ_MARKER,
    WRITE_MARKER,
    REMOVE_MARKER
};

class FakeRecordStore : public DeviceSettings::RecordStore {
public:
    struct SlotData {
        bool present = false;
        size_t length = 0;
        uint8_t bytes[DeviceSettings::RECORD_SIZE] = {};
    };

    DeviceSettings::StoreResult readSlot(
        DeviceSettings::RecordSlot slot,
        uint8_t* output,
        size_t capacity,
        size_t& length
    ) override {
        ++readCount;
        if (slot == DeviceSettings::RecordSlot::A) {
            ++readACount;
            recordOperation(FakeOperation::READ_A);
        } else {
            ++readBCount;
            recordOperation(FakeOperation::READ_B);
        }

        if (failReadAtCount != 0 && readCount == failReadAtCount) {
            length = 0;
            return failReadResult;
        }

        if (nextReadResult != DeviceSettings::StoreResult::OK) {
            const DeviceSettings::StoreResult result = nextReadResult;
            nextReadResult = DeviceSettings::StoreResult::OK;
            length = 0;
            return result;
        }

        const DeviceSettings::StoreResult configuredResult =
            slot == DeviceSettings::RecordSlot::A
                ? slotAReadResult
                : slotBReadResult;
        if (configuredResult != DeviceSettings::StoreResult::OK) {
            length = 0;
            return configuredResult;
        }

        if (replaceNextRead ||
            (replaceReadAtCount != 0 && readCount == replaceReadAtCount)) {
            replaceNextRead = false;
            length = replacement.length;
            copyToOutput(replacement, output, capacity);
            return DeviceSettings::StoreResult::OK;
        }

        const SlotData& source = data(slot);
        if (!source.present) {
            length = 0;
            return DeviceSettings::StoreResult::MISSING;
        }

        length = source.length;
        copyToOutput(source, output, capacity);
        return DeviceSettings::StoreResult::OK;
    }

    DeviceSettings::StoreResult writeSlot(
        DeviceSettings::RecordSlot slot,
        const uint8_t* input,
        size_t length
    ) override {
        ++writeCount;
        if (slot == DeviceSettings::RecordSlot::A) {
            ++writeACount;
            recordOperation(FakeOperation::WRITE_A);
        } else {
            ++writeBCount;
            recordOperation(FakeOperation::WRITE_B);
        }
        lastWrittenSlot = slot;
        lastWriteLength = length;

        if (writeResult != DeviceSettings::StoreResult::OK) {
            return writeResult;
        }

        SlotData& target = data(slot);
        if (slot == DeviceSettings::RecordSlot::A) {
            slotAReadResult = DeviceSettings::StoreResult::OK;
        } else {
            slotBReadResult = DeviceSettings::StoreResult::OK;
        }
        target.present = true;
        target.length = length;
        const size_t copyLength = length < DeviceSettings::RECORD_SIZE
            ? length
            : DeviceSettings::RECORD_SIZE;
        for (size_t index = 0; index < copyLength; ++index) {
            target.bytes[index] = input[index];
        }
        return DeviceSettings::StoreResult::OK;
    }

    DeviceSettings::StoreResult removeSlot(
        DeviceSettings::RecordSlot slot
    ) override {
        ++removeCount;
        if (slot == DeviceSettings::RecordSlot::A) {
            ++removeACount;
            recordOperation(FakeOperation::REMOVE_A);
            if (removeAResult != DeviceSettings::StoreResult::OK) {
                return removeAResult;
            }
            if (!slotA.present) {
                return DeviceSettings::StoreResult::MISSING;
            }
            if (!removeADeletes) {
                return DeviceSettings::StoreResult::OK;
            }
        } else {
            ++removeBCount;
            recordOperation(FakeOperation::REMOVE_B);
            if (removeBResult != DeviceSettings::StoreResult::OK) {
                return removeBResult;
            }
            if (!slotB.present) {
                return DeviceSettings::StoreResult::MISSING;
            }
            if (!removeBDeletes) {
                return DeviceSettings::StoreResult::OK;
            }
        }
        data(slot).present = false;
        data(slot).length = 0;
        return DeviceSettings::StoreResult::OK;
    }

    DeviceSettings::StoreResult readResetMarker(bool& pending) override {
        ++markerReadCount;
        recordOperation(FakeOperation::READ_MARKER);
        if (nextMarkerReadResult != DeviceSettings::StoreResult::OK) {
            const DeviceSettings::StoreResult result = nextMarkerReadResult;
            nextMarkerReadResult = DeviceSettings::StoreResult::OK;
            return result;
        }
        if (!markerPresent) {
            pending = false;
            return DeviceSettings::StoreResult::MISSING;
        }
        pending = markerValue;
        return DeviceSettings::StoreResult::OK;
    }

    DeviceSettings::StoreResult writeResetMarker(bool pending) override {
        ++markerWriteCount;
        recordOperation(FakeOperation::WRITE_MARKER);
        if (markerWriteResult != DeviceSettings::StoreResult::OK) {
            return markerWriteResult;
        }
        markerPresent = markerWritePersists;
        markerValue = markerWriteValueOverride
            ? false
            : pending;
        return DeviceSettings::StoreResult::OK;
    }

    DeviceSettings::StoreResult removeResetMarker() override {
        ++markerRemoveCount;
        recordOperation(FakeOperation::REMOVE_MARKER);
        if (markerRemoveResult != DeviceSettings::StoreResult::OK) {
            return markerRemoveResult;
        }
        if (markerRemovePersists) {
            markerPresent = false;
            markerValue = false;
        }
        return DeviceSettings::StoreResult::OK;
    }

    void setRecord(
        DeviceSettings::RecordSlot slot,
        const DeviceSettings::Settings& settings,
        uint32_t generation
    ) {
        SlotData& target = data(slot);
        target.present = true;
        target.length = DeviceSettings::RECORD_SIZE;
        assertCodecResult(
            DeviceSettings::CodecResult::OK,
            DeviceSettings::encodeRecord(
                settings,
                generation,
                target.bytes,
                sizeof(target.bytes)
            )
        );
    }

    void copySlot(
        DeviceSettings::RecordSlot destination,
        DeviceSettings::RecordSlot source
    ) {
        data(destination) = data(source);
    }

    SlotData& data(DeviceSettings::RecordSlot slot) {
        return slot == DeviceSettings::RecordSlot::A ? slotA : slotB;
    }

    const SlotData& data(DeviceSettings::RecordSlot slot) const {
        return slot == DeviceSettings::RecordSlot::A ? slotA : slotB;
    }

    void prepareReplacement(
        const DeviceSettings::Settings& settings,
        uint32_t generation
    ) {
        replacement.present = true;
        replacement.length = DeviceSettings::RECORD_SIZE;
        assertCodecResult(
            DeviceSettings::CodecResult::OK,
            DeviceSettings::encodeRecord(
                settings,
                generation,
                replacement.bytes,
                sizeof(replacement.bytes)
            )
        );
        replaceNextRead = true;
    }

    SlotData slotA;
    SlotData slotB;
    SlotData replacement;
    DeviceSettings::StoreResult writeResult =
        DeviceSettings::StoreResult::OK;
    DeviceSettings::StoreResult nextReadResult =
        DeviceSettings::StoreResult::OK;
    DeviceSettings::StoreResult slotAReadResult =
        DeviceSettings::StoreResult::OK;
    DeviceSettings::StoreResult slotBReadResult =
        DeviceSettings::StoreResult::OK;
    bool replaceNextRead = false;
    uint32_t failReadAtCount = 0;
    DeviceSettings::StoreResult failReadResult =
        DeviceSettings::StoreResult::ERROR;
    uint32_t replaceReadAtCount = 0;
    uint32_t readCount = 0;
    uint32_t readACount = 0;
    uint32_t readBCount = 0;
    uint32_t writeCount = 0;
    uint32_t writeACount = 0;
    uint32_t writeBCount = 0;
    uint32_t removeCount = 0;
    uint32_t removeACount = 0;
    uint32_t removeBCount = 0;
    uint32_t markerReadCount = 0;
    uint32_t markerWriteCount = 0;
    uint32_t markerRemoveCount = 0;
    bool markerPresent = false;
    bool markerValue = false;
    bool markerWritePersists = true;
    bool markerWriteValueOverride = false;
    bool markerRemovePersists = true;
    DeviceSettings::StoreResult markerWriteResult =
        DeviceSettings::StoreResult::OK;
    DeviceSettings::StoreResult nextMarkerReadResult =
        DeviceSettings::StoreResult::OK;
    DeviceSettings::StoreResult markerRemoveResult =
        DeviceSettings::StoreResult::OK;
    DeviceSettings::StoreResult removeAResult =
        DeviceSettings::StoreResult::OK;
    DeviceSettings::StoreResult removeBResult =
        DeviceSettings::StoreResult::OK;
    bool removeADeletes = true;
    bool removeBDeletes = true;
    FakeOperation operations[64] = {};
    size_t operationCount = 0;
    DeviceSettings::RecordSlot lastWrittenSlot =
        DeviceSettings::RecordSlot::A;
    size_t lastWriteLength = 0;

private:
    void recordOperation(FakeOperation operation) {
        if (operationCount < sizeof(operations) / sizeof(operations[0])) {
            operations[operationCount++] = operation;
        }
    }

    static void copyToOutput(
        const SlotData& source,
        uint8_t* output,
        size_t capacity
    ) {
        const size_t copyLength = source.length < capacity
            ? source.length
            : capacity;
        for (size_t index = 0; index < copyLength; ++index) {
            output[index] = source.bytes[index];
        }
    }
};

void assertLoadStatus(
    DeviceSettings::LoadStatus expected,
    DeviceSettings::LoadStatus actual
) {
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(expected),
        static_cast<uint8_t>(actual)
    );
}

void assertSaveStatus(
    DeviceSettings::SaveStatus expected,
    DeviceSettings::SaveStatus actual
) {
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(expected),
        static_cast<uint8_t>(actual)
    );
}

void assertResetResult(
    DeviceSettings::ResetResult expected,
    DeviceSettings::ResetResult actual
) {
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(expected),
        static_cast<uint8_t>(actual)
    );
}

void assertMigrationResult(
    DeviceSettings::MigrationResult expected,
    DeviceSettings::MigrationResult actual
) {
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(expected),
        static_cast<uint8_t>(actual)
    );
}

DeviceSettings::Settings settingsWithTimeout(uint16_t timeout) {
    DeviceSettings::Settings settings;
    settings.displayTimeoutSeconds = timeout;
    return settings;
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

void testLoadNoSlotsDefaultsWithoutWriting() {
    FakeRecordStore store;
    DeviceSettings::SettingsManager manager;

    assertLoadStatus(
        DeviceSettings::LoadStatus::DEFAULTED_MISSING,
        manager.load(store)
    );
    TEST_ASSERT_TRUE(manager.settings() == DeviceSettings::defaults());
    TEST_ASSERT_FALSE(manager.hasActiveSlot());
    TEST_ASSERT_FALSE(manager.repairPending());
    TEST_ASSERT_EQUAL_UINT32(2, store.readCount);
    TEST_ASSERT_EQUAL_UINT32(0, store.writeCount);
}

void testLoadSlotAOnly() {
    FakeRecordStore store;
    const DeviceSettings::Settings expected = settingsWithTimeout(60);
    store.setRecord(DeviceSettings::RecordSlot::A, expected, 7);
    DeviceSettings::SettingsManager manager;

    assertLoadStatus(DeviceSettings::LoadStatus::LOADED, manager.load(store));
    TEST_ASSERT_TRUE(manager.settings() == expected);
    TEST_ASSERT_EQUAL_UINT32(7, manager.generation());
    TEST_ASSERT_TRUE(manager.hasActiveSlot());
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(DeviceSettings::RecordSlot::A),
        static_cast<uint8_t>(manager.activeSlot())
    );
}

void testLoadSlotBOnly() {
    FakeRecordStore store;
    const DeviceSettings::Settings expected = settingsWithTimeout(120);
    store.setRecord(DeviceSettings::RecordSlot::B, expected, 8);
    DeviceSettings::SettingsManager manager;

    assertLoadStatus(DeviceSettings::LoadStatus::LOADED, manager.load(store));
    TEST_ASSERT_TRUE(manager.settings() == expected);
    TEST_ASSERT_EQUAL_UINT32(8, manager.generation());
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(DeviceSettings::RecordSlot::B),
        static_cast<uint8_t>(manager.activeSlot())
    );
}

void testTwoValidSlotsSelectNewerA() {
    FakeRecordStore store;
    store.setRecord(
        DeviceSettings::RecordSlot::A, settingsWithTimeout(120), 11
    );
    store.setRecord(
        DeviceSettings::RecordSlot::B, settingsWithTimeout(60), 10
    );
    DeviceSettings::SettingsManager manager;

    assertLoadStatus(DeviceSettings::LoadStatus::LOADED, manager.load(store));
    TEST_ASSERT_EQUAL_UINT16(120, manager.settings().displayTimeoutSeconds);
    TEST_ASSERT_EQUAL_UINT32(11, manager.generation());
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(DeviceSettings::RecordSlot::A),
        static_cast<uint8_t>(manager.activeSlot())
    );
}

void testTwoValidSlotsSelectNewerB() {
    FakeRecordStore store;
    store.setRecord(
        DeviceSettings::RecordSlot::A, settingsWithTimeout(60), 10
    );
    store.setRecord(
        DeviceSettings::RecordSlot::B, settingsWithTimeout(120), 11
    );
    DeviceSettings::SettingsManager manager;

    assertLoadStatus(DeviceSettings::LoadStatus::LOADED, manager.load(store));
    TEST_ASSERT_EQUAL_UINT16(120, manager.settings().displayTimeoutSeconds);
    TEST_ASSERT_EQUAL_UINT32(11, manager.generation());
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(DeviceSettings::RecordSlot::B),
        static_cast<uint8_t>(manager.activeSlot())
    );
}

void testGenerationComparisonAndSelectionHandleRollover() {
    TEST_ASSERT_TRUE(DeviceSettings::generationIsNewer(0, UINT32_MAX));
    TEST_ASSERT_FALSE(DeviceSettings::generationIsNewer(UINT32_MAX, 0));

    FakeRecordStore store;
    store.setRecord(
        DeviceSettings::RecordSlot::A,
        settingsWithTimeout(60),
        UINT32_MAX
    );
    store.setRecord(
        DeviceSettings::RecordSlot::B, settingsWithTimeout(120), 0
    );
    DeviceSettings::SettingsManager manager;
    assertLoadStatus(DeviceSettings::LoadStatus::LOADED, manager.load(store));
    TEST_ASSERT_EQUAL_UINT16(120, manager.settings().displayTimeoutSeconds);
    TEST_ASSERT_EQUAL_UINT32(0, manager.generation());
}

void testEqualGenerationIdenticalRecordsLoadDeterministically() {
    FakeRecordStore store;
    store.setRecord(
        DeviceSettings::RecordSlot::A, settingsWithTimeout(60), 15
    );
    store.copySlot(DeviceSettings::RecordSlot::B, DeviceSettings::RecordSlot::A);
    DeviceSettings::SettingsManager manager;

    assertLoadStatus(DeviceSettings::LoadStatus::LOADED, manager.load(store));
    TEST_ASSERT_EQUAL_UINT16(60, manager.settings().displayTimeoutSeconds);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(DeviceSettings::RecordSlot::A),
        static_cast<uint8_t>(manager.activeSlot())
    );
}

void testEqualGenerationDifferentRecordsDefaultAsCorrupt() {
    FakeRecordStore store;
    store.setRecord(
        DeviceSettings::RecordSlot::A, settingsWithTimeout(60), 15
    );
    store.setRecord(
        DeviceSettings::RecordSlot::B, settingsWithTimeout(120), 15
    );
    DeviceSettings::SettingsManager manager;

    assertLoadStatus(
        DeviceSettings::LoadStatus::DEFAULTED_CORRUPT,
        manager.load(store)
    );
    TEST_ASSERT_TRUE(manager.settings() == DeviceSettings::defaults());
    TEST_ASSERT_FALSE(manager.hasActiveSlot());
    TEST_ASSERT_TRUE(manager.repairPending());
    TEST_ASSERT_EQUAL_UINT32(0, store.writeCount);
}

void testCorruptNewerSlotFallsBackToOlderValidSlot() {
    FakeRecordStore store;
    const DeviceSettings::Settings older = settingsWithTimeout(60);
    store.setRecord(DeviceSettings::RecordSlot::A, older, 20);
    store.setRecord(
        DeviceSettings::RecordSlot::B, settingsWithTimeout(120), 21
    );
    store.slotB.bytes[20] ^= 1;
    DeviceSettings::SettingsManager manager;

    assertLoadStatus(
        DeviceSettings::LoadStatus::LOADED_FALLBACK_SLOT,
        manager.load(store)
    );
    TEST_ASSERT_TRUE(manager.settings() == older);
    TEST_ASSERT_EQUAL_UINT32(20, manager.generation());
    TEST_ASSERT_TRUE(manager.repairPending());
    TEST_ASSERT_EQUAL_UINT32(0, store.writeCount);
}

void testBothCorruptDefaultWithoutAutomaticWrite() {
    FakeRecordStore store;
    store.setRecord(
        DeviceSettings::RecordSlot::A, settingsWithTimeout(60), 1
    );
    store.setRecord(
        DeviceSettings::RecordSlot::B, settingsWithTimeout(120), 2
    );
    store.slotA.bytes[20] ^= 1;
    store.slotB.bytes[21] ^= 1;
    DeviceSettings::SettingsManager manager;

    assertLoadStatus(
        DeviceSettings::LoadStatus::DEFAULTED_CORRUPT,
        manager.load(store)
    );
    TEST_ASSERT_TRUE(manager.settings() == DeviceSettings::defaults());
    TEST_ASSERT_TRUE(manager.repairPending());
    TEST_ASSERT_EQUAL_UINT32(0, store.writeCount);
}

void testValidAWithMalformedBLoadsAAsFallback() {
    FakeRecordStore store;
    const DeviceSettings::Settings valid = settingsWithTimeout(60);
    store.setRecord(DeviceSettings::RecordSlot::A, valid, 10);
    store.slotBReadResult = DeviceSettings::StoreResult::MALFORMED;
    DeviceSettings::SettingsManager manager;

    assertLoadStatus(
        DeviceSettings::LoadStatus::LOADED_FALLBACK_SLOT,
        manager.load(store)
    );
    TEST_ASSERT_TRUE(manager.settings() == valid);
    TEST_ASSERT_EQUAL_UINT32(10, manager.generation());
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(DeviceSettings::RecordSlot::A),
        static_cast<uint8_t>(manager.activeSlot())
    );
    TEST_ASSERT_TRUE(manager.repairPending());
    TEST_ASSERT_EQUAL_UINT32(0, store.writeCount);
}

void testMalformedAWithValidBLoadsBAsFallback() {
    FakeRecordStore store;
    const DeviceSettings::Settings valid = settingsWithTimeout(120);
    store.slotAReadResult = DeviceSettings::StoreResult::MALFORMED;
    store.setRecord(DeviceSettings::RecordSlot::B, valid, 10);
    DeviceSettings::SettingsManager manager;

    assertLoadStatus(
        DeviceSettings::LoadStatus::LOADED_FALLBACK_SLOT,
        manager.load(store)
    );
    TEST_ASSERT_TRUE(manager.settings() == valid);
    TEST_ASSERT_EQUAL_UINT32(10, manager.generation());
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(DeviceSettings::RecordSlot::B),
        static_cast<uint8_t>(manager.activeSlot())
    );
    TEST_ASSERT_TRUE(manager.repairPending());
    TEST_ASSERT_EQUAL_UINT32(0, store.writeCount);
}

void testBothMalformedSlotsDefaultAsCorrupt() {
    FakeRecordStore store;
    store.slotAReadResult = DeviceSettings::StoreResult::MALFORMED;
    store.slotBReadResult = DeviceSettings::StoreResult::MALFORMED;
    DeviceSettings::SettingsManager manager;

    assertLoadStatus(
        DeviceSettings::LoadStatus::DEFAULTED_CORRUPT,
        manager.load(store)
    );
    TEST_ASSERT_TRUE(manager.settings() == DeviceSettings::defaults());
    TEST_ASSERT_FALSE(manager.hasActiveSlot());
    TEST_ASSERT_TRUE(manager.repairPending());
    TEST_ASSERT_EQUAL_UINT32(0, store.writeCount);
}

void testExactSizeCrcInvalidSlotRemainsCorruptNotUnavailable() {
    FakeRecordStore store;
    const DeviceSettings::Settings valid = settingsWithTimeout(60);
    store.setRecord(DeviceSettings::RecordSlot::A, valid, 10);
    store.setRecord(
        DeviceSettings::RecordSlot::B,
        settingsWithTimeout(120),
        11
    );
    TEST_ASSERT_EQUAL_UINT32(
        DeviceSettings::RECORD_SIZE,
        store.slotB.length
    );
    store.slotB.bytes[20] ^= 1U;
    DeviceSettings::SettingsManager manager;

    assertLoadStatus(
        DeviceSettings::LoadStatus::LOADED_FALLBACK_SLOT,
        manager.load(store)
    );
    TEST_ASSERT_TRUE(manager.settings() == valid);
    TEST_ASSERT_EQUAL_UINT32(10, manager.generation());
    TEST_ASSERT_TRUE(manager.repairPending());
}

void testValidSchemaRecordRepairsAllInvalidFieldsInRam() {
    FakeRecordStore store;
    DeviceSettings::Settings stored;
    stored.ledEnabled = false;
    stored.diagnosticsEnabled = false;
    stored.buttonFeedbackEnabled = true;
    store.setRecord(DeviceSettings::RecordSlot::A, stored, 9);
    store.slotA.bytes[12] = 1;
    store.slotA.bytes[13] = 0;
    store.slotA.bytes[14] = 15;
    store.slotA.bytes[15] = 6;
    updateRecordCrc(store.slotA.bytes);
    DeviceSettings::SettingsManager manager;

    assertLoadStatus(
        DeviceSettings::LoadStatus::REPAIRED_SCHEMA_1,
        manager.load(store)
    );
    TEST_ASSERT_EQUAL_UINT16(30, manager.settings().displayTimeoutSeconds);
    TEST_ASSERT_EQUAL_UINT8(207, manager.settings().displayContrast);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(DeviceSettings::DefaultScreen::HOME),
        static_cast<uint8_t>(manager.settings().defaultScreen)
    );
    TEST_ASSERT_FALSE(manager.settings().ledEnabled);
    TEST_ASSERT_FALSE(manager.settings().diagnosticsEnabled);
    TEST_ASSERT_TRUE(manager.settings().buttonFeedbackEnabled);
    TEST_ASSERT_TRUE(manager.repairPending());
    TEST_ASSERT_EQUAL_UINT32(0, store.writeCount);
}

void testSupportedAWithUnsupportedBLoadsA() {
    FakeRecordStore store;
    const DeviceSettings::Settings supported = settingsWithTimeout(120);
    store.setRecord(DeviceSettings::RecordSlot::A, supported, 19);
    store.setRecord(
        DeviceSettings::RecordSlot::B, settingsWithTimeout(60), 20
    );
    DeviceSettings::writeUint16Le(store.slotB.bytes + 4, 2);
    updateRecordCrc(store.slotB.bytes);
    DeviceSettings::SettingsManager manager;

    assertLoadStatus(DeviceSettings::LoadStatus::LOADED, manager.load(store));
    TEST_ASSERT_TRUE(manager.settings() == supported);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(DeviceSettings::RecordSlot::A),
        static_cast<uint8_t>(manager.activeSlot())
    );
    TEST_ASSERT_TRUE(manager.unsupportedSchema());
    TEST_ASSERT_FALSE(
        manager.unsupportedSlotPresent(DeviceSettings::RecordSlot::A)
    );
    TEST_ASSERT_TRUE(
        manager.unsupportedSlotPresent(DeviceSettings::RecordSlot::B)
    );
    TEST_ASSERT_EQUAL_UINT32(0, store.writeCount);
}

void testUnsupportedAWithSupportedBLoadsB() {
    FakeRecordStore store;
    const DeviceSettings::Settings supported = settingsWithTimeout(120);
    store.setRecord(
        DeviceSettings::RecordSlot::A, settingsWithTimeout(60), 20
    );
    DeviceSettings::writeUint16Le(store.slotA.bytes + 4, 2);
    updateRecordCrc(store.slotA.bytes);
    store.setRecord(DeviceSettings::RecordSlot::B, supported, 19);
    DeviceSettings::SettingsManager manager;

    assertLoadStatus(DeviceSettings::LoadStatus::LOADED, manager.load(store));
    TEST_ASSERT_TRUE(manager.settings() == supported);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(DeviceSettings::RecordSlot::B),
        static_cast<uint8_t>(manager.activeSlot())
    );
    TEST_ASSERT_TRUE(
        manager.unsupportedSlotPresent(DeviceSettings::RecordSlot::A)
    );
    TEST_ASSERT_FALSE(
        manager.unsupportedSlotPresent(DeviceSettings::RecordSlot::B)
    );
    TEST_ASSERT_EQUAL_UINT32(0, store.writeCount);
}

void testUnsupportedInactiveSlotIsNeverSelectedAsSaveTarget() {
    const DeviceSettings::RecordSlot unsupportedSlots[] = {
        DeviceSettings::RecordSlot::A,
        DeviceSettings::RecordSlot::B
    };

    for (const DeviceSettings::RecordSlot unsupportedSlot : unsupportedSlots) {
        FakeRecordStore store;
        const DeviceSettings::RecordSlot supportedSlot =
            unsupportedSlot == DeviceSettings::RecordSlot::A
                ? DeviceSettings::RecordSlot::B
                : DeviceSettings::RecordSlot::A;
        store.setRecord(unsupportedSlot, settingsWithTimeout(60), 20);
        DeviceSettings::writeUint16Le(
            store.data(unsupportedSlot).bytes + 4, 2
        );
        updateRecordCrc(store.data(unsupportedSlot).bytes);
        store.setRecord(supportedSlot, settingsWithTimeout(120), 19);
        uint8_t unsupportedBefore[DeviceSettings::RECORD_SIZE] = {};
        memcpy(
            unsupportedBefore,
            store.data(unsupportedSlot).bytes,
            sizeof(unsupportedBefore)
        );
        DeviceSettings::SettingsManager manager;
        manager.load(store);

        assertSaveStatus(
            DeviceSettings::SaveStatus::UNSUPPORTED_SCHEMA,
            manager.save(store, settingsWithTimeout(300))
        );
        TEST_ASSERT_EQUAL_UINT32(0, store.writeCount);
        TEST_ASSERT_TRUE(manager.settings() == settingsWithTimeout(120));
        TEST_ASSERT_EQUAL_UINT8_ARRAY(
            unsupportedBefore,
            store.data(unsupportedSlot).bytes,
            sizeof(unsupportedBefore)
        );
    }
}

void testUnsupportedSchemaInBothSlotsIsPreserved() {
    FakeRecordStore store;
    store.setRecord(
        DeviceSettings::RecordSlot::A, settingsWithTimeout(60), 1
    );
    store.setRecord(
        DeviceSettings::RecordSlot::B, settingsWithTimeout(120), 2
    );
    DeviceSettings::writeUint16Le(store.slotA.bytes + 4, 2);
    DeviceSettings::writeUint16Le(store.slotB.bytes + 4, 3);
    updateRecordCrc(store.slotA.bytes);
    updateRecordCrc(store.slotB.bytes);
    uint8_t beforeA[DeviceSettings::RECORD_SIZE] = {};
    uint8_t beforeB[DeviceSettings::RECORD_SIZE] = {};
    memcpy(beforeA, store.slotA.bytes, sizeof(beforeA));
    memcpy(beforeB, store.slotB.bytes, sizeof(beforeB));
    DeviceSettings::SettingsManager manager;

    assertLoadStatus(
        DeviceSettings::LoadStatus::UNSUPPORTED_SCHEMA,
        manager.load(store)
    );
    assertSaveStatus(
        DeviceSettings::SaveStatus::UNSUPPORTED_SCHEMA,
        manager.save(store, DeviceSettings::defaults())
    );
    TEST_ASSERT_EQUAL_UINT8_ARRAY(beforeA, store.slotA.bytes, sizeof(beforeA));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(beforeB, store.slotB.bytes, sizeof(beforeB));
    TEST_ASSERT_EQUAL_UINT32(0, store.writeCount);
}

void testUnsupportedAndMissingDefaultsAndBlocksSave() {
    const DeviceSettings::RecordSlot unsupportedSlots[] = {
        DeviceSettings::RecordSlot::A,
        DeviceSettings::RecordSlot::B
    };
    for (const DeviceSettings::RecordSlot unsupportedSlot : unsupportedSlots) {
        FakeRecordStore store;
        store.setRecord(unsupportedSlot, settingsWithTimeout(60), 1);
        DeviceSettings::writeUint16Le(
            store.data(unsupportedSlot).bytes + 4, 2
        );
        updateRecordCrc(store.data(unsupportedSlot).bytes);
        uint8_t before[DeviceSettings::RECORD_SIZE] = {};
        memcpy(before, store.data(unsupportedSlot).bytes, sizeof(before));
        DeviceSettings::SettingsManager manager;

        assertLoadStatus(
            DeviceSettings::LoadStatus::UNSUPPORTED_SCHEMA,
            manager.load(store)
        );
        TEST_ASSERT_TRUE(manager.settings() == DeviceSettings::defaults());
        assertSaveStatus(
            DeviceSettings::SaveStatus::UNSUPPORTED_SCHEMA,
            manager.save(store, settingsWithTimeout(60))
        );
        TEST_ASSERT_EQUAL_UINT32(0, store.writeCount);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(
            before, store.data(unsupportedSlot).bytes, sizeof(before)
        );
    }
}

void testUnsupportedAndCorruptDefaultsAndBlocksSave() {
    FakeRecordStore store;
    store.setRecord(
        DeviceSettings::RecordSlot::A, settingsWithTimeout(60), 2
    );
    DeviceSettings::writeUint16Le(store.slotA.bytes + 4, 2);
    updateRecordCrc(store.slotA.bytes);
    store.setRecord(
        DeviceSettings::RecordSlot::B, settingsWithTimeout(120), 1
    );
    store.slotB.bytes[20] ^= 1;
    uint8_t unsupportedBefore[DeviceSettings::RECORD_SIZE] = {};
    memcpy(unsupportedBefore, store.slotA.bytes, sizeof(unsupportedBefore));
    DeviceSettings::SettingsManager manager;

    assertLoadStatus(
        DeviceSettings::LoadStatus::UNSUPPORTED_SCHEMA,
        manager.load(store)
    );
    TEST_ASSERT_TRUE(manager.settings() == DeviceSettings::defaults());
    assertSaveStatus(
        DeviceSettings::SaveStatus::UNSUPPORTED_SCHEMA,
        manager.save(store, settingsWithTimeout(60))
    );
    TEST_ASSERT_EQUAL_UINT32(0, store.writeCount);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(
        unsupportedBefore, store.slotA.bytes, sizeof(unsupportedBefore)
    );
}

void testStorageUnavailableReadsBothSlotsAndDefaults() {
    FakeRecordStore store;
    store.nextReadResult = DeviceSettings::StoreResult::UNAVAILABLE;
    DeviceSettings::SettingsManager manager;

    assertLoadStatus(
        DeviceSettings::LoadStatus::STORAGE_UNAVAILABLE,
        manager.load(store)
    );
    TEST_ASSERT_TRUE(manager.settings() == DeviceSettings::defaults());
    TEST_ASSERT_EQUAL_UINT32(2, store.readCount);
    TEST_ASSERT_EQUAL_UINT32(1, store.readACount);
    TEST_ASSERT_EQUAL_UINT32(1, store.readBCount);
    TEST_ASSERT_EQUAL_UINT32(0, store.writeCount);
}

void testStorageUnavailableFromSlotBRemainsUnavailable() {
    FakeRecordStore store;
    store.setRecord(
        DeviceSettings::RecordSlot::A,
        settingsWithTimeout(60),
        10
    );
    store.slotBReadResult = DeviceSettings::StoreResult::UNAVAILABLE;
    DeviceSettings::SettingsManager manager;

    assertLoadStatus(
        DeviceSettings::LoadStatus::STORAGE_UNAVAILABLE,
        manager.load(store)
    );
    TEST_ASSERT_TRUE(manager.settings() == DeviceSettings::defaults());
    TEST_ASSERT_FALSE(manager.hasActiveSlot());
    TEST_ASSERT_EQUAL_UINT32(1, store.readACount);
    TEST_ASSERT_EQUAL_UINT32(1, store.readBCount);
    TEST_ASSERT_EQUAL_UINT32(0, store.writeCount);
}

void testFallbackRepairSaveCanonicalizesInactiveSlot() {
    FakeRecordStore store;
    const DeviceSettings::Settings valid = settingsWithTimeout(60);
    store.setRecord(DeviceSettings::RecordSlot::A, valid, 10);
    store.slotBReadResult = DeviceSettings::StoreResult::MALFORMED;
    DeviceSettings::SettingsManager manager;

    assertLoadStatus(
        DeviceSettings::LoadStatus::LOADED_FALLBACK_SLOT,
        manager.load(store)
    );
    TEST_ASSERT_TRUE(manager.settings() == valid);
    TEST_ASSERT_EQUAL_UINT32(10, manager.generation());
    TEST_ASSERT_TRUE(manager.repairPending());

    assertSaveStatus(
        DeviceSettings::SaveStatus::SAVED,
        manager.save(store, manager.settings())
    );
    TEST_ASSERT_EQUAL_UINT32(1, store.writeCount);
    TEST_ASSERT_EQUAL_UINT32(0, store.writeACount);
    TEST_ASSERT_EQUAL_UINT32(1, store.writeBCount);
    TEST_ASSERT_EQUAL_UINT32(11, manager.generation());
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(DeviceSettings::RecordSlot::B),
        static_cast<uint8_t>(manager.activeSlot())
    );
    TEST_ASSERT_FALSE(manager.repairPending());

    DeviceSettings::SettingsManager reloaded;
    assertLoadStatus(
        DeviceSettings::LoadStatus::LOADED,
        reloaded.load(store)
    );
    TEST_ASSERT_TRUE(reloaded.settings() == valid);
    TEST_ASSERT_EQUAL_UINT32(11, reloaded.generation());
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(DeviceSettings::RecordSlot::B),
        static_cast<uint8_t>(reloaded.activeSlot())
    );
    TEST_ASSERT_FALSE(reloaded.repairPending());
}

void testUnchangedSavePerformsZeroStorageOperations() {
    FakeRecordStore store;
    store.setRecord(
        DeviceSettings::RecordSlot::A, settingsWithTimeout(60), 4
    );
    DeviceSettings::SettingsManager manager;
    manager.load(store);
    const uint32_t readsBefore = store.readCount;

    assertSaveStatus(
        DeviceSettings::SaveStatus::UNCHANGED,
        manager.save(store, settingsWithTimeout(60))
    );
    TEST_ASSERT_EQUAL_UINT32(0, store.writeCount);
    TEST_ASSERT_EQUAL_UINT32(readsBefore, store.readCount);
}

void testChangedSaveWritesInactiveSlotOnceAndVerifiesReadBack() {
    FakeRecordStore store;
    store.setRecord(
        DeviceSettings::RecordSlot::A, settingsWithTimeout(60), 4
    );
    DeviceSettings::SettingsManager manager;
    manager.load(store);
    const uint32_t readsBefore = store.readCount;
    const DeviceSettings::Settings changed = settingsWithTimeout(120);

    assertSaveStatus(
        DeviceSettings::SaveStatus::SAVED,
        manager.save(store, changed)
    );
    TEST_ASSERT_EQUAL_UINT32(1, store.writeCount);
    TEST_ASSERT_EQUAL_UINT32(0, store.writeACount);
    TEST_ASSERT_EQUAL_UINT32(1, store.writeBCount);
    TEST_ASSERT_EQUAL_UINT32(readsBefore + 1, store.readCount);
    TEST_ASSERT_EQUAL_UINT32(DeviceSettings::RECORD_SIZE, store.lastWriteLength);
    TEST_ASSERT_TRUE(manager.settings() == changed);
    TEST_ASSERT_EQUAL_UINT32(5, manager.generation());
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(DeviceSettings::RecordSlot::B),
        static_cast<uint8_t>(manager.activeSlot())
    );
    TEST_ASSERT_TRUE(store.slotA.present);
}

void testWriteFailurePreservesPreviousAuthoritativeState() {
    FakeRecordStore store;
    const DeviceSettings::Settings original = settingsWithTimeout(60);
    store.setRecord(DeviceSettings::RecordSlot::A, original, 4);
    DeviceSettings::SettingsManager manager;
    manager.load(store);
    store.writeResult = DeviceSettings::StoreResult::ERROR;

    assertSaveStatus(
        DeviceSettings::SaveStatus::WRITE_FAILED,
        manager.save(store, settingsWithTimeout(120))
    );
    TEST_ASSERT_TRUE(manager.settings() == original);
    TEST_ASSERT_EQUAL_UINT32(4, manager.generation());
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(DeviceSettings::RecordSlot::A),
        static_cast<uint8_t>(manager.activeSlot())
    );
    TEST_ASSERT_EQUAL_UINT32(1, store.writeCount);
}

void testReadBackFailurePreservesPreviousAuthoritativeState() {
    FakeRecordStore store;
    const DeviceSettings::Settings original = settingsWithTimeout(60);
    store.setRecord(DeviceSettings::RecordSlot::A, original, 4);
    DeviceSettings::SettingsManager manager;
    manager.load(store);
    store.nextReadResult = DeviceSettings::StoreResult::ERROR;

    assertSaveStatus(
        DeviceSettings::SaveStatus::READ_BACK_FAILED,
        manager.save(store, settingsWithTimeout(120))
    );
    TEST_ASSERT_TRUE(manager.settings() == original);
    TEST_ASSERT_EQUAL_UINT32(4, manager.generation());
    TEST_ASSERT_EQUAL_UINT32(1, store.writeCount);
}

void testReadBackMismatchPreservesPreviousAuthoritativeState() {
    FakeRecordStore store;
    const DeviceSettings::Settings original = settingsWithTimeout(60);
    store.setRecord(DeviceSettings::RecordSlot::A, original, 4);
    DeviceSettings::SettingsManager manager;
    manager.load(store);
    store.prepareReplacement(settingsWithTimeout(300), 5);

    assertSaveStatus(
        DeviceSettings::SaveStatus::VERIFICATION_FAILED,
        manager.save(store, settingsWithTimeout(120))
    );
    TEST_ASSERT_TRUE(manager.settings() == original);
    TEST_ASSERT_EQUAL_UINT32(4, manager.generation());
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(DeviceSettings::RecordSlot::A),
        static_cast<uint8_t>(manager.activeSlot())
    );
    TEST_ASSERT_EQUAL_UINT32(1, store.writeCount);
}

void testMultipleDraftChangesProduceOneCompleteRecordWrite() {
    FakeRecordStore store;
    DeviceSettings::SettingsManager manager;
    manager.load(store);
    DeviceSettings::Settings draft = manager.settings();
    draft.displayTimeoutSeconds = 120;
    draft.displayContrast = 64;
    draft.ledEnabled = false;
    draft.diagnosticsEnabled = false;
    draft.defaultScreen = DeviceSettings::DefaultScreen::ABOUT;
    draft.buttonFeedbackEnabled = true;

    assertSaveStatus(
        DeviceSettings::SaveStatus::SAVED,
        manager.save(store, draft)
    );
    TEST_ASSERT_EQUAL_UINT32(1, store.writeCount);
    TEST_ASSERT_EQUAL_UINT32(DeviceSettings::RECORD_SIZE, store.lastWriteLength);
    DeviceSettings::Settings decoded;
    uint32_t generation = 0;
    assertCodecResult(
        DeviceSettings::CodecResult::OK,
        DeviceSettings::decodeRecord(
            store.slotA.bytes,
            store.slotA.length,
            decoded,
            generation
        )
    );
    TEST_ASSERT_TRUE(decoded == draft);
    TEST_ASSERT_EQUAL_UINT32(1, generation);
}

void testSaveCanonicalizesInvalidCandidateInOneWrite() {
    FakeRecordStore store;
    DeviceSettings::SettingsManager manager;
    manager.load(store);
    DeviceSettings::Settings candidate = settingsWithTimeout(1);
    candidate.displayContrast = 15;
    candidate.defaultScreen = static_cast<DeviceSettings::DefaultScreen>(6);
    candidate.ledEnabled = false;

    assertSaveStatus(
        DeviceSettings::SaveStatus::REPAIRED_SAVED,
        manager.save(store, candidate)
    );
    TEST_ASSERT_EQUAL_UINT16(30, manager.settings().displayTimeoutSeconds);
    TEST_ASSERT_EQUAL_UINT8(207, manager.settings().displayContrast);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(DeviceSettings::DefaultScreen::HOME),
        static_cast<uint8_t>(manager.settings().defaultScreen)
    );
    TEST_ASSERT_FALSE(manager.settings().ledEnabled);
    TEST_ASSERT_EQUAL_UINT32(1, store.writeCount);
}

void testSaveReportsRepairedUnchangedWithoutWriting() {
    FakeRecordStore store;
    DeviceSettings::SettingsManager manager;
    manager.load(store);
    DeviceSettings::Settings candidate = DeviceSettings::defaults();
    candidate.displayTimeoutSeconds = 1;
    candidate.displayContrast = 15;
    candidate.defaultScreen = static_cast<DeviceSettings::DefaultScreen>(6);

    assertSaveStatus(
        DeviceSettings::SaveStatus::REPAIRED_UNCHANGED,
        manager.save(store, candidate)
    );
    TEST_ASSERT_TRUE(manager.settings() == DeviceSettings::defaults());
    TEST_ASSERT_EQUAL_UINT32(0, store.writeCount);
}

void testSaveStorageUnavailableDoesNotRetryOrChangeState() {
    FakeRecordStore store;
    DeviceSettings::SettingsManager manager;
    manager.load(store);
    store.writeResult = DeviceSettings::StoreResult::UNAVAILABLE;

    assertSaveStatus(
        DeviceSettings::SaveStatus::STORAGE_UNAVAILABLE,
        manager.save(store, settingsWithTimeout(60))
    );
    TEST_ASSERT_EQUAL_UINT32(1, store.writeCount);
    TEST_ASSERT_TRUE(manager.settings() == DeviceSettings::defaults());
    TEST_ASSERT_FALSE(manager.hasActiveSlot());
}

void testNoResetMarkerContinuesOrdinaryLoad() {
    FakeRecordStore store;
    const DeviceSettings::Settings expected = settingsWithTimeout(60);
    store.setRecord(DeviceSettings::RecordSlot::A, expected, 4);
    DeviceSettings::SettingsManager manager;

    assertLoadStatus(DeviceSettings::LoadStatus::LOADED, manager.load(store));
    TEST_ASSERT_TRUE(manager.settings() == expected);
    TEST_ASSERT_EQUAL_UINT32(1, store.markerReadCount);
    TEST_ASSERT_EQUAL_UINT32(2, store.readCount);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(FakeOperation::READ_MARKER),
        static_cast<uint8_t>(store.operations[0])
    );
}

void testExplicitFactoryResetCompletesCanonicalSequence() {
    FakeRecordStore store;
    store.setRecord(
        DeviceSettings::RecordSlot::A, settingsWithTimeout(60), 7
    );
    store.setRecord(
        DeviceSettings::RecordSlot::B, settingsWithTimeout(120), 8
    );
    DeviceSettings::SettingsManager manager;

    assertResetResult(
        DeviceSettings::ResetResult::RESET_COMPLETED,
        manager.factoryReset(store)
    );
    TEST_ASSERT_TRUE(manager.settings() == DeviceSettings::defaults());
    TEST_ASSERT_EQUAL_UINT32(1, manager.generation());
    TEST_ASSERT_TRUE(manager.hasActiveSlot());
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(DeviceSettings::RecordSlot::A),
        static_cast<uint8_t>(manager.activeSlot())
    );
    TEST_ASSERT_FALSE(manager.repairPending());
    TEST_ASSERT_TRUE(store.slotA.present);
    TEST_ASSERT_FALSE(store.slotB.present);
    TEST_ASSERT_FALSE(store.markerPresent);

    DeviceSettings::Settings decoded;
    uint32_t generation = 0;
    assertCodecResult(
        DeviceSettings::CodecResult::OK,
        DeviceSettings::decodeRecord(
            store.slotA.bytes,
            store.slotA.length,
            decoded,
            generation
        )
    );
    TEST_ASSERT_TRUE(decoded == DeviceSettings::defaults());
    TEST_ASSERT_EQUAL_UINT32(1, generation);
}

void testResetOperationOrderAndCountsAreDeterministic() {
    FakeRecordStore store;
    store.setRecord(
        DeviceSettings::RecordSlot::A, settingsWithTimeout(60), 7
    );
    store.setRecord(
        DeviceSettings::RecordSlot::B, settingsWithTimeout(120), 8
    );
    DeviceSettings::SettingsManager manager;
    manager.factoryReset(store);
    const FakeOperation expected[] = {
        FakeOperation::WRITE_MARKER,
        FakeOperation::READ_MARKER,
        FakeOperation::REMOVE_A,
        FakeOperation::READ_A,
        FakeOperation::REMOVE_B,
        FakeOperation::READ_B,
        FakeOperation::WRITE_A,
        FakeOperation::READ_A,
        FakeOperation::REMOVE_MARKER,
        FakeOperation::READ_MARKER
    };

    TEST_ASSERT_EQUAL_UINT32(sizeof(expected) / sizeof(expected[0]),
        store.operationCount);
    for (size_t index = 0; index < sizeof(expected) / sizeof(expected[0]);
         ++index) {
        TEST_ASSERT_EQUAL_UINT8(
            static_cast<uint8_t>(expected[index]),
            static_cast<uint8_t>(store.operations[index])
        );
    }
    TEST_ASSERT_EQUAL_UINT32(1, store.markerWriteCount);
    TEST_ASSERT_EQUAL_UINT32(2, store.markerReadCount);
    TEST_ASSERT_EQUAL_UINT32(1, store.removeACount);
    TEST_ASSERT_EQUAL_UINT32(1, store.removeBCount);
    TEST_ASSERT_EQUAL_UINT32(1, store.writeACount);
    TEST_ASSERT_EQUAL_UINT32(0, store.writeBCount);
    TEST_ASSERT_EQUAL_UINT32(3, store.readCount);
    TEST_ASSERT_EQUAL_UINT32(1, store.markerRemoveCount);
}

void testMarkerWriteFailurePreservesBothSlots() {
    FakeRecordStore store;
    store.setRecord(
        DeviceSettings::RecordSlot::A, settingsWithTimeout(60), 1
    );
    store.setRecord(
        DeviceSettings::RecordSlot::B, settingsWithTimeout(120), 2
    );
    store.markerWriteResult = DeviceSettings::StoreResult::ERROR;
    DeviceSettings::SettingsManager manager;

    assertResetResult(
        DeviceSettings::ResetResult::MARKER_WRITE_FAILED,
        manager.factoryReset(store)
    );
    TEST_ASSERT_TRUE(store.slotA.present);
    TEST_ASSERT_TRUE(store.slotB.present);
    TEST_ASSERT_EQUAL_UINT32(0, store.removeCount);
}

void testMarkerVerificationFailurePreservesBothSlots() {
    FakeRecordStore store;
    store.setRecord(
        DeviceSettings::RecordSlot::A, settingsWithTimeout(60), 1
    );
    store.setRecord(
        DeviceSettings::RecordSlot::B, settingsWithTimeout(120), 2
    );
    store.markerWritePersists = false;
    DeviceSettings::SettingsManager manager;

    assertResetResult(
        DeviceSettings::ResetResult::MARKER_VERIFY_FAILED,
        manager.factoryReset(store)
    );
    TEST_ASSERT_TRUE(store.slotA.present);
    TEST_ASSERT_TRUE(store.slotB.present);
    TEST_ASSERT_EQUAL_UINT32(0, store.removeCount);
}

void testSlotARemoveFailureLeavesMarkerPending() {
    FakeRecordStore store;
    store.setRecord(
        DeviceSettings::RecordSlot::A, settingsWithTimeout(60), 1
    );
    store.setRecord(
        DeviceSettings::RecordSlot::B, settingsWithTimeout(120), 2
    );
    store.removeAResult = DeviceSettings::StoreResult::ERROR;
    DeviceSettings::SettingsManager manager;

    assertResetResult(
        DeviceSettings::ResetResult::SLOT_REMOVE_FAILED,
        manager.factoryReset(store)
    );
    TEST_ASSERT_TRUE(store.markerPresent);
    TEST_ASSERT_TRUE(store.slotA.present);
    TEST_ASSERT_TRUE(store.slotB.present);
    TEST_ASSERT_EQUAL_UINT32(0, store.writeCount);
}

void testSlotBRemoveFailureLeavesMarkerPending() {
    FakeRecordStore store;
    store.setRecord(
        DeviceSettings::RecordSlot::A, settingsWithTimeout(60), 1
    );
    store.setRecord(
        DeviceSettings::RecordSlot::B, settingsWithTimeout(120), 2
    );
    store.removeBResult = DeviceSettings::StoreResult::ERROR;
    DeviceSettings::SettingsManager manager;

    assertResetResult(
        DeviceSettings::ResetResult::SLOT_REMOVE_FAILED,
        manager.factoryReset(store)
    );
    TEST_ASSERT_TRUE(store.markerPresent);
    TEST_ASSERT_FALSE(store.slotA.present);
    TEST_ASSERT_TRUE(store.slotB.present);
    TEST_ASSERT_EQUAL_UINT32(0, store.writeCount);
}

void testPendingResetResumesWhenSlotAIsAlreadyMissing() {
    FakeRecordStore store;
    store.markerPresent = true;
    store.markerValue = true;
    store.setRecord(
        DeviceSettings::RecordSlot::B, settingsWithTimeout(120), 2
    );
    DeviceSettings::SettingsManager manager;

    assertLoadStatus(
        DeviceSettings::LoadStatus::RESET_COMPLETED,
        manager.load(store)
    );
    TEST_ASSERT_TRUE(manager.settings() == DeviceSettings::defaults());
    TEST_ASSERT_EQUAL_UINT32(1, manager.generation());
    TEST_ASSERT_EQUAL_UINT32(1, store.removeACount);
    TEST_ASSERT_EQUAL_UINT32(2, store.readACount);
    TEST_ASSERT_FALSE(store.markerPresent);
}

void testPendingResetResumesWhenSlotBIsAlreadyMissing() {
    FakeRecordStore store;
    store.markerPresent = true;
    store.markerValue = true;
    store.setRecord(
        DeviceSettings::RecordSlot::A, settingsWithTimeout(60), 1
    );
    DeviceSettings::SettingsManager manager;

    assertLoadStatus(
        DeviceSettings::LoadStatus::RESET_COMPLETED,
        manager.load(store)
    );
    TEST_ASSERT_TRUE(manager.settings() == DeviceSettings::defaults());
    TEST_ASSERT_EQUAL_UINT32(1, store.removeBCount);
    TEST_ASSERT_EQUAL_UINT32(1, store.readBCount);
    TEST_ASSERT_FALSE(store.markerPresent);
}

void testRemoveSlotMissingIsIdempotentSuccessWithVerification() {
    FakeRecordStore store;
    store.markerPresent = true;
    store.markerValue = true;
    DeviceSettings::SettingsManager manager;

    assertResetResult(
        DeviceSettings::ResetResult::RESET_COMPLETED,
        manager.recoverPendingReset(store)
    );
    TEST_ASSERT_EQUAL_UINT32(1, store.removeACount);
    TEST_ASSERT_EQUAL_UINT32(1, store.removeBCount);
    TEST_ASSERT_EQUAL_UINT32(3, store.readCount);
    TEST_ASSERT_TRUE(store.slotA.present);
    TEST_ASSERT_FALSE(store.slotB.present);
}

void testSlotRemovalVerificationFailsWhenSlotRemainsPresent() {
    FakeRecordStore store;
    store.markerPresent = true;
    store.markerValue = true;
    store.setRecord(
        DeviceSettings::RecordSlot::A, settingsWithTimeout(60), 1
    );
    store.setRecord(
        DeviceSettings::RecordSlot::B, settingsWithTimeout(120), 2
    );
    store.removeADeletes = false;
    DeviceSettings::SettingsManager manager;

    assertResetResult(
        DeviceSettings::ResetResult::SLOT_REMOVE_FAILED,
        manager.recoverPendingReset(store)
    );
    TEST_ASSERT_TRUE(store.slotA.present);
    TEST_ASSERT_TRUE(store.slotB.present);
    TEST_ASSERT_TRUE(store.markerPresent);
    TEST_ASSERT_EQUAL_UINT32(1, store.removeACount);
    TEST_ASSERT_EQUAL_UINT32(1, store.readACount);
    TEST_ASSERT_EQUAL_UINT32(0, store.removeBCount);
}

void testDefaultWriteFailureLeavesMarkerPending() {
    FakeRecordStore store;
    store.markerPresent = true;
    store.markerValue = true;
    store.writeResult = DeviceSettings::StoreResult::ERROR;
    DeviceSettings::SettingsManager manager;

    assertResetResult(
        DeviceSettings::ResetResult::DEFAULT_WRITE_FAILED,
        manager.recoverPendingReset(store)
    );
    TEST_ASSERT_TRUE(store.markerPresent);
    TEST_ASSERT_FALSE(store.slotA.present);
    TEST_ASSERT_FALSE(store.slotB.present);
    TEST_ASSERT_EQUAL_UINT32(1, store.writeCount);
}

void testDefaultReadBackFailureLeavesMarkerPending() {
    FakeRecordStore store;
    store.markerPresent = true;
    store.markerValue = true;
    store.failReadAtCount = 3;
    store.failReadResult = DeviceSettings::StoreResult::ERROR;
    DeviceSettings::SettingsManager manager;

    assertResetResult(
        DeviceSettings::ResetResult::DEFAULT_VERIFY_FAILED,
        manager.recoverPendingReset(store)
    );
    TEST_ASSERT_TRUE(store.markerPresent);
    TEST_ASSERT_TRUE(store.slotA.present);
    TEST_ASSERT_EQUAL_UINT32(0, store.markerRemoveCount);
}

void testDefaultMismatchLeavesMarkerPending() {
    FakeRecordStore store;
    store.markerPresent = true;
    store.markerValue = true;
    store.prepareReplacement(settingsWithTimeout(60), 1);
    store.replaceNextRead = false;
    store.replaceReadAtCount = 3;
    DeviceSettings::SettingsManager manager;

    assertResetResult(
        DeviceSettings::ResetResult::DEFAULT_VERIFY_FAILED,
        manager.recoverPendingReset(store)
    );
    TEST_ASSERT_TRUE(store.markerPresent);
    TEST_ASSERT_TRUE(store.slotA.present);
    TEST_ASSERT_EQUAL_UINT32(0, store.markerRemoveCount);
}

void testMarkerRemovalFailureDoesNotReportCompletion() {
    FakeRecordStore store;
    store.markerPresent = true;
    store.markerValue = true;
    store.markerRemoveResult = DeviceSettings::StoreResult::ERROR;
    DeviceSettings::SettingsManager manager;

    assertResetResult(
        DeviceSettings::ResetResult::MARKER_REMOVE_FAILED,
        manager.recoverPendingReset(store)
    );
    TEST_ASSERT_TRUE(store.markerPresent);
    TEST_ASSERT_FALSE(manager.hasActiveSlot());
    TEST_ASSERT_EQUAL_UINT32(0, manager.generation());
}

void testInterruptedResetStagesResumeIdempotently() {
    for (uint8_t stage = 0; stage < 5; ++stage) {
        FakeRecordStore store;
        store.setRecord(
            DeviceSettings::RecordSlot::A, settingsWithTimeout(60), 5
        );
        store.setRecord(
            DeviceSettings::RecordSlot::B, settingsWithTimeout(120), 6
        );
        if (stage == 0) {
            store.removeAResult = DeviceSettings::StoreResult::ERROR;
        } else if (stage == 1) {
            store.removeBResult = DeviceSettings::StoreResult::ERROR;
        } else if (stage == 2) {
            store.writeResult = DeviceSettings::StoreResult::ERROR;
        } else if (stage == 3) {
            store.failReadAtCount = 3;
        } else {
            store.markerRemoveResult = DeviceSettings::StoreResult::ERROR;
        }
        DeviceSettings::SettingsManager manager;
        TEST_ASSERT_NOT_EQUAL(
            static_cast<uint8_t>(DeviceSettings::ResetResult::RESET_COMPLETED),
            static_cast<uint8_t>(manager.factoryReset(store))
        );
        TEST_ASSERT_TRUE(store.markerPresent);

        store.removeAResult = DeviceSettings::StoreResult::OK;
        store.removeBResult = DeviceSettings::StoreResult::OK;
        store.writeResult = DeviceSettings::StoreResult::OK;
        store.failReadAtCount = 0;
        store.markerRemoveResult = DeviceSettings::StoreResult::OK;

        assertLoadStatus(
            DeviceSettings::LoadStatus::RESET_COMPLETED,
            manager.load(store)
        );
        TEST_ASSERT_TRUE(manager.settings() == DeviceSettings::defaults());
        TEST_ASSERT_EQUAL_UINT32(1, manager.generation());
        TEST_ASSERT_FALSE(store.markerPresent);
    }
}

void testPendingResetOnBootCompletesBeforeOrdinarySelection() {
    FakeRecordStore store;
    store.markerPresent = true;
    store.markerValue = true;
    store.setRecord(
        DeviceSettings::RecordSlot::A, settingsWithTimeout(60), 50
    );
    store.setRecord(
        DeviceSettings::RecordSlot::B, settingsWithTimeout(120), 51
    );
    DeviceSettings::SettingsManager manager;

    assertLoadStatus(
        DeviceSettings::LoadStatus::RESET_COMPLETED,
        manager.load(store)
    );
    TEST_ASSERT_TRUE(manager.settings() == DeviceSettings::defaults());
    TEST_ASSERT_EQUAL_UINT32(1, manager.generation());
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(FakeOperation::READ_MARKER),
        static_cast<uint8_t>(store.operations[0])
    );
    TEST_ASSERT_EQUAL_UINT32(3, store.readCount);
}

void testResetRecoveryFailureStopsBeforeOrdinarySelection() {
    FakeRecordStore store;
    store.markerPresent = true;
    store.markerValue = true;
    store.removeAResult = DeviceSettings::StoreResult::ERROR;
    DeviceSettings::SettingsManager manager;

    assertLoadStatus(
        DeviceSettings::LoadStatus::RESET_RECOVERY_FAILED,
        manager.load(store)
    );
    assertResetResult(
        DeviceSettings::ResetResult::SLOT_REMOVE_FAILED,
        manager.lastResetResult()
    );
    TEST_ASSERT_EQUAL_UINT32(0, store.readCount);
}

void testExplicitResetMayRemoveUnsupportedSchemaSlots() {
    FakeRecordStore store;
    store.setRecord(
        DeviceSettings::RecordSlot::A, settingsWithTimeout(60), 1
    );
    store.setRecord(
        DeviceSettings::RecordSlot::B, settingsWithTimeout(120), 2
    );
    DeviceSettings::writeUint16Le(store.slotA.bytes + 4, 2);
    DeviceSettings::writeUint16Le(store.slotB.bytes + 4, 3);
    updateRecordCrc(store.slotA.bytes);
    updateRecordCrc(store.slotB.bytes);
    DeviceSettings::SettingsManager manager;

    assertResetResult(
        DeviceSettings::ResetResult::RESET_COMPLETED,
        manager.factoryReset(store)
    );
    TEST_ASSERT_TRUE(manager.settings() == DeviceSettings::defaults());
    TEST_ASSERT_TRUE(store.slotA.present);
    TEST_ASSERT_FALSE(store.slotB.present);
    TEST_ASSERT_FALSE(manager.unsupportedSchema());
}

void testSchemaOneDirectLoadIsNotMigration() {
    uint8_t record[DeviceSettings::RECORD_SIZE] = {};
    encodeDefaults(record);
    assertMigrationResult(
        DeviceSettings::MigrationResult::SCHEMA_1_DIRECT,
        DeviceSettings::dispatchMigration(record, sizeof(record))
    );
}

void testUnknownSchemaIsUnsupportedWithoutDecoding() {
    uint8_t record[DeviceSettings::RECORD_SIZE] = {};
    encodeDefaults(record);
    DeviceSettings::writeUint16Le(record + 4, 2);
    updateRecordCrc(record);
    assertMigrationResult(
        DeviceSettings::MigrationResult::UNSUPPORTED_SCHEMA,
        DeviceSettings::dispatchMigration(record, sizeof(record))
    );
}

void testMissingAndUnversionedStorageAreNotMigration() {
    uint8_t unversioned[DeviceSettings::RECORD_SIZE] = {};
    assertMigrationResult(
        DeviceSettings::MigrationResult::NOT_MIGRATION,
        DeviceSettings::dispatchMigration(nullptr, 0)
    );
    assertMigrationResult(
        DeviceSettings::MigrationResult::NOT_MIGRATION,
        DeviceSettings::dispatchMigration(
            unversioned,
            sizeof(unversioned)
        )
    );
    assertMigrationResult(
        DeviceSettings::MigrationResult::NOT_MIGRATION,
        DeviceSettings::dispatchMigration(unversioned, 4)
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
    RUN_TEST(testLoadNoSlotsDefaultsWithoutWriting);
    RUN_TEST(testLoadSlotAOnly);
    RUN_TEST(testLoadSlotBOnly);
    RUN_TEST(testTwoValidSlotsSelectNewerA);
    RUN_TEST(testTwoValidSlotsSelectNewerB);
    RUN_TEST(testGenerationComparisonAndSelectionHandleRollover);
    RUN_TEST(testEqualGenerationIdenticalRecordsLoadDeterministically);
    RUN_TEST(testEqualGenerationDifferentRecordsDefaultAsCorrupt);
    RUN_TEST(testCorruptNewerSlotFallsBackToOlderValidSlot);
    RUN_TEST(testBothCorruptDefaultWithoutAutomaticWrite);
    RUN_TEST(testValidAWithMalformedBLoadsAAsFallback);
    RUN_TEST(testMalformedAWithValidBLoadsBAsFallback);
    RUN_TEST(testBothMalformedSlotsDefaultAsCorrupt);
    RUN_TEST(testExactSizeCrcInvalidSlotRemainsCorruptNotUnavailable);
    RUN_TEST(testValidSchemaRecordRepairsAllInvalidFieldsInRam);
    RUN_TEST(testSupportedAWithUnsupportedBLoadsA);
    RUN_TEST(testUnsupportedAWithSupportedBLoadsB);
    RUN_TEST(testUnsupportedInactiveSlotIsNeverSelectedAsSaveTarget);
    RUN_TEST(testUnsupportedSchemaInBothSlotsIsPreserved);
    RUN_TEST(testUnsupportedAndMissingDefaultsAndBlocksSave);
    RUN_TEST(testUnsupportedAndCorruptDefaultsAndBlocksSave);
    RUN_TEST(testStorageUnavailableReadsBothSlotsAndDefaults);
    RUN_TEST(testStorageUnavailableFromSlotBRemainsUnavailable);
    RUN_TEST(testFallbackRepairSaveCanonicalizesInactiveSlot);
    RUN_TEST(testUnchangedSavePerformsZeroStorageOperations);
    RUN_TEST(testChangedSaveWritesInactiveSlotOnceAndVerifiesReadBack);
    RUN_TEST(testWriteFailurePreservesPreviousAuthoritativeState);
    RUN_TEST(testReadBackFailurePreservesPreviousAuthoritativeState);
    RUN_TEST(testReadBackMismatchPreservesPreviousAuthoritativeState);
    RUN_TEST(testMultipleDraftChangesProduceOneCompleteRecordWrite);
    RUN_TEST(testSaveCanonicalizesInvalidCandidateInOneWrite);
    RUN_TEST(testSaveReportsRepairedUnchangedWithoutWriting);
    RUN_TEST(testSaveStorageUnavailableDoesNotRetryOrChangeState);
    RUN_TEST(testNoResetMarkerContinuesOrdinaryLoad);
    RUN_TEST(testExplicitFactoryResetCompletesCanonicalSequence);
    RUN_TEST(testResetOperationOrderAndCountsAreDeterministic);
    RUN_TEST(testMarkerWriteFailurePreservesBothSlots);
    RUN_TEST(testMarkerVerificationFailurePreservesBothSlots);
    RUN_TEST(testSlotARemoveFailureLeavesMarkerPending);
    RUN_TEST(testSlotBRemoveFailureLeavesMarkerPending);
    RUN_TEST(testPendingResetResumesWhenSlotAIsAlreadyMissing);
    RUN_TEST(testPendingResetResumesWhenSlotBIsAlreadyMissing);
    RUN_TEST(testRemoveSlotMissingIsIdempotentSuccessWithVerification);
    RUN_TEST(testSlotRemovalVerificationFailsWhenSlotRemainsPresent);
    RUN_TEST(testDefaultWriteFailureLeavesMarkerPending);
    RUN_TEST(testDefaultReadBackFailureLeavesMarkerPending);
    RUN_TEST(testDefaultMismatchLeavesMarkerPending);
    RUN_TEST(testMarkerRemovalFailureDoesNotReportCompletion);
    RUN_TEST(testInterruptedResetStagesResumeIdempotently);
    RUN_TEST(testPendingResetOnBootCompletesBeforeOrdinarySelection);
    RUN_TEST(testResetRecoveryFailureStopsBeforeOrdinarySelection);
    RUN_TEST(testExplicitResetMayRemoveUnsupportedSchemaSlots);
    RUN_TEST(testSchemaOneDirectLoadIsNotMigration);
    RUN_TEST(testUnknownSchemaIsUnsupportedWithoutDecoding);
    RUN_TEST(testMissingAndUnversionedStorageAreNotMigration);
    return UNITY_END();
}
