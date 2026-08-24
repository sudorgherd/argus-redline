#include <unity.h>

#include "host_role_integration.h"
#include "simulated_capabilities.h"

namespace {

struct FakeStream {
    uint8_t input[512] = {};
    size_t inputLength = 0;
    size_t inputOffset = 0;
    uint8_t output[512] = {};
    size_t outputLength = 0;
    int available() { return static_cast<int>(inputLength - inputOffset); }
    int read() { return inputOffset < inputLength ? input[inputOffset++] : -1; }
    int availableForWrite() { return 512; }
    size_t write(const uint8_t* bytes, size_t length) {
        for (size_t i = 0; i < length; ++i) output[outputLength++] = bytes[i];
        return length;
    }
    void append(const uint8_t* bytes, size_t length) {
        for (size_t i = 0; i < length; ++i) input[inputLength++] = bytes[i];
    }
};

bool availability(const void* context, DeviceCapabilities::CapabilityId id,
    bool& available) {
    return SimulatedCapabilities::capabilityAvailability(
        *static_cast<const SimulatedCapabilities::State*>(context), id, available);
}

struct Fixture {
    FakeStream stream;
    HostRoleIntegration::Stack<FakeStream> stack;
    SimulatedCapabilities::State state;
    SimulatedCapabilities::Handler handler;
    DeviceCapabilities::CapabilityDiagnostics capabilityDiagnostics;
    RuntimeState::State runtime;
    HostOperationService::AvailabilityProvider provider;
    Fixture() : stack(stream, true), handler(state),
        runtime(RuntimeState::DeviceRole::HUB, 1, 16),
        provider(HostOperationService::makeAvailabilityProvider(&state,
            availability)) {
        runtime.setReady(true);
    }
    HostRoleIntegration::Result service(bool bridge = true) {
        return stack.serviceRx(HostOperationService::makeDeviceSnapshot(runtime, 9),
            16, true, SimulatedCapabilities::registryView(), handler,
            DeviceCapabilities::InterlockState::CLEAR, capabilityDiagnostics,
            runtime, provider, 10, 100, bridge);
    }
};

void appendFrame(Fixture& f, HostProtocol::MessageType type, uint16_t requestId,
    const uint8_t* payload, size_t payloadLength,
    uint8_t minor = HostProtocol::VERSION_MINOR_0_1) {
    HostProtocol::Frame frame = {};
    frame.major = HostProtocol::VERSION_MAJOR;
    frame.minor = minor;
    frame.messageType = type;
    frame.requestId = requestId;
    frame.payloadLength = static_cast<uint16_t>(payloadLength);
    for (size_t i = 0; i < payloadLength; ++i) frame.payload[i] = payload[i];
    uint8_t encoded[HostProtocol::MAX_ENCODED_FRAME_SIZE] = {};
    size_t length = 0;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HostProtocol::EncodeResult::OK),
        static_cast<uint8_t>(HostProtocol::encodeFrame(frame, encoded,
            sizeof(encoded), length)));
    f.stream.append(encoded, length);
}

HostProtocol::Frame drainAndDecode(Fixture& fixture) {
    while (fixture.stack.txPending()) fixture.stack.serviceTx();
    HostProtocol::Frame frame = {};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HostProtocol::DecodeResult::OK),
        static_cast<uint8_t>(HostProtocol::decodeFrame(fixture.stream.output,
            fixture.stream.outputLength, frame)));
    fixture.stream.outputLength = 0;
    return frame;
}

void test_hello_handoff_and_role_feature_bits() {
    Fixture f;
    HostProtocol::HelloRequest hello = {1, 1};
    uint8_t payload[HostProtocol::MAX_PAYLOAD_SIZE] = {};
    size_t length = 0;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HostProtocol::PayloadResult::OK),
        static_cast<uint8_t>(HostProtocol::encodeHelloRequest(hello, payload,
            sizeof(payload), length)));
    appendFrame(f, HostProtocol::MessageType::HELLO_REQUEST, 7, payload, length);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HostRoleIntegration::Action::HOST_RESPONSE_HANDOFF),
        static_cast<uint8_t>(f.service(true).action));
    const HostProtocolDiagnostics::Snapshot counters = f.stack.diagnostics().snapshot();
    TEST_ASSERT_EQUAL_UINT32(1, counters.framesReceived);
    TEST_ASSERT_EQUAL_UINT32(1, counters.framesAccepted);
    TEST_ASSERT_EQUAL_UINT32(1, counters.responsesEmitted);
}

