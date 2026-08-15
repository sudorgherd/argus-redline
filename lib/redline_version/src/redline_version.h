#pragma once

#include <stdint.h>

#include "protocol.h"

namespace RedlineVersion {

constexpr char FIRMWARE[] = "v0.5.1";
constexpr uint8_t WIRE_PROTOCOL = Protocol::VERSION;
constexpr char HARDWARE_PROFILE[] = "HELTEC_V4";

}  // namespace RedlineVersion
