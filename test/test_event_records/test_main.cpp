#include <unity.h>

#include <stdint.h>

#include "event_records.h"
#include "event_store.h"

using namespace EventRecords;
using namespace EventStorage;

namespace {

NodeRecord makeNode(NodeState state = NodeState::QUEUED) {
    NodeRecord value = {};
    value.generation = 7;
    value.state = state;
    value.flags = EventProtocol::IMPORTANT_FLAG;
    value.family = static_cast<uint8_t>(EventProtocol::Family::BUTTON);
    value.bodyLength = 1;
    value.eventEpoch = 0x11223344U;
    value.eventId = 0x55667788U;
    value.lifetimeBudgetSeconds = 3600;
    value.remainingActiveSeconds = 3500;
    value.attemptsUsed = 2;
    value.body[0] = static_cast<uint8_t>(EventProtocol::ButtonEvent::RELEASE);
    return value;
}

NodeRecord makeFree(uint32_t generation = 7) {
    NodeRecord value = {};
    value.generation = generation;
    value.state = NodeState::FREE;
    return value;
}

HubRecord makeHub(HubState state = HubState::ACTIVE) {
    HubRecord value = {};
    value.generation = 9;
    value.state = state;
    value.sourceDeviceId = 0x10;
    value.family = static_cast<uint8_t>(EventProtocol::Family::BUTTON);
    value.flags = EventProtocol::IMPORTANT_FLAG;
    value.bodyLength = 1;
    value.eventEpoch = 0x11223344U;
    value.eventId = 0x55667788U;
    value.lifetimeBudgetSeconds = 60;
    value.admissionOrdinal = 3;
    value.body[0] = static_cast<uint8_t>(EventProtocol::ButtonEvent::RELEASE);
    return value;
}

HubRecord makeEmpty(uint32_t generation = 9) {
    HubRecord value = {};
    value.generation = generation;
    value.state = HubState::EMPTY;
    return value;
}

template <size_t Size>
void refreshCrc(uint8_t (&bytes)[Size]) {
    uint32_t crc = 0;
    TEST_ASSERT_TRUE(crc32IsoHdlc(bytes, Size - 4, crc));
    writeUint32Le(bytes + Size - 4, crc);
}

template <size_t Size>
FixedCopy<Size> readable(const uint8_t (&bytes)[Size]) {
    FixedCopy<Size> copy = {};
    copy.status = FixedReadStatus::OK;
    for (size_t i = 0; i < Size; ++i) copy.bytes[i] = bytes[i];
    return copy;
}

void testCrcMatchesStandardCheckValueAndSettingsConvention() {
    const uint8_t check[] = {'1','2','3','4','5','6','7','8','9'};
    uint32_t crc = 0;
    TEST_ASSERT_TRUE(crc32IsoHdlc(check, sizeof(check), crc));
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926U, crc);
    TEST_ASSERT_FALSE(crc32IsoHdlc(nullptr, 1, crc));
}

void testNodeMetadataGoldenRoundTripAndGeometry() {
    NodeMetadata input = {0x89ABCDEFU, 0x10, 0x11223344U, 0x55667788U};
    uint8_t encoded[NODE_METADATA_SIZE] = {};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::OK),
        static_cast<uint8_t>(encodeNodeMetadata(input, encoded, sizeof(encoded))));
    const uint8_t golden[NODE_METADATA_SIZE] = {
        0x45,0x49,0x44,0x31,0x01,0x00,0x1C,0x00,
        0xEF,0xCD,0xAB,0x89,0x10,0x00,0x00,0x00,
        0x44,0x33,0x22,0x11,0x88,0x77,0x66,0x55,
        0xCD,0x11,0xBB,0xBA
    };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(golden, encoded, sizeof(golden));
    NodeMetadata output = {};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::OK),
        static_cast<uint8_t>(decodeNodeMetadata(encoded, sizeof(encoded), output)));
    TEST_ASSERT_EQUAL_HEX32(input.generation, output.generation);
    TEST_ASSERT_EQUAL_HEX8(input.custodySourceDeviceId, output.custodySourceDeviceId);
    TEST_ASSERT_EQUAL_HEX32(input.eventEpoch, output.eventEpoch);
    TEST_ASSERT_EQUAL_HEX32(input.nextUnreservedEventId, output.nextUnreservedEventId);
}

