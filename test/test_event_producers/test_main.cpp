#include <unity.h>

#include "event_producers.h"
#include "simulated_capabilities.h"

using namespace EventProducers;

namespace {

class RecordingSink final : public CreationSink {
public:
    CreationResult next = CreationResult::ENQUEUED;
    unsigned calls = 0;
    NodeEventStore::EventInput last = {};

    CreationResult create(const NodeEventStore::EventInput& input) override {
        ++calls;
        last = input;
        return next;
    }
};

DeviceInput::ButtonEvents events(DeviceInput::ButtonEvent first,
                                 DeviceInput::ButtonEvent second = DeviceInput::ButtonEvent::NONE) {
    DeviceInput::ButtonEvents value;
    value.first = first;
    value.second = second;
    return value;
}

ButtonContext context(bool awake = true, bool editor = false, bool home = true) {
    return {awake, editor, home};
}

ThresholdPolicy normalizedAbove(uint16_t id = SimulatedCapabilities::ANALOG_INPUT_0_ID) {
    return {id, EventProtocol::SensorValueType::NORMALIZED_U16, 0xC000U, 0x1000U,
            EventProtocol::ThresholdRelation::CROSSED_ABOVE};
}

void assertPolicy(const NodeEventStore::EventInput& input) {
    TEST_ASSERT_EQUAL_UINT8(PRODUCER_FLAGS, input.flags);
    TEST_ASSERT_EQUAL_UINT32(PRODUCER_LIFETIME_SECONDS, input.lifetimeBudgetSeconds);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(EventProtocol::MIN_LIFETIME_SECONDS,
                                        input.lifetimeBudgetSeconds);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(EventProtocol::MAX_LIFETIME_SECONDS,
                                     input.lifetimeBudgetSeconds);
}

void test_creation_status_boundary_is_closed() {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CreationResult::ENQUEUED),
        static_cast<uint8_t>(normalize(NodeEventStore::EnqueueStatus::ENQUEUED)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CreationResult::QUEUE_FULL),
        static_cast<uint8_t>(normalize(NodeEventStore::EnqueueStatus::QUEUE_FULL)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CreationResult::INVALID_EVENT),
        static_cast<uint8_t>(normalize(NodeEventStore::EnqueueStatus::INVALID_EVENT)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CreationResult::STORAGE_FAILURE),
        static_cast<uint8_t>(normalize(NodeEventStore::EnqueueStatus::STORAGE_FAILURE)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CreationResult::STORAGE_FAILURE),
        static_cast<uint8_t>(normalize(NodeEventStore::EnqueueStatus::IDENTITY_FAILURE)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CreationResult::STORAGE_FAILURE),
        static_cast<uint8_t>(normalize(NodeEventStore::EnqueueStatus::DEGRADED)));
}

void test_button_schema_and_numeric_mapping_are_exact() {
    RecordingSink sink;
    const DeviceInput::ButtonEvent values[] = {
        DeviceInput::ButtonEvent::PRESS, DeviceInput::ButtonEvent::RELEASE,
        DeviceInput::ButtonEvent::SHORT_PRESS, DeviceInput::ButtonEvent::LONG_PRESS,
        DeviceInput::ButtonEvent::VERY_LONG_PRESS
    };
    for (uint8_t index = 0; index < 5; ++index) {
        const Emission emitted = emitButton(sink, values[index]);
        TEST_ASSERT_TRUE(emitted.attempted);
        TEST_ASSERT_EQUAL_UINT8(0x40, sink.last.family);
        TEST_ASSERT_EQUAL_UINT8(1, sink.last.bodyLength);
        TEST_ASSERT_EQUAL_UINT8(index + 1U, sink.last.body[0]);
        assertPolicy(sink.last);
    }
    TEST_ASSERT_EQUAL_UINT(5, sink.calls);
}

