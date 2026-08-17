#include "structured_trace.h"

#if defined(ARGUS_STRUCTURED_TRACE)
namespace StructuredTrace {

extern "C" {
volatile Entry g_structuredTrace[CAPACITY] __attribute__((used)) = {};
volatile uint8_t g_structuredTraceHead __attribute__((used)) = 0;
volatile uint8_t g_structuredTraceCount __attribute__((used)) = 0;
}

void record(uint32_t timestampMs, Event event, uint16_t requestId,
    uint8_t sequence, uint8_t opcode, uint8_t phase, uint8_t detail,
    uint8_t owner) {
    const uint8_t index = g_structuredTraceHead;
    volatile Entry& entry = g_structuredTrace[index];
    entry.timestampMs = timestampMs;
    entry.requestId = requestId;
    entry.event = static_cast<uint8_t>(event);
    entry.sequence = sequence;
    entry.opcode = opcode;
    entry.phase = phase;
    entry.detail = detail;
    entry.owner = owner;
    g_structuredTraceHead = static_cast<uint8_t>((index + 1U) % CAPACITY);
    if (g_structuredTraceCount < CAPACITY) ++g_structuredTraceCount;
}

}  // namespace StructuredTrace
#endif