void testNodeMetadataRejectsEnvelopeReservedAndZeroFieldsWithoutMutation() {
    NodeMetadata input = {UINT32_MAX, 1, 1, 1};
    uint8_t bytes[NODE_METADATA_SIZE] = {};
    TEST_ASSERT_EQUAL_UINT8(0, static_cast<uint8_t>(encodeNodeMetadata(input, bytes, sizeof(bytes))));
    NodeMetadata output = {0xA5A5A5A5U, 0xA5, 0xA5A5A5A5U, 0xA5A5A5A5U};
    bytes[0] ^= 1; refreshCrc(bytes);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::BAD_MAGIC), static_cast<uint8_t>(decodeNodeMetadata(bytes, sizeof(bytes), output)));
    bytes[0] ^= 1; bytes[4] = 2; refreshCrc(bytes);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::BAD_SCHEMA), static_cast<uint8_t>(decodeNodeMetadata(bytes, sizeof(bytes), output)));
    bytes[4] = 1; bytes[6] = 27; refreshCrc(bytes);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::BAD_RECORD_LENGTH), static_cast<uint8_t>(decodeNodeMetadata(bytes, sizeof(bytes), output)));
    bytes[6] = 28; bytes[13] = 1; refreshCrc(bytes);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::NONZERO_RESERVED), static_cast<uint8_t>(decodeNodeMetadata(bytes, sizeof(bytes), output)));
    bytes[13] = 0; bytes[12] = 0; refreshCrc(bytes);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::INVALID_METADATA), static_cast<uint8_t>(decodeNodeMetadata(bytes, sizeof(bytes), output)));
    NodeMetadata invalid=input; invalid.eventEpoch=0;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::INVALID_METADATA),static_cast<uint8_t>(encodeNodeMetadata(invalid,bytes,sizeof(bytes))));
    invalid=input; invalid.nextUnreservedEventId=0;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::INVALID_METADATA),static_cast<uint8_t>(encodeNodeMetadata(invalid,bytes,sizeof(bytes))));
    TEST_ASSERT_EQUAL_HEX32(0xA5A5A5A5U, output.generation);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::WRONG_LENGTH), static_cast<uint8_t>(decodeNodeMetadata(bytes, sizeof(bytes)-1, output)));
    bytes[24] ^= 1;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::BAD_CRC), static_cast<uint8_t>(decodeNodeMetadata(bytes, sizeof(bytes), output)));
}

void testHubMetadataGoldenRoundTripAndBoundaries() {
    HubMetadata input = {0x01020304U, 1};
    uint8_t encoded[HUB_METADATA_SIZE] = {};
    TEST_ASSERT_EQUAL_UINT8(0, static_cast<uint8_t>(encodeHubMetadata(input, encoded, sizeof(encoded))));
    const uint8_t golden[HUB_METADATA_SIZE] = {
        0x48,0x41,0x4F,0x31,0x01,0x00,0x18,0x00,
        0x04,0x03,0x02,0x01,0x01,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0xC0,0x1F,0xF9,0x71
    };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(golden, encoded, sizeof(golden));
    HubMetadata output = {};
    TEST_ASSERT_EQUAL_UINT8(0, static_cast<uint8_t>(decodeHubMetadata(encoded, sizeof(encoded), output)));
    TEST_ASSERT_EQUAL_UINT32(1, output.nextUnreservedAdmissionOrdinal);
    input.nextUnreservedAdmissionOrdinal = UINT32_MAX;
    TEST_ASSERT_EQUAL_UINT8(0, static_cast<uint8_t>(encodeHubMetadata(input, encoded, sizeof(encoded))));
    input.nextUnreservedAdmissionOrdinal = 0;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::INVALID_METADATA), static_cast<uint8_t>(encodeHubMetadata(input, encoded, sizeof(encoded))));
}

