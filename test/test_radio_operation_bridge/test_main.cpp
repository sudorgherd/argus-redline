#include <unity.h>

#include "radio_operation_bridge.h"
#include "simulated_capabilities.h"

namespace {

using namespace RadioOperationBridge;
using namespace HostProtocol;

constexpr uint8_t HUB = 1;
constexpr uint8_t NODE = 16;

bool simulatedAvailability(
    const void* context,
    DeviceCapabilities::CapabilityId id,
    bool& available
) {
    return SimulatedCapabilities::capabilityAvailability(
        *static_cast<const SimulatedCapabilities::State*>(context), id, available);
}

struct NodeFixture {
    SimulatedCapabilities::State capabilityState;
    SimulatedCapabilities::Handler handler;
    DeviceCapabilities::CapabilityDiagnostics diagnostics;
    RuntimeState::State runtime;
    HostOperationService::DeviceSnapshot snapshot;
    HostOperationService::AvailabilityProvider availability;
    NodeStructuredOperationProcessor processor;

    NodeFixture() : handler(capabilityState),
        runtime(RuntimeState::DeviceRole::NODE, NODE, HUB),
        snapshot(HostOperationService::makeDeviceSnapshot(runtime, 0x12345678)),
        availability(HostOperationService::makeAvailabilityProvider(
            &capabilityState, simulatedAvailability)) {}

    NodeResult execute() {
        return processor.executeAdmittedOperation(snapshot, true,
            SimulatedCapabilities::registryView(), handler,
            DeviceCapabilities::InterlockState::CLEAR, diagnostics,
            runtime, availability);
    }
};

OperationRequest request(
    OperationCategory category,
    OperationCode operation,
    uint16_t target = 0,
    uint8_t device = NODE
) {
    OperationRequest value = {};
    value.category = category;
    value.operation = operation;
    value.targetDeviceId = device;
    value.targetId = target;
    setNoneValue(value.value);
    return value;
}

HubResult submitSupported(
    HubStructuredOperationBridge& hub,
    const OperationRequest& operation,
    uint16_t requestId = 0x3456
) {
    hub.configurePeerSupport(WireOperations::PeerSupport::SUPPORTED);
    return hub.submit(requestId, operation, RuntimeState::DeviceRole::HUB,
        HUB, NODE, 100, 50, 1);
}

void assertHostStatus(
    const HubResult& result,
    ResultClass resultClass,
    uint8_t code
) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HubAction::HOST_RESPONSE_READY),
        static_cast<uint8_t>(result.action));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(resultClass),
        static_cast<uint8_t>(result.response.resultClass));
    TEST_ASSERT_EQUAL_UINT8(code, result.response.resultCode);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PayloadResult::OK),
        static_cast<uint8_t>(validateOperationResponse(result.response)));
}

HubResult runThroughNode(
    HubStructuredOperationBridge& hub,
    NodeFixture& node,
    const OperationRequest& operation,
    uint16_t requestId = 0x3456,
    bool responseBeforeAck = false
) {
    const HubResult submitted = submitSupported(hub, operation, requestId);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HubAction::TRANSMIT_COMMAND),
        static_cast<uint8_t>(submitted.action));
    const NodeResult admitted = node.processor.admit(submitted.packet, NODE, HUB);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NodeAction::ACK_THEN_EXECUTE),
        static_cast<uint8_t>(admitted.action));
    const NodeResult completed = node.execute();
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NodeAction::RESPONSE_READY),
        static_cast<uint8_t>(completed.action));
    if (responseBeforeAck) return hub.receive(completed.response);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HubAction::NONE),
        static_cast<uint8_t>(hub.receive(admitted.acknowledgment).action));
    return hub.receive(completed.response);
}

