#include <unity.h>

#include "runtime_state.h"
#include "transaction_engine.h"

namespace {

constexpr uint8_t HUB_ID = 0x01;
constexpr uint8_t NODE_ID = 0x10;

void testHubDefaultsAreDeterministic() {
    const RuntimeState::State state(
        RuntimeState::DeviceRole::HUB,
        HUB_ID,
        NODE_ID
    );

    TEST_ASSERT_FALSE(state.isReady());
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(RuntimeState::RuntimePhase::IDLE),
        static_cast<uint8_t>(state.phase())
    );
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 0.0F, state.latestRssi());
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 0.0F, state.latestSnr());
}

void testRolesInitializeCorrectly() {
    const RuntimeState::State hub(
        RuntimeState::DeviceRole::HUB,
        HUB_ID,
        NODE_ID
    );
    const RuntimeState::State node(
        RuntimeState::DeviceRole::NODE,
        NODE_ID,
        HUB_ID
    );

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(RuntimeState::DeviceRole::HUB),
        static_cast<uint8_t>(hub.role())
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(RuntimeState::DeviceRole::NODE),
        static_cast<uint8_t>(node.role())
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(RuntimeState::RuntimePhase::LISTENING),
        static_cast<uint8_t>(node.phase())
    );
}

void testIdentityIsExposed() {
    const RuntimeState::State state(
        RuntimeState::DeviceRole::HUB,
        HUB_ID,
        NODE_ID
    );

    TEST_ASSERT_EQUAL_UINT8(HUB_ID, state.localId());
    TEST_ASSERT_EQUAL_UINT8(NODE_ID, state.peerId());
}

void testReadinessTransitions() {
    RuntimeState::State state(
        RuntimeState::DeviceRole::HUB,
        HUB_ID,
        NODE_ID
    );

    state.setReady(true);
    TEST_ASSERT_TRUE(state.isReady());
    state.setReady(false);
    TEST_ASSERT_FALSE(state.isReady());
}

void testRadioMetricsUpdateTogether() {
    RuntimeState::State state(
        RuntimeState::DeviceRole::NODE,
        NODE_ID,
        HUB_ID
    );

    state.updateRadioMetrics(-91.5F, 7.25F);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, -91.5F, state.latestRssi());
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 7.25F, state.latestSnr());
}

void testTransactionFacingPhaseUpdates() {
    RuntimeState::State state(
        RuntimeState::DeviceRole::HUB,
        HUB_ID,
        NODE_ID
    );

    state.setPhase(RuntimeState::RuntimePhase::TRANSMITTING);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(RuntimeState::RuntimePhase::TRANSMITTING),
        static_cast<uint8_t>(state.phase())
    );
    state.setPhase(RuntimeState::RuntimePhase::WAITING_FOR_ACK);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(RuntimeState::RuntimePhase::WAITING_FOR_ACK),
        static_cast<uint8_t>(state.phase())
    );
}

void testHubAndNodeStateRemainIndependent() {
    RuntimeState::State hub(
        RuntimeState::DeviceRole::HUB,
        HUB_ID,
        NODE_ID
    );
    RuntimeState::State node(
        RuntimeState::DeviceRole::NODE,
        NODE_ID,
        HUB_ID
    );

    hub.setReady(true);
    hub.updateRadioMetrics(-80.0F, 9.0F);

    TEST_ASSERT_FALSE(node.isReady());
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 0.0F, node.latestRssi());
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 0.0F, node.latestSnr());
}

void testRuntimeUpdatesDoNotModifyTransactionEngineState() {
    RuntimeState::State runtime(
        RuntimeState::DeviceRole::HUB,
        HUB_ID,
        NODE_ID
    );
    TransactionEngine::HubTransactionState transaction;

    runtime.setReady(true);
    runtime.setPhase(RuntimeState::RuntimePhase::WAITING_FOR_ACK);
    runtime.updateRadioMetrics(-85.0F, 6.0F);

    TEST_ASSERT_EQUAL_UINT8(1, transaction.currentSequence());
    TEST_ASSERT_EQUAL_UINT8(0, transaction.retryCount());
    TEST_ASSERT_FALSE(transaction.isAwaitingAcknowledgment());
}

