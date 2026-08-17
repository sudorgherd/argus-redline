#pragma once

#include <stdint.h>

namespace DeviceCapabilities {

using CapabilityId = uint16_t;

constexpr CapabilityId INVALID_CAPABILITY_ID = 0x0000;

enum class CapabilityClass : uint8_t {
    INVALID = 0x00,
    DIGITAL_INPUT = 0x01,
    ANALOG_INPUT = 0x02,
    SENSOR = 0x03,
    INDICATOR_OUTPUT = 0x04,
    SIGNAL_OUTPUT = 0x05,
    RELAY_OUTPUT = 0x06,
    POWER_MONITOR = 0x07,
    LOCATION_PROVIDER = 0x08,
    LOCAL_STORAGE = 0x09,
    LOCAL_PROCEDURE = 0x0A
};

enum class Operation : uint8_t {
    ENUMERATE = 0x00,
    DESCRIBE = 0x01,
    READ = 0x02,
    SET = 0x03,
    TRIGGER = 0x04,
    RUN_LOCAL_PROCEDURE = 0x05
};

enum class OperationFlag : uint8_t {
    DESCRIBE = 0x01,
    READ = 0x02,
    SET = 0x04,
    TRIGGER = 0x08,
    RUN_LOCAL_PROCEDURE = 0x10
};

constexpr uint8_t OPERATION_FLAG_DESCRIBE =
    static_cast<uint8_t>(OperationFlag::DESCRIBE);
constexpr uint8_t OPERATION_FLAG_READ =
    static_cast<uint8_t>(OperationFlag::READ);
constexpr uint8_t OPERATION_FLAG_SET =
    static_cast<uint8_t>(OperationFlag::SET);
constexpr uint8_t OPERATION_FLAG_TRIGGER =
    static_cast<uint8_t>(OperationFlag::TRIGGER);
constexpr uint8_t OPERATION_FLAG_RUN_LOCAL_PROCEDURE =
    static_cast<uint8_t>(OperationFlag::RUN_LOCAL_PROCEDURE);
constexpr uint8_t KNOWN_OPERATION_FLAGS =
    OPERATION_FLAG_DESCRIBE |
    OPERATION_FLAG_READ |
    OPERATION_FLAG_SET |
    OPERATION_FLAG_TRIGGER |
    OPERATION_FLAG_RUN_LOCAL_PROCEDURE;
constexpr uint8_t RESERVED_OPERATION_FLAGS = 0xE0;

enum class ValueType : uint8_t {
    NONE = 0x00,
    BOOLEAN = 0x01,
    UNSIGNED_32 = 0x02,
    SIGNED_32 = 0x03,
    NORMALIZED_U16 = 0x04,
    FIXED_Q16_16 = 0x05,
    ENUM_U16 = 0x06
};

enum class UnitCode : uint8_t {
    NONE = 0x00,
    BOOLEAN = 0x01,
    COUNT = 0x02,
    RAW_ADC = 0x03,
    NORMALIZED = 0x04,
    MILLIVOLTS = 0x05
};

enum class CallerClass : uint8_t {
    INVALID = 0x00,
    FIRMWARE_LOCAL = 0x01,
    UI_LOCAL = 0x02,
    TEST = 0x03,
    FUTURE_REMOTE = 0x04,
    HOST_LOCAL = 0x05
};

struct CallerContext {
    CallerClass callerClass;
    uint8_t reserved[3];
};

enum class InterlockState : uint8_t {
    CLEAR = 0x00,
    ACTIVE = 0x01
};

struct CapabilityValue {
    ValueType type;
    uint8_t reserved[3];
    uint32_t bits;
};

enum class OperationStatus : uint8_t {
    OK = 0x00,
    CAPABILITY_NOT_FOUND = 0x01,
    UNSUPPORTED_OPERATION = 0x02,
    INVALID_VALUE_TYPE = 0x03,
    VALUE_OUT_OF_RANGE = 0x04,
    UNAUTHORIZED = 0x05,
    INTERLOCK_ACTIVE = 0x06,
    HARDWARE_UNAVAILABLE = 0x07,
    OPERATION_FAILED = 0x08,
    BUSY = 0x09,
    INVALID_DESCRIPTOR = 0x0A
};

struct OperationResult {
    OperationStatus status;
    uint8_t reserved[3];
    CapabilityValue value;
};

struct CapabilityDiagnosticCounters {
    uint32_t lookupAttempts;
    uint32_t acceptedOperations;
    uint32_t authorizationDenials;
    uint32_t unsupportedOperations;
    uint32_t validationFailures;
    uint32_t interlockDenials;
    uint32_t busyOutcomes;
    uint32_t hardwareFailures;
};

struct CapabilityDiagnosticsSnapshot {
    CapabilityDiagnosticCounters counters;
    uint8_t lastStatusAvailable;
    OperationStatus lastStatus;
    uint8_t reserved[2];
};

constexpr uint8_t LOCAL_ONLY_AUTHORIZATION_POLICY_ID = 0x00;
constexpr uint8_t DEFAULT_AUTHORIZATION_POLICY_ID =
    LOCAL_ONLY_AUTHORIZATION_POLICY_ID;
constexpr uint8_t NO_ADDITIONAL_SAFETY_POLICY_ID = 0x00;
constexpr uint8_t MUTATING_INTERLOCK_SAFETY_POLICY_ID = 0x01;

constexpr uint16_t METADATA_SIMULATED = 0x0001;
constexpr uint16_t KNOWN_METADATA_FLAGS = METADATA_SIMULATED;

struct CapabilityDescriptor {
    CapabilityId id;
    CapabilityClass capabilityClass;
    uint8_t operationFlags;
    ValueType valueType;
    uint8_t authorizationPolicyId;
    uint8_t safetyPolicyId;
    uint8_t unitCode;
    uint16_t metadataFlags;
    uint16_t reserved;
    uint32_t minimumBits;
    uint32_t maximumBits;
};

static_assert(sizeof(CapabilityId) == 2, "CapabilityId must be 16 bits");
static_assert(sizeof(CapabilityClass) == 1, "CapabilityClass must be 8 bits");
static_assert(sizeof(Operation) == 1, "Operation must be 8 bits");
static_assert(sizeof(OperationFlag) == 1, "OperationFlag must be 8 bits");
static_assert(sizeof(ValueType) == 1, "ValueType must be 8 bits");
static_assert(sizeof(UnitCode) == 1, "UnitCode must be 8 bits");
static_assert(sizeof(CallerClass) == 1, "CallerClass must be 8 bits");
static_assert(sizeof(CallerContext) == 4,
    "CallerContext must be exactly 4 bytes");
static_assert(sizeof(InterlockState) == 1,
    "InterlockState must be 8 bits");
static_assert(sizeof(CapabilityValue) == 8,
    "CapabilityValue must be exactly 8 bytes");
static_assert(sizeof(OperationStatus) == 1,
    "OperationStatus must be 8 bits");
static_assert(sizeof(OperationResult) == 12,
    "OperationResult must be exactly 12 bytes");
static_assert(sizeof(CapabilityDiagnosticCounters) == 32,
    "CapabilityDiagnosticCounters must be exactly 32 bytes");
static_assert(sizeof(CapabilityDiagnosticsSnapshot) == 36,
    "CapabilityDiagnosticsSnapshot must be exactly 36 bytes");
static_assert(sizeof(CapabilityDescriptor) == 20,
    "CapabilityDescriptor must be exactly 20 bytes");

constexpr uint8_t MAX_CAPABILITIES = 16;

struct CapabilityRegistryView {
    const CapabilityDescriptor* descriptors;
    uint8_t count;
};

class LocalCapabilityHandler {
public:
    virtual OperationResult execute(
        const CapabilityDescriptor& descriptor,
        Operation operation,
        const CapabilityValue& input
    ) = 0;

protected:
    ~LocalCapabilityHandler() = default;
};

inline bool isKnownCapabilityClass(CapabilityClass capabilityClass) {
    switch (capabilityClass) {
        case CapabilityClass::DIGITAL_INPUT:
        case CapabilityClass::ANALOG_INPUT:
        case CapabilityClass::SENSOR:
        case CapabilityClass::INDICATOR_OUTPUT:
        case CapabilityClass::SIGNAL_OUTPUT:
        case CapabilityClass::RELAY_OUTPUT:
        case CapabilityClass::POWER_MONITOR:
        case CapabilityClass::LOCATION_PROVIDER:
        case CapabilityClass::LOCAL_STORAGE:
        case CapabilityClass::LOCAL_PROCEDURE:
            return true;

        case CapabilityClass::INVALID:
            return false;
    }

    return false;
}

inline bool isKnownValueType(ValueType valueType) {
    switch (valueType) {
        case ValueType::NONE:
        case ValueType::BOOLEAN:
        case ValueType::UNSIGNED_32:
        case ValueType::SIGNED_32:
        case ValueType::NORMALIZED_U16:
        case ValueType::FIXED_Q16_16:
        case ValueType::ENUM_U16:
            return true;
    }

    return false;
}

inline bool isValidCapabilityValue(const CapabilityValue& value) {
    if (value.reserved[0] != 0 ||
        value.reserved[1] != 0 ||
        value.reserved[2] != 0 ||
        !isKnownValueType(value.type)) {
        return false;
    }

    switch (value.type) {
        case ValueType::NONE:
            return value.bits == 0;

        case ValueType::BOOLEAN:
            return value.bits <= 1;

        case ValueType::UNSIGNED_32:
        case ValueType::SIGNED_32:
        case ValueType::FIXED_Q16_16:
            return true;

        case ValueType::NORMALIZED_U16:
        case ValueType::ENUM_U16:
            return (value.bits & 0xFFFF0000U) == 0;
    }

    return false;
}

inline bool isKnownOperationStatus(OperationStatus status) {
    switch (status) {
        case OperationStatus::OK:
        case OperationStatus::CAPABILITY_NOT_FOUND:
        case OperationStatus::UNSUPPORTED_OPERATION:
        case OperationStatus::INVALID_VALUE_TYPE:
        case OperationStatus::VALUE_OUT_OF_RANGE:
        case OperationStatus::UNAUTHORIZED:
        case OperationStatus::INTERLOCK_ACTIVE:
        case OperationStatus::HARDWARE_UNAVAILABLE:
        case OperationStatus::OPERATION_FAILED:
        case OperationStatus::BUSY:
        case OperationStatus::INVALID_DESCRIPTOR:
            return true;
    }

    return false;
}

inline bool isValidCapabilityDiagnosticsSnapshot(
    const CapabilityDiagnosticsSnapshot& snapshot
) {
    if (snapshot.lastStatusAvailable > 1 ||
        snapshot.reserved[0] != 0 ||
        snapshot.reserved[1] != 0) {
        return false;
    }

    if (snapshot.lastStatusAvailable == 0) {
        return snapshot.lastStatus == OperationStatus::OK;
    }

    return isKnownOperationStatus(snapshot.lastStatus);
}

class CapabilityDiagnostics {
public:
    bool recordOutcome(OperationStatus status, uint32_t amount = 1) {
        if (!isKnownOperationStatus(status)) {
            return false;
        }
        if (amount == 0) {
            return true;
        }

        if (status != OperationStatus::INVALID_DESCRIPTOR) {
            saturatingIncrement(snapshot_.counters.lookupAttempts, amount);
        }

        switch (status) {
            case OperationStatus::OK:
                saturatingIncrement(
                    snapshot_.counters.acceptedOperations,
                    amount
                );
                break;

            case OperationStatus::CAPABILITY_NOT_FOUND:
                break;

            case OperationStatus::UNSUPPORTED_OPERATION:
                saturatingIncrement(
                    snapshot_.counters.unsupportedOperations,
                    amount
                );
                break;

            case OperationStatus::INVALID_DESCRIPTOR:
            case OperationStatus::INVALID_VALUE_TYPE:
            case OperationStatus::VALUE_OUT_OF_RANGE:
                saturatingIncrement(
                    snapshot_.counters.validationFailures,
                    amount
                );
                break;

            case OperationStatus::UNAUTHORIZED:
                saturatingIncrement(
                    snapshot_.counters.authorizationDenials,
                    amount
                );
                break;

            case OperationStatus::INTERLOCK_ACTIVE:
                saturatingIncrement(
                    snapshot_.counters.interlockDenials,
                    amount
                );
                break;

            case OperationStatus::BUSY:
                saturatingIncrement(
                    snapshot_.counters.acceptedOperations,
                    amount
                );
                saturatingIncrement(snapshot_.counters.busyOutcomes, amount);
                break;

            case OperationStatus::HARDWARE_UNAVAILABLE:
            case OperationStatus::OPERATION_FAILED:
                saturatingIncrement(
                    snapshot_.counters.acceptedOperations,
                    amount
                );
                saturatingIncrement(
                    snapshot_.counters.hardwareFailures,
                    amount
                );
                break;
        }

        snapshot_.lastStatusAvailable = 1;
        snapshot_.lastStatus = status;
        return true;
    }

