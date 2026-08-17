#pragma once

#include <stdint.h>

#include "protocol.h"

namespace RedlineVersion {

constexpr uint8_t FIRMWARE_MAJOR = 0;
constexpr uint8_t FIRMWARE_MINOR = 6;
constexpr uint8_t FIRMWARE_PATCH = 0;
constexpr char FIRMWARE[] = "v0.6.0";
constexpr uint8_t WIRE_PROTOCOL = Protocol::VERSION;
constexpr char HARDWARE_PROFILE[] = "HELTEC_V4";

}  // namespace RedlineVersion
