#include <unity.h>

#include "simulated_capabilities.h"
#include "wire_transaction_state.h"

namespace {

using namespace WireOperations;

constexpr uint8_t HUB = 1;
constexpr uint8_t NODE = 16;
constexpr uint8_t SEQ = 0x2A;

Value none() {
    Value value = {};
    value.type = static_cast<uint8_t>(ValueType::NONE);
    return value;
}

Value u32(uint32_t bits) {
    Value value = {};
    value.type = static_cast<uint8_t>(ValueType::UNSIGNED_32);
    value.length = 4;
    HostProtocol::writeUint32Le(value.bytes, bits);
    return value;
}

Request request(uint8_t opcode, uint16_t target = 0) {
    (void)opcode;
    Request value = {};
    value.targetId = target;
    value.value = none();
    return value;
}

Response ok(uint8_t opcode, uint16_t target = 0) {
    Response response = {};
    response.status = OperationStatus::OK;
    response.targetId = target;
    response.value = none();
    if (opcode == static_cast<uint8_t>(Protocol::Opcode::PING)) {
        response.value = u32(0x12345678);
    }
    return response;
}

Protocol::Packet command(uint8_t opcode, const Request& value) {
    return makeCommand(HUB, NODE, SEQ, opcode, value);
}

void testNumericAuthoritiesAndBounds() {
    TEST_ASSERT_EQUAL_UINT8(1, Protocol::VERSION);
    TEST_ASSERT_EQUAL_UINT32(6, Protocol::HEADER_SIZE);
    TEST_ASSERT_EQUAL_UINT32(32, Protocol::MAX_PACKET_SIZE);
    TEST_ASSERT_EQUAL_UINT32(26, Protocol::MAX_PAYLOAD_SIZE);
    TEST_ASSERT_EQUAL_UINT32(22, MAX_REQUEST_VALUE_SIZE);
    TEST_ASSERT_EQUAL_UINT32(21, MAX_RESPONSE_VALUE_SIZE);
    TEST_ASSERT_EQUAL_HEX8(0x04, static_cast<uint8_t>(Protocol::PacketType::RESPONSE));
    for (uint8_t index = 0; index < 9; ++index) {
        TEST_ASSERT_EQUAL_HEX8(0x20 + index,
            static_cast<uint8_t>(Protocol::Opcode::PING) + index);
    }
    TEST_ASSERT_EQUAL_HEX8(0x64, Protocol::OPCODE_TEST);
    TEST_ASSERT_EQUAL_HEX8(0, static_cast<uint8_t>(Protocol::AckStatus::SUCCESS));
    TEST_ASSERT_EQUAL_HEX8(1, static_cast<uint8_t>(Protocol::AckStatus::UNSUPPORTED_OPCODE));
    TEST_ASSERT_EQUAL_HEX8(2, static_cast<uint8_t>(Protocol::AckStatus::MALFORMED_PACKET));
}

void testEveryRequestSchemaAndLittleEndian() {
    const uint8_t opcodes[] = {0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28};
    for (uint8_t opcode : opcodes) {
        Request value = request(opcode,
            (opcode >= 0x24 && opcode <= 0x27) ? 0x1234 : 0);
        if (opcode == 0x26) {
            value.value.type = static_cast<uint8_t>(ValueType::BOOLEAN);
            value.value.length = 1; value.value.bytes[0] = 1;
        }
        uint8_t payload[Protocol::MAX_PAYLOAD_SIZE] = {};
        size_t length = 0;
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::OK),
            static_cast<uint8_t>(encodeRequest(opcode, value, payload,
                sizeof(payload), length)));
        TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(value.targetId), payload[0]);
        TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(value.targetId >> 8), payload[1]);
        Request decoded = {};
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::OK),
            static_cast<uint8_t>(decodeRequest(opcode, payload, length, decoded)));
        TEST_ASSERT_EQUAL_HEX16(value.targetId, decoded.targetId);
    }
}

