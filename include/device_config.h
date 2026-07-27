#pragma once

#include <stdint.h>

#ifndef ARGUS_LOCAL_DEVICE_ID
#error "ARGUS_LOCAL_DEVICE_ID must be defined by the build environment"
#endif

#ifndef ARGUS_PEER_DEVICE_ID
#error "ARGUS_PEER_DEVICE_ID must be defined by the build environment"
#endif

namespace DeviceConfig {

constexpr uint8_t LOCAL_ID =
    static_cast<uint8_t>(ARGUS_LOCAL_DEVICE_ID);

constexpr uint8_t PEER_ID =
    static_cast<uint8_t>(ARGUS_PEER_DEVICE_ID);

static_assert(
    LOCAL_ID != PEER_ID,
    "Local and peer device IDs must be different"
);

}  // namespace DeviceConfig