void testHubTargetClassificationSupportAndCorrelationBoundary() {
    HubStructuredOperationBridge hub;
    OperationRequest ping = request(OperationCategory::DEVICE,
        OperationCode::PING);
    HubResult result = hub.submit(0xBEEF, ping, RuntimeState::DeviceRole::HUB,
        HUB, NODE, 0, 10);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HubAction::TRANSMIT_COMMAND),
        static_cast<uint8_t>(result.action));
    uint8_t encoded[Protocol::MAX_PACKET_SIZE] = {};
    size_t encodedLength = 0;
    TEST_ASSERT_TRUE(Protocol::encode(result.packet, encoded,
        sizeof(encoded), encodedLength));
    for (size_t index = 0; index + 1 < encodedLength; ++index) {
        TEST_ASSERT_FALSE(encoded[index] == 0xEF && encoded[index + 1] == 0xBE);
    }
    hub.reset();
    ping.targetDeviceId = HUB;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HubAction::NOT_HANDLED),
        static_cast<uint8_t>(hub.submit(1, ping, RuntimeState::DeviceRole::HUB,
            HUB, NODE, 0, 10).action));
    ping.targetDeviceId = 99;
    assertHostStatus(hub.submit(1, ping, RuntimeState::DeviceRole::HUB,
        HUB, NODE, 0, 10), ResultClass::REQUEST_REJECTED,
        static_cast<uint8_t>(RequestRejectionCode::BAD_TARGET));
    ping.targetDeviceId = NODE;
    assertHostStatus(hub.submit(1, ping, RuntimeState::DeviceRole::NODE,
        HUB, NODE, 0, 10), ResultClass::REQUEST_REJECTED,
        static_cast<uint8_t>(RequestRejectionCode::BAD_TARGET));

    HubStructuredOperationBridge unknown;
    OperationRequest status = request(OperationCategory::DEVICE,
        OperationCode::GET_STATUS);
    assertHostStatus(unknown.submit(2, status, RuntimeState::DeviceRole::HUB,
        HUB, NODE, 0, 10), ResultClass::REQUEST_REJECTED,
        static_cast<uint8_t>(RequestRejectionCode::UNSUPPORTED_OPERATION));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HubAction::TRANSMIT_COMMAND),
        static_cast<uint8_t>(unknown.submit(3, status,
            RuntimeState::DeviceRole::HUB, HUB, NODE, 0, 10, 1, true).action));
}

void testPingVerticalSliceAckBeforeExecutionAndResponseBeforeAck() {
    HubStructuredOperationBridge hub;
    NodeFixture node;
    const OperationRequest ping = request(OperationCategory::DEVICE,
        OperationCode::PING);
    HubResult submitted = submitSupported(hub, ping, 0x1234);
    NodeResult admitted = node.processor.admit(submitted.packet, NODE, HUB);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NodeAction::ACK_THEN_EXECUTE),
        static_cast<uint8_t>(admitted.action));
    TEST_ASSERT_TRUE(node.processor.active());
    TEST_ASSERT_FALSE(node.processor.complete());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HubAction::NONE),
        static_cast<uint8_t>(hub.receive(admitted.acknowledgment).action));
    NodeResult completed = node.execute();
    TEST_ASSERT_TRUE(node.processor.complete());
    HubResult terminal = hub.receive(completed.response);
    assertHostStatus(terminal, ResultClass::OPERATION_RESULT,
        static_cast<uint8_t>(DeviceCapabilities::OperationStatus::OK));
    TEST_ASSERT_EQUAL_HEX16(0x1234, terminal.requestId);
    TEST_ASSERT_EQUAL_HEX32(0x12345678,
        readUint32Le(terminal.response.value.bytes));
    HubResult next = submitSupported(hub,
        request(OperationCategory::DEVICE, OperationCode::GET_STATUS), 0x1235);
    TEST_ASSERT_EQUAL_UINT8(1, next.packet.sequence);
    hub.reset();

    HubStructuredOperationBridge beforeAck;
    NodeFixture secondNode;
    terminal = runThroughNode(beforeAck, secondNode, ping, 0x2222, true);
    assertHostStatus(terminal, ResultClass::OPERATION_RESULT,
        static_cast<uint8_t>(DeviceCapabilities::OperationStatus::OK));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HubAction::NONE),
        static_cast<uint8_t>(beforeAck.receive(
            TransactionEngine::makeAcknowledgment(
                WireOperations::makeCommand(HUB, NODE, 0, 0x20,
                    WireOperations::Request{}), Protocol::AckStatus::SUCCESS)
            ).action));
}