void testRequestMalformedCasesFailClosed() {
    Request ping = request(0x20);
    uint8_t payload[Protocol::MAX_PAYLOAD_SIZE + 1] = {};
    size_t length = 0;
    ping.targetId = 1;
    TEST_ASSERT_NOT_EQUAL(static_cast<uint8_t>(CodecResult::OK),
        static_cast<uint8_t>(encodeRequest(0x20, ping, payload, sizeof(payload), length)));
    ping.targetId = 0;
    ping.value.type = STRUCTURE_VALUE_TYPE;
    TEST_ASSERT_NOT_EQUAL(static_cast<uint8_t>(CodecResult::OK),
        static_cast<uint8_t>(encodeRequest(0x20, ping, payload, sizeof(payload), length)));
    const uint8_t unknown[] = {0,0,0x7E,0};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::INVALID_VALUE_TYPE),
        static_cast<uint8_t>(decodeRequest(0x20, unknown, sizeof(unknown), ping)));
    const uint8_t wrongWidth[] = {0,0,2,3,1,2,3};
    TEST_ASSERT_NOT_EQUAL(static_cast<uint8_t>(CodecResult::OK),
        static_cast<uint8_t>(decodeRequest(0x23, wrongWidth, sizeof(wrongWidth), ping)));
    const uint8_t trailing[] = {0,0,0,0,1};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::INVALID_LENGTH),
        static_cast<uint8_t>(decodeRequest(0x20, trailing, sizeof(trailing), ping)));
}

void testResponseSchemasStatusesAndMaximumRecords() {
    Response response = ok(0x20);
    uint8_t payload[Protocol::MAX_PAYLOAD_SIZE] = {};
    size_t length = 0;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::OK),
        static_cast<uint8_t>(encodeResponse(0x20, response, payload,
            sizeof(payload), length)));
    TEST_ASSERT_EQUAL_UINT32(9, length);
    for (uint8_t status = 1; status <= 10; ++status) {
        response.status = static_cast<OperationStatus>(status);
        response.value = none();
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::OK),
            static_cast<uint8_t>(encodeResponse(0x20, response, payload,
                sizeof(payload), length)));
    }
    response.status = static_cast<OperationStatus>(0xFE);
    TEST_ASSERT_NOT_EQUAL(static_cast<uint8_t>(CodecResult::OK),
        static_cast<uint8_t>(encodeResponse(0x20, response, payload,
            sizeof(payload), length)));

    response = {};
    response.status = OperationStatus::OK;
    response.value.type = STRUCTURE_VALUE_TYPE;
    response.value.length = 21;
    response.value.bytes[2] = 9;
    for (uint8_t i = 0; i < 9; ++i)
        HostProtocol::writeUint16Le(response.value.bytes + 3 + 2*i, i + 1);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::OK),
        static_cast<uint8_t>(encodeResponse(0x23, response, payload,
            sizeof(payload), length)));
    TEST_ASSERT_EQUAL_UINT32(26, length);
}

void testFixedResponseRecordsAndMalformedResponse() {
    const uint8_t ops[] = {0x21,0x22,0x24,0x28};
    const uint8_t sizes[] = {8,10,6,17};
    for (uint8_t i = 0; i < 4; ++i) {
        Response response = {};
        response.status = OperationStatus::OK;
        response.targetId = ops[i] == 0x24 ? 1 : 0;
        response.value.type = STRUCTURE_VALUE_TYPE;
        response.value.length = sizes[i];
        if (ops[i] == 0x21) { response.value.bytes[5]=1; response.value.bytes[6]=1; }
        if (ops[i] == 0x24) { response.value.bytes[0]=4; response.value.bytes[1]=1;
            response.value.bytes[2]=5; response.value.bytes[3]=1; response.value.bytes[4]=1; }
        if (ops[i] == 0x28) response.value.bytes[1]=3;
        uint8_t payload[26] = {}; size_t length = 0;
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::OK),
            static_cast<uint8_t>(encodeResponse(ops[i], response, payload, sizeof(payload), length)));
        Response decoded = {};
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::OK),
            static_cast<uint8_t>(decodeResponse(ops[i], payload, length, decoded)));
        payload[0] = 0xFE;
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CodecResult::INVALID_STATUS),
            static_cast<uint8_t>(decodeResponse(ops[i], payload, length, decoded)));
    }
}