void testNewStateRestoresVolatileDefaults() {
    RuntimeState::State first(
        RuntimeState::DeviceRole::NODE,
        NODE_ID,
        HUB_ID
    );
    first.setReady(true);
    first.setPhase(RuntimeState::RuntimePhase::TRANSMITTING_ACK);
    first.updateRadioMetrics(-72.0F, 11.0F);

    const RuntimeState::State restarted(
        RuntimeState::DeviceRole::NODE,
        NODE_ID,
        HUB_ID
    );
    TEST_ASSERT_FALSE(restarted.isReady());
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(RuntimeState::RuntimePhase::LISTENING),
        static_cast<uint8_t>(restarted.phase())
    );
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 0.0F, restarted.latestRssi());
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 0.0F, restarted.latestSnr());
}

void assertCountersAreZero(const RuntimeState::DiagnosticCounters& counters) {
    TEST_ASSERT_EQUAL_UINT32(0, counters.transmissionsCompleted);
    TEST_ASSERT_EQUAL_UINT32(0, counters.decodedPacketsReceived);
    TEST_ASSERT_EQUAL_UINT32(0, counters.successfulTransactions);
    TEST_ASSERT_EQUAL_UINT32(0, counters.acceptedCommands);
    TEST_ASSERT_EQUAL_UINT32(0, counters.retransmissions);
    TEST_ASSERT_EQUAL_UINT32(0, counters.acknowledgmentTimeouts);
    TEST_ASSERT_EQUAL_UINT32(0, counters.duplicates);
    TEST_ASSERT_EQUAL_UINT32(0, counters.malformedPackets);
    TEST_ASSERT_EQUAL_UINT32(0, counters.ignoredPackets);
    TEST_ASSERT_EQUAL_UINT32(0, counters.radioErrors);
}

void testPresentationDefaultsAreDeterministic() {
    const RuntimeState::State state(
        RuntimeState::DeviceRole::HUB,
        HUB_ID,
        NODE_ID
    );

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(RuntimeState::Health::STARTING),
        static_cast<uint8_t>(state.health())
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(RuntimeState::ErrorClass::NONE),
        static_cast<uint8_t>(state.lastError())
    );
    TEST_ASSERT_FALSE(state.hasRadioMetrics());
    TEST_ASSERT_FALSE(state.lastInboundPacket().available);
    TEST_ASSERT_FALSE(state.hasLastActivity());
    TEST_ASSERT_EQUAL_UINT32(0, state.lastActivityAtMs());
    assertCountersAreZero(state.counters());
}

void testHealthValuesAndReadinessAreIndependent() {
    RuntimeState::State state(
        RuntimeState::DeviceRole::HUB,
        HUB_ID,
        NODE_ID
    );
    const RuntimeState::Health values[] = {
        RuntimeState::Health::STARTING,
        RuntimeState::Health::READY,
        RuntimeState::Health::DEGRADED,
        RuntimeState::Health::ERROR
    };

    state.setReady(true);
    for (const RuntimeState::Health value : values) {
        state.setHealth(value);
        TEST_ASSERT_EQUAL_UINT8(
            static_cast<uint8_t>(value),
            static_cast<uint8_t>(state.health())
        );
        TEST_ASSERT_TRUE(state.isReady());
    }

    state.setHealth(RuntimeState::Health::DEGRADED);
    state.setReady(false);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(RuntimeState::Health::DEGRADED),
        static_cast<uint8_t>(state.health())
    );
}

void testErrorValuesCanBeRecordedWithoutChangingHealth() {
    RuntimeState::State state(
        RuntimeState::DeviceRole::NODE,
        NODE_ID,
        HUB_ID
    );
    const RuntimeState::ErrorClass values[] = {
        RuntimeState::ErrorClass::NONE,
        RuntimeState::ErrorClass::RADIO_INITIALIZATION,
        RuntimeState::ErrorClass::RADIO_START_RECEIVE,
        RuntimeState::ErrorClass::RADIO_START_TRANSMIT,
        RuntimeState::ErrorClass::RADIO_READ,
        RuntimeState::ErrorClass::PACKET_LENGTH,
        RuntimeState::ErrorClass::PACKET_DECODE,
        RuntimeState::ErrorClass::PACKET_IGNORED,
        RuntimeState::ErrorClass::ACK_TIMEOUT,
        RuntimeState::ErrorClass::REMOTE_ACK,
        RuntimeState::ErrorClass::ACK_STATUS
    };

    state.setHealth(RuntimeState::Health::READY);
    for (const RuntimeState::ErrorClass value : values) {
        state.recordError(value);
        TEST_ASSERT_EQUAL_UINT8(
            static_cast<uint8_t>(value),
            static_cast<uint8_t>(state.lastError())
        );
        TEST_ASSERT_EQUAL_UINT8(
            static_cast<uint8_t>(RuntimeState::Health::READY),
            static_cast<uint8_t>(state.health())
        );
    }
}

