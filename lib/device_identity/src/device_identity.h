#pragma once

#include <stddef.h>
#include <stdint.h>

#include "event_records.h"
#include "event_store.h"

namespace DeviceIdentity {

constexpr size_t RECORD_SIZE = 64;
constexpr size_t LABEL_SIZE = 16;
constexpr uint16_t SCHEMA_VERSION = 1;

enum class DurableState : uint8_t {
    UNPROVISIONED = 0x01,
    PENDING = 0x02,
    PROVISIONED = 0x03
};

enum class TransactionKind : uint8_t {
    NONE = 0,
    APPLY = 1,
    FULL_RESET = 2
};

// Identity Schema 1 values are independent of RuntimeState::DeviceRole.
enum class Role : uint8_t {
    NONE = 0,
    HUB = 1,
    NODE = 2
};

enum class HardwareProfile : uint8_t { HELTEC_V4 = 1 };
enum class CapabilityProfile : uint8_t { HELTEC_V4_BASE = 1 };

enum class CodecResult : uint8_t {
    OK,
    NULL_POINTER,
    WRONG_LENGTH,
    BAD_MAGIC,
    BAD_SCHEMA,
    BAD_RECORD_LENGTH,
    BAD_CRC,
    INVALID_STATE,
    INVALID_TRANSACTION,
    INVALID_COMBINATION,
    INVALID_ROLE,
    INVALID_DEVICE_ID,
    DUPLICATE_DEVICE_ID,
    INVALID_HARDWARE_PROFILE,
    INVALID_CAPABILITY_PROFILE,
    INVALID_NETWORK_ID,
    INVALID_PROVISIONING_GENERATION,
    INVALID_LABEL_LENGTH,
    INVALID_LABEL_BYTE,
    NONZERO_LABEL_TAIL,
    NONZERO_RESERVED,
    NONCANONICAL_ZERO
};

struct Record {
    uint32_t recordGeneration;
    uint32_t provisioningGeneration;
    DurableState state;
    TransactionKind transaction;
    Role role;
    uint8_t localDeviceId;
    uint8_t peerDeviceId;
    HardwareProfile hardwareProfile;
    CapabilityProfile capabilityProfile;
    uint8_t labelLength;
    uint32_t networkId;
    uint8_t label[LABEL_SIZE];
};

inline bool validDeviceId(uint8_t id) {
    return id != 0 && id != 0xFF;
}

inline CodecResult validateRecord(const Record& value) {
    switch (value.state) {
        case DurableState::UNPROVISIONED:
        case DurableState::PENDING:
        case DurableState::PROVISIONED:
            break;
        default:
            return CodecResult::INVALID_STATE;
    }
    switch (value.transaction) {
        case TransactionKind::NONE:
        case TransactionKind::APPLY:
        case TransactionKind::FULL_RESET:
            break;
        default:
            return CodecResult::INVALID_TRANSACTION;
    }
    const bool zeroIdentity = value.state == DurableState::UNPROVISIONED ||
        value.transaction == TransactionKind::FULL_RESET;
    const bool validCombination =
        (value.state == DurableState::UNPROVISIONED &&
            value.transaction == TransactionKind::NONE) ||
        (value.state == DurableState::PROVISIONED &&
            value.transaction == TransactionKind::NONE) ||
        (value.state == DurableState::PENDING &&
            (value.transaction == TransactionKind::APPLY ||
                value.transaction == TransactionKind::FULL_RESET));
    if (!validCombination) return CodecResult::INVALID_COMBINATION;

    if (zeroIdentity) {
        if (value.provisioningGeneration != 0 ||
            value.role != Role::NONE || value.localDeviceId != 0 ||
            value.peerDeviceId != 0 ||
            static_cast<uint8_t>(value.hardwareProfile) != 0 ||
            static_cast<uint8_t>(value.capabilityProfile) != 0 ||
            value.labelLength != 0 || value.networkId != 0 ||
            !EventRecords::allZero(value.label, LABEL_SIZE)) {
            return CodecResult::NONCANONICAL_ZERO;
        }
        return CodecResult::OK;
    }

    if (value.role != Role::HUB && value.role != Role::NODE) {
        return CodecResult::INVALID_ROLE;
    }
    if (!validDeviceId(value.localDeviceId) ||
        !validDeviceId(value.peerDeviceId)) {
        return CodecResult::INVALID_DEVICE_ID;
    }
    if (value.localDeviceId == value.peerDeviceId) {
        return CodecResult::DUPLICATE_DEVICE_ID;
    }
    if (value.hardwareProfile != HardwareProfile::HELTEC_V4) {
        return CodecResult::INVALID_HARDWARE_PROFILE;
    }
    if (value.capabilityProfile != CapabilityProfile::HELTEC_V4_BASE) {
        return CodecResult::INVALID_CAPABILITY_PROFILE;
    }
    if (value.networkId == 0) return CodecResult::INVALID_NETWORK_ID;
    // The frozen Host contract reserves UINT32_MAX and caps successful
    // provisioning generations at UINT32_MAX - 1.
    if (value.provisioningGeneration == 0 ||
        value.provisioningGeneration == UINT32_MAX) {
        return CodecResult::INVALID_PROVISIONING_GENERATION;
    }
    if (value.labelLength > LABEL_SIZE) {
        return CodecResult::INVALID_LABEL_LENGTH;
    }
    for (size_t index = 0; index < value.labelLength; ++index) {
        if (value.label[index] < 0x20 || value.label[index] > 0x7E) {
            return CodecResult::INVALID_LABEL_BYTE;
        }
    }
    if (!EventRecords::allZero(value.label + value.labelLength,
                               LABEL_SIZE - value.labelLength)) {
        return CodecResult::NONZERO_LABEL_TAIL;
    }
    return CodecResult::OK;
}

inline CodecResult encodeRecord(
    const Record* value, uint8_t* output, size_t outputLength
) {
    if (value == nullptr || output == nullptr) return CodecResult::NULL_POINTER;
    if (outputLength != RECORD_SIZE) return CodecResult::WRONG_LENGTH;
    const CodecResult validation = validateRecord(*value);
    if (validation != CodecResult::OK) return validation;

    EventRecords::clearBytes(output, RECORD_SIZE);
    output[0] = 'R'; output[1] = 'L'; output[2] = 'I'; output[3] = '1';
    EventRecords::writeUint16Le(output + 4, SCHEMA_VERSION);
    EventRecords::writeUint16Le(output + 6, RECORD_SIZE);
    EventRecords::writeUint32Le(output + 8, value->recordGeneration);
    EventRecords::writeUint32Le(output + 12, value->provisioningGeneration);
    output[16] = static_cast<uint8_t>(value->state);
    output[17] = static_cast<uint8_t>(value->transaction);
    output[18] = static_cast<uint8_t>(value->role);
    output[19] = value->localDeviceId;
    output[20] = value->peerDeviceId;
    output[21] = static_cast<uint8_t>(value->hardwareProfile);
    output[22] = static_cast<uint8_t>(value->capabilityProfile);
    output[23] = value->labelLength;
    EventRecords::writeUint32Le(output + 24, value->networkId);
    for (size_t index = 0; index < value->labelLength; ++index) {
        output[28 + index] = value->label[index];
    }
    EventRecords::finishCrc(output, RECORD_SIZE);
    return CodecResult::OK;
}

inline CodecResult encodeRecord(
    const Record& value, uint8_t* output, size_t outputLength
) {
    return encodeRecord(&value, output, outputLength);
}

inline CodecResult decodeRecord(
    const uint8_t* input, size_t inputLength, Record* output
) {
    if (input == nullptr || output == nullptr) return CodecResult::NULL_POINTER;
    if (inputLength != RECORD_SIZE) return CodecResult::WRONG_LENGTH;
    if (!EventRecords::sameMagic(input, "RLI1")) return CodecResult::BAD_MAGIC;
    if (EventRecords::readUint16Le(input + 4) != SCHEMA_VERSION) {
        return CodecResult::BAD_SCHEMA;
    }
    if (EventRecords::readUint16Le(input + 6) != RECORD_SIZE) {
        return CodecResult::BAD_RECORD_LENGTH;
    }
    uint32_t crc = 0;
    EventRecords::crc32IsoHdlc(input, RECORD_SIZE - 4, crc);
    if (EventRecords::readUint32Le(input + 60) != crc) {
        return CodecResult::BAD_CRC;
    }
    if (!EventRecords::allZero(input + 44, 16)) {
        return CodecResult::NONZERO_RESERVED;
    }

    Record decoded = {};
    decoded.recordGeneration = EventRecords::readUint32Le(input + 8);
    decoded.provisioningGeneration = EventRecords::readUint32Le(input + 12);
    decoded.state = static_cast<DurableState>(input[16]);
    decoded.transaction = static_cast<TransactionKind>(input[17]);
    decoded.role = static_cast<Role>(input[18]);
    decoded.localDeviceId = input[19];
    decoded.peerDeviceId = input[20];
    decoded.hardwareProfile = static_cast<HardwareProfile>(input[21]);
    decoded.capabilityProfile = static_cast<CapabilityProfile>(input[22]);
    decoded.labelLength = input[23];
    decoded.networkId = EventRecords::readUint32Le(input + 24);
    for (size_t index = 0; index < LABEL_SIZE; ++index) {
        decoded.label[index] = input[28 + index];
    }
    const CodecResult validation = validateRecord(decoded);
    if (validation != CodecResult::OK) return validation;
    *output = decoded;
    return CodecResult::OK;
}

inline CodecResult decodeRecord(
    const uint8_t* input, size_t inputLength, Record& output
) {
    return decodeRecord(input, inputLength, &output);
}

using GenerationOrder = EventStorage::GenerationOrder;

inline GenerationOrder compareGenerations(uint32_t left, uint32_t right) {
    return EventStorage::compareGenerations(left, right);
}

}  // namespace DeviceIdentity