void testDeviceAndMetadataOperationsRoundTrip() {
    const struct {
        OperationCategory category;
        OperationCode operation;
        uint16_t target;
        uint8_t expectedLength;
    } cases[] = {
        {OperationCategory::DEVICE, OperationCode::GET_DEVICE_INFO, 0, 8},
        {OperationCategory::DEVICE, OperationCode::GET_STATUS, 0, 10},
        {OperationCategory::CAPABILITY, OperationCode::GET_CAPABILITIES, 0, 9},
        {OperationCategory::CAPABILITY, OperationCode::DESCRIBE_CAPABILITY,
            SimulatedCapabilities::APPLICATION_INDICATOR_ID, 6},
        {OperationCategory::DIAGNOSTIC, OperationCode::GET_DIAGNOSTICS, 0, 17}
    };
    for (const auto& item : cases) {
        HubStructuredOperationBridge hub;
        NodeFixture node;
        const HubResult terminal = runThroughNode(hub, node,
            request(item.category, item.operation, item.target));
        assertHostStatus(terminal, ResultClass::OPERATION_RESULT,
            static_cast<uint8_t>(DeviceCapabilities::OperationStatus::OK));
        TEST_ASSERT_EQUAL_UINT8(STRUCTURE_VALUE_TYPE,
            terminal.response.value.type);
        TEST_ASSERT_EQUAL_UINT8(item.expectedLength,
            terminal.response.value.length);
    }

    HubStructuredOperationBridge hub;
    NodeFixture node;
    OperationRequest finalDiagnostics = request(OperationCategory::DIAGNOSTIC,
        OperationCode::GET_DIAGNOSTICS);
    setUint32Value(finalDiagnostics.value,
        DeviceCapabilities::ValueType::UNSIGNED_32, 9);
    const HubResult terminal = runThroughNode(hub, node, finalDiagnostics);
    HostProtocol::DiagnosticPageRecord page = {};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PayloadResult::OK),
        static_cast<uint8_t>(decodeDiagnosticPageRecord(
            terminal.response.value, page)));
    TEST_ASSERT_EQUAL_UINT8(1, page.count);
    TEST_ASSERT_EQUAL_HEX8(0x0A, page.entries[0].metricId);
    TEST_ASSERT_EQUAL_HEX8(DIAGNOSTIC_PAGE_END, page.nextCursor);
}

void testRemoteCapabilityPolicyAndProcedureDenial() {
    const struct {
        OperationCategory category;
        OperationCode operation;
        uint16_t target;
    } cases[] = {
        {OperationCategory::CAPABILITY, OperationCode::READ_CAPABILITY,
            SimulatedCapabilities::DIGITAL_INPUT_ID},
        {OperationCategory::CAPABILITY, OperationCode::SET_INDICATOR,
            SimulatedCapabilities::APPLICATION_INDICATOR_ID},
        {OperationCategory::PROCEDURE, OperationCode::RUN_PROCEDURE, 1}
    };
    for (const auto& item : cases) {
        HubStructuredOperationBridge hub;
        NodeFixture node;
        OperationRequest operation = request(item.category, item.operation,
            item.target);
        if (item.operation == OperationCode::SET_INDICATOR) {
            setBooleanValue(operation.value, 1);
        }
        HubResult terminal = runThroughNode(hub, node, operation);
        assertHostStatus(terminal, ResultClass::OPERATION_RESULT,
            static_cast<uint8_t>(DeviceCapabilities::OperationStatus::UNAUTHORIZED));
        TEST_ASSERT_FALSE(node.capabilityState.indicator);
        TEST_ASSERT_EQUAL_UINT32(
            item.category == OperationCategory::CAPABILITY ? 1 : 0,
            node.diagnostics.snapshot().counters.authorizationDenials);
    }
}

