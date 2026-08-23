#include <unity.h>

#include "device_capabilities.h"
#include "device_input.h"
#include "event_protocol.h"

namespace {

using namespace EventProtocol;

constexpr uint8_t HUB = 0x01;
constexpr uint8_t NODE = 0x10;
constexpr uint8_t SEQUENCE = 0x2A;
constexpr uint32_t EPOCH = 0x11223344UL;
constexpr uint32_t EVENT_ID = 0x01020304UL;

Event buttonEvent(uint8_t value = 0x02) {
    Event event = {};
    event.source = NODE;
    event.destination = HUB;
    event.sequence = SEQUENCE;
    event.family = static_cast<uint8_t>(Family::BUTTON);
    event.epoch = EPOCH;
    event.id = EVENT_ID;
    event.flags = IMPORTANT_FLAG;
    event.lifetimeBudgetSeconds = 3600;
    event.bodyLength = 1;
    event.body[0] = value;
    return event;
}

Event sensorEvent(
    uint8_t valueType = static_cast<uint8_t>(SensorValueType::UNSIGNED_32),
    uint8_t relation = static_cast<uint8_t>(ThresholdRelation::CROSSED_ABOVE)
) {
    Event event = buttonEvent();
    event.family = static_cast<uint8_t>(Family::SENSOR_THRESHOLD);
    event.bodyLength = 8;
    event.body[0] = 0x34;
    event.body[1] = 0x12;
    event.body[2] = valueType;
    event.body[3] = 0x78;
    event.body[4] = 0x56;
    event.body[5] = 0x34;
    event.body[6] = 0x12;
    event.body[7] = relation;
    return event;
}

Event manualEvent(uint8_t reason = 0x01) {
    Event event = buttonEvent();
    event.family = static_cast<uint8_t>(Family::MANUAL_CHECK_IN);
    event.body[0] = reason;
    return event;
}

void assertEventEqual(const Event& expected, const Event& actual) {
    TEST_ASSERT_EQUAL_UINT8(expected.source, actual.source);
    TEST_ASSERT_EQUAL_UINT8(expected.destination, actual.destination);
    TEST_ASSERT_EQUAL_UINT8(expected.sequence, actual.sequence);
    TEST_ASSERT_EQUAL_UINT8(expected.family, actual.family);
    TEST_ASSERT_EQUAL_HEX32(expected.epoch, actual.epoch);
    TEST_ASSERT_EQUAL_HEX32(expected.id, actual.id);
    TEST_ASSERT_EQUAL_UINT8(expected.flags, actual.flags);
    TEST_ASSERT_EQUAL_UINT32(
        expected.lifetimeBudgetSeconds,
        actual.lifetimeBudgetSeconds
    );
    TEST_ASSERT_EQUAL_UINT8(expected.bodyLength, actual.bodyLength);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(
        expected.body,
        actual.body,
        expected.bodyLength
    );
}

void testFrozenNumericRegistriesAndBounds() {
    TEST_ASSERT_EQUAL_HEX8(0x05,
        static_cast<uint8_t>(Protocol::PacketType::EVENT));
    TEST_ASSERT_EQUAL_HEX8(0x40, static_cast<uint8_t>(Family::BUTTON));
    TEST_ASSERT_EQUAL_HEX8(0x41,
        static_cast<uint8_t>(Family::SENSOR_THRESHOLD));
    TEST_ASSERT_EQUAL_HEX8(0x44,
        static_cast<uint8_t>(Family::MANUAL_CHECK_IN));
    TEST_ASSERT_EQUAL_UINT32(14, COMMON_PAYLOAD_SIZE);
    TEST_ASSERT_EQUAL_UINT32(12, MAX_BODY_SIZE);
    TEST_ASSERT_EQUAL_UINT32(9, ADMISSION_PAYLOAD_SIZE);
    TEST_ASSERT_EQUAL_UINT32(26, Protocol::MAX_PAYLOAD_SIZE);
    TEST_ASSERT_EQUAL_UINT32(32, Protocol::MAX_PACKET_SIZE);
}

void testButtonRegistryMatchesExistingDeviceInputSemantics() {
    TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(DeviceInput::ButtonEvent::PRESS),
        static_cast<uint8_t>(ButtonEvent::PRESS));
    TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(DeviceInput::ButtonEvent::RELEASE),
        static_cast<uint8_t>(ButtonEvent::RELEASE));
    TEST_ASSERT_EQUAL_HEX8(
        static_cast<uint8_t>(DeviceInput::ButtonEvent::SHORT_PRESS),
        static_cast<uint8_t>(ButtonEvent::SHORT_PRESS));
    TEST_ASSERT_EQUAL_HEX8(
        static_cast<uint8_t>(DeviceInput::ButtonEvent::LONG_PRESS),
        static_cast<uint8_t>(ButtonEvent::LONG_PRESS));
    TEST_ASSERT_EQUAL_HEX8(
        static_cast<uint8_t>(DeviceInput::ButtonEvent::VERY_LONG_PRESS),
        static_cast<uint8_t>(ButtonEvent::VERY_LONG_PRESS));
}

