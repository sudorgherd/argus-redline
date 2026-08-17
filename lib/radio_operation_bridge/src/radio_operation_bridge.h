#pragma once

#include <stdint.h>

#include "host_device_service.h"
#include "wire_transaction_state.h"

namespace RadioOperationBridge {

enum class HubAction : uint8_t {
    NONE,
    NOT_HANDLED,
    NOT_ACCEPTED,
    TRANSMIT_COMMAND,
    HOST_RESPONSE_READY
};

struct HubResult {
    HubAction action;
    uint16_t requestId;
    Protocol::Packet packet;
    HostProtocol::OperationResponse response;
};

inline void copyHostToWireValue(
    const HostProtocol::TypedValue& source,
    WireOperations::Value& destination
) {
    destination = {};
    destination.type = source.type;
    destination.length = source.length;
    for (uint8_t index = 0; index < source.length; ++index) {
        destination.bytes[index] = source.bytes[index];
    }
}

inline void copyWireToHostValue(
    const WireOperations::Value& source,
    HostProtocol::TypedValue& destination
) {
    destination = {};
    destination.type = source.type;
    destination.length = source.length;
    for (uint8_t index = 0; index < source.length; ++index) {
        destination.bytes[index] = source.bytes[index];
    }
}

inline HostProtocol::OperationResponse makeHostResponseBase(
    const HostProtocol::OperationRequest& request
) {
    HostProtocol::OperationResponse response = {};
    response.category = request.category;
    response.operation = request.operation;
    response.targetDeviceId = request.targetDeviceId;
    response.targetId = request.targetId;
    HostProtocol::setNoneValue(response.value);
    return response;
}

class HubStructuredOperationBridge {
public:
    WireOperations::PeerSupport peerSupport() const { return peerSupport_; }
    bool active() const { return active_; }
    uint8_t nextSequence() const { return nextSequence_; }

    void configurePeerSupport(WireOperations::PeerSupport support) {
        peerSupport_ = support;
    }

    HubResult submit(
        uint16_t requestId,
        const HostProtocol::OperationRequest& request,
        RuntimeState::DeviceRole localRole,
        uint8_t localId,
        uint8_t peerId,
        uint32_t now,
        uint32_t overallTimeout,
        uint8_t maxRetries = 2,
        bool explicitlyConfigured = false
    ) {
        HubResult result = {};
        result.requestId = requestId;
        result.response = makeHostResponseBase(request);
        const HostProtocol::PayloadResult validation =
            HostProtocol::validateOperationRequest(request);
        if (validation != HostProtocol::PayloadResult::OK) {
            return reject(result,
                validation == HostProtocol::PayloadResult::UNSUPPORTED_CATEGORY_OPERATION
                    ? HostProtocol::RequestRejectionCode::UNSUPPORTED_OPERATION
                    : HostProtocol::RequestRejectionCode::MALFORMED_REQUEST);
        }
        if (request.targetDeviceId == localId) {
            result.action = HubAction::NOT_HANDLED;
            return result;
        }
        if (localRole != RuntimeState::DeviceRole::HUB ||
            request.targetDeviceId != peerId) {
            return reject(result, HostProtocol::RequestRejectionCode::BAD_TARGET);
        }
        if (active_) {
            result.action = HubAction::NOT_ACCEPTED;
            return result;
        }
        const uint8_t opcode = static_cast<uint8_t>(request.operation);
        if (!WireOperations::mayStartStructuredOperation(
                peerSupport_, opcode, explicitlyConfigured)) {
            return reject(result,
                HostProtocol::RequestRejectionCode::UNSUPPORTED_OPERATION);
        }
        WireOperations::Request wireRequest = {};
        wireRequest.targetId = request.targetId;
        copyHostToWireValue(request.value, wireRequest.value);
        const Protocol::Packet command = WireOperations::makeCommand(
            localId, peerId, nextSequence_, opcode, wireRequest);
        if (command.payloadLength == 0 ||
            !transaction_.begin(command, now, overallTimeout, maxRetries)) {
            result.action = HubAction::NOT_ACCEPTED;
            return result;
        }
        transaction_.configureSupport(peerSupport_);
        active_ = true;
        requestId_ = requestId;
        request_ = request;
        result.action = HubAction::TRANSMIT_COMMAND;
        result.packet = command;
        return result;
    }

    HubResult receive(const Protocol::Packet& packet) {
        HubResult result = activeBase();
        if (!active_) return result;
        const WireOperations::StructuredEvent event = transaction_.receive(packet);
        peerSupport_ = transaction_.peerSupport();
        if (event == WireOperations::StructuredEvent::RESPONSE_COMPLETE) {
            result.response.resultClass = HostProtocol::ResultClass::OPERATION_RESULT;
            result.response.resultCode = static_cast<uint8_t>(
                transaction_.result().status);
            copyWireToHostValue(transaction_.result().value,
                result.response.value);
            return finish(result);
        }
        if (event == WireOperations::StructuredEvent::REMOTE_REJECTED) {
            result.response.resultClass = HostProtocol::ResultClass::RADIO_RESULT;
            result.response.resultCode = static_cast<uint8_t>(
                HostProtocol::RadioResultCode::REMOTE_REJECTED);
            return finish(result);
        }
        return result;
    }