void test_local_ping_dispatch_and_exact_bounded_output() {
    Fixture f;
    HostProtocol::OperationRequest request = {};
    request.category = HostProtocol::OperationCategory::DEVICE;
    request.operation = HostProtocol::OperationCode::PING;
    request.targetDeviceId = 1;
    HostProtocol::setNoneValue(request.value);
    uint8_t payload[HostProtocol::MAX_PAYLOAD_SIZE] = {};
    size_t length = 0;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HostProtocol::PayloadResult::OK),
        static_cast<uint8_t>(HostProtocol::encodeOperationRequest(request, payload,
            sizeof(payload), length)));
    appendFrame(f, HostProtocol::MessageType::OPERATION_REQUEST, 8, payload, length);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HostRoleIntegration::Action::HOST_RESPONSE_HANDOFF),
        static_cast<uint8_t>(f.service().action));
    TEST_ASSERT_EQUAL_UINT32(1, f.stack.diagnostics().snapshot().requestsDispatched);
    while (f.stack.txPending()) f.stack.serviceTx(3);
    TEST_ASSERT_GREATER_THAN_UINT32(0, f.stream.outputLength);
}

void test_disconnect_clears_transport_but_preserves_completion() {
    Fixture f;
    f.stack.observeConnection(false);
    TEST_ASSERT_FALSE(f.stack.connected());
    TEST_ASSERT_EQUAL_UINT32(1, f.stack.diagnostics().snapshot().transportResets);
    f.stack.observeConnection(true);
    TEST_ASSERT_TRUE(f.stack.connected());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HostOperationLifecycle::State::EMPTY),
        static_cast<uint8_t>(f.stack.lifecycle().state()));
}

void test_service_budgets_match_complete_frame_bound() {
    TEST_ASSERT_EQUAL_UINT32(140, HostRoleIntegration::RX_SERVICE_BUDGET);
    TEST_ASSERT_EQUAL_UINT32(140, HostRoleIntegration::TX_SERVICE_BUDGET);
}

void test_remote_ping_ack_before_execute_and_terminal_handoff() {
    Fixture hub;
    hub.stack.lifecycle().remoteBridge().configurePeerSupport(
        WireOperations::PeerSupport::SUPPORTED);
    HostProtocol::OperationRequest request = {};
    request.category = HostProtocol::OperationCategory::DEVICE;
    request.operation = HostProtocol::OperationCode::PING;
    request.targetDeviceId = 16;
    HostProtocol::setNoneValue(request.value);
    uint8_t payload[HostProtocol::MAX_PAYLOAD_SIZE] = {};
    size_t length = 0;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HostProtocol::PayloadResult::OK),
        static_cast<uint8_t>(HostProtocol::encodeOperationRequest(request, payload,
            sizeof(payload), length)));
    appendFrame(hub, HostProtocol::MessageType::OPERATION_REQUEST, 0x1234,
        payload, length);
    const HostRoleIntegration::Result outbound = hub.service();
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HostRoleIntegration::Action::TRANSMIT_COMMAND),
        static_cast<uint8_t>(outbound.action));

    RadioOperationBridge::NodeStructuredOperationProcessor node;
    RadioOperationBridge::NodeResult admitted = node.admit(outbound.packet, 16, 1);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RadioOperationBridge::NodeAction::ACK_THEN_EXECUTE),
        static_cast<uint8_t>(admitted.action));
    TEST_ASSERT_FALSE(node.complete());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HostRoleIntegration::Action::NONE),
        static_cast<uint8_t>(hub.stack.onRadioPacket(admitted.acknowledgment).action));

    SimulatedCapabilities::State nodeState;
    SimulatedCapabilities::Handler nodeHandler(nodeState);
    DeviceCapabilities::CapabilityDiagnostics nodeDiagnostics;
    RuntimeState::State nodeRuntime(RuntimeState::DeviceRole::NODE, 16, 1);
    nodeRuntime.setReady(true);
    const HostOperationService::AvailabilityProvider nodeAvailability =
        HostOperationService::makeAvailabilityProvider(&nodeState, availability);
    const RadioOperationBridge::NodeResult completed = node.executeAdmittedOperation(
        HostOperationService::makeDeviceSnapshot(nodeRuntime, 77), true,
        SimulatedCapabilities::registryView(), nodeHandler,
        DeviceCapabilities::InterlockState::CLEAR, nodeDiagnostics, nodeRuntime,
        nodeAvailability);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RadioOperationBridge::NodeAction::RESPONSE_READY),
        static_cast<uint8_t>(completed.action));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HostRoleIntegration::Action::HOST_RESPONSE_HANDOFF),
        static_cast<uint8_t>(hub.stack.onRadioPacket(completed.response).action));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HostOperationLifecycle::State::COMPLETED),
        static_cast<uint8_t>(hub.stack.lifecycle().state()));
}

