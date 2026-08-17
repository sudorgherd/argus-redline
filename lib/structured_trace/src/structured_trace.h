#pragma once

#include <stddef.h>
#include <stdint.h>

namespace StructuredTrace {

constexpr size_t CAPACITY = 64;

enum class Event : uint8_t {
    HOST_BYTES_AVAILABLE = 1,
    HOST_FRAME_TERMINAL,
    HOST_FRAME_ACCEPTED,
    HOST_REMOTE_REQUEST,
    HOST_LIFECYCLE_ACCEPTED_REMOTE,
    LEGACY_OWNER_ACQUIRED,
    LEGACY_OWNER_RELEASED,
    STRUCTURED_OWNER_ACQUIRED,
    STRUCTURED_COMMAND_PREPARED,
    STRUCTURED_TX_START,
    STRUCTURED_TX_COMPLETE,
    STRUCTURED_RX_ARM_ACK,
    STRUCTURED_PACKET_RX,
    STRUCTURED_ACK_MATCHED,
    STRUCTURED_RX_ARM_RESPONSE,
    STRUCTURED_RESPONSE_MATCHED,
    STRUCTURED_TERMINAL,
    STRUCTURED_OWNER_RELEASED,
    HOST_TERMINAL_RETAINED,
    HOST_RESPONSE_HANDOFF,
    HOST_TX_SUBMITTED,
    HOST_TX_WRITE_RESULT,
    HOST_TX_FRAME_RETIRED,
    RADIO_START_TX_FAILURE,
    RADIO_START_RX_FAILURE,
    RADIO_READ_FAILURE,
    HOST_BYTES_WHILE_CONNECTION_FALSE,
};

struct Entry {
    uint32_t timestampMs;
    uint16_t requestId;
    uint8_t event;
    uint8_t sequence;
    uint8_t opcode;
    uint8_t phase;
    uint8_t detail;
    uint8_t owner;
};

static_assert(sizeof(Entry) == 12, "Structured trace entry geometry changed");

class Ring {
public:
    void reset() {
        head_ = 0;
        count_ = 0;
        for (size_t i = 0; i < CAPACITY; ++i) entries_[i] = {};
    }

    void append(const Entry& entry) {
        entries_[head_] = entry;
        head_ = static_cast<uint8_t>((head_ + 1U) % CAPACITY);
        if (count_ < CAPACITY) ++count_;
    }

    size_t size() const { return count_; }
    size_t head() const { return head_; }
    const Entry& chronological(size_t index) const {
        const size_t first = count_ == CAPACITY ? head_ : 0U;
        return entries_[(first + index) % CAPACITY];
    }
    const Entry* data() const { return entries_; }

private:
    Entry entries_[CAPACITY] = {};
    uint8_t head_ = 0;
    uint8_t count_ = 0;
};

#if defined(ARGUS_STRUCTURED_TRACE)
extern "C" {
extern volatile Entry g_structuredTrace[CAPACITY];
extern volatile uint8_t g_structuredTraceHead;
extern volatile uint8_t g_structuredTraceCount;
}

void record(uint32_t timestampMs, Event event, uint16_t requestId = 0,
    uint8_t sequence = 0, uint8_t opcode = 0, uint8_t phase = 0,
    uint8_t detail = 0, uint8_t owner = 0);
#endif

}  // namespace StructuredTrace
