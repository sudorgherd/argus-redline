#include <unity.h>

#include <stdint.h>

#include "device_identity.h"

using namespace DeviceIdentity;

namespace {

Record hub() {
    Record value = {};
    value.recordGeneration = 0x01020304U;
    value.provisioningGeneration = 0x0A0B0C0DU;
    value.state = DurableState::PROVISIONED;
    value.transaction = TransactionKind::NONE;
    value.role = Role::HUB;
    value.localDeviceId = 1;
    value.peerDeviceId = 0x10;
    value.hardwareProfile = HardwareProfile::HELTEC_V4;
    value.capabilityProfile = CapabilityProfile::HELTEC_V4_BASE;
    value.labelLength = 3;
    value.networkId = 0x11223344U;
    value.label[0] = 'H'; value.label[1] = 'U'; value.label[2] = 'B';
    return value;
}

Record node() {
    Record value = hub();
    value.role = Role::NODE;
    value.localDeviceId = 0x10;
    value.peerDeviceId = 1;
    return value;
}

Record zero(DurableState state, TransactionKind transaction) {
    Record value = {};
    value.recordGeneration = 7;
    value.state = state;
    value.transaction = transaction;
    return value;
}

void assertResult(CodecResult expected, CodecResult actual) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected),
                            static_cast<uint8_t>(actual));
}

void assertSame(const Record& expected, const Record& actual) {
    TEST_ASSERT_EQUAL_UINT32(expected.recordGeneration, actual.recordGeneration);
    TEST_ASSERT_EQUAL_UINT32(expected.provisioningGeneration,
                             actual.provisioningGeneration);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected.state),
                            static_cast<uint8_t>(actual.state));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected.transaction),
                            static_cast<uint8_t>(actual.transaction));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected.role),
                            static_cast<uint8_t>(actual.role));
    TEST_ASSERT_EQUAL_UINT8(expected.localDeviceId, actual.localDeviceId);
    TEST_ASSERT_EQUAL_UINT8(expected.peerDeviceId, actual.peerDeviceId);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected.hardwareProfile),
                            static_cast<uint8_t>(actual.hardwareProfile));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected.capabilityProfile),
                            static_cast<uint8_t>(actual.capabilityProfile));
    TEST_ASSERT_EQUAL_UINT8(expected.labelLength, actual.labelLength);
    TEST_ASSERT_EQUAL_UINT32(expected.networkId, actual.networkId);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.label, actual.label, LABEL_SIZE);
}

void encode(const Record& value, uint8_t (&bytes)[RECORD_SIZE]) {
    assertResult(CodecResult::OK, encodeRecord(value, bytes, sizeof(bytes)));
}

void refreshCrc(uint8_t (&bytes)[RECORD_SIZE]) {
    uint32_t crc = 0;
    TEST_ASSERT_TRUE(EventRecords::crc32IsoHdlc(bytes, 60, crc));
    EventRecords::writeUint32Le(bytes + 60, crc);
}

void roundTrip(const Record& input) {
    uint8_t bytes[RECORD_SIZE] = {};
    encode(input, bytes);
    Record output = {};
    assertResult(CodecResult::OK, decodeRecord(bytes, sizeof(bytes), output));
    assertSame(input, output);
}

void rejectByte(size_t offset, uint8_t replacement, CodecResult expected) {
    uint8_t bytes[RECORD_SIZE] = {};
    encode(hub(), bytes);
    bytes[offset] = replacement;
    refreshCrc(bytes);
    Record output = {};
    assertResult(expected, decodeRecord(bytes, sizeof(bytes), output));
}

