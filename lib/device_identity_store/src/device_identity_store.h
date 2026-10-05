#pragma once

#include <stddef.h>
#include <stdint.h>

#include "device_identity.h"

namespace DeviceIdentityStore {

using CopySlot = EventStorage::CopySlot;

enum class StorageResult : uint8_t {
    OK,
    MISSING,
    MALFORMED,
    UNAVAILABLE,
    ERROR
};

class Storage {
public:
    virtual ~Storage() = default;
    virtual StorageResult read(
        CopySlot slot, uint8_t* output, size_t capacity, size_t& length
    ) = 0;
    virtual StorageResult write(
        CopySlot slot, const uint8_t* input, size_t length
    ) = 0;
    virtual StorageResult commit() = 0;
};

enum class CopyCondition : uint8_t {
    MISSING,
    VALID,
    MALFORMED,
    CORRUPT,
    UNSUPPORTED_SCHEMA,
    UNAVAILABLE
};

enum class Selection : uint8_t {
    VIRGIN,
    ONLY_A_VALID,
    ONLY_B_VALID,
    EQUAL_IDENTICAL,
    NEWEST_A,
    NEWEST_B,
    EQUAL_DISAGREEMENT,
    GENERATION_AMBIGUOUS,
    UNSUPPORTED_SCHEMA,
    STORAGE_UNAVAILABLE,
    NEITHER_VALID
};

enum class Outcome : uint8_t {
    VIRGIN_UNPROVISIONED,
    UNPROVISIONED,
    PROVISIONED,
    PENDING,
    INVALID_PROVISIONING,
    STORAGE_UNAVAILABLE
};

struct CopyInspection {
    CopyCondition condition;
    DeviceIdentity::CodecResult codecResult;
};

struct Snapshot {
    Outcome outcome;
    Selection selection;
    CopyInspection copyA;
    CopyInspection copyB;
    bool hasAuthority;
    CopySlot authoritativeSlot;
    DeviceIdentity::Record record;
    uint8_t bytes[DeviceIdentity::RECORD_SIZE];
};

enum class WriteResult : uint8_t {
    OK,
    NO_AUTHORITY,
    RECOVERY_REQUIRED,
    GENERATION_EXHAUSTED,
    INVALID_RECORD,
    WRITE_FAILED,
    COMMIT_FAILED,
    READBACK_MISSING,
    READBACK_UNAVAILABLE,
    READBACK_MALFORMED,
    READBACK_INVALID,
    READBACK_MISMATCH
};

enum class RepairResult : uint8_t {
    OK,
    NOT_NEEDED,
    ALREADY_ATTEMPTED,
    RECOVERY_REQUIRED,
    FAILED
};

inline CopySlot otherSlot(CopySlot slot) {
    return slot == CopySlot::A ? CopySlot::B : CopySlot::A;
}

class Store {
public:
    Store() : current_{}, recovered_(false), writeBlocked_(false),
              repairAttempted_(false) {
        current_.outcome = Outcome::STORAGE_UNAVAILABLE;
        current_.selection = Selection::STORAGE_UNAVAILABLE;
        current_.copyA.condition = CopyCondition::UNAVAILABLE;
        current_.copyB.condition = CopyCondition::UNAVAILABLE;
    }

    const Snapshot& snapshot() const { return current_; }

    // The controller can reserve enough successors for PENDING and its final
    // record without copying generation arithmetic into another layer.
    bool canAdvanceRecordGeneration(uint32_t writes) const {
        if (!recovered_ || !writableOutcome() || writes == 0) return false;
        return !current_.hasAuthority ||
            current_.record.recordGeneration <= UINT32_MAX - writes;
    }

