#pragma once

#include <stddef.h>
#include <stdint.h>

#include "device_identity_store.h"

namespace ProvisioningService {

enum class Presence : uint8_t { EMPTY, PRESENT, UNAVAILABLE };

// Implementations must cover every ARGUS Node and Hub Event key, including
// custody, allocator, admission, and ordinal metadata. Stage 3 supplies only
// the policy boundary; a later integration stage supplies the concrete adapter.
class EventDomain {
public:
    virtual ~EventDomain() = default;
    virtual Presence inspect() = 0;
    virtual bool clearAll() = 0;
    virtual bool verifyEmpty() = 0;
};

// This adapter invokes the existing recoverable Configuration Schema 1 reset.
class SettingsReset {
public:
    virtual ~SettingsReset() = default;
    virtual bool resetToDefaults() = 0;
    virtual bool verifyDefaults() = 0;
};

struct Proposal {
    DeviceIdentity::Role role;
    uint8_t localDeviceId;
    uint8_t peerDeviceId;
    DeviceIdentity::HardwareProfile hardwareProfile;
    DeviceIdentity::CapabilityProfile capabilityProfile;
    uint32_t networkId;
    uint8_t labelLength;
    uint8_t label[DeviceIdentity::LABEL_SIZE];
};

enum class Phase : uint8_t {
    IDLE,
    READY_APPLY,
    READY_FULL_RESET,
    PENDING_APPLY,
    PENDING_FULL_RESET,
    REBOOT_REQUIRED
};

enum class ResultCode : uint8_t {
    ACCEPTED,
    APPLIED_REBOOT_REQUIRED,
    RESET_REBOOT_REQUIRED,
    UNCHANGED,
    ALREADY_UNPROVISIONED,
    INVALID_REQUEST,
    ACK_EVENT_ERASURE_REQUIRED,
    PROVISIONING_GENERATION_EXHAUSTED,
    RECORD_GENERATION_EXHAUSTED,
    IDENTITY_STORE_FAILURE,
    SETTINGS_RESET_FAILURE,
    SETTINGS_VERIFY_FAILURE,
    EVENT_INSPECTION_FAILURE,
    EVENT_CLEAR_FAILURE,
    EVENT_VERIFY_FAILURE,
    RECOVERY_REQUIRED,
    CANCELLED,
    CANCEL_REJECTED,
    INVALID_PROVISIONING,
    STORAGE_UNAVAILABLE,
    NO_RECOVERY_NEEDED,
    INVALID_LIFECYCLE
};

struct Result {
    ResultCode code;
    DeviceIdentityStore::WriteResult storeResult;
};

class Controller {
public:
    Controller(DeviceIdentityStore::Store& identity,
               DeviceIdentityStore::Storage& storage,
               EventDomain& events, SettingsReset& settings) :
        identity_(identity), storage_(storage), events_(events),
        settings_(settings), phase_(Phase::IDLE), readyTarget_{},
        acknowledgedErasure_(false), attemptedPendingWrite_(false) {}

    Phase phase() const { return phase_; }

