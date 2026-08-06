#include <string.h>

#include <unity.h>

#include "hub_settings_integration.h"

namespace {

class MemoryStore final : public DeviceSettings::RecordStore {
public:
    DeviceSettings::StoreResult readSlot(
        DeviceSettings::RecordSlot slot,
        uint8_t* output,
        size_t capacity,
        size_t& length
    ) override {
        if (unavailable) return DeviceSettings::StoreResult::UNAVAILABLE;
        const uint8_t index = static_cast<uint8_t>(slot);
        if (!present[index]) {
            length = 0;
            return DeviceSettings::StoreResult::MISSING;
        }
        length = lengths[index];
        if (length > capacity) return DeviceSettings::StoreResult::MALFORMED;
        memcpy(output, slots[index], length);
        return DeviceSettings::StoreResult::OK;
    }

    DeviceSettings::StoreResult writeSlot(
        DeviceSettings::RecordSlot slot,
        const uint8_t* input,
        size_t length
    ) override {
        if (unavailable) return DeviceSettings::StoreResult::UNAVAILABLE;
        const uint8_t index = static_cast<uint8_t>(slot);
        memcpy(slots[index], input, length);
        lengths[index] = length;
        present[index] = true;
        writes++;
        return DeviceSettings::StoreResult::OK;
    }

    DeviceSettings::StoreResult removeSlot(
        DeviceSettings::RecordSlot slot
    ) override {
        if (unavailable) return DeviceSettings::StoreResult::UNAVAILABLE;
        present[static_cast<uint8_t>(slot)] = false;
        return DeviceSettings::StoreResult::OK;
    }

    DeviceSettings::StoreResult readResetMarker(bool& pending) override {
        if (unavailable) return DeviceSettings::StoreResult::UNAVAILABLE;
        if (!markerPresent) return DeviceSettings::StoreResult::MISSING;
        pending = marker;
        return DeviceSettings::StoreResult::OK;
    }

    DeviceSettings::StoreResult writeResetMarker(bool pending) override {
        if (unavailable) return DeviceSettings::StoreResult::UNAVAILABLE;
        markerPresent = true;
        marker = pending;
        return DeviceSettings::StoreResult::OK;
    }

    DeviceSettings::StoreResult removeResetMarker() override {
        if (unavailable) return DeviceSettings::StoreResult::UNAVAILABLE;
        markerPresent = false;
        return DeviceSettings::StoreResult::OK;
    }

    void put(DeviceSettings::RecordSlot slot, const uint8_t* data, size_t size) {
        const uint8_t index = static_cast<uint8_t>(slot);
        memcpy(slots[index], data, size);
        lengths[index] = size;
        present[index] = true;
    }

    uint8_t slots[2][DeviceSettings::RECORD_SIZE] = {};
    size_t lengths[2] = {};
    bool present[2] = {};
    bool markerPresent = false;
    bool marker = false;
    bool unavailable = false;
    uint32_t writes = 0;
};

DeviceSettings::Settings changedSettings(uint16_t timeout) {
    DeviceSettings::Settings settings = DeviceSettings::defaults();
    settings.displayTimeoutSeconds = timeout;
    return settings;
}

void testRequestPriorityAndSingleConsumption() {
    HubSettingsIntegration::RequestQueue queue;
    queue.queueAutomaticRepair();
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(HubSettingsIntegration::Request::AUTOMATIC_REPAIR),
        static_cast<uint8_t>(queue.pending())
    );
    const DeviceSettings::Settings draft = changedSettings(60);
    queue.queueSave(draft);
    TEST_ASSERT_TRUE(queue.saveDraft() == draft);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(HubSettingsIntegration::Request::SAVE),
        static_cast<uint8_t>(queue.pending())
    );
    queue.queueFactoryReset();
    queue.queueSave(changedSettings(120));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(HubSettingsIntegration::Request::FACTORY_RESET),
        static_cast<uint8_t>(queue.pending())
    );
    TEST_ASSERT_TRUE(queue.saveDraft() == DeviceSettings::defaults());
    queue.clear();
    TEST_ASSERT_EQUAL_UINT8(0, static_cast<uint8_t>(queue.pending()));
    TEST_ASSERT_TRUE(queue.saveDraft() == DeviceSettings::defaults());
    queue.queueAutomaticRepair();
    TEST_ASSERT_TRUE(queue.saveDraft() == DeviceSettings::defaults());
}