void testDuplicatesActiveBusyReplacementAndReset() {
    HubStructuredOperationBridge hub;
    NodeFixture node;
    HubResult a = submitSupported(hub,
        request(OperationCategory::DEVICE, OperationCode::PING));
    NodeResult admitted = node.processor.admit(a.packet, NODE, HUB);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NodeAction::ACK_THEN_EXECUTE),
        static_cast<uint8_t>(admitted.action));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NodeAction::SEND_ACK),
        static_cast<uint8_t>(node.processor.admit(a.packet, NODE, HUB).action));
    Protocol::Packet b = a.packet;
    ++b.sequence;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NodeAction::ACTIVE_BUSY),
        static_cast<uint8_t>(node.processor.admit(b, NODE, HUB).action));
    NodeResult completed = node.execute();
    NodeResult replay = node.processor.admit(a.packet, NODE, HUB);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NodeAction::SEND_ACK_AND_RESPONSE),
        static_cast<uint8_t>(replay.action));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(completed.response.payload,
        replay.response.payload, completed.response.payloadLength);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NodeAction::ACK_THEN_EXECUTE),
        static_cast<uint8_t>(node.processor.admit(b, NODE, HUB).action));
    node.processor.reset();
    TEST_ASSERT_FALSE(node.processor.active());
    TEST_ASSERT_FALSE(node.processor.complete());
}

void testPeerBootstrapRejectionAndSuccess() {
    OperationRequest ping = request(OperationCategory::DEVICE,
        OperationCode::PING);
    HubStructuredOperationBridge legacy;
    HubResult submitted = legacy.submit(7, ping, RuntimeState::DeviceRole::HUB,
        HUB, NODE, 0, 10);
    Protocol::Packet rejection = TransactionEngine::makeAcknowledgment(
        submitted.packet, Protocol::AckStatus::UNSUPPORTED_OPCODE);
    HubResult terminal = legacy.receive(rejection);
    assertHostStatus(terminal, ResultClass::RADIO_RESULT,
        static_cast<uint8_t>(RadioResultCode::REMOTE_REJECTED));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WireOperations::PeerSupport::UNSUPPORTED),
        static_cast<uint8_t>(legacy.peerSupport()));

    HubStructuredOperationBridge modern;
    NodeFixture node;
    terminal = runThroughNode(modern, node, ping);
    assertHostStatus(terminal, ResultClass::OPERATION_RESULT,
        static_cast<uint8_t>(DeviceCapabilities::OperationStatus::OK));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WireOperations::PeerSupport::SUPPORTED),
        static_cast<uint8_t>(modern.peerSupport()));
}

void testTimeoutRetryMismatchAndSingleTerminalResponse() {
    HubStructuredOperationBridge hub;
    HubResult initial = hub.submit(0x7777,
        request(OperationCategory::DEVICE, OperationCode::PING),
        RuntimeState::DeviceRole::HUB, HUB, NODE, 100, 10, 1);
    Protocol::Packet wrong = TransactionEngine::makeAcknowledgment(
        initial.packet, Protocol::AckStatus::SUCCESS);
    wrong.source = 99;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HubAction::NONE),
        static_cast<uint8_t>(hub.receive(wrong).action));
    Protocol::Packet malformed = {};
    malformed.type = Protocol::PacketType::RESPONSE;
    malformed.source = NODE;
    malformed.destination = HUB;
    malformed.sequence = initial.packet.sequence;
    malformed.opcode = initial.packet.opcode;
    malformed.payloadLength = 1;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HubAction::NONE),
        static_cast<uint8_t>(hub.receive(malformed).action));
    HubResult retry = hub.service(110);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HubAction::TRANSMIT_COMMAND),
        static_cast<uint8_t>(retry.action));
    TEST_ASSERT_EQUAL_MEMORY(&initial.packet, &retry.packet,
        sizeof(Protocol::Packet));
    HubResult timeout = hub.service(110);
    assertHostStatus(timeout, ResultClass::RADIO_RESULT,
        static_cast<uint8_t>(RadioResultCode::TIMEOUT));
    TEST_ASSERT_EQUAL_HEX16(0x7777, timeout.requestId);
    TEST_ASSERT_FALSE(hub.active());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HubAction::NONE),
        static_cast<uint8_t>(hub.receive(wrong).action));
    TEST_ASSERT_EQUAL_UINT8(1, hub.nextSequence());
}

