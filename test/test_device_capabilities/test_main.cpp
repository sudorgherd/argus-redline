#include <limits.h>
#include <unity.h>

#include "device_capabilities.h"

namespace {

using namespace DeviceCapabilities;

CapabilityDescriptor makeReadDescriptor(
    CapabilityClass capabilityClass = CapabilityClass::DIGITAL_INPUT,
    CapabilityId id = 0x0001,
    ValueType valueType = ValueType::BOOLEAN,
    UnitCode unitCode = UnitCode::BOOLEAN
) {
    CapabilityDescriptor descriptor = {};
    descriptor.id = id;
    descriptor.capabilityClass = capabilityClass;
    descriptor.operationFlags =
        OPERATION_FLAG_DESCRIBE | OPERATION_FLAG_READ;
    descriptor.valueType = valueType;
    descriptor.authorizationPolicyId = DEFAULT_AUTHORIZATION_POLICY_ID;
    descriptor.safetyPolicyId = NO_ADDITIONAL_SAFETY_POLICY_ID;
    descriptor.unitCode = static_cast<uint8_t>(unitCode);
    descriptor.minimumBits = valueType == ValueType::BOOLEAN ? 0 : 1;
    descriptor.maximumBits = valueType == ValueType::BOOLEAN ? 1 : 2;
    return descriptor;
}

CapabilityDescriptor makeDescriptorOnly(
    CapabilityClass capabilityClass,
    CapabilityId id = 0x0001
) {
    CapabilityDescriptor descriptor = {};
    descriptor.id = id;
    descriptor.capabilityClass = capabilityClass;
    descriptor.operationFlags = OPERATION_FLAG_DESCRIBE;
    descriptor.valueType = ValueType::NONE;
    descriptor.authorizationPolicyId = DEFAULT_AUTHORIZATION_POLICY_ID;
    descriptor.safetyPolicyId = NO_ADDITIONAL_SAFETY_POLICY_ID;
    descriptor.unitCode = static_cast<uint8_t>(UnitCode::NONE);
    return descriptor;
}

bool descriptorsEqual(
    const CapabilityDescriptor& left,
    const CapabilityDescriptor& right
) {
    return left.id == right.id &&
        left.capabilityClass == right.capabilityClass &&
        left.operationFlags == right.operationFlags &&
        left.valueType == right.valueType &&
        left.authorizationPolicyId == right.authorizationPolicyId &&
        left.safetyPolicyId == right.safetyPolicyId &&
        left.unitCode == right.unitCode &&
        left.metadataFlags == right.metadataFlags &&
        left.reserved == right.reserved &&
        left.minimumBits == right.minimumBits &&
        left.maximumBits == right.maximumBits;
}

void fillUniqueDescriptors(
    CapabilityDescriptor* descriptors,
    uint8_t count
) {
    for (uint8_t index = 0; index < count; ++index) {
        descriptors[index] = makeReadDescriptor(
            CapabilityClass::DIGITAL_INPUT,
            static_cast<CapabilityId>(index + 1)
        );
    }
}

CapabilityValue makeValue(ValueType type, uint32_t bits) {
    CapabilityValue value = {};
    value.type = type;
    value.bits = bits;
    return value;
}

CapabilityDescriptor makeValueDescriptor(
    ValueType type,
    uint32_t minimumBits,
    uint32_t maximumBits
) {
    if (type == ValueType::NONE) {
        return makeDescriptorOnly(CapabilityClass::LOCAL_STORAGE);
    }

    CapabilityDescriptor descriptor = makeReadDescriptor(
        CapabilityClass::SENSOR,
        0x0100,
        type,
        UnitCode::COUNT
    );
    descriptor.minimumBits = minimumBits;
    descriptor.maximumBits = maximumBits;
    return descriptor;
}

OperationResult makeResult(
    OperationStatus status,
    const CapabilityValue& value
) {
    OperationResult result = {};
    result.status = status;
    result.value = value;
    return result;
}

CallerContext makeCaller(CallerClass callerClass) {
    CallerContext caller = {};
    caller.callerClass = callerClass;
    return caller;
}

CapabilityDescriptor makeIndicatorDescriptor(bool readable = false) {
    CapabilityDescriptor descriptor = makeReadDescriptor(
        CapabilityClass::INDICATOR_OUTPUT
    );
    descriptor.operationFlags = OPERATION_FLAG_DESCRIBE | OPERATION_FLAG_SET;
    if (readable) {
        descriptor.operationFlags |= OPERATION_FLAG_READ;
    }
    return descriptor;
}

struct RecordingHandler : public LocalCapabilityHandler {
    uint8_t callCount = 0;
    CapabilityDescriptor lastDescriptor = {};
    Operation lastOperation = Operation::ENUMERATE;
    CapabilityValue lastInput = {};
    OperationResult configuredResult = {};