void testSafePredicateRejectsEveryActiveCondition() {
    using RuntimeState::RuntimePhase;
    TEST_ASSERT_TRUE(HubSettingsIntegration::persistenceSafe(
        RuntimePhase::IDLE, false, false, false, false
    ));
    TEST_ASSERT_FALSE(HubSettingsIntegration::persistenceSafe(
        RuntimePhase::TRANSMITTING, false, false, false, false
    ));
    TEST_ASSERT_FALSE(HubSettingsIntegration::persistenceSafe(
        RuntimePhase::WAITING_FOR_ACK, true, false, false, false
    ));
    TEST_ASSERT_FALSE(HubSettingsIntegration::persistenceSafe(
        RuntimePhase::IDLE, false, true, false, false
    ));
    TEST_ASSERT_FALSE(HubSettingsIntegration::persistenceSafe(
        RuntimePhase::IDLE, false, false, true, false
    ));
    TEST_ASSERT_FALSE(HubSettingsIntegration::persistenceSafe(
        RuntimePhase::IDLE, false, false, false, true
    ));
}

void testLoadedSlotMappingsIncludeSourceAndGeneration() {
    MemoryStore store;
    DeviceSettings::SettingsManager writer;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(DeviceSettings::SaveStatus::SAVED),
        static_cast<uint8_t>(writer.save(store, changedSettings(60)))
    );
    DeviceSettings::SettingsManager loadedA;
    DeviceSettings::LoadStatus load = loadedA.load(store);
    HubSettingsIntegration::ConfigurationState state =
        HubSettingsIntegration::fromLoad(load, loadedA);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(DeviceUi::ConfigurationSource::SLOT_A),
        static_cast<uint8_t>(state.source)
    );
    TEST_ASSERT_EQUAL_UINT32(1, state.generation);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(DeviceSettings::SaveStatus::SAVED),
        static_cast<uint8_t>(loadedA.save(store, changedSettings(120)))
    );
    DeviceSettings::SettingsManager loadedB;
    load = loadedB.load(store);
    state = HubSettingsIntegration::fromLoad(load, loadedB);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(DeviceUi::ConfigurationSource::SLOT_B),
        static_cast<uint8_t>(state.source)
    );
    TEST_ASSERT_EQUAL_UINT32(2, state.generation);
}

void testMissingCorruptUnavailableAndUnsupportedMapDistinctly() {
    MemoryStore missing;
    DeviceSettings::SettingsManager manager;
    DeviceSettings::LoadStatus result = manager.load(missing);
    TEST_ASSERT_EQUAL_UINT32(0, missing.writes);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(DeviceUi::ConfigurationStatus::DEFAULTED),
        static_cast<uint8_t>(
            HubSettingsIntegration::fromLoad(result, manager).status
        )
    );

    MemoryStore corrupt;
    const uint8_t bad = 0x42;
    corrupt.put(DeviceSettings::RecordSlot::A, &bad, 1);
    result = manager.load(corrupt);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(DeviceUi::ConfigurationStatus::DEFAULTED),
        static_cast<uint8_t>(
            HubSettingsIntegration::fromLoad(result, manager).status
        )
    );

    MemoryStore unavailable;
    unavailable.unavailable = true;
    result = manager.load(unavailable);
    TEST_ASSERT_TRUE(manager.settings() == DeviceSettings::defaults());
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(DeviceUi::ConfigurationStatus::UNAVAILABLE),
        static_cast<uint8_t>(
            HubSettingsIntegration::fromLoad(result, manager).status
        )
    );

    MemoryStore unsupported;
    uint8_t record[DeviceSettings::RECORD_SIZE] = {};
    DeviceSettings::encodeRecord(
        DeviceSettings::defaults(), 7, record, sizeof(record)
    );
    record[4] = 2;
    record[5] = 0;
    uint32_t crc = 0;
    DeviceSettings::crc32IsoHdlc(record, 20, crc);
    record[20] = static_cast<uint8_t>(crc);
    record[21] = static_cast<uint8_t>(crc >> 8);
    record[22] = static_cast<uint8_t>(crc >> 16);
    record[23] = static_cast<uint8_t>(crc >> 24);
    unsupported.put(DeviceSettings::RecordSlot::A, record, sizeof(record));
    result = manager.load(unsupported);
    const HubSettingsIntegration::ConfigurationState unsupportedState =
        HubSettingsIntegration::fromLoad(result, manager);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(DeviceUi::ConfigurationStatus::UNSUPPORTED),
        static_cast<uint8_t>(unsupportedState.status)
    );
    TEST_ASSERT_TRUE(unsupportedState.unsupportedPreserved);
}