    Result prepareApply(const Proposal& proposal, bool acknowledgeEventErasure) {
        if (phase_ != Phase::IDLE) return phaseConflict();
        DeviceIdentity::Record target = fromProposal(proposal);
        target.provisioningGeneration = 1;
        if (DeviceIdentity::validateRecord(target) !=
            DeviceIdentity::CodecResult::OK) {
            return result(ResultCode::INVALID_REQUEST);
        }

        const DeviceIdentityStore::Snapshot& current = identity_.recover(storage_);
        if (current.outcome == DeviceIdentityStore::Outcome::PENDING) {
            return result(ResultCode::RECOVERY_REQUIRED);
        }
        if (current.outcome == DeviceIdentityStore::Outcome::STORAGE_UNAVAILABLE) {
            return result(ResultCode::STORAGE_UNAVAILABLE);
        }
        if (current.outcome == DeviceIdentityStore::Outcome::INVALID_PROVISIONING) {
            return result(ResultCode::INVALID_PROVISIONING);
        }
        if (current.outcome == DeviceIdentityStore::Outcome::PROVISIONED &&
            sameIdentity(current.record, target)) {
            return result(ResultCode::UNCHANGED);
        }

        if (current.outcome == DeviceIdentityStore::Outcome::PROVISIONED) {
            if (current.record.provisioningGeneration >= UINT32_MAX - 1U) {
                return result(ResultCode::PROVISIONING_GENERATION_EXHAUSTED);
            }
            target.provisioningGeneration =
                current.record.provisioningGeneration + 1U;
        }
        if (!identity_.canAdvanceRecordGeneration(2)) {
            return result(ResultCode::RECORD_GENERATION_EXHAUSTED,
                DeviceIdentityStore::WriteResult::GENERATION_EXHAUSTED);
        }
        const Result preflight = checkErasureAcknowledgment(acknowledgeEventErasure);
        if (preflight.code != ResultCode::ACCEPTED) return preflight;
        readyTarget_ = target;
        acknowledgedErasure_ = acknowledgeEventErasure;
        attemptedPendingWrite_ = false;
        phase_ = Phase::READY_APPLY;
        return result(ResultCode::ACCEPTED);
    }

    Result prepareFullReset(bool acknowledgeEventErasure) {
        if (phase_ != Phase::IDLE) return phaseConflict();
        const DeviceIdentityStore::Snapshot& current = identity_.recover(storage_);
        if (current.outcome == DeviceIdentityStore::Outcome::PENDING) {
            return result(ResultCode::RECOVERY_REQUIRED);
        }
        if (current.outcome == DeviceIdentityStore::Outcome::STORAGE_UNAVAILABLE) {
            return result(ResultCode::STORAGE_UNAVAILABLE);
        }
        const Result preflight = checkErasureAcknowledgment(acknowledgeEventErasure);
        if (preflight.code != ResultCode::ACCEPTED) return preflight;
        if ((current.outcome == DeviceIdentityStore::Outcome::UNPROVISIONED ||
             current.outcome == DeviceIdentityStore::Outcome::VIRGIN_UNPROVISIONED) &&
            events_.inspect() == Presence::EMPTY && settings_.verifyDefaults()) {
            return result(ResultCode::ALREADY_UNPROVISIONED);
        }
        acknowledgedErasure_ = acknowledgeEventErasure;
        attemptedPendingWrite_ = false;
        phase_ = Phase::READY_FULL_RESET;
        return result(ResultCode::ACCEPTED);
    }

    // Execute only at a foreground safe point held through the transaction.
    // The safe point is supplied by a later integration stage.
    Result execute() {
        if (phase_ != Phase::READY_APPLY && phase_ != Phase::READY_FULL_RESET) {
            return phaseConflict();
        }
        if (attemptedPendingWrite_) return result(ResultCode::RECOVERY_REQUIRED);
        const Result preflight = checkErasureAcknowledgment(acknowledgedErasure_);
        if (preflight.code != ResultCode::ACCEPTED) return preflight;

        const bool applying = phase_ == Phase::READY_APPLY;
        DeviceIdentity::Record pending = {};
        if (applying) pending = readyTarget_;
        pending.state = DeviceIdentity::DurableState::PENDING;
        pending.transaction = applying
            ? DeviceIdentity::TransactionKind::APPLY
            : DeviceIdentity::TransactionKind::FULL_RESET;

        DeviceIdentityStore::WriteResult write;
        attemptedPendingWrite_ = true;
        const DeviceIdentityStore::Snapshot& current = identity_.snapshot();
        if (!applying &&
            (current.outcome == DeviceIdentityStore::Outcome::INVALID_PROVISIONING ||
             !identity_.canAdvanceRecordGeneration(2))) {
            write = identity_.writeResetPendingFromInvalidOrExhausted(storage_);
        } else {
            write = identity_.writeNext(storage_, pending);
        }
        if (write != DeviceIdentityStore::WriteResult::OK) {
            return storeFailure(write);
        }
        phase_ = applying ? Phase::PENDING_APPLY : Phase::PENDING_FULL_RESET;
        return finishPending();
    }