void testVocabularyAndGeometry() {
    TEST_ASSERT_EQUAL_UINT32(64, RECORD_SIZE);
    TEST_ASSERT_EQUAL_UINT32(16, LABEL_SIZE);
    TEST_ASSERT_EQUAL_UINT8(1, static_cast<uint8_t>(DurableState::UNPROVISIONED));
    TEST_ASSERT_EQUAL_UINT8(2, static_cast<uint8_t>(DurableState::PENDING));
    TEST_ASSERT_EQUAL_UINT8(3, static_cast<uint8_t>(DurableState::PROVISIONED));
    TEST_ASSERT_EQUAL_UINT8(0, static_cast<uint8_t>(TransactionKind::NONE));
    TEST_ASSERT_EQUAL_UINT8(1, static_cast<uint8_t>(TransactionKind::APPLY));
    TEST_ASSERT_EQUAL_UINT8(2, static_cast<uint8_t>(TransactionKind::FULL_RESET));
    TEST_ASSERT_EQUAL_UINT8(0, static_cast<uint8_t>(Role::NONE));
    TEST_ASSERT_EQUAL_UINT8(1, static_cast<uint8_t>(Role::HUB));
    TEST_ASSERT_EQUAL_UINT8(2, static_cast<uint8_t>(Role::NODE));
    TEST_ASSERT_EQUAL_UINT8(1, static_cast<uint8_t>(HardwareProfile::HELTEC_V4));
    TEST_ASSERT_EQUAL_UINT8(1, static_cast<uint8_t>(CapabilityProfile::HELTEC_V4_BASE));
}

void testGoldenHubVector() {
    const uint8_t golden[RECORD_SIZE] = {
        0x52,0x4C,0x49,0x31,0x01,0x00,0x40,0x00,
        0x04,0x03,0x02,0x01,0x0D,0x0C,0x0B,0x0A,
        0x03,0x00,0x01,0x01,0x10,0x01,0x01,0x03,
        0x44,0x33,0x22,0x11,0x48,0x55,0x42,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0xB0,0x0C,0xCF,0x89
    };
    uint8_t bytes[RECORD_SIZE] = {};
    encode(hub(), bytes);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(golden, bytes, RECORD_SIZE);
}

void testGoldenUnprovisionedVector() {
    uint8_t golden[RECORD_SIZE] = {};
    golden[0] = 'R'; golden[1] = 'L'; golden[2] = 'I'; golden[3] = '1';
    golden[4] = 1; golden[6] = 64; golden[8] = 1; golden[16] = 1;
    golden[60] = 0x55; golden[61] = 0xF4;
    golden[62] = 0xED; golden[63] = 0x9C;
    Record value = zero(DurableState::UNPROVISIONED, TransactionKind::NONE);
    value.recordGeneration = 1;
    uint8_t bytes[RECORD_SIZE] = {};
    encode(value, bytes);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(golden, bytes, RECORD_SIZE);
}

void testProvisionedHubRoundTrip() { roundTrip(hub()); }
void testProvisionedNodeRoundTrip() { roundTrip(node()); }

void testPendingApplyRoundTrip() {
    Record value = node();
    value.state = DurableState::PENDING;
    value.transaction = TransactionKind::APPLY;
    roundTrip(value);
}

void testUnprovisionedRoundTrip() {
    roundTrip(zero(DurableState::UNPROVISIONED, TransactionKind::NONE));
}

void testPendingFullResetRoundTrip() {
    roundTrip(zero(DurableState::PENDING, TransactionKind::FULL_RESET));
}

void testCrcStandardCheckAndCorruption() {
    const uint8_t check[] = {'1','2','3','4','5','6','7','8','9'};
    uint32_t crc = 0;
    TEST_ASSERT_TRUE(EventRecords::crc32IsoHdlc(check, sizeof(check), crc));
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926U, crc);
    uint8_t bytes[RECORD_SIZE] = {};
    encode(hub(), bytes);
    bytes[28] ^= 1;
    Record output = {};
    assertResult(CodecResult::BAD_CRC, decodeRecord(bytes, sizeof(bytes), output));
}

void testNullPointers() {
    uint8_t bytes[RECORD_SIZE] = {};
    Record value = hub();
    assertResult(CodecResult::NULL_POINTER, encodeRecord(nullptr, bytes, sizeof(bytes)));
    assertResult(CodecResult::NULL_POINTER, encodeRecord(&value, nullptr, RECORD_SIZE));
    assertResult(CodecResult::NULL_POINTER, decodeRecord(nullptr, RECORD_SIZE, &value));
    assertResult(CodecResult::NULL_POINTER, decodeRecord(bytes, RECORD_SIZE, nullptr));
}

