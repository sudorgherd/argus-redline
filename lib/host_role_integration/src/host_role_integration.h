#pragma once

#include <stddef.h>
#include <stdint.h>

#include "host_operation_lifecycle.h"
#include "host_protocol_diagnostics.h"
#include "host_transport.h"
#if defined(ARGUS_STRUCTURED_TRACE)
#include "structured_trace.h"
#endif

namespace HostRoleIntegration {

constexpr size_t RX_SERVICE_BUDGET = HostProtocol::MAX_ENCODED_FRAME_SIZE;
constexpr size_t TX_SERVICE_BUDGET = HostProtocol::MAX_ENCODED_FRAME_SIZE;

enum class Action : uint8_t { NONE, TRANSMIT_COMMAND, HOST_RESPONSE_HANDOFF };

struct Result {
    Action action;
    Protocol::Packet packet;
};

template <typename ByteStream>
class Stack {
public:
    explicit Stack(ByteStream& stream, bool connected = false)
        : transport_(stream), connected_(connected) {
        if (!connected_) lifecycle_.onHostDisconnect();
    }

    bool connected() const { return connected_; }
    bool txPending() const { return transport_.hasPendingTx(); }
    HostOperationLifecycle::Lifecycle& lifecycle() { return lifecycle_; }
    HostProtocolDiagnostics::Diagnostics& diagnostics() { return diagnostics_; }
    HostTransport::Adapter<ByteStream>& transport() { return transport_; }
    void setEventService(HostEventService::Service* service) {
        eventService_ = service;
    }

    void observeConnection(bool connected) {
        if (connected == connected_) return;
        connected_ = connected;
        if (!connected) {
            transport_.reset();
            lifecycle_.onHostDisconnect();
            pendingOriginalDelivery_ = false;
            diagnostics_.observeTransportReset();
        } else {
            lifecycle_.onHostReconnect();
        }
    }

    HostTransport::TxResult serviceTx(size_t budget = TX_SERVICE_BUDGET) {
        return transport_.serviceTx(budget);
    }

    bool servicePendingDelivery() {
        if (!connected_ || !pendingOriginalDelivery_ || transport_.hasPendingTx() ||
            lifecycle_.state() != HostOperationLifecycle::State::COMPLETED)
            return false;
        const HostOperationLifecycle::RetainedEntry& entry = lifecycle_.entry();
        if (!submitOperationResponse(entry.requestMinor,
                entry.requestId, entry.response)) return false;
        pendingOriginalDelivery_ = false;
        return true;
    }