    // Recovery is read-only. An unknown schema in either copy prevents an
    // older Schema 1 copy from downgrading it or repairing over it.
    const Snapshot& recover(Storage& storage) {
        const Observation a = inspect(storage, CopySlot::A);
        const Observation b = inspect(storage, CopySlot::B);

        Snapshot next = {};
        next.copyA = {a.condition, a.codecResult};
        next.copyB = {b.condition, b.codecResult};
        next.outcome = Outcome::INVALID_PROVISIONING;
        next.selection = Selection::NEITHER_VALID;

        if (a.condition == CopyCondition::UNAVAILABLE ||
            b.condition == CopyCondition::UNAVAILABLE) {
            next.outcome = Outcome::STORAGE_UNAVAILABLE;
            next.selection = Selection::STORAGE_UNAVAILABLE;
        } else if (a.condition == CopyCondition::UNSUPPORTED_SCHEMA ||
                   b.condition == CopyCondition::UNSUPPORTED_SCHEMA) {
            next.selection = Selection::UNSUPPORTED_SCHEMA;
        } else if (a.condition == CopyCondition::MISSING &&
                   b.condition == CopyCondition::MISSING) {
            next.outcome = Outcome::VIRGIN_UNPROVISIONED;
            next.selection = Selection::VIRGIN;
        } else if (a.condition == CopyCondition::VALID &&
                   b.condition != CopyCondition::VALID) {
            select(next, a, CopySlot::A, Selection::ONLY_A_VALID);
        } else if (b.condition == CopyCondition::VALID &&
                   a.condition != CopyCondition::VALID) {
            select(next, b, CopySlot::B, Selection::ONLY_B_VALID);
        } else if (a.condition == CopyCondition::VALID &&
                   b.condition == CopyCondition::VALID) {
            const DeviceIdentity::GenerationOrder order =
                DeviceIdentity::compareGenerations(
                    a.record.recordGeneration, b.record.recordGeneration);
            if (order == DeviceIdentity::GenerationOrder::EQUAL) {
                if (EventStorage::bytesEqual(
                        a.bytes, b.bytes, DeviceIdentity::RECORD_SIZE)) {
                    select(next, a, CopySlot::A, Selection::EQUAL_IDENTICAL);
                } else {
                    next.selection = Selection::EQUAL_DISAGREEMENT;
                }
            } else if (order == DeviceIdentity::GenerationOrder::AMBIGUOUS) {
                next.selection = Selection::GENERATION_AMBIGUOUS;
            } else if (order == DeviceIdentity::GenerationOrder::LEFT_NEWER) {
                select(next, a, CopySlot::A, Selection::NEWEST_A);
            } else {
                select(next, b, CopySlot::B, Selection::NEWEST_B);
            }
        }

        const bool sameRepairEvent = repairAttempted_ &&
            next.hasAuthority && current_.hasAuthority &&
            next.authoritativeSlot == current_.authoritativeSlot &&
            next.copyA.condition == current_.copyA.condition &&
            next.copyB.condition == current_.copyB.condition &&
            EventStorage::bytesEqual(
                next.bytes, current_.bytes, DeviceIdentity::RECORD_SIZE);
        repairAttempted_ = sameRepairEvent;
        current_ = next;
        recovered_ = true;
        writeBlocked_ = false;
        return current_;
    }

    // A higher-level provisioning controller supplies the intended durable
    // form. This primitive owns only the record-generation successor and the
    // verified copy write; cross-domain transaction ordering belongs to Stage 3.
    WriteResult writeNext(Storage& storage, const DeviceIdentity::Record& proposal) {
        if (!recovered_ || !writableOutcome()) return WriteResult::NO_AUTHORITY;
        if (writeBlocked_) return WriteResult::RECOVERY_REQUIRED;
        if (!canAdvanceRecordGeneration(1)) {
            return WriteResult::GENERATION_EXHAUSTED;
        }
        DeviceIdentity::Record next = proposal;
        next.recordGeneration = current_.hasAuthority
            ? current_.record.recordGeneration + 1U : 1U;
        uint8_t bytes[DeviceIdentity::RECORD_SIZE] = {};
        if (DeviceIdentity::encodeRecord(next, bytes, sizeof(bytes)) !=
            DeviceIdentity::CodecResult::OK) {
            return WriteResult::INVALID_RECORD;
        }

        const CopySlot destination = current_.hasAuthority
            ? otherSlot(current_.authoritativeSlot) : CopySlot::A;
        const WriteResult written = writeAndVerify(storage, destination, bytes);
        if (written != WriteResult::OK) {
            // The physical write may have committed despite a failed read-back.
            // Require a fresh two-copy recovery before another mutation.
            writeBlocked_ = true;
            return written;
        }
        Snapshot published = current_;
        published.hasAuthority = true;
        published.authoritativeSlot = destination;
        published.record = next;
        published.outcome = outcomeFor(next);
        const CopyCondition otherCondition = destination == CopySlot::A
            ? published.copyB.condition : published.copyA.condition;
        published.selection = otherCondition == CopyCondition::VALID
            ? (destination == CopySlot::A
                ? Selection::NEWEST_A : Selection::NEWEST_B)
            : (destination == CopySlot::A
                ? Selection::ONLY_A_VALID : Selection::ONLY_B_VALID);
        for (size_t index = 0; index < DeviceIdentity::RECORD_SIZE; ++index) {
            published.bytes[index] = bytes[index];
        }
        CopyInspection& target = destination == CopySlot::A
            ? published.copyA : published.copyB;
        target = {CopyCondition::VALID, DeviceIdentity::CodecResult::OK};
        current_ = published;
        repairAttempted_ = false;
        return WriteResult::OK;
    }

