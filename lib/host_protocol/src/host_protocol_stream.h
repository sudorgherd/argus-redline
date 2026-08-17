#pragma once

#include <stddef.h>
#include <stdint.h>

#include <host_protocol.h>

namespace HostProtocol {

constexpr size_t STREAM_CANDIDATE_STORAGE_SIZE = MAX_COBS_CANDIDATE_SIZE;

enum class StreamEvent : uint8_t {
    NONE = 0x00,
    FRAME_READY = 0x01,
    FRAME_REJECTED = 0x02,
    OVERSIZED_CANDIDATE = 0x03,
    INVALID_ARGUMENT = 0x04
};

struct StreamResult {
    size_t consumed;
    StreamEvent event;
    DecodeResult decodeResult;
    Frame frame;
};

class StreamParser {
public:
    StreamParser() : candidateLength_(0), discardingOversize_(false) {}

    void reset() {
        candidateLength_ = 0;
        discardingOversize_ = false;
    }

    StreamResult consume(const uint8_t* input, size_t inputLength) {
        StreamResult result = {};
        result.event = StreamEvent::NONE;
        result.decodeResult = DecodeResult::OK;
        if (input == nullptr && inputLength != 0) {
            result.event = StreamEvent::INVALID_ARGUMENT;
            result.decodeResult = DecodeResult::NULL_ARGUMENT;
            return result;
        }

        while (result.consumed < inputLength) {
            const uint8_t value = input[result.consumed++];
            if (discardingOversize_) {
                if (value == FRAME_DELIMITER) {
                    reset();
                    result.event = StreamEvent::OVERSIZED_CANDIDATE;
                    result.decodeResult = DecodeResult::ENCODED_FRAME_TOO_LARGE;
                    return result;
                }
                continue;
            }

            if (value != FRAME_DELIMITER) {
                if (candidateLength_ < MAX_COBS_CANDIDATE_SIZE) {
                    candidate_[candidateLength_++] = value;
                } else {
                    candidateLength_ = 0;
                    discardingOversize_ = true;
                }
                continue;
            }

            if (candidateLength_ == 0) {
                continue;
            }

            uint8_t completeFrame[MAX_ENCODED_FRAME_SIZE] = {};
            for (size_t index = 0; index < candidateLength_; ++index) {
                completeFrame[index] = candidate_[index];
            }
            completeFrame[candidateLength_] = FRAME_DELIMITER;
            const size_t completeLength = candidateLength_ + 1;
            reset();
            result.decodeResult = decodeFrame(
                completeFrame, completeLength, result.frame);
            result.event = result.decodeResult == DecodeResult::OK
                ? StreamEvent::FRAME_READY
                : StreamEvent::FRAME_REJECTED;
            return result;
        }
        return result;
    }

    size_t candidateLength() const {
        return candidateLength_;
    }

    bool isDiscardingOversize() const {
        return discardingOversize_;
    }

private:
    uint8_t candidate_[STREAM_CANDIDATE_STORAGE_SIZE];
    size_t candidateLength_;
    bool discardingOversize_;
};

static_assert(STREAM_CANDIDATE_STORAGE_SIZE == 139,
    "stream parser stores only the maximum non-delimiter candidate");

}  // namespace HostProtocol