void testRadioMetricValidityIncludesRealZeroValues() {
    RuntimeState::State state(
        RuntimeState::DeviceRole::NODE,
        NODE_ID,
        HUB_ID
    );

    state.updateRadioMetrics(0.0F, 0.0F);
    TEST_ASSERT_TRUE(state.hasRadioMetrics());
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 0.0F, state.latestRssi());
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 0.0F, state.latestSnr());

    state.updateRadioMetrics(-67.5F, 10.25F);
    state.setReady(true);
    TEST_ASSERT_TRUE(state.hasRadioMetrics());
    TEST_ASSERT_FLOAT_WITHIN(0.001F, -67.5F, state.latestRssi());
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 10.25F, state.latestSnr());
}

void testInboundPacketRetainsAllFieldsAndAckStatus() {
    RuntimeState::State state(
        RuntimeState::DeviceRole::HUB,
        HUB_ID,
        NODE_ID
    );

    state.recordInboundPacket(
        2, NODE_ID, HUB_ID, 45, 0x64, 1, true, 3, 123456
    );
    const RuntimeState::LastInboundPacket& packet = state.lastInboundPacket();

    TEST_ASSERT_TRUE(packet.available);
    TEST_ASSERT_EQUAL_UINT8(2, packet.rawType);
    TEST_ASSERT_EQUAL_UINT8(NODE_ID, packet.source);
    TEST_ASSERT_EQUAL_UINT8(HUB_ID, packet.destination);
    TEST_ASSERT_EQUAL_UINT8(45, packet.sequence);
    TEST_ASSERT_EQUAL_UINT8(0x64, packet.opcode);
    TEST_ASSERT_EQUAL_UINT8(1, packet.payloadLength);
    TEST_ASSERT_TRUE(packet.ackStatusAvailable);
    TEST_ASSERT_EQUAL_UINT8(3, packet.rawAckStatus);
    TEST_ASSERT_EQUAL_UINT32(123456, packet.observedAtMs);
}

void testInboundPacketReplacementClearsStaleOptionalStatus() {
    RuntimeState::State state(
        RuntimeState::DeviceRole::NODE,
        NODE_ID,
        HUB_ID
    );
    state.recordInboundPacket(
        2, NODE_ID, HUB_ID, 45, 0x64, 1, true, 3, 123456
    );

    state.recordInboundPacket(0, 0, 0, 0, 0, 0, false, 99, 0);
    const RuntimeState::LastInboundPacket& packet = state.lastInboundPacket();

    TEST_ASSERT_TRUE(packet.available);
    TEST_ASSERT_EQUAL_UINT8(0, packet.rawType);
    TEST_ASSERT_EQUAL_UINT8(0, packet.source);
    TEST_ASSERT_EQUAL_UINT8(0, packet.destination);
    TEST_ASSERT_EQUAL_UINT8(0, packet.sequence);
    TEST_ASSERT_EQUAL_UINT8(0, packet.opcode);
    TEST_ASSERT_EQUAL_UINT8(0, packet.payloadLength);
    TEST_ASSERT_FALSE(packet.ackStatusAvailable);
    TEST_ASSERT_EQUAL_UINT8(0, packet.rawAckStatus);
    TEST_ASSERT_EQUAL_UINT32(0, packet.observedAtMs);
}

