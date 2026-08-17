#include <unity.h>

#include "protocol.h"
#include "redline_version.h"

namespace {

void testReleaseFirmwareIdentifier() {
    TEST_ASSERT_EQUAL_STRING("v0.6.0", RedlineVersion::FIRMWARE);
    TEST_ASSERT_EQUAL_UINT8(0, RedlineVersion::FIRMWARE_MAJOR);
    TEST_ASSERT_EQUAL_UINT8(6, RedlineVersion::FIRMWARE_MINOR);
    TEST_ASSERT_EQUAL_UINT8(0, RedlineVersion::FIRMWARE_PATCH);
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
    RUN_TEST(testReleaseFirmwareIdentifier);
    RUN_TEST(testWireProtocolVersion);
    RUN_TEST(testHardwareProfileIdentifier);
    return UNITY_END();
}