    // Called once on boot or explicit service after a failed transaction.
    // It re-reads both identity copies and never trusts volatile prior steps.
    Result recoverPending() {
        const DeviceIdentityStore::Snapshot& current = identity_.recover(storage_);
        if (current.outcome == DeviceIdentityStore::Outcome::STORAGE_UNAVAILABLE) {
            return result(ResultCode::STORAGE_UNAVAILABLE);
        }
        if (current.outcome == DeviceIdentityStore::Outcome::INVALID_PROVISIONING) {
            return result(ResultCode::INVALID_PROVISIONING);
        }
        if (current.outcome != DeviceIdentityStore::Outcome::PENDING) {
            if (phase_ == Phase::PENDING_APPLY ||
                phase_ == Phase::PENDING_FULL_RESET) {
                phase_ = Phase::REBOOT_REQUIRED;
            } else if (attemptedPendingWrite_) {
                phase_ = Phase::IDLE;
                attemptedPendingWrite_ = false;
            }
            return result(ResultCode::NO_RECOVERY_NEEDED);
        }
        phase_ = current.record.transaction == DeviceIdentity::TransactionKind::APPLY
            ? Phase::PENDING_APPLY : Phase::PENDING_FULL_RESET;
        return finishPending();
    }

    Result cancel() {
        if (phase_ == Phase::READY_APPLY || phase_ == Phase::READY_FULL_RESET) {
            if (attemptedPendingWrite_) {
                const DeviceIdentityStore::Snapshot& current =
                    identity_.recover(storage_);
                if (current.outcome == DeviceIdentityStore::Outcome::PENDING) {
                    phase_ = current.record.transaction ==
                        DeviceIdentity::TransactionKind::APPLY
                        ? Phase::PENDING_APPLY : Phase::PENDING_FULL_RESET;
                    return result(ResultCode::CANCEL_REJECTED);
                }
                if (current.outcome ==
                        DeviceIdentityStore::Outcome::INVALID_PROVISIONING ||
                    current.outcome ==
                        DeviceIdentityStore::Outcome::STORAGE_UNAVAILABLE) {
                    return result(ResultCode::CANCEL_REJECTED);
                }
            }
            phase_ = Phase::IDLE;
            readyTarget_ = {};
            acknowledgedErasure_ = false;
            attemptedPendingWrite_ = false;
            return result(ResultCode::CANCELLED);
        }
        return result(ResultCode::CANCEL_REJECTED);
    }

private:
    static Result result(ResultCode code,
                         DeviceIdentityStore::WriteResult write =
                             DeviceIdentityStore::WriteResult::OK) {
        return {code, write};
    }

    Result phaseConflict() const {
        return result(phase_ == Phase::PENDING_APPLY ||
                      phase_ == Phase::PENDING_FULL_RESET
            ? ResultCode::RECOVERY_REQUIRED : ResultCode::INVALID_LIFECYCLE);
    }

    static DeviceIdentity::Record fromProposal(const Proposal& proposal) {
        DeviceIdentity::Record record = {};
        record.state = DeviceIdentity::DurableState::PROVISIONED;
        record.transaction = DeviceIdentity::TransactionKind::NONE;
        record.role = proposal.role;
        record.localDeviceId = proposal.localDeviceId;
        record.peerDeviceId = proposal.peerDeviceId;
        record.hardwareProfile = proposal.hardwareProfile;
        record.capabilityProfile = proposal.capabilityProfile;
        record.networkId = proposal.networkId;
        record.labelLength = proposal.labelLength;
        for (size_t i = 0; i < DeviceIdentity::LABEL_SIZE; ++i) {
            record.label[i] = proposal.label[i];
        }
        return record;
    }

