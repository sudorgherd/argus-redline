#include <unity.h>

#include "device_capabilities.h"
#include "heltec_v4_capabilities.h"
#include "simulated_capabilities.h"

namespace {

using namespace DeviceCapabilities;
using namespace HeltecV4Capabilities;

CapabilityValue makeValue(ValueType type, uint32_t bits) {
    CapabilityValue value = {};
    value.type = type;
    value.bits = bits;
    return value;
}

CallerContext makeCaller(CallerClass callerClass) {
    CallerContext caller = {};
    caller.callerClass = callerClass;
    return caller;
}

OperationResult dispatchProfile(
    HeltecV4CapabilityHandler& handler,
    CapabilityId id,
    Operation operation,
    const CapabilityValue& input,
    CallerClass callerClass = CallerClass::FIRMWARE_LOCAL,
    InterlockState interlock = InterlockState::CLEAR
) {
    return dispatchCapabilityOperation(
        registryView(),
        handler,
        id,
        operation,
        input,
        makeCaller(callerClass),
        interlock
    );
}

void assertStatus(OperationStatus expected, const OperationResult& result) {
    TEST_ASSERT_EQUAL_HEX8(
        static_cast<uint8_t>(expected),
        static_cast<uint8_t>(result.status)
    );
    TEST_ASSERT_TRUE(isValidOperationResult(result));
}

void assertBooleanResult(bool expected, const OperationResult& result) {
    assertStatus(OperationStatus::OK, result);
    TEST_ASSERT_EQUAL_HEX8(
        static_cast<uint8_t>(ValueType::BOOLEAN),
        static_cast<uint8_t>(result.value.type)
    );
    TEST_ASSERT_EQUAL_UINT32(expected ? 1 : 0, result.value.bits);
}

void testVerifiedResourcePlanConstants() {
    TEST_ASSERT_EQUAL_UINT8(0, Resources::USER_BUTTON_GPIO);
    TEST_ASSERT_EQUAL_UINT8(4, Resources::EXTERNAL_ANALOG_GPIO);
    TEST_ASSERT_EQUAL_UINT8(1, Resources::EXTERNAL_ANALOG_ADC_UNIT);
    TEST_ASSERT_EQUAL_UINT8(3, Resources::EXTERNAL_ANALOG_ADC_CHANNEL);
    TEST_ASSERT_EQUAL_UINT8(8, Resources::SX1262_NSS_GPIO);
    TEST_ASSERT_EQUAL_UINT8(9, Resources::SX1262_SCK_GPIO);
    TEST_ASSERT_EQUAL_UINT8(10, Resources::SX1262_MOSI_GPIO);
    TEST_ASSERT_EQUAL_UINT8(11, Resources::SX1262_MISO_GPIO);
    TEST_ASSERT_EQUAL_UINT8(12, Resources::SX1262_RESET_GPIO);
    TEST_ASSERT_EQUAL_UINT8(13, Resources::SX1262_BUSY_GPIO);
    TEST_ASSERT_EQUAL_UINT8(14, Resources::SX1262_DIO1_GPIO);
    TEST_ASSERT_EQUAL_UINT8(17, Resources::OLED_SDA_GPIO);
    TEST_ASSERT_EQUAL_UINT8(18, Resources::OLED_SCL_GPIO);
    TEST_ASSERT_EQUAL_UINT8(21, Resources::OLED_RESET_GPIO);
    TEST_ASSERT_EQUAL_UINT8(34, Resources::GNSS_POWER_GPIO);
    TEST_ASSERT_EQUAL_UINT8(35, Resources::APPLICATION_LED_GPIO);
    TEST_ASSERT_EQUAL_UINT8(36, Resources::VEXT_CONTROL_GPIO);
    TEST_ASSERT_EQUAL_UINT8(37, Resources::BOARD_ADC_CONTROL_GPIO);
    TEST_ASSERT_EQUAL_UINT8(2, Resources::RF_FRONT_END_ENABLE_GPIO);
    TEST_ASSERT_EQUAL_UINT8(7, Resources::RF_FRONT_END_POWER_GPIO);
    TEST_ASSERT_EQUAL_UINT8(45, Resources::RF_FRONT_END_PA_GPIO);
}

void testProfileRegistryAndDescriptorOrderAreExact() {
    const CapabilityRegistryView registry = registryView();
    TEST_ASSERT_TRUE(isValidCapabilityRegistry(registry));
    TEST_ASSERT_EQUAL_UINT8(3, capabilityCount(registry));
    TEST_ASSERT_EQUAL_HEX16(0x0101, DESCRIPTORS[0].id);
    TEST_ASSERT_EQUAL_HEX16(0x0201, DESCRIPTORS[1].id);
    TEST_ASSERT_EQUAL_HEX16(0x0301, DESCRIPTORS[2].id);

    const CapabilityClass classes[] = {
        CapabilityClass::INDICATOR_OUTPUT,
        CapabilityClass::DIGITAL_INPUT,
        CapabilityClass::ANALOG_INPUT
    };
    const uint8_t flags[] = {
        static_cast<uint8_t>(
            OPERATION_FLAG_DESCRIBE |
            OPERATION_FLAG_READ |
            OPERATION_FLAG_SET
        ),
        static_cast<uint8_t>(OPERATION_FLAG_DESCRIBE | OPERATION_FLAG_READ),
        static_cast<uint8_t>(OPERATION_FLAG_DESCRIBE | OPERATION_FLAG_READ)
    };
    const ValueType types[] = {
        ValueType::BOOLEAN,
        ValueType::BOOLEAN,
        ValueType::NORMALIZED_U16
    };
    const UnitCode units[] = {
        UnitCode::BOOLEAN,
        UnitCode::BOOLEAN,
        UnitCode::NORMALIZED
    };
    const uint8_t safetyPolicies[] = {
        MUTATING_INTERLOCK_SAFETY_POLICY_ID,
        NO_ADDITIONAL_SAFETY_POLICY_ID,
        NO_ADDITIONAL_SAFETY_POLICY_ID
    };
    const uint32_t maximums[] = {1, 1, 65535};

    for (uint8_t index = 0; index < CAPABILITY_COUNT; ++index) {
        CapabilityDescriptor snapshot = {};
        TEST_ASSERT_TRUE(getCapabilityByIndex(registry, index, snapshot));
        TEST_ASSERT_EQUAL_HEX8(
            static_cast<uint8_t>(classes[index]),
            static_cast<uint8_t>(snapshot.capabilityClass)
        );
        TEST_ASSERT_EQUAL_HEX8(flags[index], snapshot.operationFlags);
        TEST_ASSERT_EQUAL_HEX8(
            static_cast<uint8_t>(types[index]),
            static_cast<uint8_t>(snapshot.valueType)
        );
        TEST_ASSERT_EQUAL_HEX8(
            static_cast<uint8_t>(units[index]),
            snapshot.unitCode
        );
        TEST_ASSERT_EQUAL_HEX8(
            LOCAL_ONLY_AUTHORIZATION_POLICY_ID,
            snapshot.authorizationPolicyId
        );
        TEST_ASSERT_EQUAL_HEX8(
            safetyPolicies[index],
            snapshot.safetyPolicyId
        );
        TEST_ASSERT_EQUAL_UINT16(0, snapshot.metadataFlags);
        TEST_ASSERT_EQUAL_UINT16(0, snapshot.reserved);
        TEST_ASSERT_EQUAL_UINT32(0, snapshot.minimumBits);
        TEST_ASSERT_EQUAL_UINT32(maximums[index], snapshot.maximumBits);
    }
}

void testProductionAndSimulatedMetadataRemainSeparate() {
    for (uint8_t index = 0; index < CAPABILITY_COUNT; ++index) {
        TEST_ASSERT_EQUAL_UINT16(0, DESCRIPTORS[index].metadataFlags);
        TEST_ASSERT_EQUAL_UINT16(
            METADATA_SIMULATED,
            SimulatedCapabilities::DESCRIPTORS[index].metadataFlags
        );
    }
}

void testProfileStateDefaultsAndInitializationAreDeterministic() {
    ProfileState state;
    TEST_ASSERT_FALSE(state.indicatorRequested);
    TEST_ASSERT_FALSE(state.digitalInputAvailable);
    TEST_ASSERT_FALSE(state.digitalInputActive);
    TEST_ASSERT_FALSE(state.analogInputAvailable);
    TEST_ASSERT_EQUAL_UINT16(0, state.analogInputNormalized);

    state = {true, true, true, true, 65535};
    initializeProfileState(state);
    TEST_ASSERT_FALSE(state.indicatorRequested);
    TEST_ASSERT_FALSE(state.digitalInputAvailable);
    TEST_ASSERT_FALSE(state.digitalInputActive);
    TEST_ASSERT_FALSE(state.analogInputAvailable);
    TEST_ASSERT_EQUAL_UINT16(0, state.analogInputNormalized);
}

void testIndicatorRequestReadSetLifecycleUsesDispatcher() {
    ProfileState state;
    HeltecV4CapabilityHandler handler(state);
    assertBooleanResult(false, dispatchProfile(
        handler, APPLICATION_INDICATOR_ID, Operation::READ,
        makeValue(ValueType::NONE, 0)
    ));

    OperationResult result = dispatchProfile(
        handler, APPLICATION_INDICATOR_ID, Operation::SET,
        makeValue(ValueType::BOOLEAN, 1)
    );
    assertStatus(OperationStatus::OK, result);
    TEST_ASSERT_TRUE(isCanonicalNoneValue(result.value));
    TEST_ASSERT_TRUE(state.indicatorRequested);
    assertBooleanResult(true, dispatchProfile(
        handler, APPLICATION_INDICATOR_ID, Operation::READ,
        makeValue(ValueType::NONE, 0)
    ));

    result = dispatchProfile(
        handler, APPLICATION_INDICATOR_ID, Operation::SET,
        makeValue(ValueType::BOOLEAN, 0)
    );
    assertStatus(OperationStatus::OK, result);
    TEST_ASSERT_FALSE(state.indicatorRequested);
    assertBooleanResult(false, dispatchProfile(
        handler, APPLICATION_INDICATOR_ID, Operation::READ,
        makeValue(ValueType::NONE, 0)
    ));
}

void testDigitalInputUsesOnlyOwnerSuppliedSnapshot() {
    ProfileState state;
    HeltecV4CapabilityHandler handler(state);
    assertStatus(OperationStatus::HARDWARE_UNAVAILABLE, dispatchProfile(
        handler, DIGITAL_INPUT_ID, Operation::READ,
        makeValue(ValueType::NONE, 0)
    ));

    state.digitalInputAvailable = true;
    state.digitalInputActive = false;
    assertBooleanResult(false, dispatchProfile(
        handler, DIGITAL_INPUT_ID, Operation::READ,
        makeValue(ValueType::NONE, 0)
    ));
    state.digitalInputActive = true;
    assertBooleanResult(true, dispatchProfile(
        handler, DIGITAL_INPUT_ID, Operation::READ,
        makeValue(ValueType::NONE, 0)
    ));

    assertStatus(OperationStatus::UNSUPPORTED_OPERATION, dispatchProfile(
        handler, DIGITAL_INPUT_ID, Operation::SET,
        makeValue(ValueType::BOOLEAN, 0)
    ));
    TEST_ASSERT_TRUE(state.digitalInputActive);
}

void testAnalogInputUsesOnlyOwnerSuppliedNormalizedSnapshot() {
    ProfileState state;
    HeltecV4CapabilityHandler handler(state);
    assertStatus(OperationStatus::HARDWARE_UNAVAILABLE, dispatchProfile(
        handler, ANALOG_INPUT_0_ID, Operation::READ,
        makeValue(ValueType::NONE, 0)
    ));

    state.analogInputAvailable = true;
    const uint16_t values[] = {0, 1, 32768, 65535};
    for (uint16_t value : values) {
        state.analogInputNormalized = value;
        const OperationResult result = dispatchProfile(
            handler, ANALOG_INPUT_0_ID, Operation::READ,
            makeValue(ValueType::NONE, 0)
        );
        assertStatus(OperationStatus::OK, result);
        TEST_ASSERT_EQUAL_HEX8(
            static_cast<uint8_t>(ValueType::NORMALIZED_U16),
            static_cast<uint8_t>(result.value.type)
        );
        TEST_ASSERT_EQUAL_UINT32(value, result.value.bits);
    }

    assertStatus(OperationStatus::UNSUPPORTED_OPERATION, dispatchProfile(
        handler, ANALOG_INPUT_0_ID, Operation::SET,
        makeValue(ValueType::NORMALIZED_U16, 1)
    ));
    TEST_ASSERT_EQUAL_UINT16(65535, state.analogInputNormalized);
}

void testIndicatorPolicyDenialsPreserveRequestAndReadRemainsSafe() {
    ProfileState state;
    state.indicatorRequested = true;
    HeltecV4CapabilityHandler handler(state);
    assertStatus(OperationStatus::UNAUTHORIZED, dispatchProfile(
        handler, APPLICATION_INDICATOR_ID, Operation::SET,
        makeValue(ValueType::BOOLEAN, 0), CallerClass::FUTURE_REMOTE
    ));
    TEST_ASSERT_TRUE(state.indicatorRequested);

    assertStatus(OperationStatus::INTERLOCK_ACTIVE, dispatchProfile(
        handler, APPLICATION_INDICATOR_ID, Operation::SET,
        makeValue(ValueType::BOOLEAN, 0), CallerClass::FIRMWARE_LOCAL,
        InterlockState::ACTIVE
    ));
    TEST_ASSERT_TRUE(state.indicatorRequested);
    assertBooleanResult(true, dispatchProfile(
        handler, APPLICATION_INDICATOR_ID, Operation::READ,
        makeValue(ValueType::NONE, 0), CallerClass::FIRMWARE_LOCAL,
        InterlockState::ACTIVE
    ));
}

void testHandlerFailsClosedForUnexpectedBinding() {
    ProfileState state;
    HeltecV4CapabilityHandler handler(state);
    CapabilityDescriptor descriptor = DESCRIPTORS[0];
    descriptor.id = 0xFFFF;
    assertStatus(OperationStatus::OPERATION_FAILED, handler.execute(
        descriptor, Operation::READ, makeValue(ValueType::NONE, 0)
    ));
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(testVerifiedResourcePlanConstants);
    RUN_TEST(testProfileRegistryAndDescriptorOrderAreExact);
    RUN_TEST(testProductionAndSimulatedMetadataRemainSeparate);
    RUN_TEST(testProfileStateDefaultsAndInitializationAreDeterministic);
    RUN_TEST(testIndicatorRequestReadSetLifecycleUsesDispatcher);
    RUN_TEST(testDigitalInputUsesOnlyOwnerSuppliedSnapshot);
    RUN_TEST(testAnalogInputUsesOnlyOwnerSuppliedNormalizedSnapshot);
    RUN_TEST(testIndicatorPolicyDenialsPreserveRequestAndReadRemainsSafe);
    RUN_TEST(testHandlerFailsClosedForUnexpectedBinding);
    return UNITY_END();
}
