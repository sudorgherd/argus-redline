#pragma once

#include <stddef.h>
#include <stdint.h>

#include "radio_operation_bridge.h"

namespace HostOperationLifecycle {

enum class State : uint8_t { EMPTY, ACTIVE, COMPLETED };

enum class Action : uint8_t {
    NONE,
    INVALID_REQUEST,
    ACTIVE_DUPLICATE,
    TRANSMIT_COMMAND,
    HOST_RESPONSE_READY,
    HOST_RESPONSE_SUPPRESSED,
    RESPONSE_RETAINED
};

struct Result {
    Action action;
    uint16_t requestId;
    Protocol::Packet packet;
    HostProtocol::OperationResponse response;
};

struct RetainedEntry {
    uint16_t requestId;
    uint8_t requestPayloadLength;
    uint8_t requestPayload[HostProtocol::MAX_PAYLOAD_SIZE];
    HostProtocol::OperationResponse response;
};

class Lifecycle {
public:
    State state() const { return state_; }
    bool hostConnected() const { return hostConnected_; }
    const RetainedEntry& entry() const { return entry_; }
    RadioOperationBridge::HubStructuredOperationBridge& remoteBridge() {
        return remoteBridge_;
    }

    Result submit(
        uint16_t requestId,
        const uint8_t* payload,
        size_t payloadLength,
        const HostOperationService::DeviceSnapshot& snapshot,
        uint8_t peerId,
        bool registryValid,
        const DeviceCapabilities::CapabilityRegistryView& registry,
        DeviceCapabilities::LocalCapabilityHandler& handler,
        DeviceCapabilities::InterlockState interlock,
        DeviceCapabilities::CapabilityDiagnostics& diagnostics,
        RuntimeState::State& runtimeState,
        const HostOperationService::AvailabilityProvider& availability,
        uint32_t now,
        uint32_t overallTimeout,
        uint8_t maxRetries = 2,
        bool explicitlyConfigured = false
    ) {
        Result result = {};
        result.requestId = requestId;
        HostProtocol::OperationRequest request = {};
        if (!HostProtocol::isValidRequestId(requestId) || payload == nullptr ||
            payloadLength > HostProtocol::MAX_PAYLOAD_SIZE ||
            HostProtocol::decodeOperationRequest(payload, payloadLength, request) !=
                HostProtocol::PayloadResult::OK) {
            result.action = Action::INVALID_REQUEST;
            return result;
        }

        if (state_ != State::EMPTY && requestId == entry_.requestId) {
            if (!samePayload(payload, payloadLength)) {
                return rejection(requestId, request,
                    HostProtocol::RequestRejectionCode::MISMATCH);
            }
            if (state_ == State::ACTIVE) {
                result.action = Action::ACTIVE_DUPLICATE;
                return result;
            }
            result.response = entry_.response;
            result.action = hostConnected_ ? Action::HOST_RESPONSE_READY
                                           : Action::RESPONSE_RETAINED;
            return result;
        }
        if (state_ == State::ACTIVE) {
            return rejection(requestId, request,
                HostProtocol::RequestRejectionCode::BUSY);
        }

        if (request.targetDeviceId == snapshot.deviceId) {
            const HostOperationService::Result local =
                HostOperationService::handleLocalOperation(requestId, request,
                    snapshot, registryValid, registry, handler, interlock,
                    diagnostics, runtimeState, availability);
            if (local.disposition != HostOperationService::Disposition::HANDLED) {
                return rejection(requestId, request,
                    local.disposition == HostOperationService::Disposition::NOT_HANDLED
                        ? HostProtocol::RequestRejectionCode::UNSUPPORTED_OPERATION
                        : local.rejectionCode);
            }
            if (local.response.resultClass ==
                HostProtocol::ResultClass::REQUEST_REJECTED) {
                result.action = hostConnected_ ? Action::HOST_RESPONSE_READY
                                               : Action::HOST_RESPONSE_SUPPRESSED;
                result.response = local.response;
                return result;
            }
            acceptIdentity(requestId, payload, payloadLength);
            complete(local.response);
            result.response = entry_.response;
            result.action = hostConnected_ ? Action::HOST_RESPONSE_READY
                                           : Action::RESPONSE_RETAINED;
            return result;
        }

        const RadioOperationBridge::HubResult remote = remoteBridge_.submit(
            requestId, request, snapshot.role, snapshot.deviceId, peerId, now,
            overallTimeout, maxRetries, explicitlyConfigured);
        if (remote.action == RadioOperationBridge::HubAction::TRANSMIT_COMMAND) {
            acceptIdentity(requestId, payload, payloadLength);
            result.action = Action::TRANSMIT_COMMAND;
            result.packet = remote.packet;
            return result;
        }
        if (remote.action == RadioOperationBridge::HubAction::HOST_RESPONSE_READY) {
            result.action = hostConnected_ ? Action::HOST_RESPONSE_READY
                                           : Action::HOST_RESPONSE_SUPPRESSED;
            result.response = remote.response;
            return result;
        }
        result.action = Action::INVALID_REQUEST;
        return result;
    }