void testSensorRegistryMatchesCapabilityValueTypes() {
    TEST_ASSERT_EQUAL_HEX8(
        static_cast<uint8_t>(DeviceCapabilities::ValueType::UNSIGNED_32),
        static_cast<uint8_t>(SensorValueType::UNSIGNED_32));
    TEST_ASSERT_EQUAL_HEX8(
        static_cast<uint8_t>(DeviceCapabilities::ValueType::SIGNED_32),
        static_cast<uint8_t>(SensorValueType::SIGNED_32));
    TEST_ASSERT_EQUAL_HEX8(
        static_cast<uint8_t>(DeviceCapabilities::ValueType::NORMALIZED_U16),
        static_cast<uint8_t>(SensorValueType::NORMALIZED_U16));
    TEST_ASSERT_EQUAL_HEX8(
        static_cast<uint8_t>(DeviceCapabilities::ValueType::FIXED_Q16_16),
        static_cast<uint8_t>(SensorValueType::FIXED_Q16_16));
    TEST_ASSERT_EQUAL_HEX8(
        static_cast<uint8_t>(DeviceCapabilities::ValueType::ENUM_U16),
        static_cast<uint8_t>(SensorValueType::ENUM_U16));
}

void testButtonGoldenVectorAndRoundTrip() {
    const Event event = buttonEvent();
    uint8_t encoded[Protocol::MAX_PACKET_SIZE] = {};
    size_t length = 0;
    TEST_ASSERT_TRUE(encodeEvent(event, encoded, sizeof(encoded), length));
    const uint8_t expected[] = {
        0x15, NODE, HUB, SEQUENCE, 0x40, 0x0F,
        0x44, 0x33, 0x22, 0x11,
        0x04, 0x03, 0x02, 0x01,
        0x01, 0x10, 0x0E, 0x00, 0x00, 0x01, 0x02
    };
    TEST_ASSERT_EQUAL_UINT32(sizeof(expected), length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, encoded, sizeof(expected));
    Event decoded = {};
    TEST_ASSERT_TRUE(decodeEvent(encoded, length, decoded));
    assertEventEqual(event, decoded);
}

void testAllFamiliesRoundTripAndEndianIsExplicit() {
    const Event events[] = {buttonEvent(), sensorEvent(), manualEvent()};
    for (const Event& event : events) {
        uint8_t encoded[Protocol::MAX_PACKET_SIZE] = {};
        size_t length = 0;
        TEST_ASSERT_TRUE(encodeEvent(event, encoded, sizeof(encoded), length));
        TEST_ASSERT_EQUAL_HEX8(0x44, encoded[6]);
        TEST_ASSERT_EQUAL_HEX8(0x11, encoded[9]);
        TEST_ASSERT_EQUAL_HEX8(0x04, encoded[10]);
        TEST_ASSERT_EQUAL_HEX8(0x01, encoded[13]);
        Event decoded = {};
        TEST_ASSERT_TRUE(decodeEvent(encoded, length, decoded));
        assertEventEqual(event, decoded);
    }
}