void testResetAndSaveResultMappingsAreExplicit() {
    MemoryStore store;
    DeviceSettings::SettingsManager manager;
    const DeviceSettings::ResetResult reset = manager.factoryReset(store);
    HubSettingsIntegration::ConfigurationState state =
        HubSettingsIntegration::fromReset(reset, manager);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(DeviceUi::ConfigurationStatus::RESET_COMPLETED),
        static_cast<uint8_t>(state.status)
    );
    TEST_ASSERT_EQUAL_UINT32(1, state.generation);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(DeviceUi::ConfigurationSource::SLOT_A),
        static_cast<uint8_t>(state.source)
    );

    const DeviceSettings::SaveStatus unchanged =
        manager.save(store, DeviceSettings::defaults());
    state = HubSettingsIntegration::fromSave(unchanged, manager);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(DeviceUi::ConfigurationStatus::UNCHANGED),
        static_cast<uint8_t>(state.status)
    );

    store.unavailable = true;
    const DeviceSettings::SaveStatus failure =
        manager.save(store, changedSettings(60));
    state = HubSettingsIntegration::fromSave(failure, manager);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(DeviceUi::ConfigurationStatus::SAVE_FAILED),
        static_cast<uint8_t>(state.status)
    );
    TEST_ASSERT_TRUE(manager.settings() == DeviceSettings::defaults());
}

void testTimeoutScreenAndFeedbackApplicationArePure() {
    DeviceSettings::Settings settings = DeviceSettings::defaults();
    settings.displayTimeoutSeconds = 0;
    TEST_ASSERT_EQUAL_UINT32(0, HubSettingsIntegration::timeoutMs(settings));
    settings.displayTimeoutSeconds = 600;
    TEST_ASSERT_EQUAL_UINT32(600000, HubSettingsIntegration::timeoutMs(settings));

    for (uint8_t value = 0; value < 6; ++value) {
        TEST_ASSERT_EQUAL_UINT8(
            value,
            static_cast<uint8_t>(HubSettingsIntegration::screen(
                static_cast<DeviceSettings::DefaultScreen>(value)
            ))
        );
    }

    settings.ledEnabled = true;
    settings.buttonFeedbackEnabled = true;
    TEST_ASSERT_TRUE(HubSettingsIntegration::feedbackAllowed(
        settings, DeviceInput::ButtonEvent::SHORT_PRESS
    ));
    TEST_ASSERT_FALSE(HubSettingsIntegration::feedbackAllowed(
        settings, DeviceInput::ButtonEvent::PRESS
    ));
    settings.ledEnabled = false;
    TEST_ASSERT_FALSE(HubSettingsIntegration::feedbackAllowed(
        settings, DeviceInput::ButtonEvent::LONG_PRESS
    ));
}

void testEveryLoadStatusMapsExplicitly() {
    DeviceSettings::SettingsManager manager;
    const DeviceSettings::LoadStatus results[] = {
        DeviceSettings::LoadStatus::LOADED,
        DeviceSettings::LoadStatus::DEFAULTED_MISSING,
        DeviceSettings::LoadStatus::LOADED_FALLBACK_SLOT,
        DeviceSettings::LoadStatus::REPAIRED_SCHEMA_1,
        DeviceSettings::LoadStatus::DEFAULTED_CORRUPT,
        DeviceSettings::LoadStatus::UNSUPPORTED_SCHEMA,
        DeviceSettings::LoadStatus::STORAGE_UNAVAILABLE,
        DeviceSettings::LoadStatus::RESET_COMPLETED,
        DeviceSettings::LoadStatus::RESET_RECOVERY_FAILED
    };
    const DeviceUi::ConfigurationStatus expected[] = {
        DeviceUi::ConfigurationStatus::LOADED,
        DeviceUi::ConfigurationStatus::DEFAULTED,
        DeviceUi::ConfigurationStatus::FALLBACK,
        DeviceUi::ConfigurationStatus::REPAIRED,
        DeviceUi::ConfigurationStatus::DEFAULTED,
        DeviceUi::ConfigurationStatus::UNSUPPORTED,
        DeviceUi::ConfigurationStatus::UNAVAILABLE,
        DeviceUi::ConfigurationStatus::RESET_COMPLETED,
        DeviceUi::ConfigurationStatus::RESET_FAILED
    };
    for (uint8_t index = 0; index < 9; ++index) {
        const HubSettingsIntegration::ConfigurationState state =
            HubSettingsIntegration::fromLoad(results[index], manager);
        TEST_ASSERT_EQUAL_UINT8(
            static_cast<uint8_t>(expected[index]),
            static_cast<uint8_t>(state.status)
        );
    }
}