    Result serviceRx(
        const HostOperationService::DeviceSnapshot& snapshot,
        uint8_t peerId,
        bool registryValid,
        const DeviceCapabilities::CapabilityRegistryView& registry,
        DeviceCapabilities::LocalCapabilityHandler& handler,
        DeviceCapabilities::InterlockState interlock,
        DeviceCapabilities::CapabilityDiagnostics& capabilityDiagnostics,
        RuntimeState::State& runtimeState,
        const HostOperationService::AvailabilityProvider& availability,
        uint32_t now,
        uint32_t overallTimeout,
        bool radioBridge,
        size_t budget = RX_SERVICE_BUDGET
    ) {
        Result result = {};
        if (!connected_ || transport_.hasPendingTx()) return result;
        const HostTransport::RxResult rx = transport_.serviceRx(budget);
        if (rx.status != HostTransport::RxStatus::PARSER_EVENT) return result;
#if defined(ARGUS_STRUCTURED_TRACE)
        StructuredTrace::record(now, StructuredTrace::Event::HOST_FRAME_TERMINAL,
            rx.parserResult.frame.requestId, 0, 0, 0,
            static_cast<uint8_t>(rx.parserResult.event));
#endif
        if (rx.parserResult.event != HostProtocol::StreamEvent::FRAME_READY) {
            diagnostics_.observeFrame(rx.parserResult);
            const HostProtocol::DecodeResult failure = rx.parserResult.decodeResult;
            HostProtocol::ProtocolError error = {};
            bool trustworthy = true;
            switch (failure) {
                case HostProtocol::DecodeResult::UNSUPPORTED_MAJOR:
                    error.errorCode = HostProtocol::ProtocolErrorCode::UNSUPPORTED_MAJOR;
                    break;
                case HostProtocol::DecodeResult::UNSUPPORTED_MINOR:
                    error.errorCode = HostProtocol::ProtocolErrorCode::UNSUPPORTED_MINOR;
                    break;
                case HostProtocol::DecodeResult::UNSUPPORTED_MESSAGE_TYPE:
                    error.errorCode = HostProtocol::ProtocolErrorCode::UNSUPPORTED_MESSAGE_TYPE;
                    break;
                case HostProtocol::DecodeResult::UNSUPPORTED_FLAGS:
                    error.errorCode = HostProtocol::ProtocolErrorCode::UNSUPPORTED_FLAGS;
                    break;
                case HostProtocol::DecodeResult::INVALID_REQUEST_ID:
                    error.errorCode = HostProtocol::ProtocolErrorCode::INVALID_REQUEST_ID;
                    break;
                default:
                    trustworthy = false;
                    break;
            }
            if (trustworthy) {
                error.offendingType = static_cast<uint8_t>(
                    rx.parserResult.frame.messageType);
                uint8_t payload[HostProtocol::MAX_PAYLOAD_SIZE] = {};
                size_t length = 0;
                if (HostProtocol::encodeProtocolError(error, payload,
                        sizeof(payload), length) == HostProtocol::PayloadResult::OK &&
                    submitFrame(HostProtocol::isSupportedVersion(
                            HostProtocol::VERSION_MAJOR, rx.parserResult.frame.minor)
                            ? rx.parserResult.frame.minor
                            : HostProtocol::VERSION_MINOR_0_1,
                        HostProtocol::MessageType::PROTOCOL_ERROR,
                        rx.parserResult.frame.requestId, payload, length))
                    result.action = Action::HOST_RESPONSE_HANDOFF;
            }
            return result;
        }
        const HostProtocol::Frame& frame = rx.parserResult.frame;
        if (frame.messageType == HostProtocol::MessageType::HELLO_REQUEST) {
            HostProtocol::HelloRequest request = {};
            if (HostProtocol::decodeHelloRequest(frame.minor,
                    frame.payload, frame.payloadLength,
                    request) != HostProtocol::PayloadResult::OK) {
                diagnostics_.observeFrame(rx.parserResult,
                    HostProtocolDiagnostics::MessageBoundaryResult::MALFORMED);
                HostProtocol::ProtocolError error = {
                    HostProtocol::ProtocolErrorCode::MALFORMED_PAYLOAD,
                    static_cast<uint8_t>(frame.messageType), 0};
                uint8_t errorPayload[HostProtocol::MAX_PAYLOAD_SIZE] = {};
                size_t errorLength = 0;
                if (HostProtocol::encodeProtocolError(error, errorPayload,
                        sizeof(errorPayload), errorLength) == HostProtocol::PayloadResult::OK &&
                    submitFrame(frame.minor,
                        HostProtocol::MessageType::PROTOCOL_ERROR,
                        frame.requestId, errorPayload, errorLength))
                    result.action = Action::HOST_RESPONSE_HANDOFF;
                return result;
            }
            diagnostics_.observeFrame(rx.parserResult);
            const HostOperationService::HelloResult hello =
                HostOperationService::handleHello(frame.requestId, frame.major,
                    frame.minor,
                    request, snapshot, radioBridge, eventService_ != nullptr);
            uint8_t payload[HostProtocol::MAX_PAYLOAD_SIZE] = {};
            size_t length = 0;
            if (hello.disposition == HostOperationService::HelloDisposition::RESPONSE) {
                if (HostProtocol::encodeHelloResponse(hello.response, payload,
                        sizeof(payload), length) == HostProtocol::PayloadResult::OK &&
                    submitFrame(frame.minor,
                        HostProtocol::MessageType::HELLO_RESPONSE,
                        frame.requestId, payload, length)) {
                    result.action = Action::HOST_RESPONSE_HANDOFF;
                }
            } else if (HostProtocol::encodeProtocolError(hello.error, payload,
                    sizeof(payload), length) == HostProtocol::PayloadResult::OK &&
                submitFrame(frame.minor,
                    HostProtocol::MessageType::PROTOCOL_ERROR,
                    frame.requestId, payload, length)) {
                result.action = Action::HOST_RESPONSE_HANDOFF;
            }
            return result;
        }
        if (frame.messageType != HostProtocol::MessageType::OPERATION_REQUEST) {
            diagnostics_.observeFrame(rx.parserResult,
                HostProtocolDiagnostics::MessageBoundaryResult::UNSUPPORTED_MESSAGE_TYPE);
            return result;
        }
        HostProtocol::OperationRequest semantic = {};
        if (HostProtocol::decodeOperationRequest(frame.minor,
                frame.payload, frame.payloadLength,
                semantic) != HostProtocol::PayloadResult::OK) {
            diagnostics_.observeFrame(rx.parserResult,
                HostProtocolDiagnostics::MessageBoundaryResult::MALFORMED);
            return result;
        }
        diagnostics_.observeFrame(rx.parserResult);
#if defined(ARGUS_STRUCTURED_TRACE)
        StructuredTrace::record(now, StructuredTrace::Event::HOST_FRAME_ACCEPTED,
            frame.requestId, 0, static_cast<uint8_t>(semantic.operation));
        if (semantic.targetDeviceId == peerId)
            StructuredTrace::record(now, StructuredTrace::Event::HOST_REMOTE_REQUEST,
                frame.requestId, 0, static_cast<uint8_t>(semantic.operation), 0,
                semantic.targetDeviceId);
#endif
        const HostOperationLifecycle::State before = lifecycle_.state();
        const bool retainedReplay = lifecycle_.isExactRetainedRequest(
            frame.requestId, frame.payload, frame.payloadLength);
        const HostOperationLifecycle::Result life = lifecycle_.submit(
            frame.requestId, frame.payload, frame.payloadLength, snapshot, peerId,
            registryValid, registry, handler, interlock, capabilityDiagnostics,
            runtimeState, availability, now, overallTimeout, 2, false, frame.minor,
            eventService_);
        if (life.action == HostOperationLifecycle::Action::TRANSMIT_COMMAND) {
#if defined(ARGUS_STRUCTURED_TRACE)
            StructuredTrace::record(now,
                StructuredTrace::Event::HOST_LIFECYCLE_ACCEPTED_REMOTE,
                frame.requestId, life.packet.sequence, life.packet.opcode);
#endif
            diagnostics_.observeRequestDispatched();
            result.action = Action::TRANSMIT_COMMAND;
            result.packet = life.packet;
        } else if (life.action == HostOperationLifecycle::Action::HOST_RESPONSE_READY) {
            if (!retainedReplay &&
                (before == HostOperationLifecycle::State::EMPTY ||
                 before == HostOperationLifecycle::State::COMPLETED)) {
                if (life.response.resultClass != HostProtocol::ResultClass::REQUEST_REJECTED)
                    diagnostics_.observeRequestDispatched();
            }
            diagnostics_.observeOperationResponse(life.response);
            if (submitOperationResponse(life.responseMinor,
                    life.requestId, life.response))
                result.action = Action::HOST_RESPONSE_HANDOFF;
        }
        return result;
    }

