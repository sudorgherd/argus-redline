#pragma once

#include <stddef.h>
#include <stdint.h>

namespace EventStorage {

enum class CopySlot : uint8_t { A, B };

enum class FixedReadStatus : uint8_t {
    OK,
    MISSING,
    UNAVAILABLE
};

enum class ReadBackResult : uint8_t {
    MATCH,
    MISMATCH,
    MISSING,
    UNAVAILABLE
};

enum class GenerationOrder : uint8_t {
    LEFT_NEWER,
    RIGHT_NEWER,
    EQUAL,
    AMBIGUOUS
};

enum class SelectionResult : uint8_t {
    NEWEST_A,
    NEWEST_B,
    ONLY_A_VALID,
    ONLY_B_VALID,
    BOTH_INVALID,
    EQUAL_IDENTICAL,
    EQUAL_DISAGREEMENT,
    GENERATION_AMBIGUOUS
};

template <size_t Size>
struct FixedCopy {
    FixedReadStatus status;
    uint8_t bytes[Size];
};

inline GenerationOrder compareGenerations(uint32_t left, uint32_t right) {
    if (left == right) {
        return GenerationOrder::EQUAL;
    }
    const uint32_t distance = left - right;
    if (distance == 0x80000000U) {
        return GenerationOrder::AMBIGUOUS;
    }
    return distance < 0x80000000U
        ? GenerationOrder::LEFT_NEWER
        : GenerationOrder::RIGHT_NEWER;
}

inline bool bytesEqual(
    const uint8_t* left,
    const uint8_t* right,
    size_t length
) {
    if (left == nullptr || right == nullptr) {
        return false;
    }
    for (size_t index = 0; index < length; ++index) {
        if (left[index] != right[index]) {
            return false;
        }
    }
    return true;
}

template <size_t Size>
inline ReadBackResult classifyReadBack(
    const uint8_t (&written)[Size],
    const FixedCopy<Size>& readBack
) {
    if (readBack.status == FixedReadStatus::MISSING) {
        return ReadBackResult::MISSING;
    }
    if (readBack.status != FixedReadStatus::OK) {
        return ReadBackResult::UNAVAILABLE;
    }
    return bytesEqual(written, readBack.bytes, Size)
        ? ReadBackResult::MATCH
        : ReadBackResult::MISMATCH;
}

template <size_t Size, typename Record, typename DecodeFunction>
inline SelectionResult selectCopies(
    const FixedCopy<Size>& copyA,
    const FixedCopy<Size>& copyB,
    DecodeFunction decode,
    Record& selected,
    CopySlot& selectedSlot
) {
    Record decodedA = {};
    Record decodedB = {};
    const bool validA = copyA.status == FixedReadStatus::OK &&
        decode(copyA.bytes, Size, decodedA);
    const bool validB = copyB.status == FixedReadStatus::OK &&
        decode(copyB.bytes, Size, decodedB);

    if (!validA && !validB) {
        return SelectionResult::BOTH_INVALID;
    }
    if (validA && !validB) {
        selected = decodedA;
        selectedSlot = CopySlot::A;
        return SelectionResult::ONLY_A_VALID;
    }
    if (!validA && validB) {
        selected = decodedB;
        selectedSlot = CopySlot::B;
        return SelectionResult::ONLY_B_VALID;
    }

    const GenerationOrder order = compareGenerations(
        decodedA.generation,
        decodedB.generation
    );
    if (order == GenerationOrder::EQUAL) {
        if (!bytesEqual(copyA.bytes, copyB.bytes, Size)) {
            return SelectionResult::EQUAL_DISAGREEMENT;
        }
        selected = decodedA;
        selectedSlot = CopySlot::A;
        return SelectionResult::EQUAL_IDENTICAL;
    }
    if (order == GenerationOrder::AMBIGUOUS) {
        return SelectionResult::GENERATION_AMBIGUOUS;
    }
    if (order == GenerationOrder::LEFT_NEWER) {
        selected = decodedA;
        selectedSlot = CopySlot::A;
        return SelectionResult::NEWEST_A;
    }
    selected = decodedB;
    selectedSlot = CopySlot::B;
    return SelectionResult::NEWEST_B;
}

}  // namespace EventStorage