void testHubMetadataRejectsReservedCrcLengthAndPreservesOutput() {
    HubMetadata input = {0, 1}; uint8_t bytes[HUB_METADATA_SIZE] = {};
    TEST_ASSERT_EQUAL_UINT8(0, static_cast<uint8_t>(encodeHubMetadata(input, bytes, sizeof(bytes))));
    HubMetadata output = {0xAAAAAAAAU, 0xBBBBBBBBU};
    bytes[16] = 1; refreshCrc(bytes);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::NONZERO_RESERVED), static_cast<uint8_t>(decodeHubMetadata(bytes, sizeof(bytes), output)));
    bytes[16] = 0; bytes[20] ^= 1;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::BAD_CRC), static_cast<uint8_t>(decodeHubMetadata(bytes, sizeof(bytes), output)));
    TEST_ASSERT_EQUAL_HEX32(0xAAAAAAAAU, output.generation);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::WRONG_LENGTH), static_cast<uint8_t>(decodeHubMetadata(bytes, sizeof(bytes)-1, output)));
}

void testHubMetadataRejectsMagicSchemaRecordLengthAndZeroOrdinal() {
    HubMetadata input={7,1}; uint8_t bytes[HUB_METADATA_SIZE]={}; HubMetadata output={0xAAAAAAAAU,0xBBBBBBBBU};
    TEST_ASSERT_EQUAL_UINT8(0,static_cast<uint8_t>(encodeHubMetadata(input,bytes,sizeof(bytes))));
    bytes[0]^=1; refreshCrc(bytes);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::BAD_MAGIC),static_cast<uint8_t>(decodeHubMetadata(bytes,sizeof(bytes),output)));
    bytes[0]^=1; bytes[4]=2; refreshCrc(bytes);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::BAD_SCHEMA),static_cast<uint8_t>(decodeHubMetadata(bytes,sizeof(bytes),output)));
    bytes[4]=1; bytes[6]=23; refreshCrc(bytes);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::BAD_RECORD_LENGTH),static_cast<uint8_t>(decodeHubMetadata(bytes,sizeof(bytes),output)));
    bytes[6]=24; writeUint32Le(bytes+12,0); refreshCrc(bytes);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::INVALID_METADATA),static_cast<uint8_t>(decodeHubMetadata(bytes,sizeof(bytes),output)));
    TEST_ASSERT_EQUAL_HEX32(0xAAAAAAAAU,output.generation);
}

void testCanonicalFreeGoldenVectorAndNonzeroGeneration() {
    NodeRecord input = makeFree(7); uint8_t encoded[NODE_RECORD_SIZE] = {};
    TEST_ASSERT_EQUAL_UINT8(0, static_cast<uint8_t>(encodeNodeRecord(input, encoded, sizeof(encoded))));
    const uint8_t golden[NODE_RECORD_SIZE] = {
        0x45,0x56,0x54,0x31,0x01,0x00,0x38,0x00,
        0x07,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x2C,0x76,0x0F,0x3E
    };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(golden, encoded, sizeof(golden));
    NodeRecord output = {};
    TEST_ASSERT_EQUAL_UINT8(0, static_cast<uint8_t>(decodeNodeRecord(encoded, sizeof(encoded), output)));
    TEST_ASSERT_EQUAL_UINT32(7, output.generation);
    input.eventId = 1;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::INVALID_EVENT), static_cast<uint8_t>(encodeNodeRecord(input, encoded, sizeof(encoded))));
}