void testWrongLengths() {
    uint8_t bytes[RECORD_SIZE + 1] = {};
    Record value = hub();
    assertResult(CodecResult::WRONG_LENGTH, encodeRecord(value, bytes, 63));
    assertResult(CodecResult::WRONG_LENGTH, encodeRecord(value, bytes, 65));
    encodeRecord(value, bytes, RECORD_SIZE);
    assertResult(CodecResult::WRONG_LENGTH, decodeRecord(bytes, 63, value));
    assertResult(CodecResult::WRONG_LENGTH, decodeRecord(bytes, 65, value));
}

void testBadMagic() { rejectByte(0, 0, CodecResult::BAD_MAGIC); }
void testBadSchema() { rejectByte(4, 2, CodecResult::BAD_SCHEMA); }
void testBadEncodedLength() { rejectByte(6, 63, CodecResult::BAD_RECORD_LENGTH); }

void testEveryInvalidStateValue() {
    for (unsigned v = 0; v < 256; ++v) {
        if (v >= 1 && v <= 3) continue;
        rejectByte(16, static_cast<uint8_t>(v), CodecResult::INVALID_STATE);
    }
}

void testEveryInvalidTransactionValue() {
    for (unsigned v = 3; v < 256; ++v) {
        rejectByte(17, static_cast<uint8_t>(v), CodecResult::INVALID_TRANSACTION);
    }
}

void testAllStateTransactionCombinations() {
    for (unsigned state = 1; state <= 3; ++state) {
        for (unsigned transaction = 0; transaction <= 2; ++transaction) {
            const bool permitted = (state == 1 && transaction == 0) ||
                (state == 2 && (transaction == 1 || transaction == 2)) ||
                (state == 3 && transaction == 0);
            Record value = permitted && (state == 1 || transaction == 2)
                ? zero(static_cast<DurableState>(state),
                       static_cast<TransactionKind>(transaction))
                : hub();
            value.state = static_cast<DurableState>(state);
            value.transaction = static_cast<TransactionKind>(transaction);
            uint8_t bytes[RECORD_SIZE] = {};
            assertResult(permitted ? CodecResult::OK : CodecResult::INVALID_COMBINATION,
                         encodeRecord(value, bytes, sizeof(bytes)));
        }
    }
}

void testInvalidRoleValues() {
    for (unsigned v = 0; v < 256; ++v) {
        if (v == 1 || v == 2) continue;
        rejectByte(18, static_cast<uint8_t>(v), CodecResult::INVALID_ROLE);
    }
}

void testReservedLocalIds() {
    rejectByte(19, 0, CodecResult::INVALID_DEVICE_ID);
    rejectByte(19, 0xFF, CodecResult::INVALID_DEVICE_ID);
}

void testReservedPeerIds() {
    rejectByte(20, 0, CodecResult::INVALID_DEVICE_ID);
    rejectByte(20, 0xFF, CodecResult::INVALID_DEVICE_ID);
}

void testHubEqualLocalPeerRejected() {
    rejectByte(20, 1, CodecResult::DUPLICATE_DEVICE_ID);
}

void testNodeEqualLocalPeerRejected() {
    Record value = node();
    value.peerDeviceId = value.localDeviceId;
    uint8_t bytes[RECORD_SIZE] = {};
    assertResult(CodecResult::DUPLICATE_DEVICE_ID,
                 encodeRecord(value, bytes, sizeof(bytes)));
}

void testZeroNetworkId() {
    Record value = hub(); value.networkId = 0;
    uint8_t bytes[RECORD_SIZE] = {};
    assertResult(CodecResult::INVALID_NETWORK_ID,
                 encodeRecord(value, bytes, sizeof(bytes)));
    encode(hub(), bytes);
    for (size_t i = 24; i < 28; ++i) bytes[i] = 0;
    refreshCrc(bytes);
    Record output = {};
    assertResult(CodecResult::INVALID_NETWORK_ID,
                 decodeRecord(bytes, sizeof(bytes), output));
}