    static bool sameIdentity(const DeviceIdentity::Record& a,
                             const DeviceIdentity::Record& b) {
        if (a.role != b.role || a.localDeviceId != b.localDeviceId ||
            a.peerDeviceId != b.peerDeviceId ||
            a.hardwareProfile != b.hardwareProfile ||
            a.capabilityProfile != b.capabilityProfile ||
            a.networkId != b.networkId || a.labelLength != b.labelLength) {
            return false;
        }
        return EventStorage::bytesEqual(
            a.label, b.label, DeviceIdentity::LABEL_SIZE);
    }

    Result checkErasureAcknowledgment(bool acknowledged) {
        const Presence presence = events_.inspect();
        if (presence == Presence::UNAVAILABLE) {
            return result(ResultCode::EVENT_INSPECTION_FAILURE);
        }
        if (presence == Presence::PRESENT && !acknowledged) {
            return result(ResultCode::ACK_EVENT_ERASURE_REQUIRED);
        }
        return result(ResultCode::ACCEPTED);
    }

    static Result storeFailure(DeviceIdentityStore::WriteResult write) {
        return result(write == DeviceIdentityStore::WriteResult::GENERATION_EXHAUSTED
            ? ResultCode::RECORD_GENERATION_EXHAUSTED
            : ResultCode::IDENTITY_STORE_FAILURE, write);
    }

    Result finishPending() {
        const DeviceIdentityStore::Snapshot& snapshot = identity_.snapshot();
        if (snapshot.outcome != DeviceIdentityStore::Outcome::PENDING) {
            return result(ResultCode::RECOVERY_REQUIRED);
        }
        if (!identity_.canAdvanceRecordGeneration(1)) {
            return result(ResultCode::RECORD_GENERATION_EXHAUSTED,
                DeviceIdentityStore::WriteResult::GENERATION_EXHAUSTED);
        }
        const bool applying = snapshot.record.transaction ==
            DeviceIdentity::TransactionKind::APPLY;
        if (!applying) {
            if (!settings_.resetToDefaults()) {
                return result(ResultCode::SETTINGS_RESET_FAILURE);
            }
            if (!settings_.verifyDefaults()) {
                return result(ResultCode::SETTINGS_VERIFY_FAILURE);
            }
        }

        const Presence presence = events_.inspect();
        if (presence == Presence::UNAVAILABLE) {
            return result(ResultCode::EVENT_INSPECTION_FAILURE);
        }
        if (presence == Presence::PRESENT && !events_.clearAll()) {
            return result(ResultCode::EVENT_CLEAR_FAILURE);
        }
        if (!events_.verifyEmpty()) {
            return result(ResultCode::EVENT_VERIFY_FAILURE);
        }

        DeviceIdentity::Record final = snapshot.record;
        if (applying) {
            final.state = DeviceIdentity::DurableState::PROVISIONED;
            final.transaction = DeviceIdentity::TransactionKind::NONE;
        } else {
            final = {};
            final.state = DeviceIdentity::DurableState::UNPROVISIONED;
            final.transaction = DeviceIdentity::TransactionKind::NONE;
        }
        const DeviceIdentityStore::WriteResult write =
            identity_.writeNext(storage_, final);
        if (write != DeviceIdentityStore::WriteResult::OK) {
            return storeFailure(write);
        }
        phase_ = Phase::REBOOT_REQUIRED;
        return result(applying ? ResultCode::APPLIED_REBOOT_REQUIRED
                               : ResultCode::RESET_REBOOT_REQUIRED);
    }

    DeviceIdentityStore::Store& identity_;
    DeviceIdentityStore::Storage& storage_;
    EventDomain& events_;
    SettingsReset& settings_;
    Phase phase_;
    DeviceIdentity::Record readyTarget_;
    bool acknowledgedErasure_;
    bool attemptedPendingWrite_;
};

}  // namespace ProvisioningService