void testNodeEventStatesFamiliesAttemptsAndLifetimes() {
    const NodeState states[] = {NodeState::QUEUED, NodeState::FAILED, NodeState::EXPIRED};
    const uint8_t families[] = {0x40, 0x41, 0x44};
    for (NodeState state : states) {
        for (uint8_t family : families) {
            NodeRecord value = makeNode(state); value.family = family;
            if (family == 0x41) { value.bodyLength=8; value.body[0]=1; value.body[1]=0; value.body[2]=2; value.body[3]=1; value.body[7]=1; }
            if (family == 0x44) value.body[0]=1;
            for (uint8_t attempts=0; attempts<=5; ++attempts) {
                value.attemptsUsed=attempts; uint8_t bytes[NODE_RECORD_SIZE]={}; NodeRecord out={};
                TEST_ASSERT_EQUAL_UINT8(0, static_cast<uint8_t>(encodeNodeRecord(value, bytes, sizeof(bytes))));
                TEST_ASSERT_EQUAL_UINT8(0, static_cast<uint8_t>(decodeNodeRecord(bytes, sizeof(bytes), out)));
            }
        }
    }
    NodeRecord bad=makeNode(); uint8_t bytes[NODE_RECORD_SIZE]={};
    bad.attemptsUsed=6; TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::INVALID_ATTEMPTS), static_cast<uint8_t>(encodeNodeRecord(bad,bytes,sizeof(bytes))));
    bad=makeNode(); bad.remainingActiveSeconds=bad.lifetimeBudgetSeconds+1;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::INVALID_LIFETIME), static_cast<uint8_t>(encodeNodeRecord(bad,bytes,sizeof(bytes))));
    bad=makeNode(); bad.state=static_cast<NodeState>(4);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::INVALID_STATE), static_cast<uint8_t>(encodeNodeRecord(bad,bytes,sizeof(bytes))));
}

void testNodeDecodeRejectsMalformedEnvelopeSemanticsTailAndPreservesOutput() {
    NodeRecord input=makeNode(); uint8_t bytes[NODE_RECORD_SIZE]={};
    TEST_ASSERT_EQUAL_UINT8(0, static_cast<uint8_t>(encodeNodeRecord(input,bytes,sizeof(bytes))));
    NodeRecord output=makeFree(0xA5A5A5A5U);
    bytes[37]=1; refreshCrc(bytes);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::NONZERO_BODY_TAIL), static_cast<uint8_t>(decodeNodeRecord(bytes,sizeof(bytes),output)));
    bytes[37]=0; bytes[33]=1; refreshCrc(bytes);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::NONZERO_RESERVED), static_cast<uint8_t>(decodeNodeRecord(bytes,sizeof(bytes),output)));
    bytes[33]=0; bytes[14]=0x42; refreshCrc(bytes);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::INVALID_EVENT), static_cast<uint8_t>(decodeNodeRecord(bytes,sizeof(bytes),output)));
    TEST_ASSERT_EQUAL_HEX32(0xA5A5A5A5U,output.generation);
    bytes[52]^=1;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::BAD_CRC), static_cast<uint8_t>(decodeNodeRecord(bytes,sizeof(bytes),output)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::WRONG_LENGTH), static_cast<uint8_t>(decodeNodeRecord(bytes,sizeof(bytes)-1,output)));
}

void testNodeRecordRejectsEveryEnvelopeClassAndEncodeTail() {
    NodeRecord value=makeNode(); uint8_t bytes[NODE_RECORD_SIZE]={}; NodeRecord output=makeFree(0xA5A5A5A5U);
    TEST_ASSERT_EQUAL_UINT8(0,static_cast<uint8_t>(encodeNodeRecord(value,bytes,sizeof(bytes))));
    bytes[0]^=1; refreshCrc(bytes);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::BAD_MAGIC),static_cast<uint8_t>(decodeNodeRecord(bytes,sizeof(bytes),output)));
    bytes[0]^=1; bytes[4]=2; refreshCrc(bytes);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::BAD_SCHEMA),static_cast<uint8_t>(decodeNodeRecord(bytes,sizeof(bytes),output)));
    bytes[4]=1; bytes[6]=55; refreshCrc(bytes);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::BAD_RECORD_LENGTH),static_cast<uint8_t>(decodeNodeRecord(bytes,sizeof(bytes),output)));
    value.body[1]=1;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::NONZERO_BODY_TAIL),static_cast<uint8_t>(encodeNodeRecord(value,bytes,sizeof(bytes))));
    TEST_ASSERT_EQUAL_HEX32(0xA5A5A5A5U,output.generation);
}