    CapabilityDiagnosticsSnapshot snapshot() const {
        return snapshot_;
    }

private:
    static void saturatingIncrement(uint32_t& value, uint32_t amount) {
        value = amount > UINT32_MAX - value
            ? UINT32_MAX
            : value + amount;
    }

    CapabilityDiagnosticsSnapshot snapshot_ = {};
};

inline bool isKnownUnitCode(uint8_t unitCode) {
    switch (static_cast<UnitCode>(unitCode)) {
        case UnitCode::NONE:
        case UnitCode::BOOLEAN:
        case UnitCode::COUNT:
        case UnitCode::RAW_ADC:
        case UnitCode::NORMALIZED:
        case UnitCode::MILLIVOLTS:
            return true;
    }

    return false;
}

inline bool hasKnownOperationFlags(uint8_t operationFlags) {
    return (operationFlags & static_cast<uint8_t>(~KNOWN_OPERATION_FLAGS)) == 0;
}

inline bool hasKnownAuthorizationPolicy(uint8_t policyId) {
    return policyId == LOCAL_ONLY_AUTHORIZATION_POLICY_ID;
}

inline bool hasKnownSafetyPolicy(uint8_t policyId) {
    return policyId == NO_ADDITIONAL_SAFETY_POLICY_ID ||
        policyId == MUTATING_INTERLOCK_SAFETY_POLICY_ID;
}

inline bool hasValidOperations(
    CapabilityClass capabilityClass,
    uint8_t operationFlags
) {
    if (!hasKnownOperationFlags(operationFlags) ||
        (operationFlags & OPERATION_FLAG_DESCRIBE) == 0) {
        return false;
    }

    const uint8_t describeRead =
        OPERATION_FLAG_DESCRIBE | OPERATION_FLAG_READ;
    const uint8_t describeSet =
        OPERATION_FLAG_DESCRIBE | OPERATION_FLAG_SET;
    const uint8_t describeReadSet = describeRead | OPERATION_FLAG_SET;

    switch (capabilityClass) {
        case CapabilityClass::DIGITAL_INPUT:
        case CapabilityClass::ANALOG_INPUT:
        case CapabilityClass::SENSOR:
        case CapabilityClass::POWER_MONITOR:
        case CapabilityClass::LOCATION_PROVIDER:
            return operationFlags == describeRead;

        case CapabilityClass::INDICATOR_OUTPUT:
            return operationFlags == describeSet ||
                operationFlags == describeReadSet;

        case CapabilityClass::SIGNAL_OUTPUT:
        case CapabilityClass::RELAY_OUTPUT:
        case CapabilityClass::LOCAL_STORAGE:
        case CapabilityClass::LOCAL_PROCEDURE:
            return operationFlags == OPERATION_FLAG_DESCRIBE;

        case CapabilityClass::INVALID:
            return false;
    }

    return false;
}

inline bool hasValidBounds(
    ValueType valueType,
    uint32_t minimumBits,
    uint32_t maximumBits
) {
    switch (valueType) {
        case ValueType::NONE:
            return minimumBits == 0 && maximumBits == 0;

        case ValueType::BOOLEAN:
            return minimumBits <= 1 &&
                maximumBits <= 1 &&
                minimumBits <= maximumBits;

        case ValueType::UNSIGNED_32:
            return minimumBits <= maximumBits;

        case ValueType::SIGNED_32:
        case ValueType::FIXED_Q16_16:
            return (minimumBits ^ 0x80000000U) <=
                (maximumBits ^ 0x80000000U);

        case ValueType::NORMALIZED_U16:
        case ValueType::ENUM_U16:
            return (minimumBits & 0xFFFF0000U) == 0 &&
                (maximumBits & 0xFFFF0000U) == 0 &&
                minimumBits <= maximumBits;
    }

    return false;
}

inline bool hasCompatibleValueContract(
    uint8_t operationFlags,
    ValueType valueType
) {
    const uint8_t valueOperations = OPERATION_FLAG_READ | OPERATION_FLAG_SET;
    const bool usesValue = (operationFlags & valueOperations) != 0;
    return usesValue
        ? valueType != ValueType::NONE
        : valueType == ValueType::NONE;
}

inline bool isValidCapabilityDescriptor(
    const CapabilityDescriptor& descriptor
) {
    return descriptor.id != INVALID_CAPABILITY_ID &&
        isKnownCapabilityClass(descriptor.capabilityClass) &&
        isKnownValueType(descriptor.valueType) &&
        hasValidOperations(
            descriptor.capabilityClass,
            descriptor.operationFlags
        ) &&
        hasCompatibleValueContract(
            descriptor.operationFlags,
            descriptor.valueType
        ) &&
        hasKnownAuthorizationPolicy(descriptor.authorizationPolicyId) &&
        hasKnownSafetyPolicy(descriptor.safetyPolicyId) &&
        isKnownUnitCode(descriptor.unitCode) &&
        (descriptor.metadataFlags &
            static_cast<uint16_t>(~KNOWN_METADATA_FLAGS)) == 0 &&
        descriptor.reserved == 0 &&
        hasValidBounds(
            descriptor.valueType,
            descriptor.minimumBits,
            descriptor.maximumBits
        );
}

inline bool isValidCallerContext(const CallerContext& caller) {
    if (caller.reserved[0] != 0 ||
        caller.reserved[1] != 0 ||
        caller.reserved[2] != 0) {
        return false;
    }

    switch (caller.callerClass) {
        case CallerClass::FIRMWARE_LOCAL:
        case CallerClass::UI_LOCAL:
        case CallerClass::TEST:
        case CallerClass::FUTURE_REMOTE:
        case CallerClass::HOST_LOCAL:
            return true;

        case CallerClass::INVALID:
            return false;
    }

    return false;
}

inline bool isValidInterlockState(InterlockState interlock) {
    switch (interlock) {
        case InterlockState::CLEAR:
        case InterlockState::ACTIVE:
            return true;
    }

    return false;
}

inline bool supportsOperation(
    const CapabilityDescriptor& descriptor,
    Operation operation
) {
    if (!isValidCapabilityDescriptor(descriptor)) {
        return false;
    }

    uint8_t requiredFlag = 0;
    switch (operation) {
        case Operation::ENUMERATE:
            return false;
        case Operation::DESCRIBE:
            requiredFlag = OPERATION_FLAG_DESCRIBE;
            break;
        case Operation::READ:
            requiredFlag = OPERATION_FLAG_READ;
            break;
        case Operation::SET:
            requiredFlag = OPERATION_FLAG_SET;
            break;
        case Operation::TRIGGER:
            requiredFlag = OPERATION_FLAG_TRIGGER;
            break;
        case Operation::RUN_LOCAL_PROCEDURE:
            requiredFlag = OPERATION_FLAG_RUN_LOCAL_PROCEDURE;
            break;
    }

    return (descriptor.operationFlags & requiredFlag) != 0;
}

inline bool isOperationAuthorized(
    const CapabilityDescriptor& descriptor,
    Operation operation,
    const CallerContext& caller
) {
    if (operation == Operation::ENUMERATE || operation == Operation::DESCRIBE ||
        !supportsOperation(descriptor, operation) ||
        !isValidCallerContext(caller) ||
        descriptor.authorizationPolicyId !=
            LOCAL_ONLY_AUTHORIZATION_POLICY_ID) {
        return false;
    }

    return caller.callerClass == CallerClass::FIRMWARE_LOCAL ||
        caller.callerClass == CallerClass::UI_LOCAL ||
        caller.callerClass == CallerClass::TEST ||
        caller.callerClass == CallerClass::HOST_LOCAL;
}

inline bool isOperationSafe(
    const CapabilityDescriptor& descriptor,
    Operation operation,
    InterlockState interlock
) {
    if (operation == Operation::ENUMERATE || operation == Operation::DESCRIBE ||
        !supportsOperation(descriptor, operation) ||
        !isValidInterlockState(interlock) ||
        !hasKnownSafetyPolicy(descriptor.safetyPolicyId)) {
        return false;
    }

    if (descriptor.safetyPolicyId == NO_ADDITIONAL_SAFETY_POLICY_ID ||
        interlock == InterlockState::CLEAR || operation == Operation::READ) {
        return true;
    }

    return false;
}

inline bool isValueCompatible(
    const CapabilityDescriptor& descriptor,
    const CapabilityValue& value
) {
    return isValidCapabilityDescriptor(descriptor) &&
        isValidCapabilityValue(value) &&
        descriptor.valueType == value.type;
}

inline uint32_t signedOrderingKey(uint32_t bits) {
    return bits ^ 0x80000000U;
}

inline bool isValueWithinBounds(
    const CapabilityDescriptor& descriptor,
    const CapabilityValue& value
) {
    if (!isValueCompatible(descriptor, value)) {
        return false;
    }

    switch (value.type) {
        case ValueType::NONE:
        case ValueType::BOOLEAN:
        case ValueType::UNSIGNED_32:
        case ValueType::NORMALIZED_U16:
        case ValueType::ENUM_U16:
            return value.bits >= descriptor.minimumBits &&
                value.bits <= descriptor.maximumBits;

        case ValueType::SIGNED_32:
        case ValueType::FIXED_Q16_16: {
            const uint32_t valueKey = signedOrderingKey(value.bits);
            return valueKey >= signedOrderingKey(descriptor.minimumBits) &&
                valueKey <= signedOrderingKey(descriptor.maximumBits);
        }
    }

    return false;
}

inline bool isValidOperationResult(const OperationResult& result) {
    if (!isKnownOperationStatus(result.status) ||
        result.reserved[0] != 0 ||
        result.reserved[1] != 0 ||
        result.reserved[2] != 0 ||
        !isValidCapabilityValue(result.value)) {
        return false;
    }

    if (result.status == OperationStatus::OK) {
        return true;
    }

    return result.value.type == ValueType::NONE && result.value.bits == 0;
}

inline bool isValidCapabilityRegistry(
    const CapabilityRegistryView& registry
) {
    if (registry.count > MAX_CAPABILITIES) {
        return false;
    }

    if (registry.count == 0) {
        return registry.descriptors == nullptr;
    }

    if (registry.descriptors == nullptr) {
        return false;
    }

    for (uint8_t index = 0; index < registry.count; ++index) {
        const CapabilityDescriptor& descriptor = registry.descriptors[index];
        if (!isValidCapabilityDescriptor(descriptor)) {
            return false;
        }

        for (uint8_t previous = 0; previous < index; ++previous) {
            if (registry.descriptors[previous].id == descriptor.id) {
                return false;
            }
        }
    }

    return true;
}

inline uint8_t capabilityCount(const CapabilityRegistryView& registry) {
    return isValidCapabilityRegistry(registry) ? registry.count : 0;
}

inline bool getCapabilityByIndex(
    const CapabilityRegistryView& registry,
    uint8_t index,
    CapabilityDescriptor& output
) {
    if (!isValidCapabilityRegistry(registry) || index >= registry.count) {
        return false;
    }

    output = registry.descriptors[index];
    return true;
}

inline bool findCapability(
    const CapabilityRegistryView& registry,
    CapabilityId id,
    CapabilityDescriptor& output
) {
    if (id == INVALID_CAPABILITY_ID ||
        !isValidCapabilityRegistry(registry)) {
        return false;
    }

    for (uint8_t index = 0; index < registry.count; ++index) {
        if (registry.descriptors[index].id == id) {
            output = registry.descriptors[index];
            return true;
        }
    }

    return false;
}

inline bool isCanonicalNoneValue(const CapabilityValue& value) {
    return isValidCapabilityValue(value) && value.type == ValueType::NONE;
}

inline OperationResult makeCanonicalFailureResult(OperationStatus status) {
    OperationResult result = {};
    result.status = status;
    result.value.type = ValueType::NONE;
    return result;
}

inline bool isHandlerDispatchableOperation(Operation operation) {
    switch (operation) {
        case Operation::READ:
        case Operation::SET:
        case Operation::TRIGGER:
        case Operation::RUN_LOCAL_PROCEDURE:
            return true;

        case Operation::ENUMERATE:
        case Operation::DESCRIBE:
            return false;
    }

    return false;
}

inline bool isHandlerOwnedStatus(OperationStatus status) {
    switch (status) {
        case OperationStatus::OK:
        case OperationStatus::HARDWARE_UNAVAILABLE:
        case OperationStatus::OPERATION_FAILED:
        case OperationStatus::BUSY:
            return true;

        case OperationStatus::CAPABILITY_NOT_FOUND:
        case OperationStatus::UNSUPPORTED_OPERATION:
        case OperationStatus::INVALID_VALUE_TYPE:
        case OperationStatus::VALUE_OUT_OF_RANGE:
        case OperationStatus::UNAUTHORIZED:
        case OperationStatus::INTERLOCK_ACTIVE:
        case OperationStatus::INVALID_DESCRIPTOR:
            return false;
    }

    return false;
}

inline OperationResult dispatchCapabilityOperation(
    const CapabilityRegistryView& registry,
    LocalCapabilityHandler& handler,
    CapabilityId capabilityId,
    Operation operation,
    const CapabilityValue& input,
    const CallerContext& caller,
    InterlockState interlock
) {
    if (!isValidCapabilityRegistry(registry)) {
        return makeCanonicalFailureResult(
            OperationStatus::INVALID_DESCRIPTOR
        );
    }

    CapabilityDescriptor descriptor = {};
    if (!findCapability(registry, capabilityId, descriptor)) {
        return makeCanonicalFailureResult(
            OperationStatus::CAPABILITY_NOT_FOUND
        );
    }

    if (!isHandlerDispatchableOperation(operation) ||
        !supportsOperation(descriptor, operation)) {
        return makeCanonicalFailureResult(
            OperationStatus::UNSUPPORTED_OPERATION
        );
    }

    if (operation == Operation::SET) {
        if (!isValidCapabilityValue(input) ||
            input.type != descriptor.valueType) {
            return makeCanonicalFailureResult(
                OperationStatus::INVALID_VALUE_TYPE
            );
        }
        if (!isValueWithinBounds(descriptor, input)) {
            return makeCanonicalFailureResult(
                OperationStatus::VALUE_OUT_OF_RANGE
            );
        }
    } else if (!isCanonicalNoneValue(input)) {
        return makeCanonicalFailureResult(
            OperationStatus::INVALID_VALUE_TYPE
        );
    }

    if (!isOperationAuthorized(descriptor, operation, caller)) {
        return makeCanonicalFailureResult(OperationStatus::UNAUTHORIZED);
    }

    if (!isOperationSafe(descriptor, operation, interlock)) {
        return makeCanonicalFailureResult(OperationStatus::INTERLOCK_ACTIVE);
    }

    const OperationResult handlerResult = handler.execute(
        descriptor,
        operation,
        input
    );

    if (!isValidOperationResult(handlerResult) ||
        !isHandlerOwnedStatus(handlerResult.status)) {
        return makeCanonicalFailureResult(OperationStatus::OPERATION_FAILED);
    }

    if (handlerResult.status != OperationStatus::OK) {
        return handlerResult;
    }

    if (operation == Operation::READ) {
        if (!isValueWithinBounds(descriptor, handlerResult.value)) {
            return makeCanonicalFailureResult(
                OperationStatus::OPERATION_FAILED
            );
        }
    } else if (!isCanonicalNoneValue(handlerResult.value)) {
        return makeCanonicalFailureResult(OperationStatus::OPERATION_FAILED);
    }

    return handlerResult;
}

inline OperationResult dispatchCapabilityOperationObserved(
    const CapabilityRegistryView& registry,
    LocalCapabilityHandler& handler,
    CapabilityId capabilityId,
    Operation operation,
    const CapabilityValue& input,
    const CallerContext& caller,
    InterlockState interlock,
    CapabilityDiagnostics& diagnostics
) {
    const OperationResult result = dispatchCapabilityOperation(
        registry,
        handler,
        capabilityId,
        operation,
        input,
        caller,
        interlock
    );
    diagnostics.recordOutcome(result.status);
    return result;
}

}  // namespace DeviceCapabilities
