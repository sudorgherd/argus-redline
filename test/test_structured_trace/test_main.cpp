#include <unity.h>

#include "structured_trace.h"

void setUp() {}
void tearDown() {}

static StructuredTrace::Entry makeEntry(uint32_t ordinal) {
    return {ordinal, static_cast<uint16_t>(0x1200U + ordinal),
        static_cast<uint8_t>(StructuredTrace::Event::STRUCTURED_PACKET_RX),
        static_cast<uint8_t>(ordinal), static_cast<uint8_t>(0x20U + ordinal),
        static_cast<uint8_t>(ordinal + 1U), static_cast<uint8_t>(ordinal + 2U),
        static_cast<uint8_t>(ordinal + 3U)};
}

void test_initial_state_and_fixed_geometry() {
    StructuredTrace::Ring ring;
    TEST_ASSERT_EQUAL_UINT32(12, sizeof(StructuredTrace::Entry));
    TEST_ASSERT_EQUAL_UINT32(64, StructuredTrace::CAPACITY);
    TEST_ASSERT_EQUAL_UINT32(0, ring.size());
    TEST_ASSERT_EQUAL_UINT32(0, ring.head());
}

void test_append_order_and_exact_fields() {
    StructuredTrace::Ring ring;
    const StructuredTrace::Entry expected = makeEntry(7);
    ring.append(expected);
    const StructuredTrace::Entry& actual = ring.chronological(0);
    TEST_ASSERT_EQUAL_UINT32(expected.timestampMs, actual.timestampMs);
    TEST_ASSERT_EQUAL_UINT16(expected.requestId, actual.requestId);
    TEST_ASSERT_EQUAL_UINT8(expected.event, actual.event);
    TEST_ASSERT_EQUAL_UINT8(expected.sequence, actual.sequence);
    TEST_ASSERT_EQUAL_UINT8(expected.opcode, actual.opcode);
    TEST_ASSERT_EQUAL_UINT8(expected.phase, actual.phase);
    TEST_ASSERT_EQUAL_UINT8(expected.detail, actual.detail);
    TEST_ASSERT_EQUAL_UINT8(expected.owner, actual.owner);
}

void test_wrap_preserves_chronological_order() {
    StructuredTrace::Ring ring;
    for (uint32_t i = 0; i < 70; ++i) ring.append(makeEntry(i));
    TEST_ASSERT_EQUAL_UINT32(64, ring.size());
    TEST_ASSERT_EQUAL_UINT32(6, ring.head());
    for (uint32_t i = 0; i < 64; ++i)
        TEST_ASSERT_EQUAL_UINT32(i + 6U, ring.chronological(i).timestampMs);
}

void test_reset_clears_metadata_and_storage() {
    StructuredTrace::Ring ring;
    ring.append(makeEntry(4));
    ring.reset();
    TEST_ASSERT_EQUAL_UINT32(0, ring.size());
    TEST_ASSERT_EQUAL_UINT32(0, ring.head());
    TEST_ASSERT_EQUAL_UINT32(0, ring.data()[0].timestampMs);
    TEST_ASSERT_EQUAL_UINT16(0, ring.data()[0].requestId);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_initial_state_and_fixed_geometry);
    RUN_TEST(test_append_order_and_exact_fields);
    RUN_TEST(test_wrap_preserves_chronological_order);
    RUN_TEST(test_reset_clears_metadata_and_storage);
    return UNITY_END();
}