void testLargestRegisteredFamilyAndStructuralMaximum() {
    const Event event = sensorEvent();
    uint8_t encoded[Protocol::MAX_PACKET_SIZE] = {};
    size_t length = 0;
    TEST_ASSERT_TRUE(encodeEvent(event, encoded, sizeof(encoded), length));
    TEST_ASSERT_EQUAL_UINT32(28, length);
    TEST_ASSERT_EQUAL_UINT8(22, encoded[5]);

    Protocol::Packet structural = {};
    structural.type = Protocol::PacketType::EVENT;
    structural.source = NODE;
    structural.destination = HUB;
    structural.sequence = SEQUENCE;
    structural.opcode = 0x42;
    structural.payloadLength = Protocol::MAX_PAYLOAD_SIZE;
    TEST_ASSERT_TRUE(Protocol::encode(
        structural, encoded, sizeof(encoded), length));
    TEST_ASSERT_EQUAL_UINT32(32, length);
    Protocol::Packet decodedPacket = {};
    TEST_ASSERT_TRUE(Protocol::decode(encoded, length, decodedPacket));
    Event decodedEvent = {};
    TEST_ASSERT_FALSE(decodeEvent(encoded, length, decodedEvent));
}

void testEncodeRejectsCapacityAndInvalidCommonFields() {
    Event event = buttonEvent();
    uint8_t encoded[Protocol::MAX_PACKET_SIZE] = {};
    size_t length = 99;
    TEST_ASSERT_FALSE(encodeEvent(event, encoded, 20, length));
    TEST_ASSERT_EQUAL_UINT32(0, length);

    event.source = 0;
    TEST_ASSERT_FALSE(isValidEvent(event));
    event = buttonEvent(); event.destination = 0;
    TEST_ASSERT_FALSE(isValidEvent(event));
    event = buttonEvent(); event.destination = NODE;
    TEST_ASSERT_FALSE(isValidEvent(event));
    event = buttonEvent(); event.epoch = 0;
    TEST_ASSERT_FALSE(isValidEvent(event));
    event = buttonEvent(); event.id = 0;
    TEST_ASSERT_FALSE(isValidEvent(event));
    event = buttonEvent(); event.flags = 0x02;
    TEST_ASSERT_FALSE(isValidEvent(event));
    event = buttonEvent(); event.lifetimeBudgetSeconds = 59;
    TEST_ASSERT_FALSE(isValidEvent(event));
    event.lifetimeBudgetSeconds = 86401;
    TEST_ASSERT_FALSE(isValidEvent(event));
}

void testLifetimeAndFlagsBoundaries() {
    Event event = buttonEvent();
    event.flags = 0;
    event.lifetimeBudgetSeconds = MIN_LIFETIME_SECONDS;
    TEST_ASSERT_TRUE(isValidEvent(event));
    event.flags = IMPORTANT_FLAG;
    event.lifetimeBudgetSeconds = MAX_LIFETIME_SECONDS;
    TEST_ASSERT_TRUE(isValidEvent(event));
}

void testDecodeRejectsWrongTypeVersionGeometryAndLeavesOutputUntouched() {
    uint8_t encoded[Protocol::MAX_PACKET_SIZE + 1] = {};
    size_t length = 0;
    TEST_ASSERT_TRUE(encodeEvent(buttonEvent(), encoded, sizeof(encoded), length));
    Event sentinel = {};
    sentinel.id = 0xAABBCCDDUL;

    encoded[0] = 0x11;
    TEST_ASSERT_FALSE(decodeEvent(encoded, length, sentinel));
    TEST_ASSERT_EQUAL_HEX32(0xAABBCCDDUL, sentinel.id);
    encoded[0] = 0x25;
    TEST_ASSERT_FALSE(decodeEvent(encoded, length, sentinel));
    encoded[0] = 0x15;
    TEST_ASSERT_FALSE(decodeEvent(encoded, length - 1, sentinel));
    encoded[length] = 0;
    TEST_ASSERT_FALSE(decodeEvent(encoded, length + 1, sentinel));
    encoded[5] = 27;
    TEST_ASSERT_FALSE(decodeEvent(encoded, length, sentinel));
}