void testEverySaveAndResetStatusMapsExplicitly() {
    DeviceSettings::SettingsManager manager;
    const DeviceSettings::SaveStatus results[] = {
        DeviceSettings::SaveStatus::SAVED,
        DeviceSettings::SaveStatus::REPAIRED_SAVED,
        DeviceSettings::SaveStatus::UNCHANGED,
        DeviceSettings::SaveStatus::REPAIRED_UNCHANGED,
        DeviceSettings::SaveStatus::UNSUPPORTED_SCHEMA,
        DeviceSettings::SaveStatus::STORAGE_UNAVAILABLE,
        DeviceSettings::SaveStatus::WRITE_FAILED,
        DeviceSettings::SaveStatus::READ_BACK_FAILED,
        DeviceSettings::SaveStatus::VERIFICATION_FAILED
    };
    const DeviceUi::ConfigurationStatus expected[] = {
        DeviceUi::ConfigurationStatus::SAVED,
        DeviceUi::ConfigurationStatus::SAVED,
        DeviceUi::ConfigurationStatus::UNCHANGED,
        DeviceUi::ConfigurationStatus::UNCHANGED,
        DeviceUi::ConfigurationStatus::UNSUPPORTED,
        DeviceUi::ConfigurationStatus::SAVE_FAILED,
        DeviceUi::ConfigurationStatus::SAVE_FAILED,
        DeviceUi::ConfigurationStatus::SAVE_FAILED,
        DeviceUi::ConfigurationStatus::SAVE_FAILED
    };
    for (uint8_t index = 0; index < 9; ++index) {
        const HubSettingsIntegration::ConfigurationState state =
            HubSettingsIntegration::fromSave(results[index], manager);
        TEST_ASSERT_EQUAL_UINT8(
            static_cast<uint8_t>(expected[index]),
            static_cast<uint8_t>(state.status)
        );
    }
    const DeviceSettings::ResetResult resetResults[] = {
        DeviceSettings::ResetResult::RESET_COMPLETED,
        DeviceSettings::ResetResult::RESET_NOT_PENDING,
        DeviceSettings::ResetResult::STORAGE_UNAVAILABLE,
        DeviceSettings::ResetResult::MARKER_WRITE_FAILED,
        DeviceSettings::ResetResult::MARKER_VERIFY_FAILED,
        DeviceSettings::ResetResult::SLOT_REMOVE_FAILED,
        DeviceSettings::ResetResult::DEFAULT_WRITE_FAILED,
        DeviceSettings::ResetResult::DEFAULT_VERIFY_FAILED,
        DeviceSettings::ResetResult::MARKER_REMOVE_FAILED
    };
    for (uint8_t index = 0; index < 9; ++index) {
        const HubSettingsIntegration::ConfigurationState state =
            HubSettingsIntegration::fromReset(resetResults[index], manager);
        TEST_ASSERT_EQUAL_UINT8(
            static_cast<uint8_t>(
                index == 0
                    ? DeviceUi::ConfigurationStatus::RESET_COMPLETED
                    : DeviceUi::ConfigurationStatus::RESET_FAILED
            ),
            static_cast<uint8_t>(state.status)
        );
    }
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(testRequestPriorityAndSingleConsumption);
    RUN_TEST(testSafePredicateRejectsEveryActiveCondition);
    RUN_TEST(testLoadedSlotMappingsIncludeSourceAndGeneration);
    RUN_TEST(testMissingCorruptUnavailableAndUnsupportedMapDistinctly);
    RUN_TEST(testResetAndSaveResultMappingsAreExplicit);
    RUN_TEST(testTimeoutScreenAndFeedbackApplicationArePure);
    RUN_TEST(testEveryLoadStatusMapsExplicitly);
    RUN_TEST(testEverySaveAndResetStatusMapsExplicitly);
    return UNITY_END();
}
