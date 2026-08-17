#pragma once

#include <stdint.h>

#include "capability_role_integration.h"
#include "host_protocol_messages.h"

namespace HostOperationService {

enum class Disposition : uint8_t {
    NOT_HANDLED = 0x00,
    HANDLED = 0x01,
    REJECTED = 0x02
};

struct Result {
    Disposition disposition;
    HostProtocol::RequestRejectionCode rejectionCode;
    uint16_t requestId;
    HostProtocol::OperationResponse response;
};

struct AvailabilityProvider {
    const void* context;
    bool (*query)(const void*, DeviceCapabilities::CapabilityId, bool&);
};

inline AvailabilityProvider makeAvailabilityProvider(
    const void* context,
    bool (*query)(const void*, DeviceCapabilities::CapabilityId, bool&)
) {
    return {context, query};
}

inline Result makeBaseResult(
    uint16_t requestId,
    const HostProtocol::OperationRequest& request
) {
    Result result = {};
    result.requestId = requestId;
    result.response.category = request.category;
    result.response.operation = request.operation;
    result.response.targetDeviceId = request.targetDeviceId;
    result.response.targetId = request.targetId;
    HostProtocol::setNoneValue(result.response.value);
    return result;
}

inline Result reject(
    uint16_t requestId,
    const HostProtocol::OperationRequest& request,
    HostProtocol::RequestRejectionCode code,
    bool responseIsRepresentable
) {
    Result result = makeBaseResult(requestId, request);
    result.disposition = responseIsRepresentable
        ? Disposition::HANDLED : Disposition::REJECTED;
    result.rejectionCode = code;
    result.response.resultClass = HostProtocol::ResultClass::REQUEST_REJECTED;
    result.response.resultCode = static_cast<uint8_t>(code);
    return result;
}

inline void setOperationResult(
    Result& result,
    DeviceCapabilities::OperationStatus status
) {
    result.disposition = Disposition::HANDLED;
    result.response.resultClass = HostProtocol::ResultClass::OPERATION_RESULT;
    result.response.resultCode = static_cast<uint8_t>(status);
    HostProtocol::setNoneValue(result.response.value);
}

inline bool hostValueToCapabilityValue(
    const HostProtocol::TypedValue& input,
    DeviceCapabilities::CapabilityValue& output
) {
    if (!HostProtocol::isValidScalarValue(input)) return false;
    DeviceCapabilities::CapabilityValue candidate = {};
    candidate.type = static_cast<DeviceCapabilities::ValueType>(input.type);
    switch (candidate.type) {
        case DeviceCapabilities::ValueType::NONE:
            candidate.bits = 0;
            break;
        case DeviceCapabilities::ValueType::BOOLEAN:
            candidate.bits = input.bytes[0];
            break;
        case DeviceCapabilities::ValueType::NORMALIZED_U16:
        case DeviceCapabilities::ValueType::ENUM_U16:
            candidate.bits = HostProtocol::readUint16Le(input.bytes);
            break;
        case DeviceCapabilities::ValueType::UNSIGNED_32:
        case DeviceCapabilities::ValueType::SIGNED_32:
        case DeviceCapabilities::ValueType::FIXED_Q16_16:
            candidate.bits = HostProtocol::readUint32Le(input.bytes);
            break;
    }
    if (!DeviceCapabilities::isValidCapabilityValue(candidate)) return false;
    output = candidate;
    return true;
}

inline bool capabilityValueToHostValue(
    const DeviceCapabilities::CapabilityValue& input,
    HostProtocol::TypedValue& output
) {
    if (!DeviceCapabilities::isValidCapabilityValue(input)) return false;
    switch (input.type) {
        case DeviceCapabilities::ValueType::NONE:
            HostProtocol::setNoneValue(output);
            return true;
        case DeviceCapabilities::ValueType::BOOLEAN:
            return HostProtocol::setBooleanValue(
                output, static_cast<uint8_t>(input.bits));
        case DeviceCapabilities::ValueType::NORMALIZED_U16:
        case DeviceCapabilities::ValueType::ENUM_U16:
            return HostProtocol::setUint16Value(
                output, input.type, static_cast<uint16_t>(input.bits));
        case DeviceCapabilities::ValueType::UNSIGNED_32:
        case DeviceCapabilities::ValueType::SIGNED_32:
        case DeviceCapabilities::ValueType::FIXED_Q16_16:
            return HostProtocol::setUint32Value(output, input.type, input.bits);
    }
    return false;
}

inline Result handleLocalCapability(
    uint16_t requestId,
    const HostProtocol::OperationRequest& request,
    uint8_t localDeviceId,
    bool registryValid,
    const DeviceCapabilities::CapabilityRegistryView& registry,
    DeviceCapabilities::LocalCapabilityHandler& handler,
    DeviceCapabilities::InterlockState interlock,
    DeviceCapabilities::CapabilityDiagnostics& diagnostics,
    RuntimeState::State& runtimeState,
    const AvailabilityProvider& availability
) {
    const HostProtocol::PayloadResult validation =
        HostProtocol::validateOperationRequest(request);
    if (validation != HostProtocol::PayloadResult::OK) {
        const bool unsupported = validation ==
            HostProtocol::PayloadResult::UNSUPPORTED_CATEGORY_OPERATION;
        return reject(requestId, request,
            unsupported
                ? HostProtocol::RequestRejectionCode::UNSUPPORTED_OPERATION
                : HostProtocol::RequestRejectionCode::MALFORMED_REQUEST,
            false);
    }
    if (request.category != HostProtocol::OperationCategory::CAPABILITY) {
        Result result = makeBaseResult(requestId, request);
        result.disposition = Disposition::NOT_HANDLED;
        return result;
    }
    if (request.targetDeviceId != localDeviceId) {
        return reject(requestId, request,
            HostProtocol::RequestRejectionCode::BAD_TARGET, true);
    }

    Result result = makeBaseResult(requestId, request);
    result.disposition = Disposition::HANDLED;
    if (request.operation == HostProtocol::OperationCode::GET_CAPABILITIES) {
        if (!registryValid ||
            !DeviceCapabilities::isValidCapabilityRegistry(registry)) {
            setOperationResult(result,
                DeviceCapabilities::OperationStatus::INVALID_DESCRIPTOR);
            return result;
        }
        uint32_t cursor = 0;
        if (!HostProtocol::isNoneValue(request.value)) {
            cursor = HostProtocol::readUint32Le(request.value.bytes);
        }
        if (cursor > registry.count) {
            setOperationResult(result,
                DeviceCapabilities::OperationStatus::VALUE_OUT_OF_RANGE);
            return result;
        }
        HostProtocol::CapabilityPageRecord page = {};
        uint8_t index = static_cast<uint8_t>(cursor);
        while (index < registry.count &&
            page.count < HostProtocol::MAX_CAPABILITY_PAGE_ENTRIES) {
            DeviceCapabilities::CapabilityDescriptor descriptor = {};
            if (!DeviceCapabilities::getCapabilityByIndex(
                    registry, index, descriptor)) {
                setOperationResult(result,
                    DeviceCapabilities::OperationStatus::INVALID_DESCRIPTOR);
                return result;
            }
            page.capabilityIds[page.count++] = descriptor.id;
            ++index;
        }
        page.nextCursor = index == registry.count
            ? HostProtocol::CAPABILITY_PAGE_END : index;
        setOperationResult(result, DeviceCapabilities::OperationStatus::OK);
        if (HostProtocol::encodeCapabilityPageRecord(page,
                result.response.value) != HostProtocol::PayloadResult::OK) {
            setOperationResult(result,
                DeviceCapabilities::OperationStatus::OPERATION_FAILED);
        }
        return result;
    }

    if (request.operation == HostProtocol::OperationCode::DESCRIBE_CAPABILITY) {
        if (!registryValid ||
            !DeviceCapabilities::isValidCapabilityRegistry(registry)) {
            setOperationResult(result,
                DeviceCapabilities::OperationStatus::INVALID_DESCRIPTOR);
            return result;
        }
        DeviceCapabilities::CapabilityDescriptor descriptor = {};
        if (!DeviceCapabilities::findCapability(
                registry, request.targetId, descriptor)) {
            setOperationResult(result,
                DeviceCapabilities::OperationStatus::CAPABILITY_NOT_FOUND);
            return result;
        }
        bool available = false;
        if (availability.query == nullptr ||
            !availability.query(availability.context, descriptor.id, available)) {
            setOperationResult(result,
                DeviceCapabilities::OperationStatus::INVALID_DESCRIPTOR);
            return result;
        }
        HostProtocol::CapabilityDescriptionRecord description = {};
        description.capabilityClass = descriptor.capabilityClass;
        description.valueType = descriptor.valueType;
        description.operationFlags = descriptor.operationFlags;
        description.unit = static_cast<DeviceCapabilities::UnitCode>(
            descriptor.unitCode);
        description.availability = available ? 1 : 0;
        setOperationResult(result, DeviceCapabilities::OperationStatus::OK);
        if (HostProtocol::encodeCapabilityDescriptionRecord(
                description, result.response.value) !=
            HostProtocol::PayloadResult::OK) {
            setOperationResult(result,
                DeviceCapabilities::OperationStatus::OPERATION_FAILED);
        }
        return result;
    }

    DeviceCapabilities::CapabilityValue input = {};
    if (!hostValueToCapabilityValue(request.value, input)) {
        return reject(requestId, request,
            HostProtocol::RequestRejectionCode::MALFORMED_REQUEST, false);
    }
    const DeviceCapabilities::Operation operation =
        request.operation == HostProtocol::OperationCode::READ_CAPABILITY
            ? DeviceCapabilities::Operation::READ
            : DeviceCapabilities::Operation::SET;
    if (operation == DeviceCapabilities::Operation::SET && registryValid &&
        DeviceCapabilities::isValidCapabilityRegistry(registry)) {
        DeviceCapabilities::CapabilityDescriptor descriptor = {};
        if (DeviceCapabilities::findCapability(
                registry, request.targetId, descriptor) &&
            DeviceCapabilities::supportsOperation(
                descriptor, DeviceCapabilities::Operation::SET) &&
            descriptor.capabilityClass !=
                DeviceCapabilities::CapabilityClass::INDICATOR_OUTPUT) {
            setOperationResult(result,
                DeviceCapabilities::OperationStatus::UNSUPPORTED_OPERATION);
            return result;
        }
    }
    DeviceCapabilities::CallerContext caller = {};
    caller.callerClass = DeviceCapabilities::CallerClass::HOST_LOCAL;
    const DeviceCapabilities::OperationResult operationResult =
        CapabilityRoleIntegration::executeLocalCapabilityNow(
            registryValid, registry, handler, request.targetId, operation,
            input, caller, interlock, diagnostics, runtimeState);
    setOperationResult(result, operationResult.status);
    if (operationResult.status == DeviceCapabilities::OperationStatus::OK &&
        operation == DeviceCapabilities::Operation::READ &&
        !capabilityValueToHostValue(operationResult.value,
            result.response.value)) {
        setOperationResult(result,
            DeviceCapabilities::OperationStatus::OPERATION_FAILED);
    }
    return result;
}

}  // namespace HostOperationService