void test_remote_completion_disconnected_is_replayed_without_new_wire_work() {
    Fixture hub;
    hub.stack.lifecycle().remoteBridge().configurePeerSupport(
        WireOperations::PeerSupport::SUPPORTED);
    HostProtocol::OperationRequest request = {};
    request.category = HostProtocol::OperationCategory::DEVICE;
    request.operation = HostProtocol::OperationCode::PING;
    request.targetDeviceId = 16;
    HostProtocol::setNoneValue(request.value);
    uint8_t payload[HostProtocol::MAX_PAYLOAD_SIZE] = {};
    size_t length = 0;
    HostProtocol::encodeOperationRequest(request, payload, sizeof(payload), length);
    appendFrame(hub, HostProtocol::MessageType::OPERATION_REQUEST, 42, payload, length);
    const Protocol::Packet command = hub.service().packet;
    WireOperations::Response wireResponse = {};
    wireResponse.status = DeviceCapabilities::OperationStatus::OK;
    wireResponse.targetId = 0;
    wireResponse.value.type = static_cast<uint8_t>(
        DeviceCapabilities::ValueType::UNSIGNED_32);
    wireResponse.value.length = 4;
    wireResponse.value.bytes[0] = 9;
    const Protocol::Packet response = WireOperations::makeResponsePacket(
        command, wireResponse);
    hub.stack.observeConnection(false);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HostRoleIntegration::Action::NONE),
        static_cast<uint8_t>(hub.stack.onRadioPacket(response).action));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HostOperationLifecycle::State::COMPLETED),
        static_cast<uint8_t>(hub.stack.lifecycle().state()));
    hub.stack.observeConnection(true);
    TEST_ASSERT_FALSE(hub.stack.txPending());
    appendFrame(hub, HostProtocol::MessageType::OPERATION_REQUEST, 42, payload, length);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HostRoleIntegration::Action::HOST_RESPONSE_HANDOFF),
        static_cast<uint8_t>(hub.service().action));
    TEST_ASSERT_FALSE(hub.stack.lifecycle().remoteBridge().active());
}