    // Deliberate full-reset entry when there is no safe record-generation
    // successor (or no unambiguous authority). Both copies must be verified
    // as PENDING/FULL_RESET before any cross-domain reset may begin. This is
    // the explicit reset/rebase path; ordinary recovery never overwrites an
    // unknown schema. A torn first/second write leaves no destructive work
    // started and must be recovered or deliberately retried.
    WriteResult writeResetPendingFromInvalidOrExhausted(Storage& storage) {
        if (!recovered_ || writeBlocked_) return WriteResult::RECOVERY_REQUIRED;
        if (current_.outcome == Outcome::STORAGE_UNAVAILABLE) {
            return WriteResult::NO_AUTHORITY;
        }
        if (current_.outcome != Outcome::INVALID_PROVISIONING &&
            (!current_.hasAuthority || current_.outcome == Outcome::PENDING ||
             canAdvanceRecordGeneration(2))) {
            return WriteResult::NO_AUTHORITY;
        }

        const CopySlot first = current_.hasAuthority
            ? otherSlot(current_.authoritativeSlot) : CopySlot::A;
        const CopySlot second = otherSlot(first);
        DeviceIdentity::Record pending = {};
        pending.state = DeviceIdentity::DurableState::PENDING;
        pending.transaction = DeviceIdentity::TransactionKind::FULL_RESET;
        uint8_t firstBytes[DeviceIdentity::RECORD_SIZE] = {};
        uint8_t secondBytes[DeviceIdentity::RECORD_SIZE] = {};
        pending.recordGeneration = 1;
        if (DeviceIdentity::encodeRecord(pending, firstBytes,
                sizeof(firstBytes)) != DeviceIdentity::CodecResult::OK) {
            return WriteResult::INVALID_RECORD;
        }
        pending.recordGeneration = 2;
        if (DeviceIdentity::encodeRecord(pending, secondBytes,
                sizeof(secondBytes)) != DeviceIdentity::CodecResult::OK) {
            return WriteResult::INVALID_RECORD;
        }
        WriteResult result = writeAndVerify(storage, first, firstBytes);
        if (result == WriteResult::OK) {
            result = writeAndVerify(storage, second, secondBytes);
        }
        if (result != WriteResult::OK) {
            writeBlocked_ = true;
            return result;
        }
        const Snapshot& selected = recover(storage);
        if (selected.outcome != Outcome::PENDING ||
            selected.authoritativeSlot != second ||
            !EventStorage::bytesEqual(
                selected.bytes, secondBytes, DeviceIdentity::RECORD_SIZE)) {
            writeBlocked_ = true;
            return WriteResult::READBACK_INVALID;
        }
        return WriteResult::OK;
    }

    // Explicit one-shot repair of a single missing or corrupt redundant copy.
    // Malformed-size and unknown-schema copies are preserved because their
    // bytes may belong to a format that this firmware cannot interpret.
    RepairResult repairRedundancy(Storage& storage) {
        if (!recovered_ || writeBlocked_) return RepairResult::RECOVERY_REQUIRED;
        if (!current_.hasAuthority) return RepairResult::NOT_NEEDED;
        const CopySlot destination = otherSlot(current_.authoritativeSlot);
        const CopyCondition condition = destination == CopySlot::A
            ? current_.copyA.condition : current_.copyB.condition;
        if (condition != CopyCondition::MISSING &&
            condition != CopyCondition::CORRUPT) {
            return RepairResult::NOT_NEEDED;
        }
        if (repairAttempted_) return RepairResult::ALREADY_ATTEMPTED;
        repairAttempted_ = true;
        if (writeAndVerify(storage, destination, current_.bytes) != WriteResult::OK) {
            writeBlocked_ = true;
            return RepairResult::FAILED;
        }
        CopyInspection& target = destination == CopySlot::A
            ? current_.copyA : current_.copyB;
        target = {CopyCondition::VALID, DeviceIdentity::CodecResult::OK};
        current_.selection = Selection::EQUAL_IDENTICAL;
        return RepairResult::OK;
    }

private:
    struct Observation {
        CopyCondition condition;
        DeviceIdentity::CodecResult codecResult;
        DeviceIdentity::Record record;
        uint8_t bytes[DeviceIdentity::RECORD_SIZE];
    };