void testUnsupportedProfiles() {
    rejectByte(21, 2, CodecResult::INVALID_HARDWARE_PROFILE);
    rejectByte(22, 2, CodecResult::INVALID_CAPABILITY_PROFILE);
    rejectByte(21, 0, CodecResult::INVALID_HARDWARE_PROFILE);
    rejectByte(22, 0, CodecResult::INVALID_CAPABILITY_PROFILE);
}

void testInvalidProvisioningGenerations() {
    Record value = hub(); uint8_t bytes[RECORD_SIZE] = {};
    value.provisioningGeneration = 0;
    assertResult(CodecResult::INVALID_PROVISIONING_GENERATION,
                 encodeRecord(value, bytes, sizeof(bytes)));
    value.provisioningGeneration = UINT32_MAX;
    assertResult(CodecResult::INVALID_PROVISIONING_GENERATION,
                 encodeRecord(value, bytes, sizeof(bytes)));
    value.provisioningGeneration = UINT32_MAX - 1;
    roundTrip(value);
}

void testLabelLengthOver16() {
    rejectByte(23, 17, CodecResult::INVALID_LABEL_LENGTH);
}

void testNonPrintableLabel() {
    rejectByte(28, 0x1F, CodecResult::INVALID_LABEL_BYTE);
    rejectByte(29, 0x7F, CodecResult::INVALID_LABEL_BYTE);
}

void testNonzeroLabelTail() {
    rejectByte(31, 'X', CodecResult::NONZERO_LABEL_TAIL);
    rejectByte(43, 'X', CodecResult::NONZERO_LABEL_TAIL);
}

void testNonzeroReservedArea() {
    for (size_t offset = 44; offset < 60; ++offset) {
        rejectByte(offset, 1, CodecResult::NONZERO_RESERVED);
    }
}

void testCanonicalLabelBoundaries() {
    Record value = hub();
    value.labelLength = 0;
    for (size_t i = 0; i < LABEL_SIZE; ++i) value.label[i] = 0;
    roundTrip(value);
    value.labelLength = LABEL_SIZE;
    for (size_t i = 0; i < LABEL_SIZE; ++i) value.label[i] = '~';
    value.label[0] = ' ';
    roundTrip(value);
}

void testUnprovisionedRejectsEveryNonzeroIdentityField() {
    uint8_t bytes[RECORD_SIZE] = {};
    encode(zero(DurableState::UNPROVISIONED, TransactionKind::NONE), bytes);
    for (size_t offset = 12; offset < 44; ++offset) {
        if (offset == 16 || offset == 17) continue;
        bytes[offset] = 1;
        refreshCrc(bytes);
        Record output = {};
        assertResult(CodecResult::NONCANONICAL_ZERO,
                     decodeRecord(bytes, sizeof(bytes), output));
        bytes[offset] = 0;
    }
}

void testPendingResetRejectsEveryNonzeroIdentityField() {
    uint8_t bytes[RECORD_SIZE] = {};
    encode(zero(DurableState::PENDING, TransactionKind::FULL_RESET), bytes);
    for (size_t offset = 12; offset < 44; ++offset) {
        if (offset == 16 || offset == 17) continue;
        bytes[offset] = 1;
        refreshCrc(bytes);
        Record output = {};
        assertResult(CodecResult::NONCANONICAL_ZERO,
                     decodeRecord(bytes, sizeof(bytes), output));
        bytes[offset] = 0;
    }
}

void testEncodeFailureLeavesOutputUntouched() {
    Record value = hub(); value.role = Role::NONE;
    uint8_t bytes[RECORD_SIZE];
    for (size_t i = 0; i < RECORD_SIZE; ++i) bytes[i] = 0xA5;
    assertResult(CodecResult::INVALID_ROLE,
                 encodeRecord(value, bytes, sizeof(bytes)));
    for (size_t i = 0; i < RECORD_SIZE; ++i) TEST_ASSERT_EQUAL_HEX8(0xA5, bytes[i]);
}

