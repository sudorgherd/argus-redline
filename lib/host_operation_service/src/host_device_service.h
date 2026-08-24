#pragma once

#include <stdint.h>

#include "device_settings.h"
#include "host_operation_service.h"
#include "redline_version.h"

namespace HostOperationService {

constexpr uint8_t DIAGNOSTIC_METRIC_COUNT = 10;

enum class DiagnosticMetric : uint8_t {
    TRANSMISSIONS_COMPLETED = 0x01,
    DECODED_PACKETS_RECEIVED = 0x02,
    SUCCESSFUL_TRANSACTIONS = 0x03,
    ACCEPTED_COMMANDS = 0x04,
    RETRANSMISSIONS = 0x05,
    ACKNOWLEDGMENT_TIMEOUTS = 0x06,
    DUPLICATES = 0x07,
    MALFORMED_PACKETS = 0x08,
    IGNORED_PACKETS = 0x09,
    RADIO_ERRORS = 0x0A
};

struct DeviceSnapshot {
    RuntimeState::DeviceRole role;
    uint8_t deviceId;
    bool ready;
    RuntimeState::RuntimePhase phase;
    RuntimeState::Health health;
    uint32_t uptimeSeconds;
    RuntimeState::DiagnosticCounters counters;
};

inline DeviceSnapshot makeDeviceSnapshot(
    const RuntimeState::State& state,
    uint32_t uptimeSeconds
) {
    return {state.role(), state.localId(), state.isReady(), state.phase(),
        state.health(), uptimeSeconds, state.counters()};
}

inline bool mapRole(
    RuntimeState::DeviceRole role,
    HostProtocol::DeviceRole& mapped
) {
    switch (role) {
        case RuntimeState::DeviceRole::HUB:
            mapped = HostProtocol::DeviceRole::HUB;
            return true;
        case RuntimeState::DeviceRole::NODE:
            mapped = HostProtocol::DeviceRole::NODE;
            return true;
    }
    return false;
}

inline uint16_t saturateUint16(uint32_t value) {
    return value > UINT16_MAX ? UINT16_MAX : static_cast<uint16_t>(value);
}

inline bool isTransactionActive(RuntimeState::RuntimePhase phase) {
    return phase == RuntimeState::RuntimePhase::TRANSMITTING ||
        phase == RuntimeState::RuntimePhase::WAITING_FOR_ACK ||
        phase == RuntimeState::RuntimePhase::TRANSMITTING_ACK ||
        phase == RuntimeState::RuntimePhase::WAITING_FOR_RESPONSE ||
        phase == RuntimeState::RuntimePhase::TRANSMITTING_RESPONSE;
}

inline uint16_t statusFlags(const DeviceSnapshot& snapshot) {
    uint16_t flags = 0;
    if (snapshot.ready) {
        flags |= HostProtocol::STATUS_READY;
        flags |= HostProtocol::STATUS_RADIO_OPERATIONAL;
    }
    if (isTransactionActive(snapshot.phase)) {
        flags |= HostProtocol::STATUS_TRANSACTION_ACTIVE;
    }
    if (snapshot.health == RuntimeState::Health::DEGRADED) {
        flags |= HostProtocol::STATUS_DEGRADED;
    }
    if (snapshot.health == RuntimeState::Health::ERROR) {
        flags |= HostProtocol::STATUS_ERROR;
    }
    return flags;
}

inline uint32_t diagnosticMetricValue(
    const RuntimeState::DiagnosticCounters& counters,
    uint8_t index
) {
    switch (index) {
        case 0: return counters.transmissionsCompleted;
        case 1: return counters.decodedPacketsReceived;
        case 2: return counters.successfulTransactions;
        case 3: return counters.acceptedCommands;
        case 4: return counters.retransmissions;
        case 5: return counters.acknowledgmentTimeouts;
        case 6: return counters.duplicates;
        case 7: return counters.malformedPackets;
        case 8: return counters.ignoredPackets;
        case 9: return counters.radioErrors;
    }
    return 0;
}

inline Result handleLocalDeviceOrDiagnostic(
    uint16_t requestId,
    const HostProtocol::OperationRequest& request,
    const DeviceSnapshot& snapshot
) {
    const HostProtocol::PayloadResult validation =
        HostProtocol::validateOperationRequest(request);
    if (validation != HostProtocol::PayloadResult::OK) {
        return reject(requestId, request,
            validation == HostProtocol::PayloadResult::UNSUPPORTED_CATEGORY_OPERATION
                ? HostProtocol::RequestRejectionCode::UNSUPPORTED_OPERATION
                : HostProtocol::RequestRejectionCode::MALFORMED_REQUEST,
            false);
    }
    if (request.category != HostProtocol::OperationCategory::DEVICE &&
        request.category != HostProtocol::OperationCategory::DIAGNOSTIC) {
        Result result = makeBaseResult(requestId, request);
        result.disposition = Disposition::NOT_HANDLED;
        return result;
    }
    if (request.targetDeviceId != snapshot.deviceId) {
        return reject(requestId, request,
            HostProtocol::RequestRejectionCode::BAD_TARGET, true);
    }

    Result result = makeBaseResult(requestId, request);
    setOperationResult(result, DeviceCapabilities::OperationStatus::OK);
    if (request.operation == HostProtocol::OperationCode::PING) {
        HostProtocol::setUint32Value(result.response.value,
            DeviceCapabilities::ValueType::UNSIGNED_32,
            snapshot.uptimeSeconds);
        return result;
    }

    HostProtocol::DeviceRole role;
    if (!mapRole(snapshot.role, role)) {
        setOperationResult(result,
            DeviceCapabilities::OperationStatus::OPERATION_FAILED);
        return result;
    }
    if (request.operation == HostProtocol::OperationCode::GET_DEVICE_INFO) {
        HostProtocol::DeviceInfoRecord record = {};
        record.firmwareMajor = RedlineVersion::FIRMWARE_MAJOR;
        record.firmwareMinor = RedlineVersion::FIRMWARE_MINOR;
        record.firmwarePatch = RedlineVersion::FIRMWARE_PATCH;
        record.wireProtocol = RedlineVersion::WIRE_PROTOCOL;
        record.configurationSchema = static_cast<uint8_t>(
            DeviceSettings::SCHEMA_VERSION);
        record.hardwareProfile = HostProtocol::HardwareProfile::HELTEC_V4;
        record.role = role;
        record.deviceId = snapshot.deviceId;
        if (HostProtocol::encodeDeviceInfoRecord(record,
                result.response.value) != HostProtocol::PayloadResult::OK) {
            setOperationResult(result,
                DeviceCapabilities::OperationStatus::OPERATION_FAILED);
        }
        return result;
    }
    if (request.operation == HostProtocol::OperationCode::GET_STATUS) {
        HostProtocol::StatusRecord record = {};
        record.statusFlags = statusFlags(snapshot);
        record.uptimeSeconds = snapshot.uptimeSeconds;
        record.retryCount = saturateUint16(snapshot.counters.retransmissions);
        record.timeoutCount = saturateUint16(
            snapshot.counters.acknowledgmentTimeouts);
        if (HostProtocol::encodeStatusRecord(record,
                result.response.value) != HostProtocol::PayloadResult::OK) {
            setOperationResult(result,
                DeviceCapabilities::OperationStatus::OPERATION_FAILED);
        }
        return result;
    }

    uint32_t cursor = 0;
    if (!HostProtocol::isNoneValue(request.value)) {
        cursor = HostProtocol::readUint32Le(request.value.bytes);
    }
    if (cursor > DIAGNOSTIC_METRIC_COUNT) {
        setOperationResult(result,
            DeviceCapabilities::OperationStatus::VALUE_OUT_OF_RANGE);
        return result;
    }
    HostProtocol::DiagnosticPageRecord page = {};
    uint8_t index = static_cast<uint8_t>(cursor);
    while (index < DIAGNOSTIC_METRIC_COUNT &&
        page.count < HostProtocol::MAX_DIAGNOSTIC_PAGE_ENTRIES) {
        page.entries[page.count].metricId = static_cast<uint8_t>(index + 1);
        page.entries[page.count].value = diagnosticMetricValue(
            snapshot.counters, index);
        ++page.count;
        ++index;
    }
    page.nextCursor = index == DIAGNOSTIC_METRIC_COUNT
        ? HostProtocol::DIAGNOSTIC_PAGE_END : index;
    if (HostProtocol::encodeDiagnosticPageRecord(page,
            result.response.value) != HostProtocol::PayloadResult::OK) {
        setOperationResult(result,
            DeviceCapabilities::OperationStatus::OPERATION_FAILED);
    }
    return result;
}

inline Result handleLocalOperation(
    uint16_t requestId,
    const HostProtocol::OperationRequest& request,
    const DeviceSnapshot& snapshot,
    bool registryValid,
    const DeviceCapabilities::CapabilityRegistryView& registry,
    DeviceCapabilities::LocalCapabilityHandler& handler,
    DeviceCapabilities::InterlockState interlock,
    DeviceCapabilities::CapabilityDiagnostics& diagnostics,
    RuntimeState::State& runtimeState,
    const AvailabilityProvider& availability
) {
    if (request.category == HostProtocol::OperationCategory::CAPABILITY) {
        return handleLocalCapability(requestId, request, snapshot.deviceId,
            registryValid, registry, handler, interlock, diagnostics,
            runtimeState, availability);
    }
    return handleLocalDeviceOrDiagnostic(requestId, request, snapshot);
}

enum class HelloDisposition : uint8_t {
    RESPONSE = 0x00,
    PROTOCOL_ERROR = 0x01
};

struct HelloResult {
    HelloDisposition disposition;
    uint16_t requestId;
    HostProtocol::HelloResponse response;
    HostProtocol::ProtocolError error;
};

inline HelloResult handleHello(
    uint16_t requestId,
    uint8_t envelopeMajor,
    uint8_t envelopeMinor,
    const HostProtocol::HelloRequest& request,
    const DeviceSnapshot& snapshot,
    bool radioStructuredBridge = false
) {
    HelloResult result = {};
    result.requestId = requestId;
    result.error.offendingType = static_cast<uint8_t>(
        HostProtocol::MessageType::HELLO_REQUEST);
    if (envelopeMajor != HostProtocol::VERSION_MAJOR) {
        result.disposition = HelloDisposition::PROTOCOL_ERROR;
        result.error.errorCode = HostProtocol::ProtocolErrorCode::UNSUPPORTED_MAJOR;
        return result;
    }
    if (!HostProtocol::isValidHelloMinorRange(envelopeMinor,
            request.minimumMinor, request.maximumMinor)) {
        result.disposition = HelloDisposition::PROTOCOL_ERROR;
        result.error.errorCode = HostProtocol::ProtocolErrorCode::UNSUPPORTED_MINOR;
        return result;
    }
    uint8_t selectedMinor = 0;
    if (!HostProtocol::selectHighestSupportedMinor(
            request.minimumMinor, request.maximumMinor, selectedMinor)) {
        result.disposition = HelloDisposition::PROTOCOL_ERROR;
        result.error.errorCode = HostProtocol::ProtocolErrorCode::UNSUPPORTED_MINOR;
        return result;
    }
    HostProtocol::DeviceRole role;
    if (!mapRole(snapshot.role, role)) {
        result.disposition = HelloDisposition::PROTOCOL_ERROR;
        result.error.errorCode = HostProtocol::ProtocolErrorCode::MALFORMED_PAYLOAD;
        return result;
    }
    result.disposition = HelloDisposition::RESPONSE;
    result.response.selectedMinor = selectedMinor;
    result.response.firmwareMajor = RedlineVersion::FIRMWARE_MAJOR;
    result.response.firmwareMinor = RedlineVersion::FIRMWARE_MINOR;
    result.response.firmwarePatch = RedlineVersion::FIRMWARE_PATCH;
    result.response.wireProtocol = RedlineVersion::WIRE_PROTOCOL;
    result.response.configurationSchema = static_cast<uint8_t>(
        DeviceSettings::SCHEMA_VERSION);
    result.response.hardwareProfile = HostProtocol::HardwareProfile::HELTEC_V4;
    result.response.role = role;
    result.response.deviceId = snapshot.deviceId;
    result.response.maximumHostPayload = HostProtocol::MAX_PAYLOAD_SIZE;
    result.response.operationCategoryBitmap =
        HostProtocol::CATEGORY_DEVICE_BIT |
        HostProtocol::CATEGORY_CAPABILITY_BIT |
        HostProtocol::CATEGORY_DIAGNOSTIC_BIT;
    result.response.featureBitmap = HostProtocol::FEATURE_LOCAL_OPERATIONS;
    if (radioStructuredBridge) {
        result.response.featureBitmap |= HostProtocol::FEATURE_RADIO_BRIDGE;
    }
    result.response.maximumOutstandingOperations =
        HostProtocol::MAX_OUTSTANDING_OPERATIONS;
    result.response.reserved = HostProtocol::HELLO_RESERVED_VALUE;
    return result;
}

inline HelloResult handleHello(uint16_t requestId, uint8_t envelopeMajor,
    const HostProtocol::HelloRequest& request,
    const DeviceSnapshot& snapshot, bool radioStructuredBridge = false) {
    return handleHello(requestId, envelopeMajor, HostProtocol::VERSION_MINOR_0_1,
        request, snapshot, radioStructuredBridge);
}

static_assert(DeviceSettings::SCHEMA_VERSION <= UINT8_MAX,
    "Configuration Schema must fit the Host Protocol uint8 field");
static_assert(DIAGNOSTIC_METRIC_COUNT == 10,
    "The v0.6 diagnostic registry must contain ten metrics");

}  // namespace HostOperationService
