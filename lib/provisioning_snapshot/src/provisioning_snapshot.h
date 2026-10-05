#pragma once

#include <optional>
#include <stdint.h>

#include "device_identity_store.h"

namespace ProvisioningRuntime {

enum class ResolutionOutcome : uint8_t {
    PROVISIONED_HUB,
    PROVISIONED_NODE,
    VIRGIN_UNPROVISIONED,
    UNPROVISIONED,
    PENDING,
    INVALID_PROVISIONING,
    STORAGE_UNAVAILABLE,
    UNSUPPORTED_HARDWARE_PROFILE,
    UNSUPPORTED_CAPABILITY_PROFILE
};

// Created only by resolving a validated, recovered PROVISIONED record. No
// persistent Record or mutable identity field escapes into runtime code.
class ProvisioningSnapshot {
public:
    ProvisioningSnapshot(const ProvisioningSnapshot&) = default;
    ProvisioningSnapshot& operator=(const ProvisioningSnapshot&) = delete;

    DeviceIdentity::Role role() const { return role_; }
    uint8_t localDeviceId() const { return localDeviceId_; }
    uint8_t peerDeviceId() const { return peerDeviceId_; }
    uint32_t networkId() const { return networkId_; }
    uint32_t provisioningGeneration() const { return provisioningGeneration_; }
    DeviceIdentity::HardwareProfile hardwareProfile() const { return hardwareProfile_; }
    DeviceIdentity::CapabilityProfile capabilityProfile() const { return capabilityProfile_; }
    uint8_t labelLength() const { return labelLength_; }
    uint8_t labelByte(size_t index) const {
        return index < labelLength_ ? label_[index] : 0;
    }
    bool hostRadioBridgeEnabled() const { return role_ == DeviceIdentity::Role::HUB; }
    bool hubEventServiceEnabled() const { return role_ == DeviceIdentity::Role::HUB; }

private:
    explicit ProvisioningSnapshot(const DeviceIdentity::Record& record) :
        role_(record.role), localDeviceId_(record.localDeviceId),
        peerDeviceId_(record.peerDeviceId), networkId_(record.networkId),
        provisioningGeneration_(record.provisioningGeneration),
        hardwareProfile_(record.hardwareProfile),
        capabilityProfile_(record.capabilityProfile),
        labelLength_(record.labelLength) {
        for (size_t i = 0; i < DeviceIdentity::LABEL_SIZE; ++i) {
            label_[i] = record.label[i];
        }
    }

    const DeviceIdentity::Role role_;
    const uint8_t localDeviceId_;
    const uint8_t peerDeviceId_;
    const uint32_t networkId_;
    const uint32_t provisioningGeneration_;
    const DeviceIdentity::HardwareProfile hardwareProfile_;
    const DeviceIdentity::CapabilityProfile capabilityProfile_;
    const uint8_t labelLength_;
    uint8_t label_[DeviceIdentity::LABEL_SIZE];

    friend class Resolution;
};

class Resolution {
public:
    ResolutionOutcome outcome() const { return outcome_; }
    bool radioEligible() const { return snapshot_.has_value(); }
    const ProvisioningSnapshot* snapshot() const {
        return snapshot_ ? &*snapshot_ : nullptr;
    }

    static Resolution fromRecovery(const DeviceIdentityStore::Snapshot& recovered) {
        using DeviceIdentityStore::Outcome;
        switch (recovered.outcome) {
            case Outcome::VIRGIN_UNPROVISIONED:
                return Resolution(ResolutionOutcome::VIRGIN_UNPROVISIONED);
            case Outcome::UNPROVISIONED:
                return Resolution(ResolutionOutcome::UNPROVISIONED);
            case Outcome::PENDING:
                return Resolution(ResolutionOutcome::PENDING);
            case Outcome::STORAGE_UNAVAILABLE:
                return Resolution(ResolutionOutcome::STORAGE_UNAVAILABLE);
            case Outcome::INVALID_PROVISIONING:
                if (recovered.selection ==
                        DeviceIdentityStore::Selection::UNSUPPORTED_SCHEMA) {
                    return Resolution(ResolutionOutcome::INVALID_PROVISIONING);
                }
                if (recovered.copyA.codecResult ==
                        DeviceIdentity::CodecResult::INVALID_HARDWARE_PROFILE ||
                    recovered.copyB.codecResult ==
                        DeviceIdentity::CodecResult::INVALID_HARDWARE_PROFILE) {
                    return Resolution(ResolutionOutcome::UNSUPPORTED_HARDWARE_PROFILE);
                }
                if (recovered.copyA.codecResult ==
                        DeviceIdentity::CodecResult::INVALID_CAPABILITY_PROFILE ||
                    recovered.copyB.codecResult ==
                        DeviceIdentity::CodecResult::INVALID_CAPABILITY_PROFILE) {
                    return Resolution(ResolutionOutcome::UNSUPPORTED_CAPABILITY_PROFILE);
                }
                return Resolution(ResolutionOutcome::INVALID_PROVISIONING);
            case Outcome::PROVISIONED:
                break;
            default:
                return Resolution(ResolutionOutcome::INVALID_PROVISIONING);
        }
        if (!recovered.hasAuthority) {
            return Resolution(ResolutionOutcome::INVALID_PROVISIONING);
        }
        DeviceIdentity::Record record = {};
        const DeviceIdentity::CodecResult decoded = DeviceIdentity::decodeRecord(
            recovered.bytes, sizeof(recovered.bytes), record);
        if (decoded == DeviceIdentity::CodecResult::INVALID_HARDWARE_PROFILE) {
            return Resolution(ResolutionOutcome::UNSUPPORTED_HARDWARE_PROFILE);
        }
        if (decoded == DeviceIdentity::CodecResult::INVALID_CAPABILITY_PROFILE) {
            return Resolution(ResolutionOutcome::UNSUPPORTED_CAPABILITY_PROFILE);
        }
        if (decoded != DeviceIdentity::CodecResult::OK ||
            record.state != DeviceIdentity::DurableState::PROVISIONED ||
            record.transaction != DeviceIdentity::TransactionKind::NONE) {
            return Resolution(ResolutionOutcome::INVALID_PROVISIONING);
        }
        // The store's decoded record and selected bytes must describe the
        // same authority; never resolve a caller-modified record view.
        uint8_t canonical[DeviceIdentity::RECORD_SIZE] = {};
        if (DeviceIdentity::encodeRecord(recovered.record, canonical,
                sizeof(canonical)) != DeviceIdentity::CodecResult::OK ||
            !EventStorage::bytesEqual(canonical, recovered.bytes,
                sizeof(canonical))) {
            return Resolution(ResolutionOutcome::INVALID_PROVISIONING);
        }
        return Resolution(record.role == DeviceIdentity::Role::HUB
            ? ResolutionOutcome::PROVISIONED_HUB
            : ResolutionOutcome::PROVISIONED_NODE, record);
    }

private:
    explicit Resolution(ResolutionOutcome outcome) : outcome_(outcome) {}
    Resolution(ResolutionOutcome outcome, const DeviceIdentity::Record& record) :
        outcome_(outcome), snapshot_(ProvisioningSnapshot(record)) {}

    ResolutionOutcome outcome_;
    std::optional<ProvisioningSnapshot> snapshot_;
};

}  // namespace ProvisioningRuntime