void testDecodeFailurePreservesOutput() {
    uint8_t bytes[RECORD_SIZE] = {};
    encode(hub(), bytes);
    Record sentinel = node();
    Record output = sentinel;
    bytes[20] = 0;
    refreshCrc(bytes);
    assertResult(CodecResult::INVALID_DEVICE_ID,
                 decodeRecord(bytes, sizeof(bytes), output));
    assertSame(sentinel, output);
    bytes[0] = 0;
    assertResult(CodecResult::BAD_MAGIC,
                 decodeRecord(bytes, sizeof(bytes), output));
    assertSame(sentinel, output);
}

void testGenerationEquality() {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(GenerationOrder::EQUAL),
        static_cast<uint8_t>(compareGenerations(7, 7)));
}

void testGenerationNormalOrdering() {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(GenerationOrder::LEFT_NEWER),
        static_cast<uint8_t>(compareGenerations(8, 7)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(GenerationOrder::RIGHT_NEWER),
        static_cast<uint8_t>(compareGenerations(7, 8)));
}

void testGenerationWrap() {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(GenerationOrder::LEFT_NEWER),
        static_cast<uint8_t>(compareGenerations(0, UINT32_MAX)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(GenerationOrder::RIGHT_NEWER),
        static_cast<uint8_t>(compareGenerations(UINT32_MAX, 0)));
}

void testGenerationHalfRangeAmbiguity() {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(GenerationOrder::AMBIGUOUS),
        static_cast<uint8_t>(compareGenerations(0, 0x80000000U)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(GenerationOrder::AMBIGUOUS),
        static_cast<uint8_t>(compareGenerations(0x80000000U, 0)));
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(testVocabularyAndGeometry);
    RUN_TEST(testGoldenHubVector);
    RUN_TEST(testGoldenUnprovisionedVector);
    RUN_TEST(testProvisionedHubRoundTrip);
    RUN_TEST(testProvisionedNodeRoundTrip);
    RUN_TEST(testPendingApplyRoundTrip);
    RUN_TEST(testUnprovisionedRoundTrip);
    RUN_TEST(testPendingFullResetRoundTrip);
    RUN_TEST(testCrcStandardCheckAndCorruption);
    RUN_TEST(testNullPointers);
    RUN_TEST(testWrongLengths);
    RUN_TEST(testBadMagic);
    RUN_TEST(testBadSchema);
    RUN_TEST(testBadEncodedLength);
    RUN_TEST(testEveryInvalidStateValue);
    RUN_TEST(testEveryInvalidTransactionValue);
    RUN_TEST(testAllStateTransactionCombinations);
    RUN_TEST(testInvalidRoleValues);
    RUN_TEST(testReservedLocalIds);
    RUN_TEST(testReservedPeerIds);
    RUN_TEST(testHubEqualLocalPeerRejected);
    RUN_TEST(testNodeEqualLocalPeerRejected);
    RUN_TEST(testZeroNetworkId);
    RUN_TEST(testUnsupportedProfiles);
    RUN_TEST(testInvalidProvisioningGenerations);
    RUN_TEST(testLabelLengthOver16);
    RUN_TEST(testNonPrintableLabel);
    RUN_TEST(testNonzeroLabelTail);
    RUN_TEST(testNonzeroReservedArea);
    RUN_TEST(testCanonicalLabelBoundaries);
    RUN_TEST(testUnprovisionedRejectsEveryNonzeroIdentityField);
    RUN_TEST(testPendingResetRejectsEveryNonzeroIdentityField);
    RUN_TEST(testEncodeFailureLeavesOutputUntouched);
    RUN_TEST(testDecodeFailurePreservesOutput);
    RUN_TEST(testGenerationEquality);
    RUN_TEST(testGenerationNormalOrdering);
    RUN_TEST(testGenerationWrap);
    RUN_TEST(testGenerationHalfRangeAmbiguity);
    return UNITY_END();
}