void test_button_none_never_reaches_sink() {
    RecordingSink sink;
    TEST_ASSERT_FALSE(emitButton(sink, DeviceInput::ButtonEvent::NONE).attempted);
    TEST_ASSERT_EQUAL_UINT(0, sink.calls);
}

void test_short_press_production_emits_once_despite_release_pair() {
    RecordingSink sink;
    ButtonProducer producer;
    producer.observe(events(DeviceInput::ButtonEvent::PRESS), context(), sink);
    const ButtonProductionResult result = producer.observe(
        events(DeviceInput::ButtonEvent::RELEASE, DeviceInput::ButtonEvent::SHORT_PRESS),
        context(), sink);
    TEST_ASSERT_TRUE(result.button.attempted);
    TEST_ASSERT_FALSE(result.manualCheckIn.attempted);
    TEST_ASSERT_EQUAL_UINT(1, sink.calls);
    TEST_ASSERT_EQUAL_UINT8(0x03, sink.last.body[0]);
}

void test_editor_wake_and_nonselected_gestures_do_not_emit_button() {
    RecordingSink sink;
    ButtonProducer producer;
    producer.observe(events(DeviceInput::ButtonEvent::PRESS), context(true, true), sink);
    producer.observe(events(DeviceInput::ButtonEvent::RELEASE,
                            DeviceInput::ButtonEvent::SHORT_PRESS), context(true, true), sink);
    producer.observe(events(DeviceInput::ButtonEvent::PRESS), context(false, false), sink);
    producer.observe(events(DeviceInput::ButtonEvent::RELEASE,
                            DeviceInput::ButtonEvent::SHORT_PRESS), context(), sink);
    producer.observe(events(DeviceInput::ButtonEvent::PRESS), context(), sink);
    producer.observe(events(DeviceInput::ButtonEvent::LONG_PRESS), context(), sink);
    TEST_ASSERT_EQUAL_UINT(0, sink.calls);
}

void test_manual_check_in_is_one_home_long_release_only() {
    RecordingSink sink;
    ButtonProducer producer;
    producer.observe(events(DeviceInput::ButtonEvent::PRESS), context(), sink);
    producer.observe(events(DeviceInput::ButtonEvent::LONG_PRESS), context(), sink);
    TEST_ASSERT_EQUAL_UINT(0, sink.calls);
    const auto result = producer.observe(events(DeviceInput::ButtonEvent::RELEASE), context(), sink);
    TEST_ASSERT_TRUE(result.manualCheckIn.attempted);
    TEST_ASSERT_EQUAL_UINT(1, sink.calls);
    TEST_ASSERT_EQUAL_UINT8(0x44, sink.last.family);
    TEST_ASSERT_EQUAL_UINT8(1, sink.last.bodyLength);
    TEST_ASSERT_EQUAL_UINT8(0x01, sink.last.body[0]);
    assertPolicy(sink.last);
}

void test_manual_check_in_excludes_navigation_editor_and_wake_gestures() {
    RecordingSink sink;
    ButtonProducer producer;
    producer.observe(events(DeviceInput::ButtonEvent::PRESS), context(true, false, false), sink);
    producer.observe(events(DeviceInput::ButtonEvent::LONG_PRESS), context(true, false, false), sink);
    producer.observe(events(DeviceInput::ButtonEvent::RELEASE), context(), sink);
    producer.observe(events(DeviceInput::ButtonEvent::PRESS), context(), sink);
    producer.observe(events(DeviceInput::ButtonEvent::LONG_PRESS), context(), sink);
    producer.observe(events(DeviceInput::ButtonEvent::VERY_LONG_PRESS), context(), sink);
    producer.observe(events(DeviceInput::ButtonEvent::RELEASE), context(true, true), sink);
    producer.observe(events(DeviceInput::ButtonEvent::PRESS), context(false), sink);
    producer.observe(events(DeviceInput::ButtonEvent::LONG_PRESS), context(), sink);
    producer.observe(events(DeviceInput::ButtonEvent::RELEASE), context(), sink);
    TEST_ASSERT_EQUAL_UINT(0, sink.calls);
}

