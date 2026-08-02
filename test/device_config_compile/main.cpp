#include "device_config.h"

// These expected values characterize the production tx/rx build flags.
// They are test fixtures, not an independent configuration authority.
#if defined(ARGUS_EXPECT_HUB_CONFIGURATION)
static_assert(
    DeviceConfig::LOCAL_ID == 0x01,
    "Hub local device ID must remain 0x01"
);
static_assert(
    DeviceConfig::PEER_ID == 0x10,
    "Hub peer device ID must remain 0x10"
);
#elif defined(ARGUS_EXPECT_NODE_CONFIGURATION)
static_assert(
    DeviceConfig::LOCAL_ID == 0x10,
    "Node local device ID must remain 0x10"
);
static_assert(
    DeviceConfig::PEER_ID == 0x01,
    "Node peer device ID must remain 0x01"
);
#endif

int main() {
    return 0;
}