void test_minor_two_hello_is_stateless_and_does_not_advertise_stage9_service() {
    Fixture f;
    HostProtocol::HelloRequest hello = {1, 2};
    uint8_t payload[HostProtocol::MAX_PAYLOAD_SIZE] = {}; size_t length = 0;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HostProtocol::PayloadResult::OK),
        static_cast<uint8_t>(HostProtocol::encodeHelloRequest(
            HostProtocol::VERSION_MINOR_0_2, hello, payload, sizeof(payload), length)));
    appendFrame(f, HostProtocol::MessageType::HELLO_REQUEST, 0x8101,
        payload, length, HostProtocol::VERSION_MINOR_0_2);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HostRoleIntegration::Action::HOST_RESPONSE_HANDOFF),
                           static_cast<uint8_t>(f.service().action));
    HostProtocol::Frame response = drainAndDecode(f);
    TEST_ASSERT_EQUAL_UINT8(HostProtocol::VERSION_MINOR_0_2, response.minor);
    HostProtocol::HelloResponse semantic = {};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HostProtocol::PayloadResult::OK),
        static_cast<uint8_t>(HostProtocol::decodeHelloResponse(
            response.payload, response.payloadLength, semantic)));
    TEST_ASSERT_EQUAL_UINT8(HostProtocol::VERSION_MINOR_0_2, semantic.selectedMinor);
    TEST_ASSERT_EQUAL_UINT16(0, semantic.operationCategoryBitmap &
        HostProtocol::CATEGORY_EVENT_BIT);
    TEST_ASSERT_EQUAL_UINT16(0, semantic.featureBitmap &
        HostProtocol::FEATURE_EVENT_SERVICE);

    hello.maximumMinor = 1;
    HostProtocol::encodeHelloRequest(HostProtocol::VERSION_MINOR_0_2,
        hello, payload, sizeof(payload), length);
    appendFrame(f, HostProtocol::MessageType::HELLO_REQUEST, 0x8103,
        payload, length, HostProtocol::VERSION_MINOR_0_2);
    f.service(); response = drainAndDecode(f);
    TEST_ASSERT_EQUAL_UINT8(HostProtocol::VERSION_MINOR_0_2, response.minor);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HostProtocol::PayloadResult::OK),
        static_cast<uint8_t>(HostProtocol::decodeHelloResponse(
            response.payload, response.payloadLength, semantic)));
    TEST_ASSERT_EQUAL_UINT8(HostProtocol::VERSION_MINOR_0_1,
        semantic.selectedMinor);

    HostProtocol::OperationRequest ping = {};
    ping.category = HostProtocol::OperationCategory::DEVICE;
    ping.operation = HostProtocol::OperationCode::PING;
    ping.targetDeviceId = 1; HostProtocol::setNoneValue(ping.value);
    HostProtocol::encodeOperationRequest(ping, payload, sizeof(payload), length);
    appendFrame(f, HostProtocol::MessageType::OPERATION_REQUEST, 0x8102,
        payload, length, HostProtocol::VERSION_MINOR_0_1);
    f.service(); response = drainAndDecode(f);
    TEST_ASSERT_EQUAL_UINT8(HostProtocol::VERSION_MINOR_0_1, response.minor);
}

void test_minor_two_event_codec_is_rejected_without_service_using_request_minor() {
    Fixture f;
    HostProtocol::OperationRequest poll = {};
    poll.category = HostProtocol::OperationCategory::EVENT;
    poll.operation = HostProtocol::OperationCode::POLL_EVENTS;
    poll.targetDeviceId = 1; HostProtocol::setNoneValue(poll.value);
    uint8_t payload[HostProtocol::MAX_PAYLOAD_SIZE] = {}; size_t length = 0;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HostProtocol::PayloadResult::OK),
        static_cast<uint8_t>(HostProtocol::encodeOperationRequest(
            HostProtocol::VERSION_MINOR_0_2, poll, payload, sizeof(payload), length)));
    appendFrame(f, HostProtocol::MessageType::OPERATION_REQUEST, 0x8201,
        payload, length, HostProtocol::VERSION_MINOR_0_2);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HostRoleIntegration::Action::HOST_RESPONSE_HANDOFF),
                           static_cast<uint8_t>(f.service().action));
    HostProtocol::Frame frame = drainAndDecode(f);
    TEST_ASSERT_EQUAL_UINT8(HostProtocol::VERSION_MINOR_0_2, frame.minor);
    HostProtocol::OperationResponse response = {};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HostProtocol::PayloadResult::OK),
        static_cast<uint8_t>(HostProtocol::decodeOperationResponse(
            HostProtocol::VERSION_MINOR_0_2, frame.payload,
            frame.payloadLength, response)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HostProtocol::ResultClass::REQUEST_REJECTED),
                           static_cast<uint8_t>(response.resultClass));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HostProtocol::RequestRejectionCode::UNSUPPORTED_OPERATION),
                           response.resultCode);
}

