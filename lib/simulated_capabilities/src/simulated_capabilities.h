#pragma once

#include <stdint.h>

#include "device_capabilities.h"

namespace SimulatedCapabilities {

using namespace DeviceCapabilities;

constexpr CapabilityId APPLICATION_INDICATOR_ID = 0x0101;
constexpr CapabilityId DIGITAL_INPUT_ID = 0x0201;
constexpr CapabilityId ANALOG_INPUT_0_ID = 0x0301;
constexpr uint8_t CAPABILITY_COUNT = 3;

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
        METADATA_SIMULATED,
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
        METADATA_SIMULATED,
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
        METADATA_SIMULATED,
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

struct State {
    bool indicator = false;
    bool digitalInput = false;
    uint16_t analogInput = 0;
};

inline OperationResult makeSuccess(ValueType type, uint32_t bits) {
    OperationResult result = {};
    result.status = OperationStatus::OK;
    result.value.type = type;
    result.value.bits = bits;
    return result;
}

class Handler : public LocalCapabilityHandler {
public:
    explicit Handler(State& state) : state_(state) {}
    ~Handler() = default;

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
                        state_.indicator ? 1U : 0U
                    );
                }
                if (operation == Operation::SET) {
                    state_.indicator = input.bits == 1U;
                    return makeSuccess(ValueType::NONE, 0);
                }
                break;

            case DIGITAL_INPUT_ID:
                if (operation == Operation::READ) {
                    return makeSuccess(
                        ValueType::BOOLEAN,
                        state_.digitalInput ? 1U : 0U
                    );
                }
                break;

            case ANALOG_INPUT_0_ID:
                if (operation == Operation::READ) {
                    return makeSuccess(
                        ValueType::NORMALIZED_U16,
                        state_.analogInput
                    );
                }
                break;
        }

        return makeCanonicalFailureResult(OperationStatus::OPERATION_FAILED);
    }

private:
    State& state_;
};

}  // namespace SimulatedCapabilities
