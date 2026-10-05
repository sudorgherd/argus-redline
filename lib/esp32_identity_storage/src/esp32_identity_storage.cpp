#include "esp32_identity_storage.h"

namespace Esp32IdentityStorage {
namespace {

constexpr char NAMESPACE[] = "red_id";
constexpr char KEY_A[] = "id_a";
constexpr char KEY_B[] = "id_b";

static_assert(sizeof(NAMESPACE) - 1U <= 15U, "NVS namespace too long");
static_assert(sizeof(KEY_A) - 1U <= 15U && sizeof(KEY_B) - 1U <= 15U,
              "NVS identity key too long");
static_assert(DeviceIdentity::RECORD_SIZE == 64,
              "Identity Schema 1 record geometry changed");

const char* key(DeviceIdentityStore::CopySlot slot) {
    return slot == DeviceIdentityStore::CopySlot::A ? KEY_A : KEY_B;
}

DeviceIdentityStore::StorageResult openResult(esp_err_t result) {
    if (result == ESP_OK) return DeviceIdentityStore::StorageResult::OK;
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        return DeviceIdentityStore::StorageResult::MISSING;
    }
    return DeviceIdentityStore::StorageResult::UNAVAILABLE;
}

}  // namespace

NvsStore::~NvsStore() {
    if (pendingHandle_ != 0) nvs_close(pendingHandle_);
}

DeviceIdentityStore::StorageResult NvsStore::read(
    DeviceIdentityStore::CopySlot slot,
    uint8_t* output, size_t capacity, size_t& length
) {
    length = 0;
    if (output == nullptr || capacity < DeviceIdentity::RECORD_SIZE ||
        pendingHandle_ != 0) {
        return DeviceIdentityStore::StorageResult::ERROR;
    }
    nvs_handle_t handle = 0;
    const esp_err_t opened = nvs_open(NAMESPACE, NVS_READONLY, &handle);
    if (opened != ESP_OK) return openResult(opened);

    size_t storedLength = 0;
    const esp_err_t measured = nvs_get_blob(
        handle, key(slot), nullptr, &storedLength);
    if (measured != ESP_OK) {
        nvs_close(handle);
        return measured == ESP_ERR_NVS_TYPE_MISMATCH
            ? DeviceIdentityStore::StorageResult::MALFORMED
            : openResult(measured);
    }
    if (storedLength != DeviceIdentity::RECORD_SIZE) {
        nvs_close(handle);
        return DeviceIdentityStore::StorageResult::MALFORMED;
    }
    size_t readLength = DeviceIdentity::RECORD_SIZE;
    const esp_err_t readResult = nvs_get_blob(
        handle, key(slot), output, &readLength);
    nvs_close(handle);
    if (readResult == ESP_ERR_NVS_TYPE_MISMATCH) {
        return DeviceIdentityStore::StorageResult::MALFORMED;
    }
    if (readResult != ESP_OK) {
        return DeviceIdentityStore::StorageResult::UNAVAILABLE;
    }
    if (readLength != DeviceIdentity::RECORD_SIZE) {
        return DeviceIdentityStore::StorageResult::MALFORMED;
    }
    length = readLength;
    return DeviceIdentityStore::StorageResult::OK;
}

DeviceIdentityStore::StorageResult NvsStore::write(
    DeviceIdentityStore::CopySlot slot,
    const uint8_t* input, size_t length
) {
    if (input == nullptr || length != DeviceIdentity::RECORD_SIZE ||
        pendingHandle_ != 0) {
        return DeviceIdentityStore::StorageResult::ERROR;
    }
    nvs_handle_t handle = 0;
    if (nvs_open(NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return DeviceIdentityStore::StorageResult::UNAVAILABLE;
    }
    const esp_err_t staged = nvs_set_blob(handle, key(slot), input, length);
    if (staged != ESP_OK) {
        nvs_close(handle);
        return DeviceIdentityStore::StorageResult::ERROR;
    }
    pendingHandle_ = handle;
    return DeviceIdentityStore::StorageResult::OK;
}

DeviceIdentityStore::StorageResult NvsStore::commit() {
    if (pendingHandle_ == 0) return DeviceIdentityStore::StorageResult::ERROR;
    const esp_err_t committed = nvs_commit(pendingHandle_);
    nvs_close(pendingHandle_);
    pendingHandle_ = 0;
    return committed == ESP_OK
        ? DeviceIdentityStore::StorageResult::OK
        : DeviceIdentityStore::StorageResult::ERROR;
}

}  // namespace Esp32IdentityStorage