void testDecodeRejectsMalformedEnvelopeAndUnknownFamily() {
    uint8_t encoded[Protocol::MAX_PACKET_SIZE] = {};
    size_t length = 0;
    TEST_ASSERT_TRUE(encodeEvent(buttonEvent(), encoded, sizeof(encoded), length));
    Event decoded = {};
    encoded[1] = 0;
    TEST_ASSERT_FALSE(decodeEvent(encoded, length, decoded));
    encoded[1] = NODE; encoded[2] = NODE;
    TEST_ASSERT_FALSE(decodeEvent(encoded, length, decoded));
    encoded[2] = HUB; encoded[4] = 0x42;
    TEST_ASSERT_FALSE(decodeEvent(encoded, length, decoded));
    encoded[4] = 0x40;
    encoded[6] = 0; encoded[7] = 0; encoded[8] = 0; encoded[9] = 0;
    TEST_ASSERT_FALSE(decodeEvent(encoded, length, decoded));
    encoded[6] = 0x44; encoded[7] = 0x33; encoded[8] = 0x22; encoded[9] = 0x11;
    encoded[10] = 0; encoded[11] = 0; encoded[12] = 0; encoded[13] = 0;
    TEST_ASSERT_FALSE(decodeEvent(encoded, length, decoded));
    encoded[10] = 0x04; encoded[11] = 0x03; encoded[12] = 0x02; encoded[13] = 0x01;
    encoded[14] = 0x02;
    TEST_ASSERT_FALSE(decodeEvent(encoded, length, decoded));
    encoded[14] = 0x01;
    encoded[15] = 0x3B; encoded[16] = 0; encoded[17] = 0; encoded[18] = 0;
    TEST_ASSERT_FALSE(decodeEvent(encoded, length, decoded));
    encoded[15] = 0x10; encoded[16] = 0x0E;
    encoded[14] = 0x02;
    TEST_ASSERT_FALSE(decodeEvent(encoded, length, decoded));
    encoded[14] = 0x01; encoded[19] = 0x02;
    TEST_ASSERT_FALSE(decodeEvent(encoded, length, decoded));
}

void testButtonClosedRegistry() {
    for (uint8_t value = 1; value <= 5; ++value) {
        TEST_ASSERT_TRUE(isValidEvent(buttonEvent(value)));
    }
    TEST_ASSERT_FALSE(isValidEvent(buttonEvent(0)));
    TEST_ASSERT_FALSE(isValidEvent(buttonEvent(6)));
    Event event = buttonEvent();
    event.bodyLength = 0;
    TEST_ASSERT_FALSE(isValidEvent(event));
    event.bodyLength = 2;
    TEST_ASSERT_FALSE(isValidEvent(event));
}

void testSensorClosedValueTypeRegistry() {
    const uint8_t validTypes[] = {0x02, 0x03, 0x04, 0x05, 0x06};
    for (uint8_t valueType : validTypes) {
        Event event = sensorEvent(valueType);
        if (valueType == 0x04 || valueType == 0x06) {
            event.body[5] = 0;
            event.body[6] = 0;
        }
        TEST_ASSERT_TRUE(isValidEvent(event));
    }
    TEST_ASSERT_FALSE(isValidEvent(sensorEvent(0x00)));
    TEST_ASSERT_FALSE(isValidEvent(sensorEvent(0x01)));
    TEST_ASSERT_FALSE(isValidEvent(sensorEvent(0x07)));
}

