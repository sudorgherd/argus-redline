#pragma once

#include <stddef.h>
#include <stdint.h>

#include <host_protocol.h>
#include <event_protocol.h>
#include <event_tx_diagnostics.h>

namespace HostProtocol {

constexpr uint8_t OPERATION_REQUEST_FIXED_SIZE = 7;
constexpr uint8_t OPERATION_RESPONSE_FIXED_SIZE = 9;
constexpr uint8_t MAX_REQUEST_VALUE_SIZE =
    MAX_PAYLOAD_SIZE - OPERATION_REQUEST_FIXED_SIZE;
constexpr uint8_t MAX_RESPONSE_VALUE_SIZE =
    MAX_PAYLOAD_SIZE - OPERATION_RESPONSE_FIXED_SIZE;

constexpr uint8_t DEVICE_INFO_SIZE = 8;
constexpr uint8_t STATUS_SIZE = 10;
constexpr uint8_t CAPABILITY_DESCRIPTION_SIZE = 6;
constexpr uint8_t MAX_CAPABILITY_PAGE_ENTRIES = 9;
constexpr uint8_t MAX_DIAGNOSTIC_PAGE_ENTRIES = 3;
constexpr uint16_t CAPABILITY_PAGE_END = 0xFFFF;
constexpr uint8_t DIAGNOSTIC_PAGE_END = 0xFF;
constexpr uint8_t HOST_EVENT_IDENTITY_SIZE = 9;
constexpr uint8_t HOST_EVENT_RECORD_SIZE = 29;
constexpr uint8_t EVENT_DIAGNOSTIC_COUNTER_COUNT = 15;
constexpr uint8_t EVENT_DIAGNOSTICS_RECORD_SIZE = 111;
constexpr uint8_t EVENT_DIAGNOSTICS_SCHEMA = 1;
constexpr uint8_t EVENT_TX_DIAGNOSTICS_SCHEMA = 2;
constexpr uint8_t EVENT_TX_DIAGNOSTICS_SIZE = 99;
static_assert(EVENT_TX_DIAGNOSTICS_SIZE <= MAX_RESPONSE_VALUE_SIZE,
    "Event TX diagnostic page must fit the existing Host frame");
static_assert(39 + 2 * EventTxDiagnostics::COUNTER_COUNT == EVENT_TX_DIAGNOSTICS_SIZE,
    "Event TX diagnostic geometry");

constexpr uint8_t EVENT_DIAGNOSTICS_WAITING_ADMISSION = 0x01;
constexpr uint8_t EVENT_DIAGNOSTICS_WAITING_RETRY = 0x02;
constexpr uint8_t EVENT_DIAGNOSTICS_LIFETIME_AVAILABLE = 0x04;
constexpr uint8_t EVENT_DIAGNOSTICS_ORDINAL_AVAILABLE = 0x08;
constexpr uint8_t EVENT_DIAGNOSTICS_PERSISTENCE_DEGRADED = 0x10;
constexpr uint8_t EVENT_DIAGNOSTICS_HUB_UNAVAILABLE = 0x20;
constexpr uint8_t EVENT_DIAGNOSTICS_KNOWN_FLAGS = 0x3F;

constexpr uint16_t STATUS_READY = 0x0001;
constexpr uint16_t STATUS_RADIO_OPERATIONAL = 0x0002;
constexpr uint16_t STATUS_TRANSACTION_ACTIVE = 0x0004;
constexpr uint16_t STATUS_DEGRADED = 0x0008;
constexpr uint16_t STATUS_ERROR = 0x0010;
constexpr uint16_t KNOWN_STATUS_FLAGS = 0x001F;
constexpr uint16_t RESERVED_STATUS_FLAGS = 0xFFE0;

enum class PayloadResult : uint8_t {
    OK = 0x00,
    NULL_ARGUMENT = 0x01,
    OUTPUT_TOO_SMALL = 0x02,
    INVALID_LENGTH = 0x03,
    INVALID_FIELD = 0x04,
    UNKNOWN_CATEGORY = 0x05,
    UNKNOWN_OPERATION = 0x06,
    UNSUPPORTED_CATEGORY_OPERATION = 0x07,
    INVALID_TARGET = 0x08,
    INVALID_VALUE_TYPE = 0x09,
    INVALID_VALUE_LENGTH = 0x0A,
    INVALID_VALUE = 0x0B,
    INVALID_RESULT_CLASS = 0x0C,
    INVALID_RESULT_CODE = 0x0D,
    INVALID_RESPONSE_VALUE = 0x0E
};

enum class OperationClassification : uint8_t {
    VALID = 0x00,
    MALFORMED = 0x01,
    UNSUPPORTED = 0x02
};

struct HelloRequest {
    uint8_t minimumMinor;
    uint8_t maximumMinor;
};

struct HelloResponse {
    uint8_t selectedMinor;
    uint8_t firmwareMajor;
    uint8_t firmwareMinor;
    uint8_t firmwarePatch;
    uint8_t wireProtocol;
    uint8_t configurationSchema;
    HardwareProfile hardwareProfile;
    DeviceRole role;
    uint8_t deviceId;
    uint8_t maximumHostPayload;
    uint16_t operationCategoryBitmap;
    uint16_t featureBitmap;
    uint8_t maximumOutstandingOperations;
    uint8_t reserved;
};

struct TypedValue {
    uint8_t type;
    uint8_t length;
    uint8_t bytes[MAX_REQUEST_VALUE_SIZE];
};

struct OperationRequest {
    OperationCategory category;
    OperationCode operation;
    uint8_t targetDeviceId;
    uint16_t targetId;
    TypedValue value;
};

struct OperationResponse {
    OperationCategory category;
    OperationCode operation;
    uint8_t targetDeviceId;
    uint16_t targetId;
    ResultClass resultClass;
    uint8_t resultCode;
    TypedValue value;
};

struct ProtocolError {
    ProtocolErrorCode errorCode;
    uint8_t offendingType;
    uint16_t detail;
};

struct HostEventIdentity {
    uint8_t sourceDeviceId;
    uint32_t eventEpoch;
    uint32_t eventId;
};

struct HostEventRecord {
    uint8_t available;
    uint8_t sourceDeviceId;
    uint8_t family;
    uint8_t flags;
    uint32_t eventEpoch;
    uint32_t eventId;
    uint32_t lifetimeBudgetSeconds;
    uint8_t bodyLength;
    uint8_t body[EventProtocol::MAX_BODY_SIZE];
};

struct EventDiagnosticsRecord {
    uint8_t schema;
    DeviceRole role;
    uint8_t custodyCount;
    uint8_t capacity;
    uint8_t activeCount;
    uint8_t consumedCount;
    uint8_t recordAvailable;
    uint8_t state;
    HostEventIdentity identity;
    uint8_t family;
    uint8_t attemptsUsed;
    uint8_t attemptsMaximum;
    uint8_t flags;
    uint32_t remainingLifetimeSeconds;
    uint32_t admissionOrdinal;
    uint8_t recentAdmissionAvailable;
    HostEventIdentity recentAdmission;
    uint8_t producerKind;
    uint8_t producerResult;
    uint8_t producerIdentityAvailable;
    HostEventIdentity producerIdentity;
    uint32_t counters[EVENT_DIAGNOSTIC_COUNTER_COUNT];
};

struct DeviceInfoRecord {
    uint8_t firmwareMajor;
    uint8_t firmwareMinor;
    uint8_t firmwarePatch;
    uint8_t wireProtocol;
    uint8_t configurationSchema;
    HardwareProfile hardwareProfile;
    DeviceRole role;
    uint8_t deviceId;
};

struct StatusRecord {
    uint16_t statusFlags;
    uint32_t uptimeSeconds;
    uint16_t retryCount;
    uint16_t timeoutCount;
};

struct CapabilityPageRecord {
    uint16_t nextCursor;
    uint8_t count;
    DeviceCapabilities::CapabilityId capabilityIds[MAX_CAPABILITY_PAGE_ENTRIES];
};

struct CapabilityDescriptionRecord {
    DeviceCapabilities::CapabilityClass capabilityClass;
    CapabilityValueType valueType;
    uint8_t operationFlags;
    DeviceCapabilities::UnitCode unit;
    uint8_t availability;
    uint8_t reserved;
};

struct DiagnosticEntry {
    uint8_t metricId;
    uint32_t value;
};

struct DiagnosticPageRecord {
    uint8_t nextCursor;
    uint8_t count;
    DiagnosticEntry entries[MAX_DIAGNOSTIC_PAGE_ENTRIES];
};

inline void writeUint16Le(uint8_t* output, uint16_t value) {
    output[0] = static_cast<uint8_t>(value);
    output[1] = static_cast<uint8_t>(value >> 8);
}

inline void writeUint32Le(uint8_t* output, uint32_t value) {
    output[0] = static_cast<uint8_t>(value);
    output[1] = static_cast<uint8_t>(value >> 8);
    output[2] = static_cast<uint8_t>(value >> 16);
    output[3] = static_cast<uint8_t>(value >> 24);
}

inline uint16_t readUint16Le(const uint8_t* input) {
    return static_cast<uint16_t>(input[0]) |
        (static_cast<uint16_t>(input[1]) << 8);
}

inline uint32_t readUint32Le(const uint8_t* input) {
    return static_cast<uint32_t>(input[0]) |
        (static_cast<uint32_t>(input[1]) << 8) |
        (static_cast<uint32_t>(input[2]) << 16) |
        (static_cast<uint32_t>(input[3]) << 24);
}

inline bool isValidHostEventIdentity(const HostEventIdentity& identity) {
    return identity.sourceDeviceId != 0 && identity.eventEpoch != 0 &&
        identity.eventId != 0;
}

inline PayloadResult encodeHostEventIdentity(const HostEventIdentity& identity,
    uint8_t* output, size_t outputCapacity, size_t& outputLength) {
    outputLength = 0;
    if (output == nullptr) return PayloadResult::NULL_ARGUMENT;
    if (!isValidHostEventIdentity(identity)) return PayloadResult::INVALID_VALUE;
    if (outputCapacity < HOST_EVENT_IDENTITY_SIZE) return PayloadResult::OUTPUT_TOO_SMALL;
    output[0] = identity.sourceDeviceId;
    writeUint32Le(output + 1, identity.eventEpoch);
    writeUint32Le(output + 5, identity.eventId);
    outputLength = HOST_EVENT_IDENTITY_SIZE;
    return PayloadResult::OK;
}

inline PayloadResult decodeHostEventIdentity(const uint8_t* input,
    size_t inputLength, HostEventIdentity& identity) {
    if (input == nullptr) return PayloadResult::NULL_ARGUMENT;
    if (inputLength != HOST_EVENT_IDENTITY_SIZE) return PayloadResult::INVALID_LENGTH;
    HostEventIdentity candidate = {input[0], readUint32Le(input + 1),
        readUint32Le(input + 5)};
    if (!isValidHostEventIdentity(candidate)) return PayloadResult::INVALID_VALUE;
    identity = candidate;
    return PayloadResult::OK;
}

inline bool isValidHostEventRecord(const HostEventRecord& record) {
    if (record.available == 0) {
        if (record.sourceDeviceId != 0 || record.family != 0 || record.flags != 0 ||
            record.eventEpoch != 0 || record.eventId != 0 ||
            record.lifetimeBudgetSeconds != 0 || record.bodyLength != 0) return false;
        for (size_t i = 0; i < EventProtocol::MAX_BODY_SIZE; ++i)
            if (record.body[i] != 0) return false;
        return true;
    }
    if (record.available != 1 || record.sourceDeviceId == 0 ||
        record.eventEpoch == 0 || record.eventId == 0 ||
        (record.flags & static_cast<uint8_t>(~EventProtocol::ALLOWED_FLAGS)) != 0 ||
        record.lifetimeBudgetSeconds < EventProtocol::MIN_LIFETIME_SECONDS ||
        record.lifetimeBudgetSeconds > EventProtocol::MAX_LIFETIME_SECONDS ||
        record.bodyLength > EventProtocol::MAX_BODY_SIZE ||
        !EventProtocol::isRegisteredFamily(record.family) ||
        !EventProtocol::isValidFamilyBody(record.family, record.body, record.bodyLength))
        return false;
    for (size_t i = record.bodyLength; i < EventProtocol::MAX_BODY_SIZE; ++i)
        if (record.body[i] != 0) return false;
    return true;
}

inline PayloadResult encodeHostEventRecord(const HostEventRecord& record,
    uint8_t* output, size_t outputCapacity, size_t& outputLength) {
    outputLength = 0;
    if (output == nullptr) return PayloadResult::NULL_ARGUMENT;
    if (!isValidHostEventRecord(record)) return PayloadResult::INVALID_VALUE;
    if (outputCapacity < HOST_EVENT_RECORD_SIZE) return PayloadResult::OUTPUT_TOO_SMALL;
    for (size_t i = 0; i < HOST_EVENT_RECORD_SIZE; ++i) output[i] = 0;
    output[0] = record.available;
    output[1] = record.sourceDeviceId;
    output[2] = record.family;
    output[3] = record.flags;
    writeUint32Le(output + 4, record.eventEpoch);
    writeUint32Le(output + 8, record.eventId);
    writeUint32Le(output + 12, record.lifetimeBudgetSeconds);
    output[16] = record.bodyLength;
    for (size_t i = 0; i < EventProtocol::MAX_BODY_SIZE; ++i) output[17 + i] = record.body[i];
    outputLength = HOST_EVENT_RECORD_SIZE;
    return PayloadResult::OK;
}

inline PayloadResult decodeHostEventRecord(const uint8_t* input,
    size_t inputLength, HostEventRecord& record) {
    if (input == nullptr) return PayloadResult::NULL_ARGUMENT;
    if (inputLength != HOST_EVENT_RECORD_SIZE) return PayloadResult::INVALID_LENGTH;
    HostEventRecord candidate = {};
    candidate.available = input[0]; candidate.sourceDeviceId = input[1];
    candidate.family = input[2]; candidate.flags = input[3];
    candidate.eventEpoch = readUint32Le(input + 4);
    candidate.eventId = readUint32Le(input + 8);
    candidate.lifetimeBudgetSeconds = readUint32Le(input + 12);
    candidate.bodyLength = input[16];
    for (size_t i = 0; i < EventProtocol::MAX_BODY_SIZE; ++i)
        candidate.body[i] = input[17 + i];
    if (!isValidHostEventRecord(candidate)) return PayloadResult::INVALID_VALUE;
    record = candidate;
    return PayloadResult::OK;
}

inline bool isZeroHostEventIdentity(const HostEventIdentity& identity) {
    return identity.sourceDeviceId == 0 && identity.eventEpoch == 0 &&
        identity.eventId == 0;
}

inline bool isValidEventDiagnosticsRecord(const EventDiagnosticsRecord& record) {
    if (record.schema != EVENT_DIAGNOSTICS_SCHEMA ||
        !isKnownDeviceRole(record.role) || record.capacity > 8 ||
        record.custodyCount > record.capacity ||
        record.activeCount > record.capacity ||
        record.consumedCount > record.capacity ||
        record.activeCount + record.consumedCount > record.capacity ||
        record.recordAvailable > 1 || record.recentAdmissionAvailable > 1 ||
        record.producerIdentityAvailable > 1 || record.state > 11 ||
        record.producerKind > 3 || record.producerResult > 5 ||
        (record.flags & static_cast<uint8_t>(~EVENT_DIAGNOSTICS_KNOWN_FLAGS)) != 0)
        return false;
    if (record.recordAvailable == 1) {
        if (!isValidHostEventIdentity(record.identity) ||
            !EventProtocol::isRegisteredFamily(record.family)) return false;
    } else if (!isZeroHostEventIdentity(record.identity) || record.family != 0 ||
        record.attemptsUsed != 0 || record.attemptsMaximum != 0 ||
        (record.flags & (EVENT_DIAGNOSTICS_LIFETIME_AVAILABLE |
            EVENT_DIAGNOSTICS_ORDINAL_AVAILABLE)) != 0 ||
        record.remainingLifetimeSeconds != 0 || record.admissionOrdinal != 0) {
        return false;
    }
    if (record.attemptsUsed > record.attemptsMaximum ||
        ((record.flags & EVENT_DIAGNOSTICS_LIFETIME_AVAILABLE) == 0 &&
            record.remainingLifetimeSeconds != 0) ||
        ((record.flags & EVENT_DIAGNOSTICS_ORDINAL_AVAILABLE) == 0 &&
            record.admissionOrdinal != 0)) return false;
    if (record.recentAdmissionAvailable == 1) {
        if (!isValidHostEventIdentity(record.recentAdmission)) return false;
    } else if (!isZeroHostEventIdentity(record.recentAdmission)) return false;
    if (record.producerIdentityAvailable == 1) {
        if (!isValidHostEventIdentity(record.producerIdentity) ||
            record.producerResult != 1) return false;
    } else if (!isZeroHostEventIdentity(record.producerIdentity)) return false;
    return true;
}

inline PayloadResult encodeEventDiagnosticsRecord(
    const EventDiagnosticsRecord& record,
    TypedValue& value
) {
    if (!isValidEventDiagnosticsRecord(record)) return PayloadResult::INVALID_VALUE;
    TypedValue candidate = {};
    candidate.type = STRUCTURE_VALUE_TYPE;
    candidate.length = EVENT_DIAGNOSTICS_RECORD_SIZE;
    uint8_t* output = candidate.bytes;
    output[0] = record.schema;
    output[1] = static_cast<uint8_t>(record.role);
    output[2] = record.custodyCount;
    output[3] = record.capacity;
    output[4] = record.activeCount;
    output[5] = record.consumedCount;
    output[6] = record.recordAvailable;
    output[7] = record.state;
    output[8] = record.identity.sourceDeviceId;
    writeUint32Le(output + 9, record.identity.eventEpoch);
    writeUint32Le(output + 13, record.identity.eventId);
    output[17] = record.family;
    output[18] = record.attemptsUsed;
    output[19] = record.attemptsMaximum;
    output[20] = record.flags;
    writeUint32Le(output + 21, record.remainingLifetimeSeconds);
    writeUint32Le(output + 25, record.admissionOrdinal);
    output[29] = record.recentAdmissionAvailable;
    output[30] = record.recentAdmission.sourceDeviceId;
    writeUint32Le(output + 31, record.recentAdmission.eventEpoch);
    writeUint32Le(output + 35, record.recentAdmission.eventId);
    output[39] = record.producerKind;
    output[40] = record.producerResult;
    output[41] = record.producerIdentityAvailable;
    output[42] = record.producerIdentity.sourceDeviceId;
    writeUint32Le(output + 43, record.producerIdentity.eventEpoch);
    writeUint32Le(output + 47, record.producerIdentity.eventId);
    for (size_t i = 0; i < EVENT_DIAGNOSTIC_COUNTER_COUNT; ++i)
        writeUint32Le(output + 51 + 4 * i, record.counters[i]);
    value = candidate;
    return PayloadResult::OK;
}

inline PayloadResult decodeEventDiagnosticsRecord(
    const TypedValue& value,
    EventDiagnosticsRecord& record
) {
    if (value.type != STRUCTURE_VALUE_TYPE ||
        value.length != EVENT_DIAGNOSTICS_RECORD_SIZE)
        return PayloadResult::INVALID_VALUE;
    const uint8_t* input = value.bytes;
    EventDiagnosticsRecord candidate = {};
    candidate.schema = input[0];
    candidate.role = static_cast<DeviceRole>(input[1]);
    candidate.custodyCount = input[2];
    candidate.capacity = input[3];
    candidate.activeCount = input[4];
    candidate.consumedCount = input[5];
    candidate.recordAvailable = input[6];
    candidate.state = input[7];
    candidate.identity = {input[8], readUint32Le(input + 9),
        readUint32Le(input + 13)};
    candidate.family = input[17];
    candidate.attemptsUsed = input[18];
    candidate.attemptsMaximum = input[19];
    candidate.flags = input[20];
    candidate.remainingLifetimeSeconds = readUint32Le(input + 21);
    candidate.admissionOrdinal = readUint32Le(input + 25);
    candidate.recentAdmissionAvailable = input[29];
    candidate.recentAdmission = {input[30], readUint32Le(input + 31),
        readUint32Le(input + 35)};
    candidate.producerKind = input[39];
    candidate.producerResult = input[40];
    candidate.producerIdentityAvailable = input[41];
    candidate.producerIdentity = {input[42], readUint32Le(input + 43),
        readUint32Le(input + 47)};
    for (size_t i = 0; i < EVENT_DIAGNOSTIC_COUNTER_COUNT; ++i)
        candidate.counters[i] = readUint32Le(input + 51 + 4 * i);
    if (!isValidEventDiagnosticsRecord(candidate)) return PayloadResult::INVALID_VALUE;
    record = candidate;
    return PayloadResult::OK;
}

inline bool validEventTxDiagnostics(DeviceRole role, const EventTxDiagnostics::Snapshot& s) {
    if (!isKnownDeviceRole(role) || (role == DeviceRole::HUB && s.available) ||
        s.lastStep > EventTxDiagnostics::COUNTER_COUNT || s.controllerState > 8 ||
        s.radioOwner > 6 || s.attempt > 5 ||
        (s.admissionStatus != 0xFF && s.admissionStatus > 4)) return false;
    const HostEventIdentity identity = {s.source, s.epoch, s.id};
    if (s.attempt ? !isValidHostEventIdentity(identity) : !isZeroHostEventIdentity(identity))
        return false;
    if (!s.available) {
        if (s.lastStep || s.controllerState || s.radioOwner || s.attempt ||
            s.lastAt || s.startAt || s.completedAt || s.previousClock || s.inputClock ||
            s.startResult || s.admissionStatus != 0xFF) return false;
        for (auto count : s.counters) if (count) return false;
    }
    return true;
}

inline PayloadResult encodeEventTxDiagnostics(DeviceRole role,
    const EventTxDiagnostics::Snapshot& s, TypedValue& value) {
    if (!validEventTxDiagnostics(role, s)) return PayloadResult::INVALID_VALUE;
    TypedValue candidate = {};
    candidate.type = STRUCTURE_VALUE_TYPE; candidate.length = EVENT_TX_DIAGNOSTICS_SIZE;
    uint8_t* b = candidate.bytes;
    b[0] = EVENT_TX_DIAGNOSTICS_SCHEMA; b[1] = static_cast<uint8_t>(role);
    b[2] = s.available; b[3] = s.lastStep; b[4] = s.controllerState;
    b[5] = s.radioOwner; b[6] = s.attempt; b[7] = s.source;
    writeUint32Le(b + 8, s.epoch); writeUint32Le(b + 12, s.id);
    writeUint32Le(b + 16, s.lastAt); writeUint32Le(b + 20, s.startAt);
    writeUint32Le(b + 24, s.completedAt); writeUint32Le(b + 28, s.previousClock);
    writeUint32Le(b + 32, s.inputClock);
    writeUint16Le(b + 36, static_cast<uint16_t>(s.startResult)); b[38] = s.admissionStatus;
    for (uint8_t i = 0; i < EventTxDiagnostics::COUNTER_COUNT; ++i)
        writeUint16Le(b + 39 + 2 * i, s.counters[i]);
    value = candidate; return PayloadResult::OK;
}

inline PayloadResult decodeEventTxDiagnostics(const TypedValue& value,
    DeviceRole& role, EventTxDiagnostics::Snapshot& snapshot) {
    if (value.type != STRUCTURE_VALUE_TYPE || value.length != EVENT_TX_DIAGNOSTICS_SIZE ||
        value.bytes[0] != EVENT_TX_DIAGNOSTICS_SCHEMA || value.bytes[2] > 1)
        return PayloadResult::INVALID_VALUE;
    const uint8_t* b = value.bytes;
    const DeviceRole decodedRole = static_cast<DeviceRole>(b[1]);
    EventTxDiagnostics::Snapshot s;
    s.available = b[2]; s.lastStep = b[3]; s.controllerState = b[4];
    s.radioOwner = b[5]; s.attempt = b[6]; s.source = b[7];
    s.epoch = readUint32Le(b + 8); s.id = readUint32Le(b + 12);
    s.lastAt = readUint32Le(b + 16); s.startAt = readUint32Le(b + 20);
    s.completedAt = readUint32Le(b + 24); s.previousClock = readUint32Le(b + 28);
    s.inputClock = readUint32Le(b + 32);
    const uint16_t rawResult = readUint16Le(b + 36);
    s.startResult = static_cast<int16_t>(rawResult <= INT16_MAX ? rawResult :
        static_cast<int32_t>(rawResult) - 65536);
    s.admissionStatus = b[38];
    for (uint8_t i = 0; i < EventTxDiagnostics::COUNTER_COUNT; ++i)
        s.counters[i] = readUint16Le(b + 39 + 2 * i);
    if (!validEventTxDiagnostics(decodedRole, s)) return PayloadResult::INVALID_VALUE;
    role = decodedRole; snapshot = s; return PayloadResult::OK;
}

inline uint8_t expectedScalarValueLength(uint8_t type) {
    switch (static_cast<CapabilityValueType>(type)) {
        case CapabilityValueType::NONE:
            return 0;
        case CapabilityValueType::BOOLEAN:
            return 1;
        case CapabilityValueType::NORMALIZED_U16:
        case CapabilityValueType::ENUM_U16:
            return 2;
        case CapabilityValueType::UNSIGNED_32:
        case CapabilityValueType::SIGNED_32:
        case CapabilityValueType::FIXED_Q16_16:
            return 4;
    }
    return 0xFF;
}

inline bool isValidScalarValue(const TypedValue& value) {
    if (value.type == STRUCTURE_VALUE_TYPE ||
        !DeviceCapabilities::isKnownValueType(
            static_cast<CapabilityValueType>(value.type)
        )) {
        return false;
    }
    const uint8_t expectedLength = expectedScalarValueLength(value.type);
    if (value.length != expectedLength) {
        return false;
    }
    return value.type != static_cast<uint8_t>(CapabilityValueType::BOOLEAN) ||
        value.bytes[0] <= 1;
}

inline bool isNoneValue(const TypedValue& value) {
    return value.type == static_cast<uint8_t>(CapabilityValueType::NONE) &&
        value.length == 0;
}

inline bool isNonNoneScalarValue(const TypedValue& value) {
    return isValidScalarValue(value) && !isNoneValue(value);
}

inline void setNoneValue(TypedValue& value) {
    value = {};
    value.type = static_cast<uint8_t>(CapabilityValueType::NONE);
}

inline bool setBooleanValue(TypedValue& value, uint8_t booleanValue) {
    if (booleanValue > 1) return false;
    value = {};
    value.type = static_cast<uint8_t>(CapabilityValueType::BOOLEAN);
    value.length = 1;
    value.bytes[0] = booleanValue;
    return true;
}

inline bool setUint16Value(
    TypedValue& value,
    CapabilityValueType type,
    uint16_t numericValue
) {
    if (type != CapabilityValueType::NORMALIZED_U16 &&
        type != CapabilityValueType::ENUM_U16) return false;
    value = {};
    value.type = static_cast<uint8_t>(type);
    value.length = 2;
    writeUint16Le(value.bytes, numericValue);
    return true;
}

inline bool setUint32Value(
    TypedValue& value,
    CapabilityValueType type,
    uint32_t bits
) {
    if (type != CapabilityValueType::UNSIGNED_32 &&
        type != CapabilityValueType::SIGNED_32 &&
        type != CapabilityValueType::FIXED_Q16_16) return false;
    value = {};
    value.type = static_cast<uint8_t>(type);
    value.length = 4;
    writeUint32Le(value.bytes, bits);
    return true;
}

inline PayloadResult encodeTypedValue(
    const TypedValue& value,
    uint8_t* output,
    size_t outputCapacity,
    size_t& outputLength
) {
    outputLength = 0;
    if (output == nullptr) {
        return PayloadResult::NULL_ARGUMENT;
    }
    if (!isValidScalarValue(value)) {
        return DeviceCapabilities::isKnownValueType(
            static_cast<CapabilityValueType>(value.type)
        ) ? PayloadResult::INVALID_VALUE : PayloadResult::INVALID_VALUE_TYPE;
    }
    if (outputCapacity < value.length) {
        return PayloadResult::OUTPUT_TOO_SMALL;
    }
    for (size_t index = 0; index < value.length; ++index) {
        output[index] = value.bytes[index];
    }
    outputLength = value.length;
    return PayloadResult::OK;
}

inline PayloadResult decodeTypedValue(
    uint8_t type,
    const uint8_t* input,
    size_t inputLength,
    TypedValue& value
) {
    if (input == nullptr && inputLength != 0) {
        return PayloadResult::NULL_ARGUMENT;
    }
    if (type == STRUCTURE_VALUE_TYPE ||
        !DeviceCapabilities::isKnownValueType(
            static_cast<CapabilityValueType>(type)
        )) {
        return PayloadResult::INVALID_VALUE_TYPE;
    }
    if (inputLength != expectedScalarValueLength(type)) {
        return PayloadResult::INVALID_VALUE_LENGTH;
    }
    TypedValue candidate = {};
    candidate.type = type;
    candidate.length = static_cast<uint8_t>(inputLength);
    for (size_t index = 0; index < inputLength; ++index) {
        candidate.bytes[index] = input[index];
    }
    if (!isValidScalarValue(candidate)) {
        return PayloadResult::INVALID_VALUE;
    }
    value = candidate;
    return PayloadResult::OK;
}

inline OperationClassification classifyCategoryOperation(
    uint8_t category,
    uint8_t operation
) {
    const OperationCategory typedCategory =
        static_cast<OperationCategory>(category);
    const OperationCode typedOperation = static_cast<OperationCode>(operation);
    if (!isKnownOperationCategory(typedCategory) ||
        !isKnownOperationCode(typedOperation)) {
        return OperationClassification::MALFORMED;
    }
    return isSupportedCategoryOperation(typedCategory, typedOperation)
        ? OperationClassification::VALID
        : OperationClassification::UNSUPPORTED;
}

inline OperationClassification classifyCategoryOperation(uint8_t minor,
    uint8_t category, uint8_t operation) {
    const OperationCategory typedCategory = static_cast<OperationCategory>(category);
    const OperationCode typedOperation = static_cast<OperationCode>(operation);
    if (!isKnownOperationCategory(minor, typedCategory) ||
        !isKnownOperationCode(minor, typedOperation))
        return OperationClassification::MALFORMED;
    return isSupportedCategoryOperation(minor, typedCategory, typedOperation)
        ? OperationClassification::VALID : OperationClassification::UNSUPPORTED;
}

inline bool isValidOperationTarget(
    OperationCategory category,
    OperationCode operation,
    uint16_t targetId
) {
    switch (category) {
        case OperationCategory::DEVICE:
        case OperationCategory::DIAGNOSTIC:
            return targetId == 0;
        case OperationCategory::CAPABILITY:
            return operation == OperationCode::GET_CAPABILITIES
                ? targetId == 0
                : targetId != DeviceCapabilities::INVALID_CAPABILITY_ID;
        case OperationCategory::PROCEDURE:
            return targetId != 0;
        case OperationCategory::EVENT:
            return targetId == 0;
    }
    return false;
}

inline bool isValidOperationRequestValue(
    OperationCode operation,
    const TypedValue& value
) {
    switch (operation) {
        case OperationCode::PING:
        case OperationCode::GET_DEVICE_INFO:
        case OperationCode::GET_STATUS:
        case OperationCode::DESCRIBE_CAPABILITY:
        case OperationCode::READ_CAPABILITY:
            return isNoneValue(value);
        case OperationCode::GET_CAPABILITIES:
            return isNoneValue(value) ||
                (value.type == static_cast<uint8_t>(
                    CapabilityValueType::UNSIGNED_32
                ) && isValidScalarValue(value));
        case OperationCode::SET_INDICATOR:
            return value.type == static_cast<uint8_t>(
                    CapabilityValueType::BOOLEAN
                ) && isValidScalarValue(value);
        case OperationCode::RUN_PROCEDURE:
            return isValidScalarValue(value);
        case OperationCode::GET_DIAGNOSTICS:
            return isNoneValue(value) ||
                (value.type == static_cast<uint8_t>(
                    CapabilityValueType::UNSIGNED_32
                ) && isValidScalarValue(value));
        case OperationCode::GET_EVENT_DIAGNOSTICS:
            return isNoneValue(value) ||
                (value.type == static_cast<uint8_t>(CapabilityValueType::UNSIGNED_32) &&
                 isValidScalarValue(value) && readUint32Le(value.bytes) == 1);
        case OperationCode::POLL_EVENTS:
        case OperationCode::CONSUME_EVENT:
            return false;
    }
    return false;
}

inline PayloadResult validateOperationRequest(uint8_t minor,
    const OperationRequest& request) {
    const OperationClassification classification = classifyCategoryOperation(minor,
        static_cast<uint8_t>(request.category),
        static_cast<uint8_t>(request.operation)
    );
    if (classification == OperationClassification::MALFORMED) {
        return !isKnownOperationCategory(minor, request.category)
            ? PayloadResult::UNKNOWN_CATEGORY
            : PayloadResult::UNKNOWN_OPERATION;
    }
    if (classification == OperationClassification::UNSUPPORTED) {
        return PayloadResult::UNSUPPORTED_CATEGORY_OPERATION;
    }
    if (!isValidOperationTarget(
            request.category, request.operation, request.targetId)) {
        return PayloadResult::INVALID_TARGET;
    }
    if (request.category == OperationCategory::EVENT) {
        if (request.operation == OperationCode::POLL_EVENTS)
            return isNoneValue(request.value) ? PayloadResult::OK : PayloadResult::INVALID_VALUE;
        if (request.operation == OperationCode::CONSUME_EVENT) {
            HostEventIdentity identity = {};
            return request.value.type == STRUCTURE_VALUE_TYPE &&
                request.value.length == HOST_EVENT_IDENTITY_SIZE &&
                decodeHostEventIdentity(request.value.bytes, request.value.length, identity) ==
                    PayloadResult::OK ? PayloadResult::OK : PayloadResult::INVALID_VALUE;
        }
    }
    if (!isValidOperationRequestValue(request.operation, request.value)) {
        return PayloadResult::INVALID_VALUE;
    }
    return PayloadResult::OK;
}

inline PayloadResult validateOperationRequest(const OperationRequest& request) {
    return validateOperationRequest(VERSION_MINOR_0_1, request);
}

inline PayloadResult encodeHelloRequest(
    uint8_t frameMinor,
    const HelloRequest& request,
    uint8_t* output,
    size_t outputCapacity,
    size_t& outputLength
) {
    outputLength = 0;
    if (output == nullptr) return PayloadResult::NULL_ARGUMENT;
    if (!isValidHelloMinorRange(frameMinor,
            request.minimumMinor, request.maximumMinor)) {
        return PayloadResult::INVALID_FIELD;
    }
    if (outputCapacity < HELLO_REQUEST_PAYLOAD_SIZE) {
        return PayloadResult::OUTPUT_TOO_SMALL;
    }
    output[0] = request.minimumMinor;
    output[1] = request.maximumMinor;
    outputLength = HELLO_REQUEST_PAYLOAD_SIZE;
    return PayloadResult::OK;
}

inline PayloadResult encodeHelloRequest(const HelloRequest& request,
    uint8_t* output, size_t outputCapacity, size_t& outputLength) {
    return encodeHelloRequest(VERSION_MINOR_0_1, request, output,
        outputCapacity, outputLength);
}

inline PayloadResult decodeHelloRequest(
    uint8_t frameMinor,
    const uint8_t* input,
    size_t inputLength,
    HelloRequest& request
) {
    if (input == nullptr) return PayloadResult::NULL_ARGUMENT;
    if (inputLength != HELLO_REQUEST_PAYLOAD_SIZE) {
        return PayloadResult::INVALID_LENGTH;
    }
    HelloRequest candidate = {input[0], input[1]};
    if (!isValidHelloMinorRange(frameMinor,
            candidate.minimumMinor, candidate.maximumMinor)) {
        return PayloadResult::INVALID_FIELD;
    }
    request = candidate;
    return PayloadResult::OK;
}

inline PayloadResult decodeHelloRequest(const uint8_t* input,
    size_t inputLength, HelloRequest& request) {
    return decodeHelloRequest(VERSION_MINOR_0_1, input, inputLength, request);
}

inline bool isValidHelloResponse(const HelloResponse& response) {
    return response.selectedMinor >= MIN_SUPPORTED_MINOR &&
        response.selectedMinor <= MAX_SUPPORTED_MINOR &&
        isKnownHardwareProfile(response.hardwareProfile) &&
        isKnownDeviceRole(response.role) &&
        response.maximumHostPayload == MAX_PAYLOAD_SIZE &&
        hasValidCategoryBitmap(response.selectedMinor,
            response.operationCategoryBitmap) &&
        hasValidFeatureBitmap(response.selectedMinor, response.featureBitmap) &&
        hasConsistentEventAdvertisement(response.selectedMinor,
            response.operationCategoryBitmap, response.featureBitmap) &&
        response.maximumOutstandingOperations == MAX_OUTSTANDING_OPERATIONS &&
        response.reserved == HELLO_RESERVED_VALUE;
}

inline PayloadResult encodeHelloResponse(
    const HelloResponse& response,
    uint8_t* output,
    size_t outputCapacity,
    size_t& outputLength
) {
    outputLength = 0;
    if (output == nullptr) return PayloadResult::NULL_ARGUMENT;
    if (!isValidHelloResponse(response)) return PayloadResult::INVALID_FIELD;
    if (outputCapacity < HELLO_RESPONSE_PAYLOAD_SIZE) {
        return PayloadResult::OUTPUT_TOO_SMALL;
    }
    output[0] = response.selectedMinor;
    output[1] = response.firmwareMajor;
    output[2] = response.firmwareMinor;
    output[3] = response.firmwarePatch;
    output[4] = response.wireProtocol;
    output[5] = response.configurationSchema;
    output[6] = static_cast<uint8_t>(response.hardwareProfile);
    output[7] = static_cast<uint8_t>(response.role);
    output[8] = response.deviceId;
    output[9] = response.maximumHostPayload;
    writeUint16Le(output + 10, response.operationCategoryBitmap);
    writeUint16Le(output + 12, response.featureBitmap);
    output[14] = response.maximumOutstandingOperations;
    output[15] = response.reserved;
    outputLength = HELLO_RESPONSE_PAYLOAD_SIZE;
    return PayloadResult::OK;
}

inline PayloadResult decodeHelloResponse(
    const uint8_t* input,
    size_t inputLength,
    HelloResponse& response
) {
    if (input == nullptr) return PayloadResult::NULL_ARGUMENT;
    if (inputLength != HELLO_RESPONSE_PAYLOAD_SIZE) {
        return PayloadResult::INVALID_LENGTH;
    }
    HelloResponse candidate = {};
    candidate.selectedMinor = input[0];
    candidate.firmwareMajor = input[1];
    candidate.firmwareMinor = input[2];
    candidate.firmwarePatch = input[3];
    candidate.wireProtocol = input[4];
    candidate.configurationSchema = input[5];
    candidate.hardwareProfile = static_cast<HardwareProfile>(input[6]);
    candidate.role = static_cast<DeviceRole>(input[7]);
    candidate.deviceId = input[8];
    candidate.maximumHostPayload = input[9];
    candidate.operationCategoryBitmap = readUint16Le(input + 10);
    candidate.featureBitmap = readUint16Le(input + 12);
    candidate.maximumOutstandingOperations = input[14];
    candidate.reserved = input[15];
    if (!isValidHelloResponse(candidate)) return PayloadResult::INVALID_FIELD;
    response = candidate;
    return PayloadResult::OK;
}

inline PayloadResult encodeOperationRequest(
    uint8_t minor,
    const OperationRequest& request,
    uint8_t* output,
    size_t outputCapacity,
    size_t& outputLength
) {
    outputLength = 0;
    if (output == nullptr) return PayloadResult::NULL_ARGUMENT;
    const PayloadResult validation = validateOperationRequest(minor, request);
    if (validation != PayloadResult::OK) return validation;
    const size_t required = OPERATION_REQUEST_FIXED_SIZE + request.value.length;
    if (required > MAX_PAYLOAD_SIZE) return PayloadResult::INVALID_LENGTH;
    if (outputCapacity < required) return PayloadResult::OUTPUT_TOO_SMALL;
    output[0] = static_cast<uint8_t>(request.category);
    output[1] = static_cast<uint8_t>(request.operation);
    output[2] = request.targetDeviceId;
    writeUint16Le(output + 3, request.targetId);
    output[5] = request.value.type;
    output[6] = request.value.length;
    for (size_t index = 0; index < request.value.length; ++index) {
        output[7 + index] = request.value.bytes[index];
    }
    outputLength = required;
    return PayloadResult::OK;
}

inline PayloadResult encodeOperationRequest(const OperationRequest& request,
    uint8_t* output, size_t outputCapacity, size_t& outputLength) {
    return encodeOperationRequest(VERSION_MINOR_0_1, request, output,
        outputCapacity, outputLength);
}

inline PayloadResult decodeOperationRequest(
    uint8_t minor,
    const uint8_t* input,
    size_t inputLength,
    OperationRequest& request
) {
    if (input == nullptr) return PayloadResult::NULL_ARGUMENT;
    if (inputLength < OPERATION_REQUEST_FIXED_SIZE ||
        inputLength > MAX_PAYLOAD_SIZE) return PayloadResult::INVALID_LENGTH;
    const uint8_t valueLength = input[6];
    if (inputLength != OPERATION_REQUEST_FIXED_SIZE + valueLength) {
        return PayloadResult::INVALID_LENGTH;
    }
    OperationRequest candidate = {};
    candidate.category = static_cast<OperationCategory>(input[0]);
    candidate.operation = static_cast<OperationCode>(input[1]);
    candidate.targetDeviceId = input[2];
    candidate.targetId = readUint16Le(input + 3);
    candidate.value.type = input[5];
    candidate.value.length = valueLength;
    for (size_t index = 0; index < valueLength; ++index)
        candidate.value.bytes[index] = input[7 + index];
    if (candidate.value.type != STRUCTURE_VALUE_TYPE &&
        !isValidScalarValue(candidate.value)) {
        return !isKnownHostValueType(candidate.value.type)
            ? PayloadResult::INVALID_VALUE_TYPE : PayloadResult::INVALID_VALUE;
    }
    const PayloadResult validation = validateOperationRequest(minor, candidate);
    if (validation != PayloadResult::OK) return validation;
    request = candidate;
    return PayloadResult::OK;
}

inline PayloadResult decodeOperationRequest(const uint8_t* input,
    size_t inputLength, OperationRequest& request) {
    return decodeOperationRequest(VERSION_MINOR_0_1, input, inputLength, request);
}

inline bool isValidStructureLength(OperationCode operation, uint8_t length) {
    switch (operation) {
        case OperationCode::GET_DEVICE_INFO:
            return length == DEVICE_INFO_SIZE;
        case OperationCode::GET_STATUS:
            return length == STATUS_SIZE;
        case OperationCode::GET_CAPABILITIES:
            return length >= 3 && length <= 21 && ((length - 3) % 2) == 0;
        case OperationCode::DESCRIBE_CAPABILITY:
            return length == CAPABILITY_DESCRIPTION_SIZE;
        case OperationCode::GET_DIAGNOSTICS:
            return length >= 2 && length <= 17 && ((length - 2) % 5) == 0;
        case OperationCode::POLL_EVENTS:
            return length == HOST_EVENT_RECORD_SIZE;
        case OperationCode::GET_EVENT_DIAGNOSTICS:
            return length == EVENT_DIAGNOSTICS_RECORD_SIZE || length == EVENT_TX_DIAGNOSTICS_SIZE;
        default:
            return false;
    }
}

inline bool isValidStructureValue(
    OperationCode operation,
    const TypedValue& value
) {
    if (value.type != STRUCTURE_VALUE_TYPE ||
        !isValidStructureLength(operation, value.length)) return false;
    switch (operation) {
        case OperationCode::GET_DEVICE_INFO:
            return isKnownHardwareProfile(
                    static_cast<HardwareProfile>(value.bytes[5])) &&
                isKnownDeviceRole(static_cast<DeviceRole>(value.bytes[6]));
        case OperationCode::GET_STATUS:
            return (readUint16Le(value.bytes) & RESERVED_STATUS_FLAGS) == 0;
        case OperationCode::GET_CAPABILITIES: {
            const uint8_t count = value.bytes[2];
            if (count > MAX_CAPABILITY_PAGE_ENTRIES ||
                value.length != 3 + 2 * count) return false;
            for (size_t index = 0; index < count; ++index) {
                if (readUint16Le(value.bytes + 3 + 2 * index) ==
                    DeviceCapabilities::INVALID_CAPABILITY_ID) return false;
            }
            return true;
        }
        case OperationCode::DESCRIBE_CAPABILITY:
            return DeviceCapabilities::isKnownCapabilityClass(
                    static_cast<DeviceCapabilities::CapabilityClass>(
                        value.bytes[0])) &&
                DeviceCapabilities::isKnownValueType(
                    static_cast<CapabilityValueType>(value.bytes[1])) &&
                DeviceCapabilities::hasKnownOperationFlags(value.bytes[2]) &&
                DeviceCapabilities::isKnownUnitCode(value.bytes[3]) &&
                value.bytes[4] <= 1 && value.bytes[5] == 0;
        case OperationCode::GET_DIAGNOSTICS: {
            const uint8_t count = value.bytes[1];
            return count <= MAX_DIAGNOSTIC_PAGE_ENTRIES &&
                value.length == 2 + 5 * count;
        }
        case OperationCode::POLL_EVENTS: {
            HostEventRecord record = {};
            return decodeHostEventRecord(value.bytes, value.length, record) == PayloadResult::OK;
        }
        case OperationCode::GET_EVENT_DIAGNOSTICS: {
            if (value.length == EVENT_TX_DIAGNOSTICS_SIZE) {
                DeviceRole role; EventTxDiagnostics::Snapshot snapshot;
                return decodeEventTxDiagnostics(value, role, snapshot) == PayloadResult::OK;
            }
            EventDiagnosticsRecord record = {};
            return decodeEventDiagnosticsRecord(value, record) == PayloadResult::OK;
        }
        default:
            return false;
    }
}

inline bool isValidSuccessfulOperationValue(
    OperationCode operation,
    const TypedValue& value
) {
    switch (operation) {
        case OperationCode::PING:
            return value.type == static_cast<uint8_t>(
                    CapabilityValueType::UNSIGNED_32
                ) && isValidScalarValue(value);
        case OperationCode::GET_DEVICE_INFO:
        case OperationCode::GET_STATUS:
        case OperationCode::GET_CAPABILITIES:
        case OperationCode::DESCRIBE_CAPABILITY:
        case OperationCode::GET_DIAGNOSTICS:
        case OperationCode::GET_EVENT_DIAGNOSTICS:
            return isValidStructureValue(operation, value);
        case OperationCode::READ_CAPABILITY:
            return isNonNoneScalarValue(value);
        case OperationCode::SET_INDICATOR:
            return isNoneValue(value);
        case OperationCode::RUN_PROCEDURE:
            return isValidScalarValue(value);
        case OperationCode::POLL_EVENTS:
        case OperationCode::CONSUME_EVENT:
            return false;
    }
    return false;
}

inline PayloadResult validateOperationResponse(uint8_t minor,
    const OperationResponse& response) {
    const OperationClassification classification = classifyCategoryOperation(minor,
        static_cast<uint8_t>(response.category),
        static_cast<uint8_t>(response.operation)
    );
    if (classification == OperationClassification::MALFORMED) {
        return !isKnownOperationCategory(minor, response.category)
            ? PayloadResult::UNKNOWN_CATEGORY
            : PayloadResult::UNKNOWN_OPERATION;
    }
    if (classification == OperationClassification::UNSUPPORTED) {
        return PayloadResult::UNSUPPORTED_CATEGORY_OPERATION;
    }
    if (!isValidOperationTarget(
            response.category, response.operation, response.targetId)) {
        return PayloadResult::INVALID_TARGET;
    }
    if (!isKnownResultClass(minor, response.resultClass)) {
        return PayloadResult::INVALID_RESULT_CLASS;
    }
    if (!isValidResultCode(response.resultClass, response.resultCode)) {
        return PayloadResult::INVALID_RESULT_CODE;
    }
    if (response.resultClass == ResultClass::EVENT_RESULT) {
        if (response.category != OperationCategory::EVENT)
            return PayloadResult::INVALID_RESULT_CLASS;
        const EventResultCode eventCode = static_cast<EventResultCode>(
            response.resultCode);
        if ((eventCode == EventResultCode::NOT_FOUND &&
                response.operation != OperationCode::CONSUME_EVENT) ||
            (eventCode == EventResultCode::STORAGE_FAILURE &&
                response.operation != OperationCode::POLL_EVENTS &&
                response.operation != OperationCode::CONSUME_EVENT))
            return PayloadResult::INVALID_RESULT_CODE;
        return isNoneValue(response.value) ? PayloadResult::OK
                                           : PayloadResult::INVALID_RESPONSE_VALUE;
    }
    if (response.category == OperationCategory::EVENT) {
        if (response.resultClass == ResultClass::SUCCESS &&
            response.resultCode == static_cast<uint8_t>(SuccessCode::OK)) {
            if (response.operation == OperationCode::POLL_EVENTS)
                return isValidStructureValue(response.operation, response.value)
                    ? PayloadResult::OK : PayloadResult::INVALID_RESPONSE_VALUE;
            return isNoneValue(response.value) ? PayloadResult::OK
                                               : PayloadResult::INVALID_RESPONSE_VALUE;
        }
        return response.resultClass == ResultClass::REQUEST_REJECTED &&
            isNoneValue(response.value) ? PayloadResult::OK
                                        : PayloadResult::INVALID_RESPONSE_VALUE;
    }
    const bool isSuccessfulTargetResult =
        response.resultClass == ResultClass::OPERATION_RESULT &&
        response.resultCode == static_cast<uint8_t>(OperationStatus::OK);
    if (isSuccessfulTargetResult) {
        return isValidSuccessfulOperationValue(
            response.operation, response.value
        ) ? PayloadResult::OK : PayloadResult::INVALID_RESPONSE_VALUE;
    }
    return isNoneValue(response.value)
        ? PayloadResult::OK
        : PayloadResult::INVALID_RESPONSE_VALUE;
}

inline PayloadResult validateOperationResponse(const OperationResponse& response) {
    return validateOperationResponse(VERSION_MINOR_0_1, response);
}

inline PayloadResult encodeOperationResponse(
    uint8_t minor,
    const OperationResponse& response,
    uint8_t* output,
    size_t outputCapacity,
    size_t& outputLength
) {
    outputLength = 0;
    if (output == nullptr) return PayloadResult::NULL_ARGUMENT;
    const PayloadResult validation = validateOperationResponse(minor, response);
    if (validation != PayloadResult::OK) return validation;
    const size_t required = OPERATION_RESPONSE_FIXED_SIZE + response.value.length;
    if (required > MAX_PAYLOAD_SIZE) return PayloadResult::INVALID_LENGTH;
    if (outputCapacity < required) return PayloadResult::OUTPUT_TOO_SMALL;
    output[0] = static_cast<uint8_t>(response.category);
    output[1] = static_cast<uint8_t>(response.operation);
    output[2] = response.targetDeviceId;
    writeUint16Le(output + 3, response.targetId);
    output[5] = static_cast<uint8_t>(response.resultClass);
    output[6] = response.resultCode;
    output[7] = response.value.type;
    output[8] = response.value.length;
    for (size_t index = 0; index < response.value.length; ++index) {
        output[9 + index] = response.value.bytes[index];
    }
    outputLength = required;
    return PayloadResult::OK;
}

inline PayloadResult encodeOperationResponse(const OperationResponse& response,
    uint8_t* output, size_t outputCapacity, size_t& outputLength) {
    return encodeOperationResponse(VERSION_MINOR_0_1, response, output,
        outputCapacity, outputLength);
}

inline PayloadResult decodeOperationResponse(
    uint8_t minor,
    const uint8_t* input,
    size_t inputLength,
    OperationResponse& response
) {
    if (input == nullptr) return PayloadResult::NULL_ARGUMENT;
    if (inputLength < OPERATION_RESPONSE_FIXED_SIZE ||
        inputLength > MAX_PAYLOAD_SIZE) return PayloadResult::INVALID_LENGTH;
    const uint8_t valueLength = input[8];
    if (inputLength != OPERATION_RESPONSE_FIXED_SIZE + valueLength) {
        return PayloadResult::INVALID_LENGTH;
    }
    OperationResponse candidate = {};
    candidate.category = static_cast<OperationCategory>(input[0]);
    candidate.operation = static_cast<OperationCode>(input[1]);
    candidate.targetDeviceId = input[2];
    candidate.targetId = readUint16Le(input + 3);
    candidate.resultClass = static_cast<ResultClass>(input[5]);
    candidate.resultCode = input[6];
    candidate.value.type = input[7];
    candidate.value.length = valueLength;
    for (size_t index = 0; index < valueLength; ++index) {
        candidate.value.bytes[index] = input[9 + index];
    }
    if (candidate.value.type != STRUCTURE_VALUE_TYPE &&
        !isValidScalarValue(candidate.value)) {
        return !isKnownHostValueType(candidate.value.type)
            ? PayloadResult::INVALID_VALUE_TYPE
            : PayloadResult::INVALID_VALUE;
    }
    const PayloadResult validation = validateOperationResponse(minor, candidate);
    if (validation != PayloadResult::OK) return validation;
    response = candidate;
    return PayloadResult::OK;
}

inline PayloadResult decodeOperationResponse(const uint8_t* input,
    size_t inputLength, OperationResponse& response) {
    return decodeOperationResponse(VERSION_MINOR_0_1, input, inputLength, response);
}

inline bool isValidProtocolError(const ProtocolError& error) {
    return isKnownProtocolError(error.errorCode) && error.detail == 0;
}

inline PayloadResult encodeProtocolError(
    const ProtocolError& error,
    uint8_t* output,
    size_t outputCapacity,
    size_t& outputLength
) {
    outputLength = 0;
    if (output == nullptr) return PayloadResult::NULL_ARGUMENT;
    if (!isValidProtocolError(error)) return PayloadResult::INVALID_FIELD;
    if (outputCapacity < PROTOCOL_ERROR_PAYLOAD_SIZE) {
        return PayloadResult::OUTPUT_TOO_SMALL;
    }
    output[0] = static_cast<uint8_t>(error.errorCode);
    output[1] = error.offendingType;
    writeUint16Le(output + 2, error.detail);
    outputLength = PROTOCOL_ERROR_PAYLOAD_SIZE;
    return PayloadResult::OK;
}

inline PayloadResult decodeProtocolError(
    const uint8_t* input,
    size_t inputLength,
    ProtocolError& error
) {
    if (input == nullptr) return PayloadResult::NULL_ARGUMENT;
    if (inputLength != PROTOCOL_ERROR_PAYLOAD_SIZE) {
        return PayloadResult::INVALID_LENGTH;
    }
    ProtocolError candidate = {};
    candidate.errorCode = static_cast<ProtocolErrorCode>(input[0]);
    candidate.offendingType = input[1];
    candidate.detail = readUint16Le(input + 2);
    if (!isValidProtocolError(candidate)) return PayloadResult::INVALID_FIELD;
    error = candidate;
    return PayloadResult::OK;
}

inline PayloadResult encodeDeviceInfoRecord(
    const DeviceInfoRecord& record, TypedValue& value
) {
    if (!isKnownHardwareProfile(record.hardwareProfile) ||
        !isKnownDeviceRole(record.role)) return PayloadResult::INVALID_FIELD;
    TypedValue candidate = {};
    candidate.type = STRUCTURE_VALUE_TYPE;
    candidate.length = DEVICE_INFO_SIZE;
    candidate.bytes[0] = record.firmwareMajor;
    candidate.bytes[1] = record.firmwareMinor;
    candidate.bytes[2] = record.firmwarePatch;
    candidate.bytes[3] = record.wireProtocol;
    candidate.bytes[4] = record.configurationSchema;
    candidate.bytes[5] = static_cast<uint8_t>(record.hardwareProfile);
    candidate.bytes[6] = static_cast<uint8_t>(record.role);
    candidate.bytes[7] = record.deviceId;
    value = candidate;
    return PayloadResult::OK;
}

inline PayloadResult decodeDeviceInfoRecord(
    const TypedValue& value, DeviceInfoRecord& record
) {
    if (value.type != STRUCTURE_VALUE_TYPE || value.length != DEVICE_INFO_SIZE) {
        return PayloadResult::INVALID_VALUE;
    }
    DeviceInfoRecord candidate = {};
    candidate.firmwareMajor = value.bytes[0];
    candidate.firmwareMinor = value.bytes[1];
    candidate.firmwarePatch = value.bytes[2];
    candidate.wireProtocol = value.bytes[3];
    candidate.configurationSchema = value.bytes[4];
    candidate.hardwareProfile = static_cast<HardwareProfile>(value.bytes[5]);
    candidate.role = static_cast<DeviceRole>(value.bytes[6]);
    candidate.deviceId = value.bytes[7];
    if (!isKnownHardwareProfile(candidate.hardwareProfile) ||
        !isKnownDeviceRole(candidate.role)) return PayloadResult::INVALID_FIELD;
    record = candidate;
    return PayloadResult::OK;
}

inline PayloadResult encodeStatusRecord(
    const StatusRecord& record, TypedValue& value
) {
    if ((record.statusFlags & RESERVED_STATUS_FLAGS) != 0) {
        return PayloadResult::INVALID_FIELD;
    }
    TypedValue candidate = {};
    candidate.type = STRUCTURE_VALUE_TYPE;
    candidate.length = STATUS_SIZE;
    writeUint16Le(candidate.bytes, record.statusFlags);
    writeUint32Le(candidate.bytes + 2, record.uptimeSeconds);
    writeUint16Le(candidate.bytes + 6, record.retryCount);
    writeUint16Le(candidate.bytes + 8, record.timeoutCount);
    value = candidate;
    return PayloadResult::OK;
}

inline PayloadResult decodeStatusRecord(
    const TypedValue& value, StatusRecord& record
) {
    if (value.type != STRUCTURE_VALUE_TYPE || value.length != STATUS_SIZE) {
        return PayloadResult::INVALID_VALUE;
    }
    StatusRecord candidate = {};
    candidate.statusFlags = readUint16Le(value.bytes);
    candidate.uptimeSeconds = readUint32Le(value.bytes + 2);
    candidate.retryCount = readUint16Le(value.bytes + 6);
    candidate.timeoutCount = readUint16Le(value.bytes + 8);
    if ((candidate.statusFlags & RESERVED_STATUS_FLAGS) != 0) {
        return PayloadResult::INVALID_FIELD;
    }
    record = candidate;
    return PayloadResult::OK;
}

inline PayloadResult encodeCapabilityPageRecord(
    const CapabilityPageRecord& record, TypedValue& value
) {
    if (record.count > MAX_CAPABILITY_PAGE_ENTRIES) {
        return PayloadResult::INVALID_FIELD;
    }
    TypedValue candidate = {};
    candidate.type = STRUCTURE_VALUE_TYPE;
    candidate.length = static_cast<uint8_t>(3 + 2 * record.count);
    writeUint16Le(candidate.bytes, record.nextCursor);
    candidate.bytes[2] = record.count;
    for (size_t index = 0; index < record.count; ++index) {
        if (record.capabilityIds[index] ==
            DeviceCapabilities::INVALID_CAPABILITY_ID) {
            return PayloadResult::INVALID_FIELD;
        }
        writeUint16Le(candidate.bytes + 3 + 2 * index,
            record.capabilityIds[index]);
    }
    value = candidate;
    return PayloadResult::OK;
}

inline PayloadResult decodeCapabilityPageRecord(
    const TypedValue& value, CapabilityPageRecord& record
) {
    if (value.type != STRUCTURE_VALUE_TYPE || value.length < 3 ||
        value.length > 21 || ((value.length - 3) % 2) != 0) {
        return PayloadResult::INVALID_VALUE;
    }
    CapabilityPageRecord candidate = {};
    candidate.nextCursor = readUint16Le(value.bytes);
    candidate.count = value.bytes[2];
    if (candidate.count > MAX_CAPABILITY_PAGE_ENTRIES ||
        value.length != 3 + 2 * candidate.count) {
        return PayloadResult::INVALID_FIELD;
    }
    for (size_t index = 0; index < candidate.count; ++index) {
        candidate.capabilityIds[index] = readUint16Le(
            value.bytes + 3 + 2 * index);
        if (candidate.capabilityIds[index] ==
            DeviceCapabilities::INVALID_CAPABILITY_ID) {
            return PayloadResult::INVALID_FIELD;
        }
    }
    record = candidate;
    return PayloadResult::OK;
}

inline PayloadResult encodeCapabilityDescriptionRecord(
    const CapabilityDescriptionRecord& record, TypedValue& value
) {
    if (!DeviceCapabilities::isKnownCapabilityClass(record.capabilityClass) ||
        !DeviceCapabilities::isKnownValueType(record.valueType) ||
        !DeviceCapabilities::hasKnownOperationFlags(record.operationFlags) ||
        !DeviceCapabilities::isKnownUnitCode(static_cast<uint8_t>(record.unit)) ||
        record.availability > 1 || record.reserved != 0) {
        return PayloadResult::INVALID_FIELD;
    }
    TypedValue candidate = {};
    candidate.type = STRUCTURE_VALUE_TYPE;
    candidate.length = CAPABILITY_DESCRIPTION_SIZE;
    candidate.bytes[0] = static_cast<uint8_t>(record.capabilityClass);
    candidate.bytes[1] = static_cast<uint8_t>(record.valueType);
    candidate.bytes[2] = record.operationFlags;
    candidate.bytes[3] = static_cast<uint8_t>(record.unit);
    candidate.bytes[4] = record.availability;
    candidate.bytes[5] = record.reserved;
    value = candidate;
    return PayloadResult::OK;
}

inline PayloadResult decodeCapabilityDescriptionRecord(
    const TypedValue& value, CapabilityDescriptionRecord& record
) {
    if (value.type != STRUCTURE_VALUE_TYPE ||
        value.length != CAPABILITY_DESCRIPTION_SIZE) {
        return PayloadResult::INVALID_VALUE;
    }
    CapabilityDescriptionRecord candidate = {};
    candidate.capabilityClass = static_cast<DeviceCapabilities::CapabilityClass>(
        value.bytes[0]);
    candidate.valueType = static_cast<CapabilityValueType>(value.bytes[1]);
    candidate.operationFlags = value.bytes[2];
    candidate.unit = static_cast<DeviceCapabilities::UnitCode>(value.bytes[3]);
    candidate.availability = value.bytes[4];
    candidate.reserved = value.bytes[5];
    TypedValue ignored = {};
    if (encodeCapabilityDescriptionRecord(candidate, ignored) !=
        PayloadResult::OK) return PayloadResult::INVALID_FIELD;
    record = candidate;
    return PayloadResult::OK;
}

inline PayloadResult encodeDiagnosticPageRecord(
    const DiagnosticPageRecord& record, TypedValue& value
) {
    if (record.count > MAX_DIAGNOSTIC_PAGE_ENTRIES) {
        return PayloadResult::INVALID_FIELD;
    }
    TypedValue candidate = {};
    candidate.type = STRUCTURE_VALUE_TYPE;
    candidate.length = static_cast<uint8_t>(2 + 5 * record.count);
    candidate.bytes[0] = record.nextCursor;
    candidate.bytes[1] = record.count;
    for (size_t index = 0; index < record.count; ++index) {
        candidate.bytes[2 + 5 * index] = record.entries[index].metricId;
        writeUint32Le(candidate.bytes + 3 + 5 * index,
            record.entries[index].value);
    }
    value = candidate;
    return PayloadResult::OK;
}

inline PayloadResult decodeDiagnosticPageRecord(
    const TypedValue& value, DiagnosticPageRecord& record
) {
    if (value.type != STRUCTURE_VALUE_TYPE || value.length < 2 ||
        value.length > 17 || ((value.length - 2) % 5) != 0) {
        return PayloadResult::INVALID_VALUE;
    }
    DiagnosticPageRecord candidate = {};
    candidate.nextCursor = value.bytes[0];
    candidate.count = value.bytes[1];
    if (candidate.count > MAX_DIAGNOSTIC_PAGE_ENTRIES ||
        value.length != 2 + 5 * candidate.count) {
        return PayloadResult::INVALID_FIELD;
    }
    for (size_t index = 0; index < candidate.count; ++index) {
        candidate.entries[index].metricId = value.bytes[2 + 5 * index];
        candidate.entries[index].value = readUint32Le(
            value.bytes + 3 + 5 * index);
    }
    record = candidate;
    return PayloadResult::OK;
}

static_assert(MAX_REQUEST_VALUE_SIZE == 121,
    "Operation request value bound must be 121 bytes");
static_assert(MAX_RESPONSE_VALUE_SIZE == 119,
    "Operation response value bound must be 119 bytes");
static_assert(3 + 2 * MAX_CAPABILITY_PAGE_ENTRIES == 21,
    "Capability page must remain within the fixed record bound");
static_assert(2 + 5 * MAX_DIAGNOSTIC_PAGE_ENTRIES == 17,
    "Diagnostic page must remain within the fixed record bound");

}  // namespace HostProtocol
