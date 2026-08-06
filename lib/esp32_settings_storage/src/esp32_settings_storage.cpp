#include "esp32_settings_storage.h"

#include <Preferences.h>
#include <nvs.h>

namespace Esp32SettingsStorage {
namespace {

constexpr char NAMESPACE[] = "redline";
constexpr char SLOT_A_KEY[] = "cfg_a";
constexpr char SLOT_B_KEY[] = "cfg_b";
constexpr char RESET_MARKER_KEY[] = "rst_pend";

constexpr DeviceSettings::StoreResult classifyNamespaceOpen(
    esp_err_t result
) {
    return result == ESP_OK
        ? DeviceSettings::StoreResult::OK
        : result == ESP_ERR_NVS_NOT_FOUND
            ? DeviceSettings::StoreResult::MISSING
            : DeviceSettings::StoreResult::UNAVAILABLE;
}

static_assert(
    classifyNamespaceOpen(ESP_OK) == DeviceSettings::StoreResult::OK,
    "An accessible namespace must continue to key lookup"
);
static_assert(
    classifyNamespaceOpen(ESP_ERR_NVS_NOT_FOUND) ==
        DeviceSettings::StoreResult::MISSING,
    "An absent namespace must behave as empty storage"
);
static_assert(
    classifyNamespaceOpen(ESP_ERR_NVS_INVALID_STATE) ==
        DeviceSettings::StoreResult::UNAVAILABLE,
    "A real NVS access failure must remain unavailable"
);

DeviceSettings::StoreResult probeNamespaceForRead() {
    nvs_handle_t handle = 0;
    const esp_err_t openResult = nvs_open(
        NAMESPACE,
        NVS_READONLY,
        &handle
    );
    const DeviceSettings::StoreResult result =
        classifyNamespaceOpen(openResult);
    if (openResult == ESP_OK) {
        nvs_close(handle);
    }
    return result;
}

const char* slotKey(DeviceSettings::RecordSlot slot) {
    return slot == DeviceSettings::RecordSlot::A
        ? SLOT_A_KEY
        : SLOT_B_KEY;
}

DeviceSettings::StoreResult removeKey(const char* key) {
    Preferences preferences;
    if (!preferences.begin(NAMESPACE, false)) {
        return DeviceSettings::StoreResult::UNAVAILABLE;
    }

    if (!preferences.isKey(key)) {
        preferences.end();
        return DeviceSettings::StoreResult::MISSING;
    }

    const bool removed = preferences.remove(key);
    preferences.end();
    return removed
        ? DeviceSettings::StoreResult::OK
        : DeviceSettings::StoreResult::ERROR;
}

}  // namespace

DeviceSettings::StoreResult PreferencesRecordStore::readSlot(
    DeviceSettings::RecordSlot slot,
    uint8_t* output,
    size_t capacity,
    size_t& length
) {
    length = 0;
    if (output == nullptr || capacity < DeviceSettings::RECORD_SIZE) {
        return DeviceSettings::StoreResult::ERROR;
    }

    const DeviceSettings::StoreResult namespaceResult =
        probeNamespaceForRead();
    if (namespaceResult != DeviceSettings::StoreResult::OK) {
        return namespaceResult;
    }

    Preferences preferences;
    if (!preferences.begin(NAMESPACE, true)) {
        return DeviceSettings::StoreResult::UNAVAILABLE;
    }

    const char* key = slotKey(slot);
    if (!preferences.isKey(key)) {
        preferences.end();
        return DeviceSettings::StoreResult::MISSING;
    }
    if (preferences.getType(key) != PT_BLOB ||
        preferences.getBytesLength(key) != DeviceSettings::RECORD_SIZE) {
        preferences.end();
        return DeviceSettings::StoreResult::ERROR;
    }

    const size_t bytesRead = preferences.getBytes(
        key,
        output,
        DeviceSettings::RECORD_SIZE
    );
    preferences.end();
    if (bytesRead != DeviceSettings::RECORD_SIZE) {
        return DeviceSettings::StoreResult::ERROR;
    }

    length = bytesRead;
    return DeviceSettings::StoreResult::OK;
}

DeviceSettings::StoreResult PreferencesRecordStore::writeSlot(
    DeviceSettings::RecordSlot slot,
    const uint8_t* input,
    size_t length
) {
    if (input == nullptr || length != DeviceSettings::RECORD_SIZE) {
        return DeviceSettings::StoreResult::ERROR;
    }

    Preferences preferences;
    if (!preferences.begin(NAMESPACE, false)) {
        return DeviceSettings::StoreResult::UNAVAILABLE;
    }
    const size_t bytesWritten = preferences.putBytes(
        slotKey(slot),
        input,
        length
    );
    preferences.end();
    return bytesWritten == DeviceSettings::RECORD_SIZE
        ? DeviceSettings::StoreResult::OK
        : DeviceSettings::StoreResult::ERROR;
}

DeviceSettings::StoreResult PreferencesRecordStore::removeSlot(
    DeviceSettings::RecordSlot slot
) {
    return removeKey(slotKey(slot));
}

DeviceSettings::StoreResult PreferencesRecordStore::readResetMarker(
    bool& pending
) {
    pending = false;
    const DeviceSettings::StoreResult namespaceResult =
        probeNamespaceForRead();
    if (namespaceResult != DeviceSettings::StoreResult::OK) {
        return namespaceResult;
    }

    Preferences preferences;
    if (!preferences.begin(NAMESPACE, true)) {
        return DeviceSettings::StoreResult::UNAVAILABLE;
    }

    if (!preferences.isKey(RESET_MARKER_KEY)) {
        preferences.end();
        return DeviceSettings::StoreResult::MISSING;
    }
    if (preferences.getType(RESET_MARKER_KEY) != PT_U8) {
        preferences.end();
        return DeviceSettings::StoreResult::ERROR;
    }

    const uint8_t stored = preferences.getUChar(RESET_MARKER_KEY, 0xFF);
    preferences.end();
    if (stored > 1U) {
        return DeviceSettings::StoreResult::ERROR;
    }

    pending = stored == 1U;
    return DeviceSettings::StoreResult::OK;
}

DeviceSettings::StoreResult PreferencesRecordStore::writeResetMarker(
    bool pending
) {
    Preferences preferences;
    if (!preferences.begin(NAMESPACE, false)) {
        return DeviceSettings::StoreResult::UNAVAILABLE;
    }
    const size_t bytesWritten = preferences.putBool(
        RESET_MARKER_KEY,
        pending
    );
    preferences.end();
    return bytesWritten == sizeof(uint8_t)
        ? DeviceSettings::StoreResult::OK
        : DeviceSettings::StoreResult::ERROR;
}

DeviceSettings::StoreResult PreferencesRecordStore::removeResetMarker() {
    return removeKey(RESET_MARKER_KEY);
}

}  // namespace Esp32SettingsStorage