    OperationResult execute(
        const CapabilityDescriptor& descriptor,
        Operation operation,
        const CapabilityValue& input
    ) override {
        ++callCount;
        lastDescriptor = descriptor;
        lastOperation = operation;
        lastInput = input;
        return configuredResult;
    }
};

OperationResult makeOkResult(const CapabilityValue& value) {
    return makeResult(OperationStatus::OK, value);
}

void assertCanonicalFailure(
    OperationStatus expectedStatus,
    const OperationResult& result
) {
    TEST_ASSERT_EQUAL_HEX8(
        static_cast<uint8_t>(expectedStatus),
        static_cast<uint8_t>(result.status)
    );
    TEST_ASSERT_TRUE(isValidOperationResult(result));
    TEST_ASSERT_TRUE(isCanonicalNoneValue(result.value));
}

void testCapabilityIdContract() {
    TEST_ASSERT_EQUAL_UINT32(2, sizeof(CapabilityId));
    TEST_ASSERT_EQUAL_HEX16(0x0000, INVALID_CAPABILITY_ID);

    CapabilityDescriptor descriptor = makeReadDescriptor();
    descriptor.id = INVALID_CAPABILITY_ID;
    TEST_ASSERT_FALSE(isValidCapabilityDescriptor(descriptor));

    descriptor.id = 0x0001;
    TEST_ASSERT_TRUE(isValidCapabilityDescriptor(descriptor));
    descriptor.id = 0xF001;
    TEST_ASSERT_TRUE(isValidCapabilityDescriptor(descriptor));
}

void testCapabilityIdDoesNotImplyClass() {
    const CapabilityId ids[] = {0x0101, 0x0201, 0x0301, 0xFFFF};
    for (CapabilityId id : ids) {
        CapabilityDescriptor descriptor = makeReadDescriptor(
            CapabilityClass::DIGITAL_INPUT,
            id
        );
        TEST_ASSERT_TRUE(isValidCapabilityDescriptor(descriptor));
        descriptor.capabilityClass = CapabilityClass::ANALOG_INPUT;
        TEST_ASSERT_TRUE(isValidCapabilityDescriptor(descriptor));
    }
}

void testCapabilityClassNumericValues() {
    TEST_ASSERT_EQUAL_HEX8(0x00, static_cast<uint8_t>(CapabilityClass::INVALID));
    TEST_ASSERT_EQUAL_HEX8(0x01, static_cast<uint8_t>(CapabilityClass::DIGITAL_INPUT));
    TEST_ASSERT_EQUAL_HEX8(0x02, static_cast<uint8_t>(CapabilityClass::ANALOG_INPUT));
    TEST_ASSERT_EQUAL_HEX8(0x03, static_cast<uint8_t>(CapabilityClass::SENSOR));
    TEST_ASSERT_EQUAL_HEX8(0x04, static_cast<uint8_t>(CapabilityClass::INDICATOR_OUTPUT));
    TEST_ASSERT_EQUAL_HEX8(0x05, static_cast<uint8_t>(CapabilityClass::SIGNAL_OUTPUT));
    TEST_ASSERT_EQUAL_HEX8(0x06, static_cast<uint8_t>(CapabilityClass::RELAY_OUTPUT));
    TEST_ASSERT_EQUAL_HEX8(0x07, static_cast<uint8_t>(CapabilityClass::POWER_MONITOR));
    TEST_ASSERT_EQUAL_HEX8(0x08, static_cast<uint8_t>(CapabilityClass::LOCATION_PROVIDER));
    TEST_ASSERT_EQUAL_HEX8(0x09, static_cast<uint8_t>(CapabilityClass::LOCAL_STORAGE));
    TEST_ASSERT_EQUAL_HEX8(0x0A, static_cast<uint8_t>(CapabilityClass::LOCAL_PROCEDURE));
}

void testOperationNumericValues() {
    TEST_ASSERT_EQUAL_HEX8(0x00, static_cast<uint8_t>(Operation::ENUMERATE));
    TEST_ASSERT_EQUAL_HEX8(0x01, static_cast<uint8_t>(Operation::DESCRIBE));
    TEST_ASSERT_EQUAL_HEX8(0x02, static_cast<uint8_t>(Operation::READ));
    TEST_ASSERT_EQUAL_HEX8(0x03, static_cast<uint8_t>(Operation::SET));
    TEST_ASSERT_EQUAL_HEX8(0x04, static_cast<uint8_t>(Operation::TRIGGER));
    TEST_ASSERT_EQUAL_HEX8(0x05, static_cast<uint8_t>(Operation::RUN_LOCAL_PROCEDURE));
}

void testOperationFlagNumericValues() {
    TEST_ASSERT_EQUAL_HEX8(0x01, OPERATION_FLAG_DESCRIBE);
    TEST_ASSERT_EQUAL_HEX8(0x02, OPERATION_FLAG_READ);
    TEST_ASSERT_EQUAL_HEX8(0x04, OPERATION_FLAG_SET);
    TEST_ASSERT_EQUAL_HEX8(0x08, OPERATION_FLAG_TRIGGER);
    TEST_ASSERT_EQUAL_HEX8(0x10, OPERATION_FLAG_RUN_LOCAL_PROCEDURE);
    TEST_ASSERT_EQUAL_HEX8(0x1F, KNOWN_OPERATION_FLAGS);
    TEST_ASSERT_EQUAL_HEX8(0xE0, RESERVED_OPERATION_FLAGS);
}

void testValueTypeNumericValues() {
    TEST_ASSERT_EQUAL_HEX8(0x00, static_cast<uint8_t>(ValueType::NONE));
    TEST_ASSERT_EQUAL_HEX8(0x01, static_cast<uint8_t>(ValueType::BOOLEAN));
    TEST_ASSERT_EQUAL_HEX8(0x02, static_cast<uint8_t>(ValueType::UNSIGNED_32));
    TEST_ASSERT_EQUAL_HEX8(0x03, static_cast<uint8_t>(ValueType::SIGNED_32));
    TEST_ASSERT_EQUAL_HEX8(0x04, static_cast<uint8_t>(ValueType::NORMALIZED_U16));
    TEST_ASSERT_EQUAL_HEX8(0x05, static_cast<uint8_t>(ValueType::FIXED_Q16_16));
    TEST_ASSERT_EQUAL_HEX8(0x06, static_cast<uint8_t>(ValueType::ENUM_U16));
}

void testUnitCodeNumericValues() {
    TEST_ASSERT_EQUAL_HEX8(0x00, static_cast<uint8_t>(UnitCode::NONE));
    TEST_ASSERT_EQUAL_HEX8(0x01, static_cast<uint8_t>(UnitCode::BOOLEAN));
    TEST_ASSERT_EQUAL_HEX8(0x02, static_cast<uint8_t>(UnitCode::COUNT));
    TEST_ASSERT_EQUAL_HEX8(0x03, static_cast<uint8_t>(UnitCode::RAW_ADC));
    TEST_ASSERT_EQUAL_HEX8(0x04, static_cast<uint8_t>(UnitCode::NORMALIZED));
    TEST_ASSERT_EQUAL_HEX8(0x05, static_cast<uint8_t>(UnitCode::MILLIVOLTS));
}

void testDescriptorLayoutAndFields() {
    TEST_ASSERT_EQUAL_UINT32(20, sizeof(CapabilityDescriptor));
    TEST_ASSERT_EQUAL_HEX8(0x00, DEFAULT_AUTHORIZATION_POLICY_ID);
    TEST_ASSERT_EQUAL_HEX8(0x00, NO_ADDITIONAL_SAFETY_POLICY_ID);
    TEST_ASSERT_EQUAL_HEX16(0x0001, METADATA_SIMULATED);
    CapabilityDescriptor descriptor = makeReadDescriptor();
    descriptor.metadataFlags = METADATA_SIMULATED;
    TEST_ASSERT_EQUAL_HEX16(0x0001, descriptor.id);
    TEST_ASSERT_EQUAL_HEX16(0x0001, descriptor.metadataFlags);
    TEST_ASSERT_TRUE(isValidCapabilityDescriptor(descriptor));
}

void testRejectsUnknownClassTypeAndUnit() {
    CapabilityDescriptor descriptor = makeReadDescriptor();
    descriptor.capabilityClass = CapabilityClass::INVALID;
    TEST_ASSERT_FALSE(isValidCapabilityDescriptor(descriptor));
    descriptor = makeReadDescriptor();
    descriptor.capabilityClass = static_cast<CapabilityClass>(0x0B);
    TEST_ASSERT_FALSE(isValidCapabilityDescriptor(descriptor));
    descriptor = makeReadDescriptor();
    descriptor.valueType = static_cast<ValueType>(0x07);
    TEST_ASSERT_FALSE(isValidCapabilityDescriptor(descriptor));
    descriptor = makeReadDescriptor();
    descriptor.unitCode = 0x06;
    TEST_ASSERT_FALSE(isValidCapabilityDescriptor(descriptor));
}

void testRejectsEveryReservedOperationBit() {
    const uint8_t bits[] = {0x20, 0x40, 0x80};
    for (uint8_t bit : bits) {
        CapabilityDescriptor descriptor = makeReadDescriptor();
        descriptor.operationFlags |= bit;
        TEST_ASSERT_FALSE(isValidCapabilityDescriptor(descriptor));
    }
}

void testRejectsReservedDescriptorState() {
    for (uint8_t bit = 1; bit < 16; ++bit) {
        CapabilityDescriptor descriptor = makeReadDescriptor();
        descriptor.metadataFlags = static_cast<uint16_t>(1U << bit);
        TEST_ASSERT_FALSE(isValidCapabilityDescriptor(descriptor));
    }

    CapabilityDescriptor descriptor = makeReadDescriptor();
    descriptor.reserved = 1;
    TEST_ASSERT_FALSE(isValidCapabilityDescriptor(descriptor));
}

void testRejectsUnknownPolicyIds() {
    CapabilityDescriptor descriptor = makeReadDescriptor();
    descriptor.authorizationPolicyId = 1;
    TEST_ASSERT_FALSE(isValidCapabilityDescriptor(descriptor));
    descriptor = makeReadDescriptor();
    descriptor.safetyPolicyId = 1;
    TEST_ASSERT_TRUE(isValidCapabilityDescriptor(descriptor));
    descriptor.safetyPolicyId = 2;
    TEST_ASSERT_FALSE(isValidCapabilityDescriptor(descriptor));
}

void testReadOrientedClassMatrix() {
    const CapabilityClass classes[] = {
        CapabilityClass::DIGITAL_INPUT,
        CapabilityClass::ANALOG_INPUT,
        CapabilityClass::SENSOR,
        CapabilityClass::POWER_MONITOR,
        CapabilityClass::LOCATION_PROVIDER
    };
    for (CapabilityClass capabilityClass : classes) {
        CapabilityDescriptor descriptor = makeReadDescriptor(capabilityClass);
        TEST_ASSERT_TRUE(isValidCapabilityDescriptor(descriptor));
        descriptor.operationFlags = OPERATION_FLAG_DESCRIBE;
        descriptor.valueType = ValueType::NONE;
        descriptor.minimumBits = 0;
        descriptor.maximumBits = 0;
        TEST_ASSERT_FALSE(isValidCapabilityDescriptor(descriptor));
        descriptor = makeReadDescriptor(capabilityClass);
        descriptor.operationFlags |= OPERATION_FLAG_SET;
        TEST_ASSERT_FALSE(isValidCapabilityDescriptor(descriptor));
    }
}

void testIndicatorOperationMatrix() {
    CapabilityDescriptor descriptor = makeReadDescriptor(
        CapabilityClass::INDICATOR_OUTPUT
    );
    descriptor.operationFlags = OPERATION_FLAG_DESCRIBE | OPERATION_FLAG_SET;
    TEST_ASSERT_TRUE(isValidCapabilityDescriptor(descriptor));
    descriptor.operationFlags |= OPERATION_FLAG_READ;
    TEST_ASSERT_TRUE(isValidCapabilityDescriptor(descriptor));
    descriptor.operationFlags |= OPERATION_FLAG_TRIGGER;
    TEST_ASSERT_FALSE(isValidCapabilityDescriptor(descriptor));
}

void testReservedClassesAreDescriptorOnly() {
    const CapabilityClass classes[] = {
        CapabilityClass::SIGNAL_OUTPUT,
        CapabilityClass::RELAY_OUTPUT,
        CapabilityClass::LOCAL_STORAGE,
        CapabilityClass::LOCAL_PROCEDURE
    };
    for (CapabilityClass capabilityClass : classes) {
        CapabilityDescriptor descriptor = makeDescriptorOnly(capabilityClass);
        TEST_ASSERT_TRUE(isValidCapabilityDescriptor(descriptor));
        descriptor.operationFlags |= OPERATION_FLAG_SET;
        descriptor.valueType = ValueType::BOOLEAN;
        descriptor.unitCode = static_cast<uint8_t>(UnitCode::BOOLEAN);
        descriptor.maximumBits = 1;
        TEST_ASSERT_FALSE(isValidCapabilityDescriptor(descriptor));
        descriptor = makeDescriptorOnly(capabilityClass);
        descriptor.operationFlags |= OPERATION_FLAG_TRIGGER;
        TEST_ASSERT_FALSE(isValidCapabilityDescriptor(descriptor));
        descriptor = makeDescriptorOnly(capabilityClass);
        descriptor.operationFlags |= OPERATION_FLAG_RUN_LOCAL_PROCEDURE;
        TEST_ASSERT_FALSE(isValidCapabilityDescriptor(descriptor));
    }
}

void testRequiresDescribeAndCompatibleValueContract() {
    CapabilityDescriptor descriptor = makeReadDescriptor();
    descriptor.operationFlags = OPERATION_FLAG_READ;
    TEST_ASSERT_FALSE(isValidCapabilityDescriptor(descriptor));
    descriptor = makeReadDescriptor();
    descriptor.valueType = ValueType::NONE;
    descriptor.minimumBits = 0;
    descriptor.maximumBits = 0;
    TEST_ASSERT_FALSE(isValidCapabilityDescriptor(descriptor));
    descriptor = makeDescriptorOnly(CapabilityClass::LOCAL_STORAGE);
    descriptor.valueType = ValueType::BOOLEAN;
    descriptor.unitCode = static_cast<uint8_t>(UnitCode::BOOLEAN);
    descriptor.maximumBits = 1;
    TEST_ASSERT_FALSE(isValidCapabilityDescriptor(descriptor));
}

void testNoneBounds() {
    TEST_ASSERT_TRUE(hasValidBounds(ValueType::NONE, 0, 0));
    TEST_ASSERT_FALSE(hasValidBounds(ValueType::NONE, 1, 0));
    TEST_ASSERT_FALSE(hasValidBounds(ValueType::NONE, 0, 1));
}

void testBooleanBounds() {
    TEST_ASSERT_TRUE(hasValidBounds(ValueType::BOOLEAN, 0, 0));
    TEST_ASSERT_TRUE(hasValidBounds(ValueType::BOOLEAN, 0, 1));
    TEST_ASSERT_TRUE(hasValidBounds(ValueType::BOOLEAN, 1, 1));
    TEST_ASSERT_FALSE(hasValidBounds(ValueType::BOOLEAN, 1, 0));
    TEST_ASSERT_FALSE(hasValidBounds(ValueType::BOOLEAN, 0, 2));
    TEST_ASSERT_FALSE(hasValidBounds(ValueType::BOOLEAN, 2, 2));
}

void testUnsignedBounds() {
    TEST_ASSERT_TRUE(hasValidBounds(ValueType::UNSIGNED_32, 0, UINT32_MAX));
    TEST_ASSERT_TRUE(hasValidBounds(ValueType::UNSIGNED_32, UINT32_MAX, UINT32_MAX));
    TEST_ASSERT_FALSE(hasValidBounds(ValueType::UNSIGNED_32, UINT32_MAX, 0));
}

void testSignedBounds() {
    const uint32_t minimum = static_cast<uint32_t>(INT32_MIN);
    const uint32_t negativeOne = UINT32_MAX;
    const uint32_t maximum = static_cast<uint32_t>(INT32_MAX);
    TEST_ASSERT_TRUE(hasValidBounds(ValueType::SIGNED_32, minimum, maximum));
    TEST_ASSERT_TRUE(hasValidBounds(ValueType::SIGNED_32, negativeOne, 0));
    TEST_ASSERT_TRUE(hasValidBounds(ValueType::SIGNED_32, 0, 1));
    TEST_ASSERT_FALSE(hasValidBounds(ValueType::SIGNED_32, 1, 0));
    TEST_ASSERT_FALSE(hasValidBounds(ValueType::SIGNED_32, 0, negativeOne));
    TEST_ASSERT_FALSE(hasValidBounds(ValueType::SIGNED_32, maximum, minimum));
}

void testNormalizedBounds() {
    TEST_ASSERT_TRUE(hasValidBounds(ValueType::NORMALIZED_U16, 0, 0xFFFF));
    TEST_ASSERT_FALSE(hasValidBounds(ValueType::NORMALIZED_U16, 0x10000, 0x10000));
    TEST_ASSERT_FALSE(hasValidBounds(ValueType::NORMALIZED_U16, 0, 0x10000));
    TEST_ASSERT_FALSE(hasValidBounds(ValueType::NORMALIZED_U16, 0xFFFF, 0));
}

void testQ16BoundsUseSignedOrdering() {
    const uint32_t negativeOne = 0xFFFF0000U;
    const uint32_t positiveOne = 0x00010000U;
    TEST_ASSERT_TRUE(hasValidBounds(ValueType::FIXED_Q16_16, negativeOne, positiveOne));
    TEST_ASSERT_TRUE(hasValidBounds(ValueType::FIXED_Q16_16, 0x80000000U, 0x7FFFFFFFU));
    TEST_ASSERT_FALSE(hasValidBounds(ValueType::FIXED_Q16_16, positiveOne, negativeOne));
}

void testEnumBounds() {
    TEST_ASSERT_TRUE(hasValidBounds(ValueType::ENUM_U16, 0, 0xFFFF));
    TEST_ASSERT_FALSE(hasValidBounds(ValueType::ENUM_U16, 0x10000, 0x10000));
    TEST_ASSERT_FALSE(hasValidBounds(ValueType::ENUM_U16, 0, 0x10000));
    TEST_ASSERT_FALSE(hasValidBounds(ValueType::ENUM_U16, 2, 1));
}

void testEmptyRegistryIsValidAndHasNoResults() {
    const CapabilityRegistryView registry = {nullptr, 0};
    CapabilityDescriptor output = makeReadDescriptor();
    TEST_ASSERT_TRUE(isValidCapabilityRegistry(registry));
    TEST_ASSERT_EQUAL_UINT8(0, capabilityCount(registry));
    TEST_ASSERT_FALSE(getCapabilityByIndex(registry, 0, output));
    TEST_ASSERT_FALSE(findCapability(registry, 1, output));
    TEST_ASSERT_FALSE(findCapability(registry, INVALID_CAPABILITY_ID, output));
}

void testNullNonemptyRegistryIsInvalid() {
    const CapabilityRegistryView registry = {nullptr, 1};
    CapabilityDescriptor output = {};
    TEST_ASSERT_FALSE(isValidCapabilityRegistry(registry));
    TEST_ASSERT_EQUAL_UINT8(0, capabilityCount(registry));
    TEST_ASSERT_FALSE(getCapabilityByIndex(registry, 0, output));
    TEST_ASSERT_FALSE(findCapability(registry, 1, output));
}

void testSingleAndMultipleEntryRegistriesAreValid() {
    CapabilityDescriptor descriptors[3];
    fillUniqueDescriptors(descriptors, 3);
    const CapabilityRegistryView single = {descriptors, 1};
    const CapabilityRegistryView multiple = {descriptors, 3};
    TEST_ASSERT_TRUE(isValidCapabilityRegistry(single));
    TEST_ASSERT_EQUAL_UINT8(1, capabilityCount(single));
    TEST_ASSERT_TRUE(isValidCapabilityRegistry(multiple));
    TEST_ASSERT_EQUAL_UINT8(3, capabilityCount(multiple));
}

void testFullCapacityRegistryIsValid() {
    CapabilityDescriptor descriptors[MAX_CAPABILITIES];
    fillUniqueDescriptors(descriptors, MAX_CAPABILITIES);
    const CapabilityRegistryView registry = {
        descriptors,
        MAX_CAPABILITIES
    };
    TEST_ASSERT_TRUE(isValidCapabilityRegistry(registry));
    TEST_ASSERT_EQUAL_UINT8(MAX_CAPABILITIES, capabilityCount(registry));
}

void testOverCapacityRejectsBeforeTableAccess() {
    CapabilityDescriptor descriptor = makeReadDescriptor();
    const CapabilityRegistryView registry = {
        &descriptor,
        static_cast<uint8_t>(MAX_CAPABILITIES + 1)
    };
    CapabilityDescriptor output = {};
    TEST_ASSERT_FALSE(isValidCapabilityRegistry(registry));
    TEST_ASSERT_EQUAL_UINT8(0, capabilityCount(registry));
    TEST_ASSERT_FALSE(getCapabilityByIndex(registry, 0, output));
    TEST_ASSERT_FALSE(findCapability(registry, descriptor.id, output));
}

void testEveryMajorInvalidDescriptorInvalidatesRegistry() {
    CapabilityDescriptor descriptor = makeReadDescriptor();
    CapabilityRegistryView registry = {&descriptor, 1};

    descriptor.id = INVALID_CAPABILITY_ID;
    TEST_ASSERT_FALSE(isValidCapabilityRegistry(registry));
    descriptor = makeReadDescriptor();
    descriptor.capabilityClass = static_cast<CapabilityClass>(0xFF);
    TEST_ASSERT_FALSE(isValidCapabilityRegistry(registry));
    descriptor = makeReadDescriptor();
    descriptor.operationFlags |= 0x80;
    TEST_ASSERT_FALSE(isValidCapabilityRegistry(registry));
    descriptor = makeReadDescriptor();
    descriptor.operationFlags = OPERATION_FLAG_READ;
    TEST_ASSERT_FALSE(isValidCapabilityRegistry(registry));
    descriptor = makeReadDescriptor();
    descriptor.valueType = static_cast<ValueType>(0xFF);
    TEST_ASSERT_FALSE(isValidCapabilityRegistry(registry));
    descriptor = makeReadDescriptor();
    descriptor.authorizationPolicyId = 1;
    TEST_ASSERT_FALSE(isValidCapabilityRegistry(registry));
    descriptor = makeReadDescriptor();
    descriptor.unitCode = 0xFF;
    TEST_ASSERT_FALSE(isValidCapabilityRegistry(registry));
    descriptor = makeReadDescriptor();
    descriptor.metadataFlags = 0x0002;
    TEST_ASSERT_FALSE(isValidCapabilityRegistry(registry));
    descriptor = makeReadDescriptor();
    descriptor.reserved = 1;
    TEST_ASSERT_FALSE(isValidCapabilityRegistry(registry));
    descriptor = makeReadDescriptor();
    descriptor.minimumBits = 1;
    descriptor.maximumBits = 0;
    TEST_ASSERT_FALSE(isValidCapabilityRegistry(registry));
}

void testAdjacentDuplicateIdsAreRejected() {
    CapabilityDescriptor descriptors[2];
    fillUniqueDescriptors(descriptors, 2);
    descriptors[1].id = descriptors[0].id;
    const CapabilityRegistryView registry = {descriptors, 2};
    TEST_ASSERT_FALSE(isValidCapabilityRegistry(registry));
}

void testSeparatedAndFirstLastDuplicateIdsAreRejected() {
    CapabilityDescriptor descriptors[4];
    fillUniqueDescriptors(descriptors, 4);
    descriptors[2].id = descriptors[0].id;
    TEST_ASSERT_FALSE(isValidCapabilityRegistry({descriptors, 4}));
    fillUniqueDescriptors(descriptors, 4);
    descriptors[3].id = descriptors[0].id;
    TEST_ASSERT_FALSE(isValidCapabilityRegistry({descriptors, 4}));
}

void testMultipleRepeatedIdsAreRejectedWithoutSorting() {
    CapabilityDescriptor descriptors[5];
    fillUniqueDescriptors(descriptors, 5);
    descriptors[2].id = descriptors[0].id;
    descriptors[4].id = descriptors[0].id;
    TEST_ASSERT_FALSE(isValidCapabilityRegistry({descriptors, 5}));

    fillUniqueDescriptors(descriptors, 5);
    TEST_ASSERT_TRUE(isValidCapabilityRegistry({descriptors, 5}));
}

void testEnumerationPreservesExactTableOrder() {
    CapabilityDescriptor descriptors[3];
    descriptors[0] = makeReadDescriptor(
        CapabilityClass::SENSOR,
        0xF001
    );
    descriptors[1] = makeReadDescriptor(
        CapabilityClass::DIGITAL_INPUT,
        0x0002
    );
    descriptors[2] = makeReadDescriptor(
        CapabilityClass::ANALOG_INPUT,
        0x0100
    );
    const CapabilityRegistryView registry = {descriptors, 3};

    for (uint8_t index = 0; index < 3; ++index) {
        CapabilityDescriptor output = {};
        TEST_ASSERT_TRUE(getCapabilityByIndex(registry, index, output));
        TEST_ASSERT_TRUE(descriptorsEqual(descriptors[index], output));
    }
}

void testEnumerationRejectsOutOfRangeIndexes() {
    CapabilityDescriptor descriptors[2];
    fillUniqueDescriptors(descriptors, 2);
    const CapabilityRegistryView registry = {descriptors, 2};
    CapabilityDescriptor output = {};
    TEST_ASSERT_TRUE(getCapabilityByIndex(registry, 0, output));
    TEST_ASSERT_TRUE(getCapabilityByIndex(registry, 1, output));
    TEST_ASSERT_FALSE(getCapabilityByIndex(registry, 2, output));
    TEST_ASSERT_FALSE(getCapabilityByIndex(registry, UINT8_MAX, output));
}

void testEnumerationReturnsIndependentCopies() {
    CapabilityDescriptor descriptor = makeReadDescriptor();
    const CapabilityRegistryView registry = {&descriptor, 1};
    CapabilityDescriptor first = {};
    CapabilityDescriptor second = {};
    TEST_ASSERT_TRUE(getCapabilityByIndex(registry, 0, first));
    first.id = 0xFFFF;
    first.maximumBits = 0;
    TEST_ASSERT_TRUE(getCapabilityByIndex(registry, 0, second));
    TEST_ASSERT_TRUE(descriptorsEqual(descriptor, second));
    TEST_ASSERT_FALSE(descriptorsEqual(first, second));
}

void testLookupFindsFirstMiddleAndLastEntries() {
    CapabilityDescriptor descriptors[3];
    fillUniqueDescriptors(descriptors, 3);
    const CapabilityRegistryView registry = {descriptors, 3};
    CapabilityDescriptor output = {};
    TEST_ASSERT_TRUE(findCapability(registry, descriptors[0].id, output));
    TEST_ASSERT_TRUE(descriptorsEqual(descriptors[0], output));
    TEST_ASSERT_TRUE(findCapability(registry, descriptors[1].id, output));
    TEST_ASSERT_TRUE(descriptorsEqual(descriptors[1], output));
    TEST_ASSERT_TRUE(findCapability(registry, descriptors[2].id, output));
    TEST_ASSERT_TRUE(descriptorsEqual(descriptors[2], output));
}

void testLookupRejectsMissingAndZeroIds() {
    CapabilityDescriptor descriptors[2];
    fillUniqueDescriptors(descriptors, 2);
    const CapabilityRegistryView registry = {descriptors, 2};
    CapabilityDescriptor output = {};
    TEST_ASSERT_FALSE(findCapability(registry, 0xFFFF, output));
    TEST_ASSERT_FALSE(findCapability(registry, INVALID_CAPABILITY_ID, output));
}

void testLookupTreatsIdsAsOpaque() {
    CapabilityDescriptor descriptors[] = {
        makeReadDescriptor(CapabilityClass::DIGITAL_INPUT, 0xF001),
        makeReadDescriptor(CapabilityClass::ANALOG_INPUT, 0x0101)
    };
    const CapabilityRegistryView registry = {descriptors, 2};
    CapabilityDescriptor output = {};
    TEST_ASSERT_TRUE(findCapability(registry, 0xF001, output));
    TEST_ASSERT_EQUAL_HEX8(
        static_cast<uint8_t>(CapabilityClass::DIGITAL_INPUT),
        static_cast<uint8_t>(output.capabilityClass)
    );
    TEST_ASSERT_TRUE(findCapability(registry, 0x0101, output));
    TEST_ASSERT_EQUAL_HEX8(
        static_cast<uint8_t>(CapabilityClass::ANALOG_INPUT),
        static_cast<uint8_t>(output.capabilityClass)
    );
}

void testLookupReturnsIndependentCopies() {
    CapabilityDescriptor descriptor = makeReadDescriptor();
    const CapabilityRegistryView registry = {&descriptor, 1};
    CapabilityDescriptor first = {};
    CapabilityDescriptor second = {};
    TEST_ASSERT_TRUE(findCapability(registry, descriptor.id, first));
    first.id = 0xFFFF;
    first.minimumBits = 1;
    TEST_ASSERT_TRUE(findCapability(registry, descriptor.id, second));
    TEST_ASSERT_TRUE(descriptorsEqual(descriptor, second));
    TEST_ASSERT_FALSE(descriptorsEqual(first, second));
}

void testRepeatedQueriesAreDeterministicAndNonmutating() {
    CapabilityDescriptor descriptors[3];
    fillUniqueDescriptors(descriptors, 3);
    const CapabilityDescriptor originals[] = {
        descriptors[0], descriptors[1], descriptors[2]
    };
    const CapabilityRegistryView registry = {descriptors, 3};
    CapabilityDescriptor first = {};
    CapabilityDescriptor second = {};

    TEST_ASSERT_TRUE(getCapabilityByIndex(registry, 1, first));
    TEST_ASSERT_TRUE(getCapabilityByIndex(registry, 1, second));
    TEST_ASSERT_TRUE(descriptorsEqual(first, second));
    TEST_ASSERT_TRUE(findCapability(registry, descriptors[1].id, first));
    TEST_ASSERT_TRUE(findCapability(registry, descriptors[1].id, second));
    TEST_ASSERT_TRUE(descriptorsEqual(first, second));
    for (uint8_t index = 0; index < 3; ++index) {
        TEST_ASSERT_TRUE(descriptorsEqual(originals[index], descriptors[index]));
    }
}

void testCapabilityValueLayout() {
    TEST_ASSERT_EQUAL_UINT32(8, sizeof(CapabilityValue));
    CapabilityValue value = makeValue(ValueType::UNSIGNED_32, 0x89ABCDEFU);
    TEST_ASSERT_EQUAL_HEX8(
        static_cast<uint8_t>(ValueType::UNSIGNED_32),
        static_cast<uint8_t>(value.type)
    );
    TEST_ASSERT_EQUAL_HEX32(0x89ABCDEFU, value.bits);
}

void testCapabilityValueRejectsEachReservedByte() {
    for (uint8_t index = 0; index < 3; ++index) {
        CapabilityValue value = makeValue(ValueType::NONE, 0);
        value.reserved[index] = 1;
        TEST_ASSERT_FALSE(isValidCapabilityValue(value));
    }
}

void testNoneAndBooleanValueStructure() {
    TEST_ASSERT_TRUE(isValidCapabilityValue(makeValue(ValueType::NONE, 0)));
    TEST_ASSERT_FALSE(isValidCapabilityValue(makeValue(ValueType::NONE, 1)));
    TEST_ASSERT_TRUE(isValidCapabilityValue(makeValue(ValueType::BOOLEAN, 0)));
    TEST_ASSERT_TRUE(isValidCapabilityValue(makeValue(ValueType::BOOLEAN, 1)));
    TEST_ASSERT_FALSE(isValidCapabilityValue(makeValue(ValueType::BOOLEAN, 2)));
    TEST_ASSERT_FALSE(isValidCapabilityValue(
        makeValue(ValueType::BOOLEAN, UINT32_MAX)
    ));
}

void testUnsignedValueAcceptsEveryBitPattern() {
    const uint32_t values[] = {
        0, 1, 0x7FFFFFFFU, 0x80000000U, UINT32_MAX
    };
    for (uint32_t bits : values) {
        TEST_ASSERT_TRUE(isValidCapabilityValue(
            makeValue(ValueType::UNSIGNED_32, bits)
        ));
    }
}

void testSignedValueAcceptsEveryRepresentativeBitPattern() {
    const uint32_t values[] = {
        static_cast<uint32_t>(INT32_MIN),
        UINT32_MAX,
        0,
        1,
        static_cast<uint32_t>(INT32_MAX)
    };
    for (uint32_t bits : values) {
        TEST_ASSERT_TRUE(isValidCapabilityValue(
            makeValue(ValueType::SIGNED_32, bits)
        ));
    }
}

void testNormalizedAndEnumValueStructure() {
    const ValueType types[] = {
        ValueType::NORMALIZED_U16,
        ValueType::ENUM_U16
    };
    for (ValueType type : types) {
        TEST_ASSERT_TRUE(isValidCapabilityValue(makeValue(type, 0)));
        TEST_ASSERT_TRUE(isValidCapabilityValue(makeValue(type, 1)));
        TEST_ASSERT_TRUE(isValidCapabilityValue(makeValue(type, 0xFFFF)));
        TEST_ASSERT_FALSE(isValidCapabilityValue(makeValue(type, 0x10000)));
    }
}

void testQ16ValueAcceptsSignedRawPatterns() {
    const uint32_t values[] = {
        static_cast<uint32_t>(INT32_MIN),
        0xFFFF0000U,
        0,
        0x00010000U,
        static_cast<uint32_t>(INT32_MAX)
    };
    for (uint32_t bits : values) {
        TEST_ASSERT_TRUE(isValidCapabilityValue(
            makeValue(ValueType::FIXED_Q16_16, bits)
        ));
    }
}

void testUnknownValueTypeIsInvalid() {
    TEST_ASSERT_FALSE(isValidCapabilityValue(
        makeValue(static_cast<ValueType>(0xFF), 0)
    ));
}

void testDescriptorValueCompatibilityRequiresExactType() {
    const ValueType types[] = {
        ValueType::BOOLEAN,
        ValueType::UNSIGNED_32,
        ValueType::SIGNED_32,
        ValueType::NORMALIZED_U16,
        ValueType::FIXED_Q16_16,
        ValueType::ENUM_U16
    };
    for (ValueType type : types) {
        CapabilityDescriptor descriptor = makeValueDescriptor(type, 0, 1);
        TEST_ASSERT_TRUE(isValueCompatible(
            descriptor,
            makeValue(type, 0)
        ));
        const ValueType other = type == ValueType::BOOLEAN
            ? ValueType::UNSIGNED_32
            : ValueType::BOOLEAN;
        TEST_ASSERT_FALSE(isValueCompatible(
            descriptor,
            makeValue(other, 0)
        ));
    }
}

void testDescriptorNoneValueCompatibility() {
    const CapabilityDescriptor descriptor = makeValueDescriptor(
        ValueType::NONE,
        0,
        0
    );
    TEST_ASSERT_TRUE(isValueCompatible(
        descriptor,
        makeValue(ValueType::NONE, 0)
    ));
    TEST_ASSERT_FALSE(isValueCompatible(
        descriptor,
        makeValue(ValueType::BOOLEAN, 0)
    ));
}

void testCompatibilityRejectsInvalidDescriptorAndValue() {
    CapabilityDescriptor descriptor = makeValueDescriptor(
        ValueType::BOOLEAN,
        0,
        1
    );
    const CapabilityValue valid = makeValue(ValueType::BOOLEAN, 0);
    descriptor.id = INVALID_CAPABILITY_ID;
    TEST_ASSERT_FALSE(isValueCompatible(descriptor, valid));

    descriptor = makeValueDescriptor(ValueType::BOOLEAN, 0, 1);
    CapabilityValue invalid = makeValue(ValueType::BOOLEAN, 0);
    invalid.reserved[1] = 1;
    TEST_ASSERT_FALSE(isValueCompatible(descriptor, invalid));
}

void testNoneAndBooleanRuntimeBounds() {
    CapabilityDescriptor descriptor = makeValueDescriptor(ValueType::NONE, 0, 0);
    TEST_ASSERT_TRUE(isValueWithinBounds(
        descriptor,
        makeValue(ValueType::NONE, 0)
    ));

    descriptor = makeValueDescriptor(ValueType::BOOLEAN, 0, 0);
    TEST_ASSERT_TRUE(isValueWithinBounds(descriptor, makeValue(ValueType::BOOLEAN, 0)));
    TEST_ASSERT_FALSE(isValueWithinBounds(descriptor, makeValue(ValueType::BOOLEAN, 1)));
    descriptor = makeValueDescriptor(ValueType::BOOLEAN, 1, 1);
    TEST_ASSERT_FALSE(isValueWithinBounds(descriptor, makeValue(ValueType::BOOLEAN, 0)));
    TEST_ASSERT_TRUE(isValueWithinBounds(descriptor, makeValue(ValueType::BOOLEAN, 1)));
    descriptor = makeValueDescriptor(ValueType::BOOLEAN, 0, 1);
    TEST_ASSERT_TRUE(isValueWithinBounds(descriptor, makeValue(ValueType::BOOLEAN, 0)));
    TEST_ASSERT_TRUE(isValueWithinBounds(descriptor, makeValue(ValueType::BOOLEAN, 1)));
}

void testUnsignedRuntimeBounds() {
    CapabilityDescriptor descriptor = makeValueDescriptor(
        ValueType::UNSIGNED_32,
        1,
        0xFFFFFFFEU
    );
    TEST_ASSERT_FALSE(isValueWithinBounds(descriptor, makeValue(ValueType::UNSIGNED_32, 0)));
    TEST_ASSERT_TRUE(isValueWithinBounds(descriptor, makeValue(ValueType::UNSIGNED_32, 1)));
    TEST_ASSERT_TRUE(isValueWithinBounds(descriptor, makeValue(ValueType::UNSIGNED_32, 0x80000000U)));
    TEST_ASSERT_TRUE(isValueWithinBounds(descriptor, makeValue(ValueType::UNSIGNED_32, 0xFFFFFFFEU)));
    TEST_ASSERT_FALSE(isValueWithinBounds(descriptor, makeValue(ValueType::UNSIGNED_32, UINT32_MAX)));

    descriptor = makeValueDescriptor(ValueType::UNSIGNED_32, 0, UINT32_MAX);
    TEST_ASSERT_TRUE(isValueWithinBounds(descriptor, makeValue(ValueType::UNSIGNED_32, 0)));
    TEST_ASSERT_TRUE(isValueWithinBounds(descriptor, makeValue(ValueType::UNSIGNED_32, UINT32_MAX)));
}

void testSignedRuntimeBounds() {
    const uint32_t negativeThree = static_cast<uint32_t>(-3);
    const uint32_t negativeTwo = static_cast<uint32_t>(-2);
    const uint32_t positiveTwo = 2;
    const uint32_t positiveThree = 3;
    CapabilityDescriptor descriptor = makeValueDescriptor(
        ValueType::SIGNED_32,
        negativeTwo,
        positiveTwo
    );
    TEST_ASSERT_FALSE(isValueWithinBounds(descriptor, makeValue(ValueType::SIGNED_32, negativeThree)));
    TEST_ASSERT_TRUE(isValueWithinBounds(descriptor, makeValue(ValueType::SIGNED_32, negativeTwo)));
    TEST_ASSERT_TRUE(isValueWithinBounds(descriptor, makeValue(ValueType::SIGNED_32, 0)));
    TEST_ASSERT_TRUE(isValueWithinBounds(descriptor, makeValue(ValueType::SIGNED_32, positiveTwo)));
    TEST_ASSERT_FALSE(isValueWithinBounds(descriptor, makeValue(ValueType::SIGNED_32, positiveThree)));

    descriptor = makeValueDescriptor(
        ValueType::SIGNED_32,
        static_cast<uint32_t>(INT32_MIN),
        static_cast<uint32_t>(INT32_MAX)
    );
    TEST_ASSERT_TRUE(isValueWithinBounds(descriptor, makeValue(ValueType::SIGNED_32, 0x80000000U)));
    TEST_ASSERT_TRUE(isValueWithinBounds(descriptor, makeValue(ValueType::SIGNED_32, 0x7FFFFFFFU)));
}

void testNormalizedRuntimeBounds() {
    CapabilityDescriptor descriptor = makeValueDescriptor(
        ValueType::NORMALIZED_U16,
        1,
        0xFFFE
    );
    TEST_ASSERT_FALSE(isValueWithinBounds(descriptor, makeValue(ValueType::NORMALIZED_U16, 0)));
    TEST_ASSERT_TRUE(isValueWithinBounds(descriptor, makeValue(ValueType::NORMALIZED_U16, 1)));
    TEST_ASSERT_TRUE(isValueWithinBounds(descriptor, makeValue(ValueType::NORMALIZED_U16, 0x8000)));
    TEST_ASSERT_TRUE(isValueWithinBounds(descriptor, makeValue(ValueType::NORMALIZED_U16, 0xFFFE)));
    TEST_ASSERT_FALSE(isValueWithinBounds(descriptor, makeValue(ValueType::NORMALIZED_U16, 0xFFFF)));
    TEST_ASSERT_FALSE(isValueWithinBounds(descriptor, makeValue(ValueType::NORMALIZED_U16, 0x10000)));
}

void testQ16RuntimeBoundsUseSignedRawOrdering() {
    const uint32_t below = 0xFFFE0000U;
    const uint32_t minimum = 0xFFFF0000U;
    const uint32_t maximum = 0x00010000U;
    const uint32_t above = 0x00020000U;
    const CapabilityDescriptor descriptor = makeValueDescriptor(
        ValueType::FIXED_Q16_16,
        minimum,
        maximum
    );
    TEST_ASSERT_FALSE(isValueWithinBounds(descriptor, makeValue(ValueType::FIXED_Q16_16, below)));
    TEST_ASSERT_TRUE(isValueWithinBounds(descriptor, makeValue(ValueType::FIXED_Q16_16, minimum)));
    TEST_ASSERT_TRUE(isValueWithinBounds(descriptor, makeValue(ValueType::FIXED_Q16_16, 0)));
    TEST_ASSERT_TRUE(isValueWithinBounds(descriptor, makeValue(ValueType::FIXED_Q16_16, maximum)));
    TEST_ASSERT_FALSE(isValueWithinBounds(descriptor, makeValue(ValueType::FIXED_Q16_16, above)));
}

void testEnumRuntimeBoundsAreNumericOnly() {
    const CapabilityDescriptor descriptor = makeValueDescriptor(
        ValueType::ENUM_U16,
        1,
        3
    );
    TEST_ASSERT_FALSE(isValueWithinBounds(descriptor, makeValue(ValueType::ENUM_U16, 0)));
    TEST_ASSERT_TRUE(isValueWithinBounds(descriptor, makeValue(ValueType::ENUM_U16, 1)));
    TEST_ASSERT_TRUE(isValueWithinBounds(descriptor, makeValue(ValueType::ENUM_U16, 2)));
    TEST_ASSERT_TRUE(isValueWithinBounds(descriptor, makeValue(ValueType::ENUM_U16, 3)));
    TEST_ASSERT_FALSE(isValueWithinBounds(descriptor, makeValue(ValueType::ENUM_U16, 4)));
}

void testBoundValidationRejectsInvalidDescriptorValueAndTypeMismatch() {
    CapabilityDescriptor descriptor = makeValueDescriptor(ValueType::BOOLEAN, 0, 1);
    CapabilityValue value = makeValue(ValueType::BOOLEAN, 0);
    descriptor.reserved = 1;
    TEST_ASSERT_FALSE(isValueWithinBounds(descriptor, value));
    descriptor = makeValueDescriptor(ValueType::BOOLEAN, 0, 1);
    value.reserved[0] = 1;
    TEST_ASSERT_FALSE(isValueWithinBounds(descriptor, value));
    TEST_ASSERT_FALSE(isValueWithinBounds(
        descriptor,
        makeValue(ValueType::UNSIGNED_32, 0)
    ));
}

void testOperationStatusNumericValues() {
    TEST_ASSERT_EQUAL_HEX8(0x00, static_cast<uint8_t>(OperationStatus::OK));
    TEST_ASSERT_EQUAL_HEX8(0x01, static_cast<uint8_t>(OperationStatus::CAPABILITY_NOT_FOUND));
    TEST_ASSERT_EQUAL_HEX8(0x02, static_cast<uint8_t>(OperationStatus::UNSUPPORTED_OPERATION));
    TEST_ASSERT_EQUAL_HEX8(0x03, static_cast<uint8_t>(OperationStatus::INVALID_VALUE_TYPE));
    TEST_ASSERT_EQUAL_HEX8(0x04, static_cast<uint8_t>(OperationStatus::VALUE_OUT_OF_RANGE));
    TEST_ASSERT_EQUAL_HEX8(0x05, static_cast<uint8_t>(OperationStatus::UNAUTHORIZED));
    TEST_ASSERT_EQUAL_HEX8(0x06, static_cast<uint8_t>(OperationStatus::INTERLOCK_ACTIVE));
    TEST_ASSERT_EQUAL_HEX8(0x07, static_cast<uint8_t>(OperationStatus::HARDWARE_UNAVAILABLE));
    TEST_ASSERT_EQUAL_HEX8(0x08, static_cast<uint8_t>(OperationStatus::OPERATION_FAILED));
    TEST_ASSERT_EQUAL_HEX8(0x09, static_cast<uint8_t>(OperationStatus::BUSY));
    TEST_ASSERT_EQUAL_HEX8(0x0A, static_cast<uint8_t>(OperationStatus::INVALID_DESCRIPTOR));
    TEST_ASSERT_FALSE(isKnownOperationStatus(static_cast<OperationStatus>(0x0B)));
    TEST_ASSERT_FALSE(isKnownOperationStatus(static_cast<OperationStatus>(0xFF)));
}

void testOperationResultLayoutAndOkValues() {
    TEST_ASSERT_EQUAL_UINT32(12, sizeof(OperationResult));
    const CapabilityValue values[] = {
        makeValue(ValueType::NONE, 0),
        makeValue(ValueType::BOOLEAN, 1),
        makeValue(ValueType::UNSIGNED_32, UINT32_MAX),
        makeValue(ValueType::SIGNED_32, UINT32_MAX),
        makeValue(ValueType::NORMALIZED_U16, 0xFFFF),
        makeValue(ValueType::FIXED_Q16_16, 0xFFFF0000U),
        makeValue(ValueType::ENUM_U16, 0xFFFF)
    };
    for (const CapabilityValue& value : values) {
        TEST_ASSERT_TRUE(isValidOperationResult(
            makeResult(OperationStatus::OK, value)
        ));
    }
}

void testOperationResultRejectsEachReservedByte() {
    for (uint8_t index = 0; index < 3; ++index) {
        OperationResult result = makeResult(
            OperationStatus::OK,
            makeValue(ValueType::NONE, 0)
        );
        result.reserved[index] = 1;
        TEST_ASSERT_FALSE(isValidOperationResult(result));
    }
}

void testOperationResultRejectsInvalidEmbeddedValue() {
    CapabilityValue value = makeValue(ValueType::BOOLEAN, 2);
    TEST_ASSERT_FALSE(isValidOperationResult(
        makeResult(OperationStatus::OK, value)
    ));
    value = makeValue(ValueType::NONE, 0);
    value.reserved[2] = 1;
    TEST_ASSERT_FALSE(isValidOperationResult(
        makeResult(OperationStatus::OK, value)
    ));
}

void testEveryFailureStatusAcceptsCanonicalNoneOnly() {
    const OperationStatus statuses[] = {
        OperationStatus::CAPABILITY_NOT_FOUND,
        OperationStatus::UNSUPPORTED_OPERATION,
        OperationStatus::INVALID_VALUE_TYPE,
        OperationStatus::VALUE_OUT_OF_RANGE,
        OperationStatus::UNAUTHORIZED,
        OperationStatus::INTERLOCK_ACTIVE,
        OperationStatus::HARDWARE_UNAVAILABLE,
        OperationStatus::OPERATION_FAILED,
        OperationStatus::BUSY,
        OperationStatus::INVALID_DESCRIPTOR
    };
    for (OperationStatus status : statuses) {
        TEST_ASSERT_TRUE(isValidOperationResult(
            makeResult(status, makeValue(ValueType::NONE, 0))
        ));
        TEST_ASSERT_FALSE(isValidOperationResult(
            makeResult(status, makeValue(ValueType::BOOLEAN, 0))
        ));
    }
}

void testOperationResultRejectsUnknownStatus() {
    TEST_ASSERT_FALSE(isValidOperationResult(makeResult(
        static_cast<OperationStatus>(0xFF),
        makeValue(ValueType::NONE, 0)
    )));
}

void testCallerClassNumericValuesAndContextLayout() {
    TEST_ASSERT_EQUAL_HEX8(0x00, static_cast<uint8_t>(CallerClass::INVALID));
    TEST_ASSERT_EQUAL_HEX8(0x01, static_cast<uint8_t>(CallerClass::FIRMWARE_LOCAL));
    TEST_ASSERT_EQUAL_HEX8(0x02, static_cast<uint8_t>(CallerClass::UI_LOCAL));
    TEST_ASSERT_EQUAL_HEX8(0x03, static_cast<uint8_t>(CallerClass::TEST));
    TEST_ASSERT_EQUAL_HEX8(0x04, static_cast<uint8_t>(CallerClass::FUTURE_REMOTE));
    TEST_ASSERT_EQUAL_UINT32(4, sizeof(CallerContext));
}

void testCallerContextAcceptsEveryRepresentableCaller() {
    const CallerClass callers[] = {
        CallerClass::FIRMWARE_LOCAL,
        CallerClass::UI_LOCAL,
        CallerClass::TEST,
        CallerClass::FUTURE_REMOTE
    };
    for (CallerClass callerClass : callers) {
        const CallerContext caller = makeCaller(callerClass);
        TEST_ASSERT_TRUE(isValidCallerContext(caller));
        TEST_ASSERT_TRUE(isValidCallerContext(caller));
    }
}

void testCallerContextRejectsInvalidUnknownAndReservedState() {
    TEST_ASSERT_FALSE(isValidCallerContext(makeCaller(CallerClass::INVALID)));
    TEST_ASSERT_FALSE(isValidCallerContext(
        makeCaller(static_cast<CallerClass>(0x05))
    ));
    TEST_ASSERT_FALSE(isValidCallerContext(
        makeCaller(static_cast<CallerClass>(0xFF))
    ));
    for (uint8_t index = 0; index < 3; ++index) {
        CallerContext caller = makeCaller(CallerClass::FIRMWARE_LOCAL);
        caller.reserved[index] = 1;
        TEST_ASSERT_FALSE(isValidCallerContext(caller));
    }
}

void testInterlockStateNumericValuesAndValidation() {
    TEST_ASSERT_EQUAL_HEX8(0x00, static_cast<uint8_t>(InterlockState::CLEAR));
    TEST_ASSERT_EQUAL_HEX8(0x01, static_cast<uint8_t>(InterlockState::ACTIVE));
    TEST_ASSERT_TRUE(isValidInterlockState(InterlockState::CLEAR));
    TEST_ASSERT_TRUE(isValidInterlockState(InterlockState::ACTIVE));
    TEST_ASSERT_FALSE(isValidInterlockState(
        static_cast<InterlockState>(0x02)
    ));
    TEST_ASSERT_FALSE(isValidInterlockState(
        static_cast<InterlockState>(0xFF)
    ));
}

void testPolicyIdVocabularyAndDescriptorValidation() {
    TEST_ASSERT_EQUAL_HEX8(0x00, LOCAL_ONLY_AUTHORIZATION_POLICY_ID);
    TEST_ASSERT_EQUAL_HEX8(0x00, DEFAULT_AUTHORIZATION_POLICY_ID);
    TEST_ASSERT_EQUAL_HEX8(0x00, NO_ADDITIONAL_SAFETY_POLICY_ID);
    TEST_ASSERT_EQUAL_HEX8(0x01, MUTATING_INTERLOCK_SAFETY_POLICY_ID);

    CapabilityDescriptor descriptor = makeReadDescriptor();
    descriptor.authorizationPolicyId = LOCAL_ONLY_AUTHORIZATION_POLICY_ID;
    TEST_ASSERT_TRUE(isValidCapabilityDescriptor(descriptor));
    descriptor.authorizationPolicyId = 1;
    TEST_ASSERT_FALSE(isValidCapabilityDescriptor(descriptor));

    descriptor = makeReadDescriptor();
    descriptor.safetyPolicyId = NO_ADDITIONAL_SAFETY_POLICY_ID;
    TEST_ASSERT_TRUE(isValidCapabilityDescriptor(descriptor));
    descriptor.safetyPolicyId = MUTATING_INTERLOCK_SAFETY_POLICY_ID;
    TEST_ASSERT_TRUE(isValidCapabilityDescriptor(descriptor));
    descriptor.safetyPolicyId = 2;
    TEST_ASSERT_FALSE(isValidCapabilityDescriptor(descriptor));
}

void testOperationSupportMapsDescriptorFlags() {
    const CapabilityDescriptor read = makeReadDescriptor();
    TEST_ASSERT_FALSE(supportsOperation(read, Operation::ENUMERATE));
    TEST_ASSERT_TRUE(supportsOperation(read, Operation::DESCRIBE));
    TEST_ASSERT_TRUE(supportsOperation(read, Operation::READ));
    TEST_ASSERT_FALSE(supportsOperation(read, Operation::SET));
    TEST_ASSERT_FALSE(supportsOperation(read, Operation::TRIGGER));
    TEST_ASSERT_FALSE(supportsOperation(read, Operation::RUN_LOCAL_PROCEDURE));

    const CapabilityDescriptor indicator = makeIndicatorDescriptor();
    TEST_ASSERT_TRUE(supportsOperation(indicator, Operation::DESCRIBE));
    TEST_ASSERT_FALSE(supportsOperation(indicator, Operation::READ));
    TEST_ASSERT_TRUE(supportsOperation(indicator, Operation::SET));
    TEST_ASSERT_FALSE(supportsOperation(
        indicator,
        static_cast<Operation>(0xFF)
    ));

    CapabilityDescriptor invalid = read;
    invalid.id = INVALID_CAPABILITY_ID;
    TEST_ASSERT_FALSE(supportsOperation(invalid, Operation::READ));
}

void testLocalOnlyAllowsLocalCallersForSupportedOperations() {
    const CapabilityDescriptor read = makeReadDescriptor();
    const CapabilityDescriptor set = makeIndicatorDescriptor();
    const CallerClass localCallers[] = {
        CallerClass::FIRMWARE_LOCAL,
        CallerClass::UI_LOCAL,
        CallerClass::TEST
    };
    for (CallerClass callerClass : localCallers) {
        const CallerContext caller = makeCaller(callerClass);
        TEST_ASSERT_TRUE(isOperationAuthorized(read, Operation::READ, caller));
        TEST_ASSERT_TRUE(isOperationAuthorized(set, Operation::SET, caller));
    }
}

void testLocalOnlyDeniesFutureRemoteForSupportedOperation() {
    const CapabilityDescriptor descriptor = makeIndicatorDescriptor();
    const CallerContext remote = makeCaller(CallerClass::FUTURE_REMOTE);
    TEST_ASSERT_TRUE(supportsOperation(descriptor, Operation::SET));
    TEST_ASSERT_TRUE(isValidCallerContext(remote));
    TEST_ASSERT_FALSE(isOperationAuthorized(
        descriptor,
        Operation::SET,
        remote
    ));
}

void testAuthorizationDoesNotCreateSupportOrGovernDiscovery() {
    const CapabilityDescriptor descriptor = makeReadDescriptor();
    const CallerContext caller = makeCaller(CallerClass::FIRMWARE_LOCAL);
    TEST_ASSERT_FALSE(isOperationAuthorized(descriptor, Operation::SET, caller));
    TEST_ASSERT_FALSE(isOperationAuthorized(
        descriptor,
        Operation::ENUMERATE,
        caller
    ));
    TEST_ASSERT_FALSE(isOperationAuthorized(
        descriptor,
        Operation::DESCRIBE,
        caller
    ));
    TEST_ASSERT_FALSE(isOperationAuthorized(
        descriptor,
        static_cast<Operation>(0xFF),
        caller
    ));
}

void testAuthorizationFailsClosedForMalformedInputs() {
    CapabilityDescriptor descriptor = makeReadDescriptor();
    CallerContext caller = makeCaller(CallerClass::FIRMWARE_LOCAL);
    descriptor.id = INVALID_CAPABILITY_ID;
    TEST_ASSERT_FALSE(isOperationAuthorized(descriptor, Operation::READ, caller));

    descriptor = makeReadDescriptor();
    caller.reserved[0] = 1;
    TEST_ASSERT_FALSE(isOperationAuthorized(descriptor, Operation::READ, caller));

    descriptor.authorizationPolicyId = 1;
    caller = makeCaller(CallerClass::FIRMWARE_LOCAL);
    TEST_ASSERT_FALSE(isOperationAuthorized(descriptor, Operation::READ, caller));
}

void testNoAdditionalInterlockAllowsClearAndActive() {
    const CapabilityDescriptor descriptor = makeIndicatorDescriptor();
    TEST_ASSERT_TRUE(isOperationSafe(
        descriptor,
        Operation::SET,
        InterlockState::CLEAR
    ));
    TEST_ASSERT_TRUE(isOperationSafe(
        descriptor,
        Operation::SET,
        InterlockState::ACTIVE
    ));
}

void testMutatingInterlockAllowsReadWhenActive() {
    CapabilityDescriptor descriptor = makeIndicatorDescriptor(true);
    descriptor.safetyPolicyId = MUTATING_INTERLOCK_SAFETY_POLICY_ID;
    TEST_ASSERT_TRUE(supportsOperation(descriptor, Operation::READ));
    TEST_ASSERT_TRUE(isOperationSafe(
        descriptor,
        Operation::READ,
        InterlockState::CLEAR
    ));
    TEST_ASSERT_TRUE(isOperationSafe(
        descriptor,
        Operation::READ,
        InterlockState::ACTIVE
    ));
}

void testMutatingInterlockBlocksActiveSetOnly() {
    CapabilityDescriptor descriptor = makeIndicatorDescriptor();
    descriptor.safetyPolicyId = MUTATING_INTERLOCK_SAFETY_POLICY_ID;
    TEST_ASSERT_TRUE(isOperationSafe(
        descriptor,
        Operation::SET,
        InterlockState::CLEAR
    ));
    TEST_ASSERT_FALSE(isOperationSafe(
        descriptor,
        Operation::SET,
        InterlockState::ACTIVE
    ));
}

void testSafetyFailsClosedForMalformedUnsupportedAndDiscoveryInputs() {
    CapabilityDescriptor descriptor = makeReadDescriptor();
    TEST_ASSERT_FALSE(isOperationSafe(
        descriptor,
        Operation::SET,
        InterlockState::CLEAR
    ));
    TEST_ASSERT_FALSE(isOperationSafe(
        descriptor,
        Operation::ENUMERATE,
        InterlockState::CLEAR
    ));
    TEST_ASSERT_FALSE(isOperationSafe(
        descriptor,
        Operation::DESCRIBE,
        InterlockState::CLEAR
    ));
    TEST_ASSERT_FALSE(isOperationSafe(
        descriptor,
        static_cast<Operation>(0xFF),
        InterlockState::CLEAR
    ));
    TEST_ASSERT_FALSE(isOperationSafe(
        descriptor,
        Operation::READ,
        static_cast<InterlockState>(0x02)
    ));
    descriptor.id = INVALID_CAPABILITY_ID;
    TEST_ASSERT_FALSE(isOperationSafe(
        descriptor,
        Operation::READ,
        InterlockState::CLEAR
    ));
    descriptor = makeReadDescriptor();
    descriptor.safetyPolicyId = 2;
    TEST_ASSERT_FALSE(isOperationSafe(
        descriptor,
        Operation::READ,
        InterlockState::CLEAR
    ));
}

void testSupportedAuthorizedAndInterlockedDecisionsRemainDistinct() {
    CapabilityDescriptor descriptor = makeIndicatorDescriptor();
    descriptor.safetyPolicyId = MUTATING_INTERLOCK_SAFETY_POLICY_ID;
    const CallerContext local = makeCaller(CallerClass::UI_LOCAL);
    const CallerContext remote = makeCaller(CallerClass::FUTURE_REMOTE);

    TEST_ASSERT_TRUE(supportsOperation(descriptor, Operation::SET));
    TEST_ASSERT_TRUE(isOperationAuthorized(descriptor, Operation::SET, local));
    TEST_ASSERT_FALSE(isOperationAuthorized(descriptor, Operation::SET, remote));
    TEST_ASSERT_FALSE(isOperationSafe(
        descriptor,
        Operation::SET,
        InterlockState::ACTIVE
    ));
}

void testDispatchRejectsInvalidRegistryBeforeLookup() {
    const CapabilityRegistryView registry = {nullptr, 1};
    RecordingHandler handler;
    const OperationResult result = dispatchCapabilityOperation(
        registry,
        handler,
        1,
        Operation::READ,
        makeValue(ValueType::NONE, 0),
        makeCaller(CallerClass::FUTURE_REMOTE),
        InterlockState::ACTIVE
    );
    assertCanonicalFailure(OperationStatus::INVALID_DESCRIPTOR, result);
    TEST_ASSERT_EQUAL_UINT8(0, handler.callCount);
}

void testDispatchRejectsMissingAndZeroIdsBeforePolicy() {
    const CapabilityDescriptor descriptor = makeReadDescriptor();
    const CapabilityRegistryView registry = {&descriptor, 1};
    RecordingHandler handler;
    const CapabilityId ids[] = {INVALID_CAPABILITY_ID, 0xFFFF};
    for (CapabilityId id : ids) {
        const OperationResult result = dispatchCapabilityOperation(
            registry,
            handler,
            id,
            Operation::READ,
            makeValue(ValueType::NONE, 0),
            makeCaller(CallerClass::FUTURE_REMOTE),
            InterlockState::ACTIVE
        );
        assertCanonicalFailure(OperationStatus::CAPABILITY_NOT_FOUND, result);
        TEST_ASSERT_EQUAL_UINT8(0, handler.callCount);
    }
}

void testDispatchRejectsNondispatchableAndUnsupportedOperations() {
    const CapabilityDescriptor descriptor = makeReadDescriptor();
    const CapabilityRegistryView registry = {&descriptor, 1};
    RecordingHandler handler;
    const Operation operations[] = {
        Operation::ENUMERATE,
        Operation::DESCRIBE,
        Operation::SET,
        Operation::TRIGGER,
        Operation::RUN_LOCAL_PROCEDURE,
        static_cast<Operation>(0xFF)
    };
    for (Operation operation : operations) {
        const OperationResult result = dispatchCapabilityOperation(
            registry,
            handler,
            descriptor.id,
            operation,
            makeValue(ValueType::BOOLEAN, 2),
            makeCaller(CallerClass::FUTURE_REMOTE),
            InterlockState::ACTIVE
        );
        assertCanonicalFailure(OperationStatus::UNSUPPORTED_OPERATION, result);
        TEST_ASSERT_EQUAL_UINT8(0, handler.callCount);
    }
}

void testDispatchRejectsMalformedAndNonNoneReadInput() {
    const CapabilityDescriptor descriptor = makeReadDescriptor();
    const CapabilityRegistryView registry = {&descriptor, 1};
    RecordingHandler handler;
    CapabilityValue malformed = makeValue(ValueType::NONE, 0);
    malformed.reserved[1] = 1;
    const CapabilityValue inputs[] = {
        malformed,
        makeValue(ValueType::BOOLEAN, 0),
        makeValue(ValueType::NONE, 1)
    };
    for (const CapabilityValue& input : inputs) {
        const OperationResult result = dispatchCapabilityOperation(
            registry,
            handler,
            descriptor.id,
            Operation::READ,
            input,
            makeCaller(CallerClass::FIRMWARE_LOCAL),
            InterlockState::CLEAR
        );
        assertCanonicalFailure(OperationStatus::INVALID_VALUE_TYPE, result);
        TEST_ASSERT_EQUAL_UINT8(0, handler.callCount);
    }
}

void testDispatchRejectsSetTypeAndStructureBeforeAuthorization() {
    const CapabilityDescriptor descriptor = makeIndicatorDescriptor();
    const CapabilityRegistryView registry = {&descriptor, 1};
    RecordingHandler handler;
    CapabilityValue malformed = makeValue(ValueType::BOOLEAN, 1);
    malformed.reserved[2] = 1;
    const CapabilityValue inputs[] = {
        makeValue(ValueType::UNSIGNED_32, 1),
        malformed
    };
    for (const CapabilityValue& input : inputs) {
        const OperationResult result = dispatchCapabilityOperation(
            registry,
            handler,
            descriptor.id,
            Operation::SET,
            input,
            makeCaller(CallerClass::FUTURE_REMOTE),
            InterlockState::ACTIVE
        );
        assertCanonicalFailure(OperationStatus::INVALID_VALUE_TYPE, result);
        TEST_ASSERT_EQUAL_UINT8(0, handler.callCount);
    }
}

void testDispatchRejectsSetBoundsBeforeAuthorization() {
    CapabilityDescriptor descriptor = makeIndicatorDescriptor();
    descriptor.valueType = ValueType::UNSIGNED_32;
    descriptor.unitCode = static_cast<uint8_t>(UnitCode::COUNT);
    descriptor.minimumBits = 10;
    descriptor.maximumBits = 20;
    const CapabilityRegistryView registry = {&descriptor, 1};
    RecordingHandler handler;
    const uint32_t values[] = {9, 21};
    for (uint32_t bits : values) {
        const OperationResult result = dispatchCapabilityOperation(
            registry,
            handler,
            descriptor.id,
            Operation::SET,
            makeValue(ValueType::UNSIGNED_32, bits),
            makeCaller(CallerClass::FUTURE_REMOTE),
            InterlockState::ACTIVE
        );
        assertCanonicalFailure(OperationStatus::VALUE_OUT_OF_RANGE, result);
        TEST_ASSERT_EQUAL_UINT8(0, handler.callCount);
    }
}

void testDispatchRejectsFutureRemoteBeforeInterlock() {
    CapabilityDescriptor descriptor = makeIndicatorDescriptor();
    descriptor.safetyPolicyId = MUTATING_INTERLOCK_SAFETY_POLICY_ID;
    const CapabilityRegistryView registry = {&descriptor, 1};
    RecordingHandler handler;
    const OperationResult result = dispatchCapabilityOperation(
        registry,
        handler,
        descriptor.id,
        Operation::SET,
        makeValue(ValueType::BOOLEAN, 1),
        makeCaller(CallerClass::FUTURE_REMOTE),
        InterlockState::ACTIVE
    );
    assertCanonicalFailure(OperationStatus::UNAUTHORIZED, result);
    TEST_ASSERT_EQUAL_UINT8(0, handler.callCount);
}

void testDispatchRejectsActiveMutatingInterlockAfterAuthorization() {
    CapabilityDescriptor descriptor = makeIndicatorDescriptor();
    descriptor.safetyPolicyId = MUTATING_INTERLOCK_SAFETY_POLICY_ID;
    const CapabilityRegistryView registry = {&descriptor, 1};
    RecordingHandler handler;
    const OperationResult result = dispatchCapabilityOperation(
        registry,
        handler,
        descriptor.id,
        Operation::SET,
        makeValue(ValueType::BOOLEAN, 1),
        makeCaller(CallerClass::UI_LOCAL),
        InterlockState::ACTIVE
    );
    assertCanonicalFailure(OperationStatus::INTERLOCK_ACTIVE, result);
    TEST_ASSERT_EQUAL_UINT8(0, handler.callCount);
}

void testDispatchReadCallsHandlerOnceAndReturnsTypedValue() {
    CapabilityDescriptor descriptor = makeReadDescriptor();
    const CapabilityRegistryView registry = {&descriptor, 1};
    RecordingHandler handler;
    handler.configuredResult = makeOkResult(
        makeValue(ValueType::BOOLEAN, 1)
    );
    const CapabilityValue input = makeValue(ValueType::NONE, 0);
    const OperationResult result = dispatchCapabilityOperation(
        registry,
        handler,
        descriptor.id,
        Operation::READ,
        input,
        makeCaller(CallerClass::FIRMWARE_LOCAL),
        InterlockState::CLEAR
    );
    TEST_ASSERT_EQUAL_UINT8(1, handler.callCount);
    TEST_ASSERT_TRUE(descriptorsEqual(descriptor, handler.lastDescriptor));
    TEST_ASSERT_EQUAL_HEX8(
        static_cast<uint8_t>(Operation::READ),
        static_cast<uint8_t>(handler.lastOperation)
    );
    TEST_ASSERT_TRUE(isCanonicalNoneValue(handler.lastInput));
    TEST_ASSERT_EQUAL_HEX8(
        static_cast<uint8_t>(OperationStatus::OK),
        static_cast<uint8_t>(result.status)
    );
    TEST_ASSERT_EQUAL_HEX8(
        static_cast<uint8_t>(ValueType::BOOLEAN),
        static_cast<uint8_t>(result.value.type)
    );
    TEST_ASSERT_EQUAL_UINT32(1, result.value.bits);
}

void testDispatchReadProceedsThroughActiveMutatingInterlock() {
    CapabilityDescriptor descriptor = makeReadDescriptor();
    descriptor.safetyPolicyId = MUTATING_INTERLOCK_SAFETY_POLICY_ID;
    const CapabilityRegistryView registry = {&descriptor, 1};
    RecordingHandler handler;
    handler.configuredResult = makeOkResult(
        makeValue(ValueType::BOOLEAN, 0)
    );
    const OperationResult result = dispatchCapabilityOperation(
        registry,
        handler,
        descriptor.id,
        Operation::READ,
        makeValue(ValueType::NONE, 0),
        makeCaller(CallerClass::UI_LOCAL),
        InterlockState::ACTIVE
    );
    TEST_ASSERT_EQUAL_UINT8(1, handler.callCount);
    TEST_ASSERT_EQUAL_HEX8(
        static_cast<uint8_t>(OperationStatus::OK),
        static_cast<uint8_t>(result.status)
    );
}

void testDispatchSetCallsHandlerOnceWithExactInput() {
    const CapabilityDescriptor descriptor = makeIndicatorDescriptor();
    const CapabilityRegistryView registry = {&descriptor, 1};
    RecordingHandler handler;
    handler.configuredResult = makeOkResult(makeValue(ValueType::NONE, 0));
    const CapabilityValue input = makeValue(ValueType::BOOLEAN, 1);
    const OperationResult result = dispatchCapabilityOperation(
        registry,
        handler,
        descriptor.id,
        Operation::SET,
        input,
        makeCaller(CallerClass::TEST),
        InterlockState::CLEAR
    );
    TEST_ASSERT_EQUAL_UINT8(1, handler.callCount);
    TEST_ASSERT_TRUE(descriptorsEqual(descriptor, handler.lastDescriptor));
    TEST_ASSERT_EQUAL_HEX8(
        static_cast<uint8_t>(Operation::SET),
        static_cast<uint8_t>(handler.lastOperation)
    );
    TEST_ASSERT_EQUAL_HEX8(
        static_cast<uint8_t>(input.type),
        static_cast<uint8_t>(handler.lastInput.type)
    );
    TEST_ASSERT_EQUAL_UINT32(input.bits, handler.lastInput.bits);
    TEST_ASSERT_EQUAL_HEX8(
        static_cast<uint8_t>(OperationStatus::OK),
        static_cast<uint8_t>(result.status)
    );
    TEST_ASSERT_TRUE(isCanonicalNoneValue(result.value));
}

void testDispatchPropagatesValidRuntimeFailures() {
    const CapabilityDescriptor descriptor = makeReadDescriptor();
    const CapabilityRegistryView registry = {&descriptor, 1};
    const OperationStatus statuses[] = {
        OperationStatus::HARDWARE_UNAVAILABLE,
        OperationStatus::OPERATION_FAILED,
        OperationStatus::BUSY
    };
    for (OperationStatus status : statuses) {
        RecordingHandler handler;
        handler.configuredResult = makeResult(
            status,
            makeValue(ValueType::NONE, 0)
        );
        const OperationResult result = dispatchCapabilityOperation(
            registry,
            handler,
            descriptor.id,
            Operation::READ,
            makeValue(ValueType::NONE, 0),
            makeCaller(CallerClass::FIRMWARE_LOCAL),
            InterlockState::CLEAR
        );
        TEST_ASSERT_EQUAL_UINT8(1, handler.callCount);
        TEST_ASSERT_EQUAL_HEX8(
            static_cast<uint8_t>(status),
            static_cast<uint8_t>(result.status)
        );
        TEST_ASSERT_TRUE(isValidOperationResult(result));
    }
}

void testDispatchNormalizesMalformedHandlerResults() {
    const CapabilityDescriptor descriptor = makeReadDescriptor();
    const CapabilityRegistryView registry = {&descriptor, 1};
    OperationResult results[] = {
        makeResult(
            static_cast<OperationStatus>(0xFF),
            makeValue(ValueType::NONE, 0)
        ),
        makeResult(OperationStatus::BUSY, makeValue(ValueType::NONE, 0)),
        makeResult(OperationStatus::BUSY, makeValue(ValueType::BOOLEAN, 0))
    };
    results[1].reserved[0] = 1;
    for (const OperationResult& configured : results) {
        RecordingHandler handler;
        handler.configuredResult = configured;
        const OperationResult result = dispatchCapabilityOperation(
            registry,
            handler,
            descriptor.id,
            Operation::READ,
            makeValue(ValueType::NONE, 0),
            makeCaller(CallerClass::FIRMWARE_LOCAL),
            InterlockState::CLEAR
        );
        TEST_ASSERT_EQUAL_UINT8(1, handler.callCount);
        assertCanonicalFailure(OperationStatus::OPERATION_FAILED, result);
    }
}

void testDispatchNormalizesHandlerOwnedBoundaryViolations() {
    const CapabilityDescriptor descriptor = makeReadDescriptor();
    const CapabilityRegistryView registry = {&descriptor, 1};
    const OperationStatus statuses[] = {
        OperationStatus::CAPABILITY_NOT_FOUND,
        OperationStatus::UNSUPPORTED_OPERATION,
        OperationStatus::INVALID_VALUE_TYPE,
        OperationStatus::VALUE_OUT_OF_RANGE,
        OperationStatus::UNAUTHORIZED,
        OperationStatus::INTERLOCK_ACTIVE,
        OperationStatus::INVALID_DESCRIPTOR
    };
    for (OperationStatus status : statuses) {
        RecordingHandler handler;
        handler.configuredResult = makeResult(
            status,
            makeValue(ValueType::NONE, 0)
        );
        const OperationResult result = dispatchCapabilityOperation(
            registry,
            handler,
            descriptor.id,
            Operation::READ,
            makeValue(ValueType::NONE, 0),
            makeCaller(CallerClass::FIRMWARE_LOCAL),
            InterlockState::CLEAR
        );
        TEST_ASSERT_EQUAL_UINT8(1, handler.callCount);
        assertCanonicalFailure(OperationStatus::OPERATION_FAILED, result);
    }
}

void testDispatchNormalizesInvalidSuccessfulReadOutput() {
    CapabilityDescriptor descriptors[] = {
        makeReadDescriptor(),
        makeReadDescriptor()
    };
    descriptors[1].maximumBits = 0;
    const CapabilityValue outputs[] = {
        makeValue(ValueType::UNSIGNED_32, 1),
        makeValue(ValueType::BOOLEAN, 1)
    };
    for (uint8_t index = 0; index < 2; ++index) {
        const CapabilityRegistryView registry = {&descriptors[index], 1};
        RecordingHandler handler;
        handler.configuredResult = makeOkResult(outputs[index]);
        const OperationResult result = dispatchCapabilityOperation(
            registry,
            handler,
            descriptors[index].id,
            Operation::READ,
            makeValue(ValueType::NONE, 0),
            makeCaller(CallerClass::FIRMWARE_LOCAL),
            InterlockState::CLEAR
        );
        TEST_ASSERT_EQUAL_UINT8(1, handler.callCount);
        assertCanonicalFailure(OperationStatus::OPERATION_FAILED, result);
    }
}

void testDispatchNormalizesNonNoneSuccessfulMutationOutput() {
    const CapabilityDescriptor descriptor = makeIndicatorDescriptor();
    const CapabilityRegistryView registry = {&descriptor, 1};
    RecordingHandler handler;
    handler.configuredResult = makeOkResult(
        makeValue(ValueType::BOOLEAN, 1)
    );
    const OperationResult result = dispatchCapabilityOperation(
        registry,
        handler,
        descriptor.id,
        Operation::SET,
        makeValue(ValueType::BOOLEAN, 1),
        makeCaller(CallerClass::FIRMWARE_LOCAL),
        InterlockState::CLEAR
    );
    TEST_ASSERT_EQUAL_UINT8(1, handler.callCount);
    assertCanonicalFailure(OperationStatus::OPERATION_FAILED, result);
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(testCapabilityIdContract);
    RUN_TEST(testCapabilityIdDoesNotImplyClass);
    RUN_TEST(testCapabilityClassNumericValues);
    RUN_TEST(testOperationNumericValues);
    RUN_TEST(testOperationFlagNumericValues);
    RUN_TEST(testValueTypeNumericValues);
    RUN_TEST(testUnitCodeNumericValues);
    RUN_TEST(testDescriptorLayoutAndFields);
    RUN_TEST(testRejectsUnknownClassTypeAndUnit);
    RUN_TEST(testRejectsEveryReservedOperationBit);
    RUN_TEST(testRejectsReservedDescriptorState);
    RUN_TEST(testRejectsUnknownPolicyIds);
    RUN_TEST(testReadOrientedClassMatrix);
    RUN_TEST(testIndicatorOperationMatrix);
    RUN_TEST(testReservedClassesAreDescriptorOnly);
    RUN_TEST(testRequiresDescribeAndCompatibleValueContract);
    RUN_TEST(testNoneBounds);
    RUN_TEST(testBooleanBounds);
    RUN_TEST(testUnsignedBounds);
    RUN_TEST(testSignedBounds);
    RUN_TEST(testNormalizedBounds);
    RUN_TEST(testQ16BoundsUseSignedOrdering);
    RUN_TEST(testEnumBounds);
    RUN_TEST(testEmptyRegistryIsValidAndHasNoResults);
    RUN_TEST(testNullNonemptyRegistryIsInvalid);
    RUN_TEST(testSingleAndMultipleEntryRegistriesAreValid);
    RUN_TEST(testFullCapacityRegistryIsValid);
    RUN_TEST(testOverCapacityRejectsBeforeTableAccess);
    RUN_TEST(testEveryMajorInvalidDescriptorInvalidatesRegistry);
    RUN_TEST(testAdjacentDuplicateIdsAreRejected);
    RUN_TEST(testSeparatedAndFirstLastDuplicateIdsAreRejected);
    RUN_TEST(testMultipleRepeatedIdsAreRejectedWithoutSorting);
    RUN_TEST(testEnumerationPreservesExactTableOrder);
    RUN_TEST(testEnumerationRejectsOutOfRangeIndexes);
    RUN_TEST(testEnumerationReturnsIndependentCopies);
    RUN_TEST(testLookupFindsFirstMiddleAndLastEntries);
    RUN_TEST(testLookupRejectsMissingAndZeroIds);
    RUN_TEST(testLookupTreatsIdsAsOpaque);
    RUN_TEST(testLookupReturnsIndependentCopies);
    RUN_TEST(testRepeatedQueriesAreDeterministicAndNonmutating);
    RUN_TEST(testCapabilityValueLayout);
    RUN_TEST(testCapabilityValueRejectsEachReservedByte);
    RUN_TEST(testNoneAndBooleanValueStructure);
    RUN_TEST(testUnsignedValueAcceptsEveryBitPattern);
    RUN_TEST(testSignedValueAcceptsEveryRepresentativeBitPattern);
    RUN_TEST(testNormalizedAndEnumValueStructure);
    RUN_TEST(testQ16ValueAcceptsSignedRawPatterns);
    RUN_TEST(testUnknownValueTypeIsInvalid);
    RUN_TEST(testDescriptorValueCompatibilityRequiresExactType);
    RUN_TEST(testDescriptorNoneValueCompatibility);
    RUN_TEST(testCompatibilityRejectsInvalidDescriptorAndValue);
    RUN_TEST(testNoneAndBooleanRuntimeBounds);
    RUN_TEST(testUnsignedRuntimeBounds);
    RUN_TEST(testSignedRuntimeBounds);
    RUN_TEST(testNormalizedRuntimeBounds);
    RUN_TEST(testQ16RuntimeBoundsUseSignedRawOrdering);
    RUN_TEST(testEnumRuntimeBoundsAreNumericOnly);
    RUN_TEST(testBoundValidationRejectsInvalidDescriptorValueAndTypeMismatch);
    RUN_TEST(testOperationStatusNumericValues);
    RUN_TEST(testOperationResultLayoutAndOkValues);
    RUN_TEST(testOperationResultRejectsEachReservedByte);
    RUN_TEST(testOperationResultRejectsInvalidEmbeddedValue);
    RUN_TEST(testEveryFailureStatusAcceptsCanonicalNoneOnly);
    RUN_TEST(testOperationResultRejectsUnknownStatus);
    RUN_TEST(testCallerClassNumericValuesAndContextLayout);
    RUN_TEST(testCallerContextAcceptsEveryRepresentableCaller);
    RUN_TEST(testCallerContextRejectsInvalidUnknownAndReservedState);
    RUN_TEST(testInterlockStateNumericValuesAndValidation);
    RUN_TEST(testPolicyIdVocabularyAndDescriptorValidation);
    RUN_TEST(testOperationSupportMapsDescriptorFlags);
    RUN_TEST(testLocalOnlyAllowsLocalCallersForSupportedOperations);
    RUN_TEST(testLocalOnlyDeniesFutureRemoteForSupportedOperation);
    RUN_TEST(testAuthorizationDoesNotCreateSupportOrGovernDiscovery);
    RUN_TEST(testAuthorizationFailsClosedForMalformedInputs);
    RUN_TEST(testNoAdditionalInterlockAllowsClearAndActive);
    RUN_TEST(testMutatingInterlockAllowsReadWhenActive);
    RUN_TEST(testMutatingInterlockBlocksActiveSetOnly);
    RUN_TEST(testSafetyFailsClosedForMalformedUnsupportedAndDiscoveryInputs);
    RUN_TEST(testSupportedAuthorizedAndInterlockedDecisionsRemainDistinct);
    RUN_TEST(testDispatchRejectsInvalidRegistryBeforeLookup);
    RUN_TEST(testDispatchRejectsMissingAndZeroIdsBeforePolicy);
    RUN_TEST(testDispatchRejectsNondispatchableAndUnsupportedOperations);
    RUN_TEST(testDispatchRejectsMalformedAndNonNoneReadInput);
    RUN_TEST(testDispatchRejectsSetTypeAndStructureBeforeAuthorization);
    RUN_TEST(testDispatchRejectsSetBoundsBeforeAuthorization);
    RUN_TEST(testDispatchRejectsFutureRemoteBeforeInterlock);
    RUN_TEST(testDispatchRejectsActiveMutatingInterlockAfterAuthorization);
    RUN_TEST(testDispatchReadCallsHandlerOnceAndReturnsTypedValue);
    RUN_TEST(testDispatchReadProceedsThroughActiveMutatingInterlock);
    RUN_TEST(testDispatchSetCallsHandlerOnceWithExactInput);
    RUN_TEST(testDispatchPropagatesValidRuntimeFailures);
    RUN_TEST(testDispatchNormalizesMalformedHandlerResults);
    RUN_TEST(testDispatchNormalizesHandlerOwnedBoundaryViolations);
    RUN_TEST(testDispatchNormalizesInvalidSuccessfulReadOutput);
    RUN_TEST(testDispatchNormalizesNonNoneSuccessfulMutationOutput);
    return UNITY_END();
}
