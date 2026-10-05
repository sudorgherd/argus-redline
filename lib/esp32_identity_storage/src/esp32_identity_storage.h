#pragma once

#include <nvs.h>

#include "device_identity_store.h"

namespace Esp32IdentityStorage {

// The only persistent keys this adapter can access are red_id/id_a and id_b.
class NvsStore final : public DeviceIdentityStore::Storage {
public:
    NvsStore() = default;
    ~NvsStore() override;
    NvsStore(const NvsStore&) = delete;
    NvsStore& operator=(const NvsStore&) = delete;

    DeviceIdentityStore::StorageResult read(
        DeviceIdentityStore::CopySlot slot,
        uint8_t* output, size_t capacity, size_t& length
    ) override;
    DeviceIdentityStore::StorageResult write(
        DeviceIdentityStore::CopySlot slot,
        const uint8_t* input, size_t length
    ) override;
    DeviceIdentityStore::StorageResult commit() override;

private:
    nvs_handle_t pendingHandle_ = 0;
};

}  // namespace Esp32IdentityStorage
