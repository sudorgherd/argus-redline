#include <unity.h>

#include "protocol.h"
#include "redline_version.h"

namespace {

void testDevelopmentFirmwareIdentifier() {
    TEST_ASSERT_EQUAL_STRING("0.2.0-dev", RedlineVersion::FIRMWARE);
}

void testWireProtocolVersion() {
    TEST_ASSERT_EQUAL_UINT8(1, RedlineVersion::WIRE_PROTOCOL);
    TEST_ASSERT_EQUAL_UINT8(
        Protocol::VERSION,
        RedlineVersion::WIRE_PROTOCOL
    );
}

void testHardwareProfileIdentifier() {
    TEST_ASSERT_EQUAL_STRING(
        "HELTEC_V4",
        RedlineVersion::HARDWARE_PROFILE
    );
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(testDevelopmentFirmwareIdentifier);
    RUN_TEST(testWireProtocolVersion);
    RUN_TEST(testHardwareProfileIdentifier);
    return UNITY_END();
}