void testActivityValidityIncludesZeroAndLaterReplacement() {
    RuntimeState::State state(
        RuntimeState::DeviceRole::HUB,
        HUB_ID,
        NODE_ID
    );

    state.recordActivity(0);
    TEST_ASSERT_TRUE(state.hasLastActivity());
    TEST_ASSERT_EQUAL_UINT32(0, state.lastActivityAtMs());

    state.recordActivity(UINT32_MAX);
    TEST_ASSERT_TRUE(state.hasLastActivity());
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, state.lastActivityAtMs());
}

#define ASSERT_SATURATING_COUNTER(method, field) \
    do { \
        RuntimeState::State state( \
            RuntimeState::DeviceRole::HUB, HUB_ID, NODE_ID \
        ); \
        state.method(); \
        TEST_ASSERT_EQUAL_UINT32(1, state.counters().field); \
        state.method(); \
        TEST_ASSERT_EQUAL_UINT32(2, state.counters().field); \
        state.method(UINT32_MAX); \
        TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, state.counters().field); \
        state.method(); \
        TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, state.counters().field); \
    } while (0)

void testEveryCounterIncrementsAndSaturates() {
    ASSERT_SATURATING_COUNTER(
        incrementTransmissionsCompleted, transmissionsCompleted
    );
    ASSERT_SATURATING_COUNTER(
        incrementDecodedPacketsReceived, decodedPacketsReceived
    );
    ASSERT_SATURATING_COUNTER(
        incrementSuccessfulTransactions, successfulTransactions
    );
    ASSERT_SATURATING_COUNTER(incrementAcceptedCommands, acceptedCommands);
    ASSERT_SATURATING_COUNTER(incrementRetransmissions, retransmissions);
    ASSERT_SATURATING_COUNTER(
        incrementAcknowledgmentTimeouts, acknowledgmentTimeouts
    );
    ASSERT_SATURATING_COUNTER(incrementDuplicates, duplicates);
    ASSERT_SATURATING_COUNTER(incrementMalformedPackets, malformedPackets);
    ASSERT_SATURATING_COUNTER(incrementIgnoredPackets, ignoredPackets);
    ASSERT_SATURATING_COUNTER(incrementRadioErrors, radioErrors);
}

#undef ASSERT_SATURATING_COUNTER

void testCounterUpdatesDoNotAlterUnrelatedState() {
    RuntimeState::State state(
        RuntimeState::DeviceRole::NODE,
        NODE_ID,
        HUB_ID
    );
    state.setReady(true);
    state.setPhase(RuntimeState::RuntimePhase::TRANSMITTING_ACK);
    state.setHealth(RuntimeState::Health::DEGRADED);
    state.recordError(RuntimeState::ErrorClass::REMOTE_ACK);
    state.updateRadioMetrics(-55.0F, 8.5F);
    state.recordInboundPacket(1, 1, 16, 9, 100, 0, false, 0, 200);

    state.incrementDuplicates();
    TEST_ASSERT_EQUAL_UINT32(1, state.counters().duplicates);
    TEST_ASSERT_EQUAL_UINT32(0, state.counters().ignoredPackets);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(RuntimeState::DeviceRole::NODE),
        static_cast<uint8_t>(state.role())
    );
    TEST_ASSERT_EQUAL_UINT8(NODE_ID, state.localId());
    TEST_ASSERT_EQUAL_UINT8(HUB_ID, state.peerId());
    TEST_ASSERT_TRUE(state.isReady());
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(RuntimeState::RuntimePhase::TRANSMITTING_ACK),
        static_cast<uint8_t>(state.phase())
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(RuntimeState::Health::DEGRADED),
        static_cast<uint8_t>(state.health())
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(RuntimeState::ErrorClass::REMOTE_ACK),
        static_cast<uint8_t>(state.lastError())
    );
    TEST_ASSERT_TRUE(state.hasRadioMetrics());
    TEST_ASSERT_FLOAT_WITHIN(0.001F, -55.0F, state.latestRssi());
    TEST_ASSERT_TRUE(state.lastInboundPacket().available);
    TEST_ASSERT_EQUAL_UINT8(9, state.lastInboundPacket().sequence);
}

