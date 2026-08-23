#include "esp32_event_storage.h"

#include <Preferences.h>
#include <nvs.h>

namespace Esp32EventStorage {
namespace {

constexpr char NAMESPACE[] = "red_evt";
constexpr char META_A[] = "id_a";
constexpr char META_B[] = "id_b";
constexpr char SLOT_KEYS[NodeEventStore::NODE_EVENT_CAPACITY][2][5] = {
    {"e0a", "e0b"}, {"e1a", "e1b"}, {"e2a", "e2b"}, {"e3a", "e3b"},
    {"e4a", "e4b"}, {"e5a", "e5b"}, {"e6a", "e6b"}, {"e7a", "e7b"}
};

static_assert(sizeof(NAMESPACE) - 1U <= 15U, "Preferences namespace too long");
static_assert(sizeof(META_A) - 1U <= 15U && sizeof(META_B) - 1U <= 15U,
              "Preferences metadata key too long");
static_assert(EventRecords::NODE_METADATA_SIZE == 28, "metadata geometry changed");
static_assert(EventRecords::NODE_RECORD_SIZE == 56, "record geometry changed");

const char* metadataKey(EventStorage::CopySlot slot) {
    return slot == EventStorage::CopySlot::A ? META_A : META_B;
}

const char* eventKey(uint8_t logicalSlot, EventStorage::CopySlot copy) {
    return logicalSlot < NodeEventStore::NODE_EVENT_CAPACITY
        ? SLOT_KEYS[logicalSlot][copy == EventStorage::CopySlot::A ? 0 : 1]
        : nullptr;
}

EventIdentity::StorageResult probeNamespace() {
    nvs_handle_t handle = 0;
    const esp_err_t result = nvs_open(NAMESPACE, NVS_READONLY, &handle);
    if (result == ESP_OK) {
        nvs_close(handle);
        return EventIdentity::StorageResult::OK;
    }
    return result == ESP_ERR_NVS_NOT_FOUND
        ? EventIdentity::StorageResult::MISSING
        : EventIdentity::StorageResult::UNAVAILABLE;
}

EventIdentity::StorageResult readBlob(
    const char* key, size_t exactSize, uint8_t* output,
    size_t capacity, size_t& length
) {
    length = 0;
    if (key == nullptr || output == nullptr || capacity < exactSize) {
        return EventIdentity::StorageResult::ERROR;
    }
    const EventIdentity::StorageResult probe = probeNamespace();
    if (probe != EventIdentity::StorageResult::OK) return probe;
    Preferences preferences;
    if (!preferences.begin(NAMESPACE, true)) return EventIdentity::StorageResult::UNAVAILABLE;
    if (!preferences.isKey(key)) {
        preferences.end();
        return EventIdentity::StorageResult::MISSING;
    }
    if (preferences.getType(key) != PT_BLOB || preferences.getBytesLength(key) != exactSize) {
        preferences.end();
        return EventIdentity::StorageResult::ERROR;
    }
    const size_t count = preferences.getBytes(key, output, exactSize);
    preferences.end();
    if (count != exactSize) return EventIdentity::StorageResult::ERROR;
    length = count;
    return EventIdentity::StorageResult::OK;
}

EventIdentity::StorageResult writeBlob(
    const char* key, size_t exactSize, const uint8_t* input, size_t length
) {
    if (key == nullptr || input == nullptr || length != exactSize) {
        return EventIdentity::StorageResult::ERROR;
    }
    Preferences preferences;
    if (!preferences.begin(NAMESPACE, false)) return EventIdentity::StorageResult::UNAVAILABLE;
    const size_t count = preferences.putBytes(key, input, length);
    preferences.end();
    return count == exactSize
        ? EventIdentity::StorageResult::OK : EventIdentity::StorageResult::ERROR;
}

}  // namespace

EventIdentity::StorageResult PreferencesStore::read(
    EventStorage::CopySlot slot, uint8_t* output, size_t capacity, size_t& length) {
    return readBlob(metadataKey(slot), EventRecords::NODE_METADATA_SIZE,
                    output, capacity, length);
}
EventIdentity::StorageResult PreferencesStore::write(
    EventStorage::CopySlot slot, const uint8_t* input, size_t length) {
    return writeBlob(metadataKey(slot), EventRecords::NODE_METADATA_SIZE, input, length);
}
EventIdentity::StorageResult PreferencesStore::commit() {
    // Preferences::putBytes commits before the namespace is closed.
    return EventIdentity::StorageResult::OK;
}
EventIdentity::StorageResult PreferencesStore::readEventCopy(
    uint8_t logicalSlot, EventStorage::CopySlot copy, uint8_t* output,
    size_t capacity, size_t& length) {
    return readBlob(eventKey(logicalSlot, copy), EventRecords::NODE_RECORD_SIZE,
                    output, capacity, length);
}
EventIdentity::StorageResult PreferencesStore::writeEventCopy(
    uint8_t logicalSlot, EventStorage::CopySlot copy,
    const uint8_t* input, size_t length) {
    return writeBlob(eventKey(logicalSlot, copy), EventRecords::NODE_RECORD_SIZE,
                     input, length);
}

}  // namespace Esp32EventStorage