void testHubActiveGoldenAndConsumedRoundTrip() {
    HubRecord input=makeHub(); uint8_t encoded[HUB_RECORD_SIZE]={};
    TEST_ASSERT_EQUAL_UINT8(0,static_cast<uint8_t>(encodeHubRecord(input,encoded,sizeof(encoded))));
    const uint8_t golden[HUB_RECORD_SIZE]={
        0x48,0x45,0x56,0x31,0x01,0x00,0x38,0x00,0x09,0x00,0x00,0x00,
        0x01,0x10,0x40,0x01,0x01,0x00,0x00,0x00,0x44,0x33,0x22,0x11,
        0x88,0x77,0x66,0x55,0x3C,0x00,0x00,0x00,0x03,0x00,0x00,0x00,
        0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x50,0x69,0x00,0xC8};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(golden,encoded,sizeof(golden));
    HubRecord output={}; TEST_ASSERT_EQUAL_UINT8(0,static_cast<uint8_t>(decodeHubRecord(encoded,sizeof(encoded),output)));
    input.state=HubState::CONSUMED; TEST_ASSERT_EQUAL_UINT8(0,static_cast<uint8_t>(encodeHubRecord(input,encoded,sizeof(encoded))));
    TEST_ASSERT_EQUAL_UINT8(0,static_cast<uint8_t>(decodeHubRecord(encoded,sizeof(encoded),output)));
}

void testHubEveryRegisteredFamilyCanonicalValidation() {
    const uint8_t families[]={0x40,0x41,0x44};
    for(uint8_t family:families){
        HubRecord value=makeHub(); value.family=family;
        if(family==0x41){value.bodyLength=8;value.body[0]=1;value.body[1]=0;value.body[2]=2;value.body[3]=1;value.body[7]=1;}
        if(family==0x44)value.body[0]=1;
        uint8_t bytes[HUB_RECORD_SIZE]={}; HubRecord output={};
        TEST_ASSERT_EQUAL_UINT8(0,static_cast<uint8_t>(encodeHubRecord(value,bytes,sizeof(bytes))));
        TEST_ASSERT_EQUAL_UINT8(0,static_cast<uint8_t>(decodeHubRecord(bytes,sizeof(bytes),output)));
    }
}

void testHubEmptyAndInvalidContentRules() {
    HubRecord value=makeEmpty(4); uint8_t bytes[HUB_RECORD_SIZE]={}; HubRecord output={};
    TEST_ASSERT_EQUAL_UINT8(0,static_cast<uint8_t>(encodeHubRecord(value,bytes,sizeof(bytes))));
    TEST_ASSERT_EQUAL_UINT8(0,static_cast<uint8_t>(decodeHubRecord(bytes,sizeof(bytes),output)));
    value.admissionOrdinal=1;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::INVALID_EVENT),static_cast<uint8_t>(encodeHubRecord(value,bytes,sizeof(bytes))));
    value=makeHub(); value.admissionOrdinal=0;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::INVALID_EVENT),static_cast<uint8_t>(encodeHubRecord(value,bytes,sizeof(bytes))));
    value=makeHub(); value.state=static_cast<HubState>(3);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::INVALID_STATE),static_cast<uint8_t>(encodeHubRecord(value,bytes,sizeof(bytes))));
}

void testHubRejectsTailReservedFamilyAndPreservesOutput() {
    HubRecord value=makeHub(); uint8_t bytes[HUB_RECORD_SIZE]={};
    TEST_ASSERT_EQUAL_UINT8(0,static_cast<uint8_t>(encodeHubRecord(value,bytes,sizeof(bytes))));
    HubRecord output=makeEmpty(0xA5A5A5A5U);
    bytes[37]=1; refreshCrc(bytes);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::NONZERO_BODY_TAIL),static_cast<uint8_t>(decodeHubRecord(bytes,sizeof(bytes),output)));
    bytes[37]=0; bytes[17]=1; refreshCrc(bytes);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::NONZERO_RESERVED),static_cast<uint8_t>(decodeHubRecord(bytes,sizeof(bytes),output)));
    bytes[17]=0; bytes[14]=0x42; refreshCrc(bytes);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::INVALID_EVENT),static_cast<uint8_t>(decodeHubRecord(bytes,sizeof(bytes),output)));
    TEST_ASSERT_EQUAL_HEX32(0xA5A5A5A5U,output.generation);
}