    static Observation inspect(Storage& storage, CopySlot slot) {
        Observation observation = {};
        size_t length = 0;
        const StorageResult read = storage.read(
            slot, observation.bytes, sizeof(observation.bytes), length);
        if (read == StorageResult::MISSING) {
            observation.condition = CopyCondition::MISSING;
        } else if (read == StorageResult::MALFORMED ||
                   (read == StorageResult::OK &&
                    length != DeviceIdentity::RECORD_SIZE)) {
            observation.condition = CopyCondition::MALFORMED;
        } else if (read != StorageResult::OK) {
            observation.condition = CopyCondition::UNAVAILABLE;
        } else {
            observation.codecResult = DeviceIdentity::decodeRecord(
                observation.bytes, length, observation.record);
            observation.condition = observation.codecResult ==
                DeviceIdentity::CodecResult::OK ? CopyCondition::VALID :
                observation.codecResult == DeviceIdentity::CodecResult::BAD_SCHEMA
                    ? CopyCondition::UNSUPPORTED_SCHEMA
                    : CopyCondition::CORRUPT;
        }
        return observation;
    }

    static Outcome outcomeFor(const DeviceIdentity::Record& record) {
        if (record.state == DeviceIdentity::DurableState::UNPROVISIONED) {
            return Outcome::UNPROVISIONED;
        }
        if (record.state == DeviceIdentity::DurableState::PENDING) {
            return Outcome::PENDING;
        }
        return Outcome::PROVISIONED;
    }

    static void select(
        Snapshot& target, const Observation& source,
        CopySlot slot, Selection selection
    ) {
        target.selection = selection;
        target.outcome = outcomeFor(source.record);
        target.hasAuthority = true;
        target.authoritativeSlot = slot;
        target.record = source.record;
        for (size_t index = 0; index < DeviceIdentity::RECORD_SIZE; ++index) {
            target.bytes[index] = source.bytes[index];
        }
    }

    bool writableOutcome() const {
        return current_.outcome == Outcome::VIRGIN_UNPROVISIONED ||
            current_.outcome == Outcome::UNPROVISIONED ||
            current_.outcome == Outcome::PROVISIONED ||
            current_.outcome == Outcome::PENDING;
    }

    static WriteResult writeAndVerify(
        Storage& storage, CopySlot destination,
        const uint8_t (&expected)[DeviceIdentity::RECORD_SIZE]
    ) {
        if (storage.write(destination, expected, DeviceIdentity::RECORD_SIZE) !=
            StorageResult::OK) return WriteResult::WRITE_FAILED;
        if (storage.commit() != StorageResult::OK) {
            return WriteResult::COMMIT_FAILED;
        }
        uint8_t readBack[DeviceIdentity::RECORD_SIZE] = {};
        size_t length = 0;
        const StorageResult read = storage.read(
            destination, readBack, sizeof(readBack), length);
        if (read == StorageResult::MISSING) return WriteResult::READBACK_MISSING;
        if (read == StorageResult::MALFORMED ||
            (read == StorageResult::OK && length != DeviceIdentity::RECORD_SIZE)) {
            return WriteResult::READBACK_MALFORMED;
        }
        if (read != StorageResult::OK) return WriteResult::READBACK_UNAVAILABLE;
        DeviceIdentity::Record decoded = {};
        if (DeviceIdentity::decodeRecord(readBack, length, decoded) !=
            DeviceIdentity::CodecResult::OK) return WriteResult::READBACK_INVALID;
        if (!EventStorage::bytesEqual(
                expected, readBack, DeviceIdentity::RECORD_SIZE)) {
            return WriteResult::READBACK_MISMATCH;
        }
        return WriteResult::OK;
    }

    Snapshot current_;
    bool recovered_;
    bool writeBlocked_;
    bool repairAttempted_;
};

}  // namespace DeviceIdentityStore
