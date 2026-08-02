#pragma once

#include <stdint.h>

#include "protocol.h"

namespace RedlineVersion {

constexpr char FIRMWARE[] = "0.2.0-dev";
constexpr uint8_t WIRE_PROTOCOL = Protocol::VERSION;
constexpr char HARDWARE_PROFILE[] = "HELTEC_V4";

}  // namespace RedlineVersion