void testHubRecordRejectsEveryEnvelopeClassAndEncodeTail() {
    HubRecord value=makeHub(); uint8_t bytes[HUB_RECORD_SIZE]={}; HubRecord output=makeEmpty(0xA5A5A5A5U);
    TEST_ASSERT_EQUAL_UINT8(0,static_cast<uint8_t>(encodeHubRecord(value,bytes,sizeof(bytes))));
    bytes[0]^=1; refreshCrc(bytes);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::BAD_MAGIC),static_cast<uint8_t>(decodeHubRecord(bytes,sizeof(bytes),output)));
    bytes[0]^=1; bytes[4]=2; refreshCrc(bytes);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::BAD_SCHEMA),static_cast<uint8_t>(decodeHubRecord(bytes,sizeof(bytes),output)));
    bytes[4]=1; bytes[6]=55; refreshCrc(bytes);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::BAD_RECORD_LENGTH),static_cast<uint8_t>(decodeHubRecord(bytes,sizeof(bytes),output)));
    bytes[6]=56; refreshCrc(bytes); bytes[52]^=1;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::BAD_CRC),static_cast<uint8_t>(decodeHubRecord(bytes,sizeof(bytes),output)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::WRONG_LENGTH),static_cast<uint8_t>(decodeHubRecord(bytes,sizeof(bytes)-1,output)));
    value.body[1]=1;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::NONZERO_BODY_TAIL),static_cast<uint8_t>(encodeHubRecord(value,bytes,sizeof(bytes))));
    TEST_ASSERT_EQUAL_HEX32(0xA5A5A5A5U,output.generation);
}

void testGenerationOrderingIncludingWrapAndHalfRange() {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(GenerationOrder::EQUAL),static_cast<uint8_t>(compareGenerations(7,7)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(GenerationOrder::LEFT_NEWER),static_cast<uint8_t>(compareGenerations(8,7)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(GenerationOrder::RIGHT_NEWER),static_cast<uint8_t>(compareGenerations(7,8)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(GenerationOrder::LEFT_NEWER),static_cast<uint8_t>(compareGenerations(0,UINT32_MAX)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(GenerationOrder::AMBIGUOUS),static_cast<uint8_t>(compareGenerations(0x80000000U,0)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(GenerationOrder::LEFT_NEWER),static_cast<uint8_t>(compareGenerations(0x7FFFFFFFU,0)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(GenerationOrder::RIGHT_NEWER),static_cast<uint8_t>(compareGenerations(0x80000001U,0)));
}

void testCopySelectionAllClassifications() {
    uint8_t a[NODE_RECORD_SIZE]={},b[NODE_RECORD_SIZE]={};
    NodeRecord ra=makeFree(10),rb=makeFree(9); encodeNodeRecord(ra,a,sizeof(a)); encodeNodeRecord(rb,b,sizeof(b));
    FixedCopy<NODE_RECORD_SIZE> ca=readable(a),cb=readable(b); NodeRecord selected={}; CopySlot slot=CopySlot::B;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SelectionResult::NEWEST_A),static_cast<uint8_t>(selectCopies(ca,cb,decodeNodeRecordForSelection,selected,slot)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CopySlot::A),static_cast<uint8_t>(slot));
    ra.generation=8; encodeNodeRecord(ra,a,sizeof(a)); ca=readable(a);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SelectionResult::NEWEST_B),static_cast<uint8_t>(selectCopies(ca,cb,decodeNodeRecordForSelection,selected,slot)));
    ca.bytes[52]^=1;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SelectionResult::ONLY_B_VALID),static_cast<uint8_t>(selectCopies(ca,cb,decodeNodeRecordForSelection,selected,slot)));
    ca=readable(a); cb.bytes[52]^=1;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SelectionResult::ONLY_A_VALID),static_cast<uint8_t>(selectCopies(ca,cb,decodeNodeRecordForSelection,selected,slot)));
    ca.bytes[52]^=1;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SelectionResult::BOTH_INVALID),static_cast<uint8_t>(selectCopies(ca,cb,decodeNodeRecordForSelection,selected,slot)));
    ca=readable(a); cb=readable(a);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SelectionResult::EQUAL_IDENTICAL),static_cast<uint8_t>(selectCopies(ca,cb,decodeNodeRecordForSelection,selected,slot)));
    rb=makeFree(8); rb.state=NodeState::QUEUED; rb.flags=1; rb.family=0x40; rb.bodyLength=1; rb.eventEpoch=1; rb.eventId=1; rb.lifetimeBudgetSeconds=60; rb.remainingActiveSeconds=60; rb.body[0]=1; encodeNodeRecord(rb,b,sizeof(b)); cb=readable(b);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SelectionResult::EQUAL_DISAGREEMENT),static_cast<uint8_t>(selectCopies(ca,cb,decodeNodeRecordForSelection,selected,slot)));
    ra=makeFree(0);rb=makeFree(0x80000000U);encodeNodeRecord(ra,a,sizeof(a));encodeNodeRecord(rb,b,sizeof(b));ca=readable(a);cb=readable(b);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SelectionResult::GENERATION_AMBIGUOUS),static_cast<uint8_t>(selectCopies(ca,cb,decodeNodeRecordForSelection,selected,slot)));
    ra=makeFree(0);rb=makeFree(UINT32_MAX);encodeNodeRecord(ra,a,sizeof(a));encodeNodeRecord(rb,b,sizeof(b));ca=readable(a);cb=readable(b);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SelectionResult::NEWEST_A),static_cast<uint8_t>(selectCopies(ca,cb,decodeNodeRecordForSelection,selected,slot)));
    ca.status=FixedReadStatus::MISSING;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SelectionResult::ONLY_B_VALID),static_cast<uint8_t>(selectCopies(ca,cb,decodeNodeRecordForSelection,selected,slot)));
}

