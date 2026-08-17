#pragma once

#include <stddef.h>
#include <stdint.h>

#include <host_protocol.h>
#include <host_protocol_stream.h>

namespace HostTransport {

enum class RxStatus : uint8_t {
    NO_ACTIVITY = 0x00,
    PROGRESS = 0x01,
    PARSER_EVENT = 0x02,
    INPUT_ERROR = 0x03
};

enum class SubmitStatus : uint8_t {
    OK = 0x00,
    BUSY = 0x01,
    INVALID_ARGUMENT = 0x02,
    INVALID_FRAME = 0x03
};

enum class TxStatus : uint8_t {
    IDLE = 0x00,
    BLOCKED = 0x01,
    PROGRESS = 0x02,
    COMPLETE = 0x03,
    OUTPUT_ERROR = 0x04
};

struct RxResult {
    RxStatus status;
    size_t bytesRead;
    HostProtocol::StreamResult parserResult;
};

struct TxResult {
    TxStatus status;
    size_t bytesWritten;
    size_t remaining;
#if defined(ARGUS_STRUCTURED_TRACE)
    size_t availableForWrite;
    size_t bytesRequested;
    bool writeAttempted;
#endif
};

template <typename ByteStream>
class Adapter {
public:
    explicit Adapter(ByteStream& stream)
        : stream_(stream), pendingLength_(0), pendingOffset_(0) {}

    RxResult serviceRx(size_t budget) {
        RxResult result = {};
        result.status = RxStatus::NO_ACTIVITY;
        result.parserResult.event = HostProtocol::StreamEvent::NONE;
        result.parserResult.decodeResult = HostProtocol::DecodeResult::OK;
        while (result.bytesRead < budget) {
            if (stream_.available() <= 0) {
                break;
            }
            const int value = stream_.read();
            if (value < 0) {
                result.status = RxStatus::INPUT_ERROR;
                return result;
            }
            const uint8_t byte = static_cast<uint8_t>(value);
            result.parserResult = parser_.consume(&byte, 1);
            ++result.bytesRead;
            if (result.parserResult.event != HostProtocol::StreamEvent::NONE) {
                result.status = RxStatus::PARSER_EVENT;
                return result;
            }
        }
        if (result.bytesRead != 0) {
            result.status = RxStatus::PROGRESS;
        }
        return result;
    }

    SubmitStatus submit(const uint8_t* frame, size_t frameLength) {
        if (pendingLength_ != 0) {
            return SubmitStatus::BUSY;
        }
        if (frame == nullptr || frameLength == 0 ||
            frameLength > HostProtocol::MAX_ENCODED_FRAME_SIZE) {
            return SubmitStatus::INVALID_ARGUMENT;
        }
        HostProtocol::Frame decoded = {};
        if (HostProtocol::decodeFrame(frame, frameLength, decoded) !=
            HostProtocol::DecodeResult::OK) {
            return SubmitStatus::INVALID_FRAME;
        }
        for (size_t index = 0; index < frameLength; ++index) {
            pending_[index] = frame[index];
        }
        pendingLength_ = frameLength;
        pendingOffset_ = 0;
        return SubmitStatus::OK;
    }

    TxResult serviceTx(size_t budget) {
        TxResult result = {};
        result.remaining = remainingTx();
        if (pendingLength_ == 0) {
            result.status = TxStatus::IDLE;
            return result;
        }
        if (budget == 0) {
            result.status = TxStatus::BLOCKED;
            return result;
        }
        const int writable = stream_.availableForWrite();
#if defined(ARGUS_STRUCTURED_TRACE)
        result.availableForWrite = writable > 0 ? static_cast<size_t>(writable) : 0;
#endif
        if (writable <= 0) {
            result.status = TxStatus::BLOCKED;
            return result;
        }
        size_t attempt = static_cast<size_t>(writable);
        if (attempt > budget) attempt = budget;
        if (attempt > result.remaining) attempt = result.remaining;
#if defined(ARGUS_STRUCTURED_TRACE)
        result.bytesRequested = attempt;
        result.writeAttempted = true;
#endif
        const size_t written = stream_.write(pending_ + pendingOffset_, attempt);
        if (written > attempt) {
            result.status = TxStatus::OUTPUT_ERROR;
            return result;
        }
        if (written == 0) {
            result.status = TxStatus::BLOCKED;
            return result;
        }
        pendingOffset_ += written;
        result.bytesWritten = written;
        result.remaining = remainingTx();
        if (pendingOffset_ == pendingLength_) {
            pendingLength_ = 0;
            pendingOffset_ = 0;
            result.status = TxStatus::COMPLETE;
        } else {
            result.status = TxStatus::PROGRESS;
        }
        return result;
    }

    void reset() {
        parser_.reset();
        pendingLength_ = 0;
        pendingOffset_ = 0;
    }

    bool hasPendingTx() const {
        return pendingLength_ != 0;
    }

    size_t remainingTx() const {
        return pendingLength_ == 0 ? 0 : pendingLength_ - pendingOffset_;
    }

private:
    ByteStream& stream_;
    HostProtocol::StreamParser parser_;
    uint8_t pending_[HostProtocol::MAX_ENCODED_FRAME_SIZE];
    size_t pendingLength_;
    size_t pendingOffset_;
};

static_assert(HostProtocol::MAX_ENCODED_FRAME_SIZE == 140,
    "transport pending-frame storage must match Host Protocol bounds");

}  // namespace HostTransport
