#pragma once

#include <stdint.h>

#include "device_capabilities.h"

namespace HeltecV4Capabilities {

using namespace DeviceCapabilities;

constexpr CapabilityId APPLICATION_INDICATOR_ID = 0x0101;
constexpr CapabilityId DIGITAL_INPUT_ID = 0x0201;
constexpr CapabilityId ANALOG_INPUT_0_ID = 0x0301;
constexpr uint8_t CAPABILITY_COUNT = 3;

namespace Resources {

constexpr uint8_t USER_BUTTON_GPIO = 0;
constexpr uint8_t EXTERNAL_ANALOG_GPIO = 4;
constexpr uint8_t EXTERNAL_ANALOG_ADC_UNIT = 1;
constexpr uint8_t EXTERNAL_ANALOG_ADC_CHANNEL = 3;
constexpr uint8_t SX1262_NSS_GPIO = 8;
constexpr uint8_t SX1262_SCK_GPIO = 9;
constexpr uint8_t SX1262_MOSI_GPIO = 10;
constexpr uint8_t SX1262_MISO_GPIO = 11;
constexpr uint8_t SX1262_RESET_GPIO = 12;
constexpr uint8_t SX1262_BUSY_GPIO = 13;
constexpr uint8_t SX1262_DIO1_GPIO = 14;
constexpr uint8_t OLED_SDA_GPIO = 17;
constexpr uint8_t OLED_SCL_GPIO = 18;
constexpr uint8_t OLED_RESET_GPIO = 21;
constexpr uint8_t GNSS_POWER_GPIO = 34;
constexpr uint8_t APPLICATION_LED_GPIO = 35;
constexpr uint8_t VEXT_CONTROL_GPIO = 36;
constexpr uint8_t BOARD_ADC_CONTROL_GPIO = 37;
constexpr uint8_t RF_FRONT_END_ENABLE_GPIO = 2;
constexpr uint8_t RF_FRONT_END_POWER_GPIO = 7;
constexpr uint8_t RF_FRONT_END_PA_GPIO = 45;

}  // namespace Resources

static const CapabilityDescriptor DESCRIPTORS[CAPABILITY_COUNT] = {
    {
        APPLICATION_INDICATOR_ID,
        CapabilityClass::INDICATOR_OUTPUT,
        static_cast<uint8_t>(
            OPERATION_FLAG_DESCRIBE |
            OPERATION_FLAG_READ |
            OPERATION_FLAG_SET
        ),
        ValueType::BOOLEAN,
        LOCAL_ONLY_AUTHORIZATION_POLICY_ID,
        MUTATING_INTERLOCK_SAFETY_POLICY_ID,
        static_cast<uint8_t>(UnitCode::BOOLEAN),
        0,
        0,
        0,
        1
    },
    {
        DIGITAL_INPUT_ID,
        CapabilityClass::DIGITAL_INPUT,
        static_cast<uint8_t>(
            OPERATION_FLAG_DESCRIBE | OPERATION_FLAG_READ
        ),
        ValueType::BOOLEAN,
        LOCAL_ONLY_AUTHORIZATION_POLICY_ID,
        NO_ADDITIONAL_SAFETY_POLICY_ID,
        static_cast<uint8_t>(UnitCode::BOOLEAN),
        0,
        0,
        0,
        1
    },
    {
        ANALOG_INPUT_0_ID,
        CapabilityClass::ANALOG_INPUT,
        static_cast<uint8_t>(
            OPERATION_FLAG_DESCRIBE | OPERATION_FLAG_READ
        ),
        ValueType::NORMALIZED_U16,
        LOCAL_ONLY_AUTHORIZATION_POLICY_ID,
        NO_ADDITIONAL_SAFETY_POLICY_ID,
        static_cast<uint8_t>(UnitCode::NORMALIZED),
        0,
        0,
        0,
        0xFFFF
    }
};

inline CapabilityRegistryView registryView() {
    const CapabilityRegistryView registry = {
        DESCRIPTORS,
        CAPABILITY_COUNT
    };
    return registry;
}

struct ProfileState {
    bool indicatorRequested = false;
    bool digitalInputAvailable = false;
    bool digitalInputActive = false;
    bool analogInputAvailable = false;
    uint16_t analogInputNormalized = 0;
};

inline void initializeProfileState(ProfileState& state) {
    state = {};
}

inline bool capabilityAvailability(
    const ProfileState& state,
    CapabilityId capabilityId,
    bool& available
) {
    switch (capabilityId) {
        case APPLICATION_INDICATOR_ID:
            available = true;
            return true;
        case DIGITAL_INPUT_ID:
            available = state.digitalInputAvailable;
            return true;
        case ANALOG_INPUT_0_ID:
            available = state.analogInputAvailable;
            return true;
    }
    return false;
}

inline OperationResult makeSuccess(ValueType type, uint32_t bits) {
    OperationResult result = {};
    result.status = OperationStatus::OK;
    result.value.type = type;
    result.value.bits = bits;
    return result;
}

class HeltecV4CapabilityHandler : public LocalCapabilityHandler {
public:
    explicit HeltecV4CapabilityHandler(ProfileState& state) : state_(state) {}
    ~HeltecV4CapabilityHandler() = default;

    OperationResult execute(
        const CapabilityDescriptor& descriptor,
        Operation operation,
        const CapabilityValue& input
    ) override {
        switch (descriptor.id) {
            case APPLICATION_INDICATOR_ID:
                if (operation == Operation::READ) {
                    return makeSuccess(
                        ValueType::BOOLEAN,
                        state_.indicatorRequested ? 1U : 0U
                    );
                }
                if (operation == Operation::SET) {
                    state_.indicatorRequested = input.bits == 1U;
                    return makeSuccess(ValueType::NONE, 0);
                }
                break;

            case DIGITAL_INPUT_ID:
                if (operation == Operation::READ) {
                    if (!state_.digitalInputAvailable) {
                        return makeCanonicalFailureResult(
                            OperationStatus::HARDWARE_UNAVAILABLE
                        );
                    }
                    return makeSuccess(
                        ValueType::BOOLEAN,
                        state_.digitalInputActive ? 1U : 0U
                    );
                }
                break;

            case ANALOG_INPUT_0_ID:
                if (operation == Operation::READ) {
                    if (!state_.analogInputAvailable) {
                        return makeCanonicalFailureResult(
                            OperationStatus::HARDWARE_UNAVAILABLE
                        );
                    }
                    return makeSuccess(
                        ValueType::NORMALIZED_U16,
                        state_.analogInputNormalized
                    );
                }
                break;
        }

        return makeCanonicalFailureResult(OperationStatus::OPERATION_FAILED);
    }

private:
    ProfileState& state_;
};

}  // namespace HeltecV4Capabilities