void test_button_and_manual_propagate_every_creation_result() {
    const CreationResult results[] = {CreationResult::ENQUEUED, CreationResult::QUEUE_FULL,
        CreationResult::INVALID_EVENT, CreationResult::STORAGE_FAILURE};
    for (CreationResult expected : results) {
        RecordingSink sink;
        sink.next = expected;
        ButtonProducer producer;
        producer.observe(events(DeviceInput::ButtonEvent::PRESS), context(), sink);
        const auto button = producer.observe(events(DeviceInput::ButtonEvent::RELEASE,
            DeviceInput::ButtonEvent::SHORT_PRESS), context(), sink);
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected),
                               static_cast<uint8_t>(button.button.result));
        ButtonProducer manual;
        manual.observe(events(DeviceInput::ButtonEvent::PRESS), context(), sink);
        manual.observe(events(DeviceInput::ButtonEvent::LONG_PRESS), context(), sink);
        const auto checkIn = manual.observe(events(DeviceInput::ButtonEvent::RELEASE), context(), sink);
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected),
                               static_cast<uint8_t>(checkIn.manualCheckIn.result));
    }
}

void test_sensor_schema_is_exact_little_endian_logical_id() {
    RecordingSink sink;
    SensorThresholdProducer producer(normalizedAbove());
    producer.observe(true, 0xB000U, sink);
    const Emission emitted = producer.observe(true, 0xC123U, sink);
    TEST_ASSERT_TRUE(emitted.attempted);
    TEST_ASSERT_EQUAL_UINT8(0x41, sink.last.family);
    TEST_ASSERT_EQUAL_UINT8(8, sink.last.bodyLength);
    const uint8_t expected[] = {0x01, 0x03, 0x04, 0x23, 0xC1, 0x00, 0x00, 0x02};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, sink.last.body, sizeof(expected));
    TEST_ASSERT_TRUE(EventProtocol::isValidFamilyBody(sink.last.family,
        sink.last.body, sink.last.bodyLength));
    assertPolicy(sink.last);
}

void test_sensor_crossing_is_edge_triggered_and_hysteresis_rearms() {
    RecordingSink sink;
    SensorThresholdProducer producer(normalizedAbove());
    TEST_ASSERT_FALSE(producer.observe(true, 0xB000U, sink).attempted);
    TEST_ASSERT_TRUE(producer.observe(true, 0xC001U, sink).attempted);
    TEST_ASSERT_FALSE(producer.observe(true, 0xD000U, sink).attempted);
    TEST_ASSERT_FALSE(producer.observe(true, 0xB800U, sink).attempted);
    TEST_ASSERT_FALSE(producer.observe(true, 0xB000U, sink).attempted);
    TEST_ASSERT_TRUE(producer.observe(true, 0xC100U, sink).attempted);
    TEST_ASSERT_EQUAL_UINT(2, sink.calls);
}

void test_sensor_initial_active_sample_does_not_fabricate_crossing() {
    RecordingSink sink;
    SensorThresholdProducer producer(normalizedAbove());
    TEST_ASSERT_FALSE(producer.observe(true, 0xD000U, sink).attempted);
    TEST_ASSERT_FALSE(producer.observe(true, 0xD100U, sink).attempted);
    TEST_ASSERT_EQUAL_UINT(0, sink.calls);
}

void test_sensor_accepts_all_permitted_types_and_relations() {
    const EventProtocol::SensorValueType types[] = {
        EventProtocol::SensorValueType::UNSIGNED_32,
        EventProtocol::SensorValueType::SIGNED_32,
        EventProtocol::SensorValueType::NORMALIZED_U16,
        EventProtocol::SensorValueType::FIXED_Q16_16,
        EventProtocol::SensorValueType::ENUM_U16
    };
    for (auto type : types) {
        RecordingSink sink;
        ThresholdPolicy policy = {0x0301, type, 10U, 1U,
            EventProtocol::ThresholdRelation::CROSSED_BELOW};
        SensorThresholdProducer producer(policy);
        producer.observe(true, 11U, sink);
        TEST_ASSERT_TRUE(producer.observe(true, 9U, sink).attempted);
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(type), sink.last.body[2]);
        TEST_ASSERT_EQUAL_UINT8(0x01, sink.last.body[7]);
    }
}