void testSensorCapabilityValueAndRelationValidation() {
    Event event = sensorEvent();
    event.body[0] = 0; event.body[1] = 0;
    TEST_ASSERT_FALSE(isValidEvent(event));

    event = sensorEvent(0x04);
    event.body[5] = 1;
    TEST_ASSERT_FALSE(isValidEvent(event));
    event = sensorEvent(0x06);
    event.body[6] = 1;
    TEST_ASSERT_FALSE(isValidEvent(event));

    event = sensorEvent(0x02, 0x01);
    TEST_ASSERT_TRUE(isValidEvent(event));
    event = sensorEvent(0x02, 0x02);
    TEST_ASSERT_TRUE(isValidEvent(event));
    event = sensorEvent(0x02, 0x00);
    TEST_ASSERT_FALSE(isValidEvent(event));
    event = sensorEvent(0x02, 0x03);
    TEST_ASSERT_FALSE(isValidEvent(event));
    event = sensorEvent(); event.bodyLength = 7;
    TEST_ASSERT_FALSE(isValidEvent(event));
    event.bodyLength = 9;
    TEST_ASSERT_FALSE(isValidEvent(event));
}

void testManualCheckInClosedRegistry() {
    TEST_ASSERT_TRUE(isValidEvent(manualEvent(0x01)));
    TEST_ASSERT_FALSE(isValidEvent(manualEvent(0x00)));
    TEST_ASSERT_FALSE(isValidEvent(manualEvent(0x02)));
    Event event = manualEvent(); event.bodyLength = 2;
    TEST_ASSERT_FALSE(isValidEvent(event));
}

void testIdentityAndCanonicalComparisons() {
    const Identity identity = {NODE, EPOCH, EVENT_ID};
    Identity other = identity;
    TEST_ASSERT_TRUE(sameIdentity(identity, other));
    other.id++;
    TEST_ASSERT_FALSE(sameIdentity(identity, other));

    const Event event = buttonEvent();
    Event changed = event;
    TEST_ASSERT_TRUE(sameCanonicalContent(event, changed));
    changed.sequence++;
    changed.destination++;
    TEST_ASSERT_TRUE(sameCanonicalContent(event, changed));
    changed.body[0] = 0x03;
    TEST_ASSERT_FALSE(sameCanonicalContent(event, changed));
}

void testAdmissionGoldenVectorAndRoundTrip() {
    const AdmissionResponse response = makeAdmissionResponse(
        buttonEvent(), AdmissionStatus::ADMITTED);
    uint8_t encoded[Protocol::MAX_PACKET_SIZE] = {};
    size_t length = 0;
    TEST_ASSERT_TRUE(encodeAdmissionResponse(
        response, encoded, sizeof(encoded), length));
    const uint8_t expected[] = {
        0x12, HUB, NODE, SEQUENCE, 0x40, 0x09, 0x00,
        0x44, 0x33, 0x22, 0x11, 0x04, 0x03, 0x02, 0x01
    };
    TEST_ASSERT_EQUAL_UINT32(sizeof(expected), length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, encoded, sizeof(expected));
    AdmissionResponse decoded = {};
    TEST_ASSERT_TRUE(decodeAdmissionResponse(encoded, length, decoded));
    TEST_ASSERT_TRUE(matchesEvent(decoded, buttonEvent()));
}

void testEveryAdmissionStatusAndUnsupportedFamilyCorrelation() {
    for (uint8_t value = 0; value <= 4; ++value) {
        AdmissionResponse response = makeAdmissionResponse(
            buttonEvent(), static_cast<AdmissionStatus>(value));
        uint8_t encoded[Protocol::MAX_PACKET_SIZE] = {};
        size_t length = 0;
        TEST_ASSERT_TRUE(encodeAdmissionResponse(
            response, encoded, sizeof(encoded), length));
        AdmissionResponse decoded = {};
        TEST_ASSERT_TRUE(decodeAdmissionResponse(encoded, length, decoded));
        TEST_ASSERT_EQUAL_UINT8(value, static_cast<uint8_t>(decoded.status));
    }
    AdmissionResponse unsupported = makeAdmissionResponse(
        buttonEvent(), AdmissionStatus::UNSUPPORTED_EVENT);
    unsupported.family = 0x42;
    TEST_ASSERT_TRUE(isValidAdmissionResponse(unsupported));
}