    HubResult service(uint32_t now) {
        HubResult result = activeBase();
        if (!active_) return result;
        const WireOperations::StructuredEvent event = transaction_.service(now);
        if (event == WireOperations::StructuredEvent::RETRANSMIT) {
            result.action = HubAction::TRANSMIT_COMMAND;
            result.packet = transaction_.command();
        } else if (event == WireOperations::StructuredEvent::TIMEOUT) {
            result.response.resultClass = HostProtocol::ResultClass::RADIO_RESULT;
            result.response.resultCode = static_cast<uint8_t>(
                HostProtocol::RadioResultCode::TIMEOUT);
            return finish(result);
        }
        return result;
    }

    void reset() {
        active_ = false;
        transaction_ = WireOperations::HubStructuredTransaction{};
        transaction_.configureSupport(peerSupport_);
        requestId_ = 0;
        request_ = {};
    }

private:
    HubResult reject(
        HubResult result,
        HostProtocol::RequestRejectionCode code
    ) const {
        result.action = HubAction::HOST_RESPONSE_READY;
        result.response.resultClass = HostProtocol::ResultClass::REQUEST_REJECTED;
        result.response.resultCode = static_cast<uint8_t>(code);
        return result;
    }

    HubResult activeBase() const {
        HubResult result = {};
        if (active_) {
            result.requestId = requestId_;
            result.response = makeHostResponseBase(request_);
        }
        return result;
    }

    HubResult finish(HubResult result) {
        result.action = HubAction::HOST_RESPONSE_READY;
        active_ = false;
        ++nextSequence_;
        return result;
    }

    bool active_ = false;
    uint8_t nextSequence_ = 0;
    uint16_t requestId_ = 0;
    HostProtocol::OperationRequest request_ = {};
    WireOperations::PeerSupport peerSupport_ = WireOperations::PeerSupport::UNKNOWN;
    WireOperations::HubStructuredTransaction transaction_ = {};
};

enum class NodeAction : uint8_t {
    IGNORE,
    SEND_ACK,
    ACK_THEN_EXECUTE,
    SEND_ACK_AND_RESPONSE,
    ACTIVE_BUSY,
    SEND_REJECTION_ACK,
    RESPONSE_READY
};

struct NodeResult {
    NodeAction action;
    Protocol::Packet acknowledgment;
    Protocol::Packet response;
};

class NodeStructuredOperationProcessor {
public:
    NodeResult admit(
        const Protocol::Packet& command,
        uint8_t localId,
        uint8_t peerId
    ) {
        NodeResult result = {};
        const WireOperations::AdmissionResult admission =
            WireOperations::admitNodeCommand(command, localId, peerId, retained_);
        switch (admission.outcome) {
            case WireOperations::AdmissionOutcome::IGNORE:
                result.action = NodeAction::IGNORE;
                return result;
            case WireOperations::AdmissionOutcome::UNSUPPORTED_OPCODE:
            case WireOperations::AdmissionOutcome::MALFORMED:
                result.action = NodeAction::SEND_REJECTION_ACK;
                result.acknowledgment = TransactionEngine::makeAcknowledgment(
                    command, admission.acknowledgmentStatus);
                return result;
            case WireOperations::AdmissionOutcome::ACTIVE_BUSY:
                result.action = NodeAction::ACTIVE_BUSY;
                return result;
            case WireOperations::AdmissionOutcome::DUPLICATE_PENDING:
                result.action = NodeAction::SEND_ACK;
                result.acknowledgment = retained_.acknowledgment();
                return result;
            case WireOperations::AdmissionOutcome::DUPLICATE_COMPLETE:
                result.action = NodeAction::SEND_ACK_AND_RESPONSE;
                result.acknowledgment = retained_.acknowledgment();
                result.response = retained_.response();
                return result;
            case WireOperations::AdmissionOutcome::ADMITTED:
                pendingCommand_ = command;
                hasPendingCommand_ = true;
                result.action = NodeAction::ACK_THEN_EXECUTE;
                result.acknowledgment = retained_.acknowledgment();
                return result;
        }
        return result;
    }

