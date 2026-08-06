#pragma once

#include <device_settings.h>

namespace Esp32SettingsStorage {

class PreferencesRecordStore final : public DeviceSettings::RecordStore {
public:
    DeviceSettings::StoreResult readSlot(
        DeviceSettings::RecordSlot slot,
        uint8_t* output,
        size_t capacity,
        size_t& length
    ) override;

    DeviceSettings::StoreResult writeSlot(
        DeviceSettings::RecordSlot slot,
        const uint8_t* input,
        size_t length
    ) override;

    DeviceSettings::StoreResult removeSlot(
        DeviceSettings::RecordSlot slot
    ) override;

    DeviceSettings::StoreResult readResetMarker(bool& pending) override;

    DeviceSettings::StoreResult writeResetMarker(bool pending) override;

    DeviceSettings::StoreResult removeResetMarker() override;
};

}  // namespace Esp32SettingsStorage