void testExpandedHubAndNodeStateRemainIndependent() {
    RuntimeState::State hub(
        RuntimeState::DeviceRole::HUB,
        HUB_ID,
        NODE_ID
    );
    RuntimeState::State node(
        RuntimeState::DeviceRole::NODE,
        NODE_ID,
        HUB_ID
    );

    hub.setHealth(RuntimeState::Health::ERROR);
    hub.recordError(RuntimeState::ErrorClass::RADIO_READ);
    hub.recordActivity(10);
    hub.incrementRadioErrors();

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(RuntimeState::Health::STARTING),
        static_cast<uint8_t>(node.health())
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(RuntimeState::ErrorClass::NONE),
        static_cast<uint8_t>(node.lastError())
    );
    TEST_ASSERT_FALSE(node.hasLastActivity());
    assertCountersAreZero(node.counters());
}

void testRuntimeDiagnosticsDoNotModifyTransactionAuthorities() {
    RuntimeState::State runtime(
        RuntimeState::DeviceRole::NODE,
        NODE_ID,
        HUB_ID
    );
    TransactionEngine::HubTransactionState hubTransaction;
    TransactionEngine::NodeDuplicateTracker duplicateTracker;
    Protocol::Packet command = {};
    command.source = HUB_ID;
    command.destination = NODE_ID;
    command.sequence = 7;
    command.opcode = static_cast<uint8_t>(Protocol::Opcode::TEST);

    runtime.setHealth(RuntimeState::Health::DEGRADED);
    runtime.recordError(RuntimeState::ErrorClass::ACK_TIMEOUT);
    runtime.incrementAcknowledgmentTimeouts();
    runtime.incrementDuplicates();

    TEST_ASSERT_EQUAL_UINT8(1, hubTransaction.currentSequence());
    TEST_ASSERT_EQUAL_UINT8(0, hubTransaction.retryCount());
    TEST_ASSERT_FALSE(hubTransaction.isAwaitingAcknowledgment());
    TEST_ASSERT_FALSE(duplicateTracker.isDuplicate(command));
}

void testCapabilitySummaryStartsCanonicalAndUnavailable() {
    const RuntimeState::State state(
        RuntimeState::DeviceRole::HUB,
        HUB_ID,
        NODE_ID
    );
    TEST_ASSERT_FALSE(state.hasCapabilityDiagnostics());
    const DeviceCapabilities::CapabilityDiagnosticsSnapshot& snapshot =
        state.capabilityDiagnostics();
    TEST_ASSERT_TRUE(
        DeviceCapabilities::isValidCapabilityDiagnosticsSnapshot(snapshot)
    );
    TEST_ASSERT_EQUAL_UINT32(0, snapshot.counters.lookupAttempts);
    TEST_ASSERT_EQUAL_UINT32(0, snapshot.counters.acceptedOperations);
    TEST_ASSERT_EQUAL_UINT8(0, snapshot.lastStatusAvailable);
    TEST_ASSERT_EQUAL_HEX8(
        static_cast<uint8_t>(DeviceCapabilities::OperationStatus::OK),
        static_cast<uint8_t>(snapshot.lastStatus)
    );
}

void testCapabilitySummaryCopiesAndReplacesExplicitly() {
    RuntimeState::State state(
        RuntimeState::DeviceRole::HUB,
        HUB_ID,
        NODE_ID
    );
    DeviceCapabilities::CapabilityDiagnostics source;
    TEST_ASSERT_TRUE(source.recordOutcome(
        DeviceCapabilities::OperationStatus::OK
    ));
    const DeviceCapabilities::CapabilityDiagnosticsSnapshot first =
        source.snapshot();
    TEST_ASSERT_TRUE(state.updateCapabilityDiagnostics(first));
    TEST_ASSERT_TRUE(state.hasCapabilityDiagnostics());
    TEST_ASSERT_EQUAL_UINT32(
        1,
        state.capabilityDiagnostics().counters.acceptedOperations
    );

    TEST_ASSERT_TRUE(source.recordOutcome(
        DeviceCapabilities::OperationStatus::UNAUTHORIZED
    ));
    TEST_ASSERT_EQUAL_UINT32(
        1,
        state.capabilityDiagnostics().counters.lookupAttempts
    );
    TEST_ASSERT_EQUAL_HEX8(
        static_cast<uint8_t>(DeviceCapabilities::OperationStatus::OK),
        static_cast<uint8_t>(state.capabilityDiagnostics().lastStatus)
    );

    const DeviceCapabilities::CapabilityDiagnosticsSnapshot second =
        source.snapshot();
    TEST_ASSERT_TRUE(state.updateCapabilityDiagnostics(second));
    TEST_ASSERT_EQUAL_UINT32(
        2,
        state.capabilityDiagnostics().counters.lookupAttempts
    );
    TEST_ASSERT_EQUAL_UINT32(
        1,
        state.capabilityDiagnostics().counters.authorizationDenials
    );
    TEST_ASSERT_EQUAL_HEX8(
        static_cast<uint8_t>(DeviceCapabilities::OperationStatus::UNAUTHORIZED),
        static_cast<uint8_t>(state.capabilityDiagnostics().lastStatus)
    );
}