void testReadBackResults() {
    uint8_t bytes[NODE_METADATA_SIZE]={}; NodeMetadata m={1,1,1,1}; encodeNodeMetadata(m,bytes,sizeof(bytes));
    FixedCopy<NODE_METADATA_SIZE> copy=readable(bytes);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ReadBackResult::MATCH),static_cast<uint8_t>(classifyReadBack(bytes,copy)));
    copy.bytes[0]^=1;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ReadBackResult::MISMATCH),static_cast<uint8_t>(classifyReadBack(bytes,copy)));
    copy.status=FixedReadStatus::MISSING;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ReadBackResult::MISSING),static_cast<uint8_t>(classifyReadBack(bytes,copy)));
    copy.status=FixedReadStatus::UNAVAILABLE;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ReadBackResult::UNAVAILABLE),static_cast<uint8_t>(classifyReadBack(bytes,copy)));
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(testCrcMatchesStandardCheckValueAndSettingsConvention);
    RUN_TEST(testNodeMetadataGoldenRoundTripAndGeometry);
    RUN_TEST(testNodeMetadataRejectsEnvelopeReservedAndZeroFieldsWithoutMutation);
    RUN_TEST(testHubMetadataGoldenRoundTripAndBoundaries);
    RUN_TEST(testHubMetadataRejectsReservedCrcLengthAndPreservesOutput);
    RUN_TEST(testHubMetadataRejectsMagicSchemaRecordLengthAndZeroOrdinal);
    RUN_TEST(testCanonicalFreeGoldenVectorAndNonzeroGeneration);
    RUN_TEST(testNodeEventStatesFamiliesAttemptsAndLifetimes);
    RUN_TEST(testNodeDecodeRejectsMalformedEnvelopeSemanticsTailAndPreservesOutput);
    RUN_TEST(testNodeRecordRejectsEveryEnvelopeClassAndEncodeTail);
    RUN_TEST(testHubActiveGoldenAndConsumedRoundTrip);
    RUN_TEST(testHubEveryRegisteredFamilyCanonicalValidation);
    RUN_TEST(testHubEmptyAndInvalidContentRules);
    RUN_TEST(testHubRejectsTailReservedFamilyAndPreservesOutput);
    RUN_TEST(testHubRecordRejectsEveryEnvelopeClassAndEncodeTail);
    RUN_TEST(testGenerationOrderingIncludingWrapAndHalfRange);
    RUN_TEST(testCopySelectionAllClassifications);
    RUN_TEST(testReadBackResults);
    return UNITY_END();
}