    NodeResult executeAdmittedOperation(
        const HostOperationService::DeviceSnapshot& snapshot,
        bool registryValid,
        const DeviceCapabilities::CapabilityRegistryView& registry,
        DeviceCapabilities::LocalCapabilityHandler& handler,
        DeviceCapabilities::InterlockState interlock,
        DeviceCapabilities::CapabilityDiagnostics& diagnostics,
        RuntimeState::State& runtimeState,
        const HostOperationService::AvailabilityProvider& availability
    ) {
        NodeResult result = {};
        if (!hasPendingCommand_ || !retained_.isActive()) return result;
        WireOperations::Request wireRequest = {};
        if (WireOperations::decodeRequest(pendingCommand_.opcode,
                pendingCommand_.payload, pendingCommand_.payloadLength,
                wireRequest) != WireOperations::CodecResult::OK) return result;
        WireOperations::Response wireResponse = {};
        wireResponse.targetId = wireRequest.targetId;
        HostProtocol::setNoneValue(hostScratch_);
        const HostProtocol::OperationCode operation =
            static_cast<HostProtocol::OperationCode>(pendingCommand_.opcode);

        if (operation == HostProtocol::OperationCode::READ_CAPABILITY ||
            operation == HostProtocol::OperationCode::SET_INDICATOR) {
            DeviceCapabilities::CapabilityValue input = {};
            HostProtocol::TypedValue hostInput = {};
            copyWireToHostValue(wireRequest.value, hostInput);
            if (!HostOperationService::hostValueToCapabilityValue(hostInput, input)) {
                wireResponse.status = DeviceCapabilities::OperationStatus::INVALID_VALUE_TYPE;
            } else {
                const DeviceCapabilities::OperationResult operationResult =
                    WireOperations::processRemoteCapability(registry, handler,
                        wireRequest.targetId,
                        operation == HostProtocol::OperationCode::READ_CAPABILITY
                            ? DeviceCapabilities::Operation::READ
                            : DeviceCapabilities::Operation::SET,
                        input, interlock, diagnostics);
                wireResponse.status = operationResult.status;
                if (operationResult.status == DeviceCapabilities::OperationStatus::OK &&
                    operation == HostProtocol::OperationCode::READ_CAPABILITY) {
                    if (!HostOperationService::capabilityValueToHostValue(
                            operationResult.value, hostScratch_)) {
                        wireResponse.status =
                            DeviceCapabilities::OperationStatus::OPERATION_FAILED;
                        HostProtocol::setNoneValue(hostScratch_);
                    }
                }
            }
        } else if (operation == HostProtocol::OperationCode::RUN_PROCEDURE) {
            wireResponse.status = DeviceCapabilities::OperationStatus::UNAUTHORIZED;
        } else {
            HostProtocol::OperationRequest hostRequest = {};
            hostRequest.operation = operation;
            hostRequest.category = categoryFor(operation);
            hostRequest.targetDeviceId = snapshot.deviceId;
            hostRequest.targetId = wireRequest.targetId;
            copyWireToHostValue(wireRequest.value, hostRequest.value);
            const HostOperationService::Result local =
                HostOperationService::handleLocalOperation(1, hostRequest,
                    snapshot, registryValid, registry, handler, interlock,
                    diagnostics, runtimeState, availability);
            wireResponse.status = static_cast<DeviceCapabilities::OperationStatus>(
                local.response.resultCode);
            hostScratch_ = local.response.value;
        }
        copyHostToWireValue(hostScratch_, wireResponse.value);
        if (wireResponse.status != DeviceCapabilities::OperationStatus::OK) {
            wireResponse.value = {};
            wireResponse.value.type = static_cast<uint8_t>(
                DeviceCapabilities::ValueType::NONE);
        }
        const Protocol::Packet response = WireOperations::makeResponsePacket(
            pendingCommand_, wireResponse);
        if (response.payloadLength == 0 || !retained_.complete(response)) {
            return result;
        }
        hasPendingCommand_ = false;
        result.action = NodeAction::RESPONSE_READY;
        result.response = retained_.response();
        return result;
    }

    bool active() const { return retained_.isActive(); }
    bool complete() const { return retained_.isComplete(); }
    void reset() {
        retained_.reset();
        pendingCommand_ = {};
        hasPendingCommand_ = false;
        HostProtocol::setNoneValue(hostScratch_);
    }

private:
    static HostProtocol::OperationCategory categoryFor(
        HostProtocol::OperationCode operation
    ) {
        switch (operation) {
            case HostProtocol::OperationCode::PING:
            case HostProtocol::OperationCode::GET_DEVICE_INFO:
            case HostProtocol::OperationCode::GET_STATUS:
                return HostProtocol::OperationCategory::DEVICE;
            case HostProtocol::OperationCode::GET_CAPABILITIES:
            case HostProtocol::OperationCode::DESCRIBE_CAPABILITY:
            case HostProtocol::OperationCode::READ_CAPABILITY:
            case HostProtocol::OperationCode::SET_INDICATOR:
                return HostProtocol::OperationCategory::CAPABILITY;
            case HostProtocol::OperationCode::RUN_PROCEDURE:
                return HostProtocol::OperationCategory::PROCEDURE;
            case HostProtocol::OperationCode::GET_DIAGNOSTICS:
                return HostProtocol::OperationCategory::DIAGNOSTIC;
        }
        return static_cast<HostProtocol::OperationCategory>(0);
    }

    WireOperations::NodeRetainedOperation retained_ = {};
    Protocol::Packet pendingCommand_ = {};
    bool hasPendingCommand_ = false;
    HostProtocol::TypedValue hostScratch_ = {};
};

static_assert(WireOperations::MAX_RESPONSE_VALUE_SIZE <=
    HostProtocol::MAX_RESPONSE_VALUE_SIZE,
    "Every bounded Wire result must fit the Host response value");

}  // namespace RadioOperationBridge