void testInvalidCapabilitySummaryPreservesPriorCopy() {
    RuntimeState::State state(
        RuntimeState::DeviceRole::NODE,
        NODE_ID,
        HUB_ID
    );
    DeviceCapabilities::CapabilityDiagnostics source;
    DeviceCapabilities::CapabilityDiagnosticsSnapshot initiallyInvalid =
        source.snapshot();
    initiallyInvalid.lastStatusAvailable = 2;
    TEST_ASSERT_FALSE(state.updateCapabilityDiagnostics(initiallyInvalid));
    TEST_ASSERT_FALSE(state.hasCapabilityDiagnostics());
    TEST_ASSERT_EQUAL_UINT8(
        0,
        state.capabilityDiagnostics().lastStatusAvailable
    );

    TEST_ASSERT_TRUE(source.recordOutcome(
        DeviceCapabilities::OperationStatus::BUSY
    ));
    TEST_ASSERT_TRUE(state.updateCapabilityDiagnostics(source.snapshot()));
    const DeviceCapabilities::CapabilityDiagnosticsSnapshot before =
        state.capabilityDiagnostics();

    DeviceCapabilities::CapabilityDiagnosticsSnapshot invalid = before;
    invalid.reserved[1] = 1;
    TEST_ASSERT_FALSE(state.updateCapabilityDiagnostics(invalid));
    TEST_ASSERT_TRUE(state.hasCapabilityDiagnostics());
    TEST_ASSERT_EQUAL_UINT32(
        before.counters.lookupAttempts,
        state.capabilityDiagnostics().counters.lookupAttempts
    );
    TEST_ASSERT_EQUAL_UINT32(
        before.counters.busyOutcomes,
        state.capabilityDiagnostics().counters.busyOutcomes
    );
    TEST_ASSERT_EQUAL_HEX8(
        static_cast<uint8_t>(before.lastStatus),
        static_cast<uint8_t>(state.capabilityDiagnostics().lastStatus)
    );
    TEST_ASSERT_EQUAL_UINT8(0, state.capabilityDiagnostics().reserved[1]);
}

void testCapabilitySummaryPreservesRuntimeAndTransportState() {
    RuntimeState::State state(
        RuntimeState::DeviceRole::NODE,
        NODE_ID,
        HUB_ID
    );
    state.setReady(true);
    state.setPhase(RuntimeState::RuntimePhase::TRANSMITTING_ACK);
    state.setHealth(RuntimeState::Health::DEGRADED);
    state.recordError(RuntimeState::ErrorClass::REMOTE_ACK);
    state.updateRadioMetrics(-61.5F, 6.25F);
    state.recordInboundPacket(1, 1, 12, 7, 100, 0, false, 0, 200);
    state.recordActivity(300);
    state.incrementTransmissionsCompleted();
    state.incrementDecodedPacketsReceived();
    state.incrementSuccessfulTransactions();
    state.incrementAcceptedCommands();
    state.incrementRetransmissions();
    state.incrementAcknowledgmentTimeouts();
    state.incrementDuplicates();
    state.incrementMalformedPackets();
    state.incrementIgnoredPackets();
    state.incrementRadioErrors();
    const RuntimeState::DiagnosticCounters transportBefore = state.counters();

    DeviceCapabilities::CapabilityDiagnostics source;
    TEST_ASSERT_TRUE(source.recordOutcome(
        DeviceCapabilities::OperationStatus::HARDWARE_UNAVAILABLE
    ));
    TEST_ASSERT_TRUE(state.updateCapabilityDiagnostics(source.snapshot()));

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(RuntimeState::DeviceRole::NODE),
        static_cast<uint8_t>(state.role())
    );
    TEST_ASSERT_EQUAL_UINT8(NODE_ID, state.localId());
    TEST_ASSERT_EQUAL_UINT8(HUB_ID, state.peerId());
    TEST_ASSERT_TRUE(state.isReady());
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(RuntimeState::RuntimePhase::TRANSMITTING_ACK),
        static_cast<uint8_t>(state.phase())
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(RuntimeState::Health::DEGRADED),
        static_cast<uint8_t>(state.health())
    );
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(RuntimeState::ErrorClass::REMOTE_ACK),
        static_cast<uint8_t>(state.lastError())
    );
    TEST_ASSERT_TRUE(state.hasRadioMetrics());
    TEST_ASSERT_FLOAT_WITHIN(0.001F, -61.5F, state.latestRssi());
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 6.25F, state.latestSnr());
    TEST_ASSERT_TRUE(state.lastInboundPacket().available);
    TEST_ASSERT_EQUAL_UINT8(7, state.lastInboundPacket().sequence);
    TEST_ASSERT_TRUE(state.hasLastActivity());
    TEST_ASSERT_EQUAL_UINT32(300, state.lastActivityAtMs());