void testExactWireVectors() {
    Protocol::Packet ping = command(0x20, request(0x20));
    uint8_t bytes[32] = {}; size_t length = 0;
    TEST_ASSERT_TRUE(Protocol::encode(ping, bytes, sizeof(bytes), length));
    const uint8_t pingExpected[] = {0x11,1,16,0x2A,0x20,4,0,0,0,0};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(pingExpected, bytes, sizeof(pingExpected));
    Protocol::Packet ack = TransactionEngine::makeAcknowledgment(
        ping, Protocol::AckStatus::SUCCESS);
    Protocol::encode(ack, bytes, sizeof(bytes), length);
    const uint8_t ackExpected[] = {0x12,16,1,0x2A,0x20,1,0};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ackExpected, bytes, sizeof(ackExpected));
    Protocol::Packet reply = makeResponsePacket(ping, ok(0x20));
    Protocol::encode(reply, bytes, sizeof(bytes), length);
    const uint8_t responseExpected[] = {
        0x14,16,1,0x2A,0x20,9,0,0,0,2,4,0x78,0x56,0x34,0x12};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(responseExpected, bytes, sizeof(responseExpected));

    Request read = request(0x25, 0x1234);
    Protocol::Packet readPacket = command(0x25, read);
    Protocol::encode(readPacket, bytes, sizeof(bytes), length);
    const uint8_t readExpected[] = {0x11,1,16,0x2A,0x25,4,0x34,0x12,0,0};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(readExpected, bytes, sizeof(readExpected));

    Protocol::Packet info = command(0x21, request(0x21));
    Protocol::encode(info, bytes, sizeof(bytes), length);
    const uint8_t infoExpected[] = {0x11,1,16,0x2A,0x21,4,0,0,0,0};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(infoExpected, bytes, sizeof(infoExpected));
    Response infoResponse = {};
    infoResponse.status = OperationStatus::OK;
    infoResponse.value.type = STRUCTURE_VALUE_TYPE;
    infoResponse.value.length = 8;
    const uint8_t infoRecord[] = {0,5,1,1,1,1,1,16};
    for (uint8_t i = 0; i < 8; ++i) infoResponse.value.bytes[i] = infoRecord[i];
    Protocol::Packet infoReply = makeResponsePacket(info, infoResponse);
    Protocol::encode(infoReply, bytes, sizeof(bytes), length);
    const uint8_t infoResponseExpected[] = {
        0x14,16,1,0x2A,0x21,13,0,0,0,0x7F,8,0,5,1,1,1,1,1,16};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(infoResponseExpected, bytes,
        sizeof(infoResponseExpected));

    Request set = request(0x26, 0x1234);
    set.value.type = static_cast<uint8_t>(ValueType::BOOLEAN);
    set.value.length = 1; set.value.bytes[0] = 1;
    Protocol::encode(command(0x26, set), bytes, sizeof(bytes), length);
    const uint8_t setExpected[] = {
        0x11,1,16,0x2A,0x26,5,0x34,0x12,1,1,1};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(setExpected, bytes, sizeof(setExpected));

    Request diagnostic = request(0x28);
    diagnostic.value = u32(3);
    Protocol::encode(command(0x28, diagnostic), bytes, sizeof(bytes), length);
    const uint8_t diagnosticExpected[] = {
        0x11,1,16,0x2A,0x28,8,0,0,2,4,3,0,0,0};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(diagnosticExpected, bytes,
        sizeof(diagnosticExpected));
}