void test_hub_event_service_advertises_and_dispatches_locally_once() {
    Fixture f;
    HubEventLedger::Ledger unavailableLedger;
    HostEventService::Service eventService(unavailableLedger);
    f.stack.setEventService(&eventService);

    HostProtocol::HelloRequest hello = {1, 2};
    uint8_t payload[HostProtocol::MAX_PAYLOAD_SIZE] = {}; size_t length = 0;
    HostProtocol::encodeHelloRequest(HostProtocol::VERSION_MINOR_0_2,
        hello, payload, sizeof(payload), length);
    appendFrame(f, HostProtocol::MessageType::HELLO_REQUEST, 0x8301,
        payload, length, HostProtocol::VERSION_MINOR_0_2);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HostRoleIntegration::Action::HOST_RESPONSE_HANDOFF),
        static_cast<uint8_t>(f.service().action));
    HostProtocol::Frame frame = drainAndDecode(f);
    HostProtocol::HelloResponse helloResponse = {};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HostProtocol::PayloadResult::OK),
        static_cast<uint8_t>(HostProtocol::decodeHelloResponse(
            frame.payload, frame.payloadLength, helloResponse)));
    TEST_ASSERT_EQUAL_UINT16(HostProtocol::CATEGORY_EVENT_BIT,
        helloResponse.operationCategoryBitmap & HostProtocol::CATEGORY_EVENT_BIT);
    TEST_ASSERT_EQUAL_UINT16(HostProtocol::FEATURE_EVENT_SERVICE,
        helloResponse.featureBitmap & HostProtocol::FEATURE_EVENT_SERVICE);

    HostProtocol::OperationRequest poll = {};
    poll.category = HostProtocol::OperationCategory::EVENT;
    poll.operation = HostProtocol::OperationCode::POLL_EVENTS;
    poll.targetDeviceId = 1; HostProtocol::setNoneValue(poll.value);
    HostProtocol::encodeOperationRequest(HostProtocol::VERSION_MINOR_0_2,
        poll, payload, sizeof(payload), length);
    appendFrame(f, HostProtocol::MessageType::OPERATION_REQUEST, 0x8302,
        payload, length, HostProtocol::VERSION_MINOR_0_2);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HostRoleIntegration::Action::HOST_RESPONSE_HANDOFF),
        static_cast<uint8_t>(f.service().action));
    frame = drainAndDecode(f);
    HostProtocol::OperationResponse response = {};
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HostProtocol::PayloadResult::OK),
        static_cast<uint8_t>(HostProtocol::decodeOperationResponse(
            HostProtocol::VERSION_MINOR_0_2, frame.payload,
            frame.payloadLength, response)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HostProtocol::ResultClass::EVENT_RESULT),
        static_cast<uint8_t>(response.resultClass));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HostProtocol::EventResultCode::STORAGE_FAILURE),
        response.resultCode);
    TEST_ASSERT_EQUAL_UINT32(1, f.stack.diagnostics().snapshot().requestsDispatched);

    appendFrame(f, HostProtocol::MessageType::OPERATION_REQUEST, 0x8302,
        payload, length, HostProtocol::VERSION_MINOR_0_2);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HostRoleIntegration::Action::HOST_RESPONSE_HANDOFF),
        static_cast<uint8_t>(f.service().action));
    drainAndDecode(f);
    TEST_ASSERT_EQUAL_UINT32(1, f.stack.diagnostics().snapshot().requestsDispatched);
}


}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_hello_handoff_and_role_feature_bits);
    RUN_TEST(test_local_ping_dispatch_and_exact_bounded_output);
    RUN_TEST(test_disconnect_clears_transport_but_preserves_completion);
    RUN_TEST(test_service_budgets_match_complete_frame_bound);
    RUN_TEST(test_remote_ping_ack_before_execute_and_terminal_handoff);
    RUN_TEST(test_remote_completion_disconnected_is_replayed_without_new_wire_work);
    RUN_TEST(test_minor_two_hello_is_stateless_and_does_not_advertise_stage9_service);
    RUN_TEST(test_minor_two_event_codec_is_rejected_without_service_using_request_minor);
    RUN_TEST(test_hub_event_service_advertises_and_dispatches_locally_once);
    return UNITY_END();
}
