#pragma once

#include <stdint.h>

#include "host_protocol_messages.h"
#include "host_protocol_stream.h"

namespace HostProtocolDiagnostics {

struct Snapshot {
    uint32_t framesReceived;
    uint32_t framesAccepted;
    uint32_t malformedFrames;
    uint32_t unsupportedVersions;
    uint32_t unsupportedMessageTypes;
    uint32_t requestsDispatched;
    uint32_t responsesEmitted;
    uint32_t busyOrRejectedRequests;
    uint32_t transportResets;
};

enum class MessageBoundaryResult : uint8_t {
    ACCEPTED,
    MALFORMED,
    UNSUPPORTED_VERSION,
    UNSUPPORTED_MESSAGE_TYPE
};

inline void saturatingAdd(uint32_t& value, uint32_t amount = 1) {
    if (UINT32_MAX - value < amount) {
        value = UINT32_MAX;
    } else {
        value += amount;
    }
}

class Diagnostics {
public:
    Diagnostics() = default;
    explicit Diagnostics(const Snapshot& initial) : counters_(initial) {}

    Snapshot snapshot() const { return counters_; }
    void reset() { counters_ = {}; }

    void observeFrame(
        const HostProtocol::StreamResult& stream,
        MessageBoundaryResult boundary = MessageBoundaryResult::ACCEPTED
    ) {
        if (stream.event != HostProtocol::StreamEvent::FRAME_READY &&
            stream.event != HostProtocol::StreamEvent::FRAME_REJECTED &&
            stream.event != HostProtocol::StreamEvent::OVERSIZED_CANDIDATE) {
            return;
        }
        saturatingAdd(counters_.framesReceived);
        if (stream.event == HostProtocol::StreamEvent::OVERSIZED_CANDIDATE) {
            saturatingAdd(counters_.malformedFrames);
            return;
        }
        if (stream.event == HostProtocol::StreamEvent::FRAME_REJECTED) {
            observeDecodeFailure(stream.decodeResult);
            return;
        }
        switch (boundary) {
            case MessageBoundaryResult::ACCEPTED:
                saturatingAdd(counters_.framesAccepted);
                return;
            case MessageBoundaryResult::MALFORMED:
                saturatingAdd(counters_.malformedFrames);
                return;
            case MessageBoundaryResult::UNSUPPORTED_VERSION:
                saturatingAdd(counters_.unsupportedVersions);
                return;
            case MessageBoundaryResult::UNSUPPORTED_MESSAGE_TYPE:
                saturatingAdd(counters_.unsupportedMessageTypes);
                return;
        }
    }

    void observeRequestDispatched() {
        saturatingAdd(counters_.requestsDispatched);
    }

    void observeOperationResponse(
        const HostProtocol::OperationResponse& response
    ) {
        if (response.resultClass == HostProtocol::ResultClass::REQUEST_REJECTED) {
            saturatingAdd(counters_.busyOrRejectedRequests);
        }
    }

    void observeResponseHandoff() {
        saturatingAdd(counters_.responsesEmitted);
    }

    void observeTransportReset() {
        saturatingAdd(counters_.transportResets);
    }

private:
    void observeDecodeFailure(HostProtocol::DecodeResult result) {
        switch (result) {
            case HostProtocol::DecodeResult::UNSUPPORTED_VERSION:
            case HostProtocol::DecodeResult::UNSUPPORTED_MAJOR:
            case HostProtocol::DecodeResult::UNSUPPORTED_MINOR:
                saturatingAdd(counters_.unsupportedVersions);
                return;
            case HostProtocol::DecodeResult::UNSUPPORTED_MESSAGE_TYPE:
                saturatingAdd(counters_.unsupportedMessageTypes);
                return;
            case HostProtocol::DecodeResult::OK:
                return;
            default:
                saturatingAdd(counters_.malformedFrames);
                return;
        }
    }

    Snapshot counters_ = {};
};

static_assert(sizeof(Snapshot) == 9 * sizeof(uint32_t),
    "Host diagnostics snapshot is exactly nine uint32 counters");
static_assert(sizeof(Diagnostics) == sizeof(Snapshot),
    "Host diagnostics owner adds no storage beyond its counters");

}  // namespace HostProtocolDiagnostics