void testAdmissionBeforeCacheAndDuplicateRetention() {
    NodeRetainedOperation retained;
    Protocol::Packet ping = command(0x20, request(0x20));
    Protocol::Packet malformed = ping; malformed.payloadLength = 3;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AdmissionOutcome::MALFORMED),
        static_cast<uint8_t>(admitNodeCommand(malformed, NODE, HUB, retained).outcome));
    TEST_ASSERT_EQUAL_UINT32(0, retained.lookupCount());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AdmissionOutcome::ADMITTED),
        static_cast<uint8_t>(admitNodeCommand(ping, NODE, HUB, retained).outcome));
    TEST_ASSERT_EQUAL_UINT32(1, retained.lookupCount());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AdmissionOutcome::DUPLICATE_PENDING),
        static_cast<uint8_t>(admitNodeCommand(ping, NODE, HUB, retained).outcome));
    Protocol::Packet reply = makeResponsePacket(ping, ok(0x20));
    TEST_ASSERT_TRUE(retained.complete(reply));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AdmissionOutcome::DUPLICATE_COMPLETE),
        static_cast<uint8_t>(admitNodeCommand(ping, NODE, HUB, retained).outcome));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Protocol::PacketType::RESPONSE),
        static_cast<uint8_t>(retained.response().type));
    retained.reset();
    TEST_ASSERT_FALSE(retained.hasEntry());
}

void testActiveCacheCannotBeOverwrittenAndIdentityIsCanonical() {
    NodeRetainedOperation retained;
    Protocol::Packet first = command(0x20, request(0x20));
    admitNodeCommand(first, NODE, HUB, retained);
    Protocol::Packet different = first;
    different.sequence++;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AdmissionOutcome::ACTIVE_BUSY),
        static_cast<uint8_t>(admitNodeCommand(different, NODE, HUB, retained).outcome));
    different = first; different.source = 2;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AdmissionOutcome::IGNORE),
        static_cast<uint8_t>(admitNodeCommand(different, NODE, HUB, retained).outcome));
    different = first; different.opcode = 0x21;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AdmissionOutcome::ACTIVE_BUSY),
        static_cast<uint8_t>(admitNodeCommand(different, NODE, HUB, retained).outcome));
    TEST_ASSERT_FALSE(retained.isDuplicate(different));
    TEST_ASSERT_TRUE(retained.isDuplicate(first));
}

void testHubAckResponseAndResponseBeforeAck() {
    Protocol::Packet ping = command(0x20, request(0x20));
    HubStructuredTransaction state;
    TEST_ASSERT_TRUE(state.begin(ping, 100, 1000));
    const uint32_t deadline = state.deadline();
    Protocol::Packet ack = TransactionEngine::makeAcknowledgment(
        ping, Protocol::AckStatus::SUCCESS);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StructuredEvent::ACK_ACCEPTED),
        static_cast<uint8_t>(state.receive(ack)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StructuredPhase::AWAITING_RESPONSE),
        static_cast<uint8_t>(state.phase()));
    TEST_ASSERT_EQUAL_UINT32(deadline, state.deadline());
    Protocol::Packet reply = makeResponsePacket(ping, ok(0x20));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StructuredEvent::RESPONSE_COMPLETE),
        static_cast<uint8_t>(state.receive(reply)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PeerSupport::SUPPORTED),
        static_cast<uint8_t>(state.peerSupport()));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StructuredEvent::MISMATCH_IGNORED),
        static_cast<uint8_t>(state.receive(reply)));

    HubStructuredTransaction reordered;
    reordered.begin(ping, 0, 1000);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StructuredEvent::RESPONSE_COMPLETE),
        static_cast<uint8_t>(reordered.receive(reply)));
}

void testHubMalformedMismatchRetryDeadlineAndTimeout() {
    Protocol::Packet ping = command(0x20, request(0x20));
    HubStructuredTransaction state;
    state.begin(ping, 10, 100, 1);
    const uint32_t deadline = state.deadline();
    Protocol::Packet wrong = makeResponsePacket(ping, ok(0x20));
    wrong.sequence++;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StructuredEvent::MISMATCH_IGNORED),
        static_cast<uint8_t>(state.receive(wrong)));
    Protocol::Packet malformed = makeResponsePacket(ping, ok(0x20));
    malformed.payload[0] = 0xFE;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StructuredEvent::MALFORMED_IGNORED),
        static_cast<uint8_t>(state.receive(malformed)));
    TEST_ASSERT_EQUAL_UINT32(deadline, state.deadline());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StructuredEvent::RETRANSMIT),
        static_cast<uint8_t>(state.service(deadline)));
    TEST_ASSERT_EQUAL_UINT8(SEQ, state.command().sequence);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ping.payload, state.command().payload,
        ping.payloadLength);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StructuredEvent::TIMEOUT),
        static_cast<uint8_t>(state.service(deadline)));
}