    Result receive(const Protocol::Packet& packet) {
        return consumeRemote(remoteBridge_.receive(packet));
    }

    Result service(uint32_t now) {
        return consumeRemote(remoteBridge_.service(now));
    }

    void onHostDisconnect() { hostConnected_ = false; }
    void onHostReconnect() { hostConnected_ = true; }

    void reset() {
        state_ = State::EMPTY;
        entry_ = {};
        remoteBridge_ = {};
        hostConnected_ = true;
    }

private:
    bool samePayload(const uint8_t* payload, size_t payloadLength) const {
        if (payloadLength != entry_.requestPayloadLength) return false;
        for (size_t index = 0; index < payloadLength; ++index) {
            if (payload[index] != entry_.requestPayload[index]) return false;
        }
        return true;
    }

    void acceptIdentity(uint16_t requestId, const uint8_t* payload,
        size_t payloadLength) {
        entry_ = {};
        entry_.requestId = requestId;
        entry_.requestPayloadLength = static_cast<uint8_t>(payloadLength);
        for (size_t index = 0; index < payloadLength; ++index) {
            entry_.requestPayload[index] = payload[index];
        }
        state_ = State::ACTIVE;
    }

    void complete(const HostProtocol::OperationResponse& response) {
        entry_.response = response;
        state_ = State::COMPLETED;
    }

    Result rejection(uint16_t requestId,
        const HostProtocol::OperationRequest& request,
        HostProtocol::RequestRejectionCode code) const {
        Result result = {};
        result.requestId = requestId;
        result.response = RadioOperationBridge::makeHostResponseBase(request);
        result.response.resultClass = HostProtocol::ResultClass::REQUEST_REJECTED;
        result.response.resultCode = static_cast<uint8_t>(code);
        result.action = hostConnected_ ? Action::HOST_RESPONSE_READY
                                       : Action::HOST_RESPONSE_SUPPRESSED;
        return result;
    }

    Result consumeRemote(const RadioOperationBridge::HubResult& remote) {
        Result result = {};
        if (state_ != State::ACTIVE) return result;
        result.requestId = entry_.requestId;
        if (remote.action == RadioOperationBridge::HubAction::TRANSMIT_COMMAND) {
            result.action = Action::TRANSMIT_COMMAND;
            result.packet = remote.packet;
        } else if (remote.action ==
            RadioOperationBridge::HubAction::HOST_RESPONSE_READY) {
            complete(remote.response);
            result.response = entry_.response;
            result.action = hostConnected_ ? Action::HOST_RESPONSE_READY
                                           : Action::RESPONSE_RETAINED;
        }
        return result;
    }

    State state_ = State::EMPTY;
    bool hostConnected_ = true;
    RetainedEntry entry_ = {};
    RadioOperationBridge::HubStructuredOperationBridge remoteBridge_ = {};
};

static_assert(sizeof(RetainedEntry::requestPayload) ==
    HostProtocol::MAX_PAYLOAD_SIZE,
    "Host lifecycle retains one exact maximum request payload");

}  // namespace HostOperationLifecycle