void test_sensor_signed_types_compare_preserved_bit_patterns_as_signed() {
    const EventProtocol::SensorValueType types[] = {
        EventProtocol::SensorValueType::SIGNED_32,
        EventProtocol::SensorValueType::FIXED_Q16_16
    };
    for (auto type : types) {
        RecordingSink sink;
        ThresholdPolicy policy = {0x0301, type, 0U, 1U,
            EventProtocol::ThresholdRelation::CROSSED_BELOW};
        SensorThresholdProducer producer(policy);
        producer.observe(true, 1U, sink);
        TEST_ASSERT_TRUE(producer.observe(true, 0xFFFFFFFFU, sink).attempted);
        TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFU,
            EventProtocol::readUint32Le(sink.last.body + 3));
    }
}

void test_sensor_invalid_or_unavailable_input_never_reaches_sink() {
    RecordingSink sink;
    SensorThresholdProducer zeroId(normalizedAbove(0));
    zeroId.observe(true, 0xB000U, sink);
    zeroId.observe(true, 0xD000U, sink);
    SensorThresholdProducer normalized(normalizedAbove());
    normalized.observe(false, 0xB000U, sink);
    normalized.observe(true, 0x10000U, sink);
    ThresholdPolicy invalidType = normalizedAbove();
    invalidType.valueType = static_cast<EventProtocol::SensorValueType>(0x01);
    SensorThresholdProducer badType(invalidType);
    badType.observe(true, 1U, sink);
    TEST_ASSERT_EQUAL_UINT(0, sink.calls);
}

void test_sensor_propagates_every_creation_result() {
    const CreationResult results[] = {CreationResult::ENQUEUED, CreationResult::QUEUE_FULL,
        CreationResult::INVALID_EVENT, CreationResult::STORAGE_FAILURE};
    for (CreationResult expected : results) {
        RecordingSink sink;
        sink.next = expected;
        SensorThresholdProducer producer(normalizedAbove());
        producer.observe(true, 0xB000U, sink);
        const Emission emitted = producer.observe(true, 0xC001U, sink);
        TEST_ASSERT_TRUE(emitted.attempted);
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected),
                               static_cast<uint8_t>(emitted.result));
    }
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_creation_status_boundary_is_closed);
    RUN_TEST(test_button_schema_and_numeric_mapping_are_exact);
    RUN_TEST(test_button_none_never_reaches_sink);
    RUN_TEST(test_short_press_production_emits_once_despite_release_pair);
    RUN_TEST(test_editor_wake_and_nonselected_gestures_do_not_emit_button);
    RUN_TEST(test_manual_check_in_is_one_home_long_release_only);
    RUN_TEST(test_manual_check_in_excludes_navigation_editor_and_wake_gestures);
    RUN_TEST(test_button_and_manual_propagate_every_creation_result);
    RUN_TEST(test_sensor_schema_is_exact_little_endian_logical_id);
    RUN_TEST(test_sensor_crossing_is_edge_triggered_and_hysteresis_rearms);
    RUN_TEST(test_sensor_initial_active_sample_does_not_fabricate_crossing);
    RUN_TEST(test_sensor_accepts_all_permitted_types_and_relations);
    RUN_TEST(test_sensor_signed_types_compare_preserved_bit_patterns_as_signed);
    RUN_TEST(test_sensor_invalid_or_unavailable_input_never_reaches_sink);
    RUN_TEST(test_sensor_propagates_every_creation_result);
    return UNITY_END();
}