void testAdmissionRejectsMalformedGeometryAndValues() {
    AdmissionResponse response = makeAdmissionResponse(
        buttonEvent(), AdmissionStatus::ADMITTED);
    uint8_t encoded[Protocol::MAX_PACKET_SIZE] = {};
    size_t length = 0;
    TEST_ASSERT_TRUE(encodeAdmissionResponse(
        response, encoded, sizeof(encoded), length));
    AdmissionResponse decoded = {};
    encoded[0] = 0x14;
    TEST_ASSERT_FALSE(decodeAdmissionResponse(encoded, length, decoded));
    encoded[0] = 0x12; encoded[5] = 8;
    TEST_ASSERT_FALSE(decodeAdmissionResponse(encoded, length, decoded));
    encoded[5] = 9; encoded[6] = 5;
    TEST_ASSERT_FALSE(decodeAdmissionResponse(encoded, length, decoded));
    encoded[6] = 0; encoded[7] = 0; encoded[8] = 0;
    encoded[9] = 0; encoded[10] = 0;
    TEST_ASSERT_FALSE(decodeAdmissionResponse(encoded, length, decoded));
    TEST_ASSERT_FALSE(decodeAdmissionResponse(encoded, length - 1, decoded));
    TEST_ASSERT_FALSE(decodeAdmissionResponse(encoded, length + 1, decoded));
}

void testAdmissionCorrelationRejectsEveryMismatch() {
    const Event event = buttonEvent();
    AdmissionResponse response = makeAdmissionResponse(
        event, AdmissionStatus::CAPACITY);
    TEST_ASSERT_TRUE(matchesEvent(response, event));
    response.source++;
    TEST_ASSERT_FALSE(matchesEvent(response, event));
    response = makeAdmissionResponse(event, AdmissionStatus::CAPACITY);
    response.destination++;
    TEST_ASSERT_FALSE(matchesEvent(response, event));
    response = makeAdmissionResponse(event, AdmissionStatus::CAPACITY);
    response.sequence++;
    TEST_ASSERT_FALSE(matchesEvent(response, event));
    response = makeAdmissionResponse(event, AdmissionStatus::CAPACITY);
    response.family = 0x41;
    TEST_ASSERT_FALSE(matchesEvent(response, event));
    response = makeAdmissionResponse(event, AdmissionStatus::CAPACITY);
    response.epoch++;
    TEST_ASSERT_FALSE(matchesEvent(response, event));
    response = makeAdmissionResponse(event, AdmissionStatus::CAPACITY);
    response.id++;
    TEST_ASSERT_FALSE(matchesEvent(response, event));
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(testFrozenNumericRegistriesAndBounds);
    RUN_TEST(testButtonRegistryMatchesExistingDeviceInputSemantics);
    RUN_TEST(testSensorRegistryMatchesCapabilityValueTypes);
    RUN_TEST(testButtonGoldenVectorAndRoundTrip);
    RUN_TEST(testAllFamiliesRoundTripAndEndianIsExplicit);
    RUN_TEST(testLargestRegisteredFamilyAndStructuralMaximum);
    RUN_TEST(testEncodeRejectsCapacityAndInvalidCommonFields);
    RUN_TEST(testLifetimeAndFlagsBoundaries);
    RUN_TEST(testDecodeRejectsWrongTypeVersionGeometryAndLeavesOutputUntouched);
    RUN_TEST(testDecodeRejectsMalformedEnvelopeAndUnknownFamily);
    RUN_TEST(testButtonClosedRegistry);
    RUN_TEST(testSensorClosedValueTypeRegistry);
    RUN_TEST(testSensorCapabilityValueAndRelationValidation);
    RUN_TEST(testManualCheckInClosedRegistry);
    RUN_TEST(testIdentityAndCanonicalComparisons);
    RUN_TEST(testAdmissionGoldenVectorAndRoundTrip);
    RUN_TEST(testEveryAdmissionStatusAndUnsupportedFamilyCorrelation);
    RUN_TEST(testAdmissionRejectsMalformedGeometryAndValues);
    RUN_TEST(testAdmissionCorrelationRejectsEveryMismatch);
    return UNITY_END();
}