void testPingBootstrapAndLegacyBehavior() {
    TEST_ASSERT_TRUE(mayStartStructuredOperation(PeerSupport::UNKNOWN, 0x20));
    TEST_ASSERT_FALSE(mayStartStructuredOperation(PeerSupport::UNKNOWN, 0x21));
    TEST_ASSERT_TRUE(mayStartStructuredOperation(PeerSupport::UNKNOWN, 0x21, true));
    HubStructuredTransaction state;
    Protocol::Packet ping = command(0x20, request(0x20));
    state.begin(ping, 0, 100);
    Protocol::Packet ack = TransactionEngine::makeAcknowledgment(
        ping, Protocol::AckStatus::UNSUPPORTED_OPCODE);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StructuredEvent::REMOTE_REJECTED),
        static_cast<uint8_t>(state.receive(ack)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PeerSupport::UNSUPPORTED),
        static_cast<uint8_t>(state.peerSupport()));
    TransactionEngine::NodeDuplicateTracker legacy;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TransactionEngine::NodeCommandOutcome::ACK_UNSUPPORTED_OPCODE),
        static_cast<uint8_t>(TransactionEngine::evaluateNodeCommand(
            ping, NODE, HUB, legacy).outcome));
}

void testRemoteAuthorizationPrecedesValueAndLocalRemainsSeparate() {
    SimulatedCapabilities::State state;
    SimulatedCapabilities::Handler handler(state);
    DeviceCapabilities::CapabilityDiagnostics diagnostics;
    DeviceCapabilities::CapabilityValue invalid = {};
    invalid.type = DeviceCapabilities::ValueType::BOOLEAN;
    invalid.bits = 2;
    DeviceCapabilities::OperationResult result = processRemoteCapability(
        SimulatedCapabilities::registryView(), handler,
        SimulatedCapabilities::APPLICATION_INDICATOR_ID,
        DeviceCapabilities::Operation::SET, invalid,
        DeviceCapabilities::InterlockState::CLEAR, diagnostics);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(OperationStatus::UNAUTHORIZED),
        static_cast<uint8_t>(result.status));
    DeviceCapabilities::CallerContext local = {};
    local.callerClass = DeviceCapabilities::CallerClass::HOST_LOCAL;
    TEST_ASSERT_TRUE(DeviceCapabilities::isOperationAuthorized(
        SimulatedCapabilities::DESCRIPTORS[0], DeviceCapabilities::Operation::SET, local));
    result = processRemoteCapability(SimulatedCapabilities::registryView(), handler,
        SimulatedCapabilities::APPLICATION_INDICATOR_ID,
        DeviceCapabilities::Operation::RUN_LOCAL_PROCEDURE, invalid,
        DeviceCapabilities::InterlockState::CLEAR, diagnostics);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(OperationStatus::UNAUTHORIZED),
        static_cast<uint8_t>(result.status));
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(testNumericAuthoritiesAndBounds);
    RUN_TEST(testEveryRequestSchemaAndLittleEndian);
    RUN_TEST(testRequestMalformedCasesFailClosed);
    RUN_TEST(testResponseSchemasStatusesAndMaximumRecords);
    RUN_TEST(testFixedResponseRecordsAndMalformedResponse);
    RUN_TEST(testExactWireVectors);
    RUN_TEST(testAdmissionBeforeCacheAndDuplicateRetention);
    RUN_TEST(testActiveCacheCannotBeOverwrittenAndIdentityIsCanonical);
    RUN_TEST(testHubAckResponseAndResponseBeforeAck);
    RUN_TEST(testHubMalformedMismatchRetryDeadlineAndTimeout);
    RUN_TEST(testPingBootstrapAndLegacyBehavior);
    RUN_TEST(testRemoteAuthorizationPrecedesValueAndLocalRemainsSeparate);
    return UNITY_END();
}