void testStructuralAdmissionAndEveryOperationStatusMapping() {
    NodeFixture node;
    WireOperations::Request wireRequest = {};
    wireRequest.value.type = static_cast<uint8_t>(
        DeviceCapabilities::ValueType::NONE);
    Protocol::Packet malformed = WireOperations::makeCommand(
        HUB, NODE, 1, static_cast<uint8_t>(Protocol::Opcode::PING), wireRequest);
    malformed.payload[3] = 1;
    NodeResult rejected = node.processor.admit(malformed, NODE, HUB);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NodeAction::SEND_REJECTION_ACK),
        static_cast<uint8_t>(rejected.action));
    TEST_ASSERT_FALSE(node.processor.active());

    const DeviceCapabilities::OperationStatus statuses[] = {
        DeviceCapabilities::OperationStatus::CAPABILITY_NOT_FOUND,
        DeviceCapabilities::OperationStatus::UNSUPPORTED_OPERATION,
        DeviceCapabilities::OperationStatus::INVALID_VALUE_TYPE,
        DeviceCapabilities::OperationStatus::VALUE_OUT_OF_RANGE,
        DeviceCapabilities::OperationStatus::UNAUTHORIZED,
        DeviceCapabilities::OperationStatus::INTERLOCK_ACTIVE,
        DeviceCapabilities::OperationStatus::HARDWARE_UNAVAILABLE,
        DeviceCapabilities::OperationStatus::OPERATION_FAILED,
        DeviceCapabilities::OperationStatus::BUSY,
        DeviceCapabilities::OperationStatus::INVALID_DESCRIPTOR
    };
    for (const auto status : statuses) {
        HubStructuredOperationBridge hub;
        const OperationRequest read = request(OperationCategory::CAPABILITY,
            OperationCode::READ_CAPABILITY,
            SimulatedCapabilities::DIGITAL_INPUT_ID);
        const HubResult submitted = submitSupported(hub, read);
        WireOperations::Response response = {};
        response.status = status;
        response.targetId = read.targetId;
        response.value.type = static_cast<uint8_t>(
            DeviceCapabilities::ValueType::NONE);
        const HubResult terminal = hub.receive(
            WireOperations::makeResponsePacket(submitted.packet, response));
        assertHostStatus(terminal, ResultClass::OPERATION_RESULT,
            static_cast<uint8_t>(status));
    }
}

void testSecondSubmissionIsInternalBusyAndDoesNotOverwrite() {
    HubStructuredOperationBridge hub;
    HubResult first = submitSupported(hub,
        request(OperationCategory::DEVICE, OperationCode::GET_STATUS), 1);
    HubResult second = hub.submit(2,
        request(OperationCategory::DEVICE, OperationCode::PING),
        RuntimeState::DeviceRole::HUB, HUB, NODE, 0, 10);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HubAction::NOT_ACCEPTED),
        static_cast<uint8_t>(second.action));
    TEST_ASSERT_EQUAL_HEX8(first.packet.sequence, hub.nextSequence());
    TEST_ASSERT_TRUE(hub.active());
    hub.reset();
    TEST_ASSERT_FALSE(hub.active());
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(testHubTargetClassificationSupportAndCorrelationBoundary);
    RUN_TEST(testPingVerticalSliceAckBeforeExecutionAndResponseBeforeAck);
    RUN_TEST(testDeviceAndMetadataOperationsRoundTrip);
    RUN_TEST(testRemoteCapabilityPolicyAndProcedureDenial);
    RUN_TEST(testDuplicatesActiveBusyReplacementAndReset);
    RUN_TEST(testPeerBootstrapRejectionAndSuccess);
    RUN_TEST(testTimeoutRetryMismatchAndSingleTerminalResponse);
    RUN_TEST(testStructuralAdmissionAndEveryOperationStatusMapping);
    RUN_TEST(testSecondSubmissionIsInternalBusyAndDoesNotOverwrite);
    return UNITY_END();
}