    Result onRadioPacket(const Protocol::Packet& packet) {
        return consumeRemote(lifecycle_.receive(packet));
    }

    Result serviceRemote(uint32_t now) {
        return consumeRemote(lifecycle_.service(now));
    }

    void reset() {
        transport_.reset();
        lifecycle_.reset();
        pendingOriginalDelivery_ = false;
        connected_ = true;
    }

private:
    bool submitFrame(uint8_t minor, HostProtocol::MessageType type,
        uint16_t requestId,
        const uint8_t* payload, size_t payloadLength) {
        HostProtocol::Frame frame = {};
        frame.major = HostProtocol::VERSION_MAJOR;
        frame.minor = minor;
        frame.messageType = type;
        frame.requestId = requestId;
        frame.payloadLength = static_cast<uint16_t>(payloadLength);
        for (size_t i = 0; i < payloadLength; ++i) frame.payload[i] = payload[i];
        uint8_t encoded[HostProtocol::MAX_ENCODED_FRAME_SIZE] = {};
        size_t encodedLength = 0;
        if (HostProtocol::encodeFrame(frame, encoded, sizeof(encoded), encodedLength) !=
            HostProtocol::EncodeResult::OK) return false;
        if (transport_.submit(encoded, encodedLength) != HostTransport::SubmitStatus::OK)
            return false;
        diagnostics_.observeResponseHandoff();
        return true;
    }

    bool submitOperationResponse(uint8_t minor, uint16_t requestId,
        const HostProtocol::OperationResponse& response) {
        uint8_t payload[HostProtocol::MAX_PAYLOAD_SIZE] = {};
        size_t length = 0;
        return HostProtocol::encodeOperationResponse(minor, response, payload,
                   sizeof(payload), length) == HostProtocol::PayloadResult::OK &&
            submitFrame(minor, HostProtocol::MessageType::OPERATION_RESPONSE,
                requestId, payload, length);
    }

    Result consumeRemote(const HostOperationLifecycle::Result& life) {
        Result result = {};
        if (life.action == HostOperationLifecycle::Action::TRANSMIT_COMMAND) {
            result.action = Action::TRANSMIT_COMMAND;
            result.packet = life.packet;
        } else if (life.action == HostOperationLifecycle::Action::HOST_RESPONSE_READY) {
            diagnostics_.observeOperationResponse(life.response);
            if (!transport_.hasPendingTx() &&
                submitOperationResponse(life.responseMinor,
                    life.requestId, life.response)) {
                result.action = Action::HOST_RESPONSE_HANDOFF;
            } else if (connected_) {
                pendingOriginalDelivery_ = true;
            }
        }
        return result;
    }

    HostTransport::Adapter<ByteStream> transport_;
    HostOperationLifecycle::Lifecycle lifecycle_ = {};
    HostProtocolDiagnostics::Diagnostics diagnostics_ = {};
    bool connected_ = false;
    bool pendingOriginalDelivery_ = false;
    HostEventService::Service* eventService_ = nullptr;
};

}  // namespace HostRoleIntegration
