#pragma once

#include <stdint.h>
#include <string.h>

#include "device_capabilities.h"

namespace CapabilityCharacterization {

constexpr uint8_t COMMAND_BUFFER_CAPACITY = 24;
constexpr uint16_t ADC_RAW_MAX = 4095;

enum class Action : uint8_t {
    NONE = 0,
    HELP,
    CAPS,
    DIGITAL,
    INDICATOR_ON,
    INDICATOR_OFF,
    ANALOG_INPUT,
    DENY_REMOTE,
    DENY_INTERLOCK,
    STATUS
};

inline Action classifyCommand(const char* command) {
    if (command == nullptr) return Action::NONE;
    if (strcmp(command, "help") == 0) return Action::HELP;
    if (strcmp(command, "caps") == 0) return Action::CAPS;
    if (strcmp(command, "digital") == 0) return Action::DIGITAL;
    if (strcmp(command, "indicator-on") == 0) return Action::INDICATOR_ON;
    if (strcmp(command, "indicator-off") == 0) return Action::INDICATOR_OFF;
    if (strcmp(command, "analog") == 0) return Action::ANALOG_INPUT;
    if (strcmp(command, "deny-remote") == 0) return Action::DENY_REMOTE;
    if (strcmp(command, "deny-interlock") == 0) {
        return Action::DENY_INTERLOCK;
    }
    if (strcmp(command, "status") == 0) return Action::STATUS;
    return Action::NONE;
}

inline uint16_t normalizeRaw12ToU16(uint16_t raw) {
    if (raw >= ADC_RAW_MAX) return UINT16_MAX;
    return static_cast<uint16_t>(
        (static_cast<uint32_t>(raw) * UINT16_MAX) / ADC_RAW_MAX
    );
}

inline const char* statusLabel(DeviceCapabilities::OperationStatus status) {
    using DeviceCapabilities::OperationStatus;
    switch (status) {
        case OperationStatus::OK: return "OK";
        case OperationStatus::CAPABILITY_NOT_FOUND: return "NOT_FOUND";
        case OperationStatus::UNSUPPORTED_OPERATION: return "UNSUPPORTED";
        case OperationStatus::INVALID_VALUE_TYPE: return "INVALID_TYPE";
        case OperationStatus::VALUE_OUT_OF_RANGE: return "OUT_OF_RANGE";
        case OperationStatus::UNAUTHORIZED: return "UNAUTHORIZED";
        case OperationStatus::INTERLOCK_ACTIVE: return "INTERLOCK_ACTIVE";
        case OperationStatus::HARDWARE_UNAVAILABLE: return "HW_UNAVAILABLE";
        case OperationStatus::OPERATION_FAILED: return "FAILED";
        case OperationStatus::BUSY: return "BUSY";
        case OperationStatus::INVALID_DESCRIPTOR: return "INVALID_DESCRIPTOR";
    }
    return "UNKNOWN";
}

}  // namespace CapabilityCharacterization
