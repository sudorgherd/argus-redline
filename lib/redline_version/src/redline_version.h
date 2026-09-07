#pragma once

#include <stdint.h>

#include "protocol.h"

namespace RedlineVersion {

constexpr uint8_t FIRMWARE_MAJOR = 0;
constexpr uint8_t FIRMWARE_MINOR = 7;
constexpr uint8_t FIRMWARE_PATCH = 0;
constexpr char FIRMWARE[] = "v0.7.0";
constexpr uint8_t WIRE_PROTOCOL = Protocol::VERSION;
constexpr char HARDWARE_PROFILE[] = "HELTEC_V4";

}  // namespace RedlineVersion