#define ASSERT_TRANSPORT_UNCHANGED(field) \
    TEST_ASSERT_EQUAL_UINT32(transportBefore.field, state.counters().field)
    ASSERT_TRANSPORT_UNCHANGED(transmissionsCompleted);
    ASSERT_TRANSPORT_UNCHANGED(decodedPacketsReceived);
    ASSERT_TRANSPORT_UNCHANGED(successfulTransactions);
    ASSERT_TRANSPORT_UNCHANGED(acceptedCommands);
    ASSERT_TRANSPORT_UNCHANGED(retransmissions);
    ASSERT_TRANSPORT_UNCHANGED(acknowledgmentTimeouts);
    ASSERT_TRANSPORT_UNCHANGED(duplicates);
    ASSERT_TRANSPORT_UNCHANGED(malformedPackets);
    ASSERT_TRANSPORT_UNCHANGED(ignoredPackets);
    ASSERT_TRANSPORT_UNCHANGED(radioErrors);
#undef ASSERT_TRANSPORT_UNCHANGED
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(testHubDefaultsAreDeterministic);
    RUN_TEST(testRolesInitializeCorrectly);
    RUN_TEST(testIdentityIsExposed);
    RUN_TEST(testReadinessTransitions);
    RUN_TEST(testRadioMetricsUpdateTogether);
    RUN_TEST(testTransactionFacingPhaseUpdates);
    RUN_TEST(testHubAndNodeStateRemainIndependent);
    RUN_TEST(testRuntimeUpdatesDoNotModifyTransactionEngineState);
    RUN_TEST(testNewStateRestoresVolatileDefaults);
    RUN_TEST(testPresentationDefaultsAreDeterministic);
    RUN_TEST(testHealthValuesAndReadinessAreIndependent);
    RUN_TEST(testErrorValuesCanBeRecordedWithoutChangingHealth);
    RUN_TEST(testRadioMetricValidityIncludesRealZeroValues);
    RUN_TEST(testInboundPacketRetainsAllFieldsAndAckStatus);
    RUN_TEST(testInboundPacketReplacementClearsStaleOptionalStatus);
    RUN_TEST(testActivityValidityIncludesZeroAndLaterReplacement);
    RUN_TEST(testEveryCounterIncrementsAndSaturates);
    RUN_TEST(testCounterUpdatesDoNotAlterUnrelatedState);
    RUN_TEST(testExpandedHubAndNodeStateRemainIndependent);
    RUN_TEST(testRuntimeDiagnosticsDoNotModifyTransactionAuthorities);
    RUN_TEST(testCapabilitySummaryStartsCanonicalAndUnavailable);
    RUN_TEST(testCapabilitySummaryCopiesAndReplacesExplicitly);
    RUN_TEST(testInvalidCapabilitySummaryPreservesPriorCopy);
    RUN_TEST(testCapabilitySummaryPreservesRuntimeAndTransportState);
    return UNITY_END();
}
