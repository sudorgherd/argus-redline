#include <type_traits>
#include <unity.h>

#include "provisioning_snapshot.h"
#include "runtime_state.h"
#include "transaction_engine.h"
#include "wire_operations.h"
#include "radio_operation_bridge.h"
#include "host_device_service.h"
#include "hub_event_radio.h"
#include "node_event_store.h"
#include "node_event_delivery.h"

using ProvisioningRuntime::ProvisioningSnapshot;
using ProvisioningRuntime::Resolution;
using ProvisioningRuntime::ResolutionOutcome;

static_assert(!std::is_default_constructible<ProvisioningSnapshot>::value,
              "an operational snapshot requires a recovered record");
static_assert(!std::is_assignable<ProvisioningSnapshot&, ProvisioningSnapshot>::value,
              "the snapshot must not be assignable");
static_assert(!std::is_constructible<ProvisioningSnapshot,
              DeviceIdentity::Record>::value,
              "a caller cannot bypass recovery with a Record");

namespace {

size_t slotIndex(EventStorage::CopySlot slot) {
    return slot == EventStorage::CopySlot::A ? 0 : 1;
}

class IdentityStorage final : public DeviceIdentityStore::Storage {
public:
    bool present[2] = {};
    uint8_t bytes[2][DeviceIdentity::RECORD_SIZE] = {};

    DeviceIdentityStore::StorageResult read(EventStorage::CopySlot slot,
        uint8_t* output, size_t capacity, size_t& length) override {
        length = 0;
        const size_t index = slotIndex(slot);
        if (!present[index]) return DeviceIdentityStore::StorageResult::MISSING;
        if (capacity != DeviceIdentity::RECORD_SIZE)
            return DeviceIdentityStore::StorageResult::ERROR;
        for (size_t i = 0; i < capacity; ++i) output[i] = bytes[index][i];
        length = capacity;
        return DeviceIdentityStore::StorageResult::OK;
    }
    DeviceIdentityStore::StorageResult write(EventStorage::CopySlot slot,
        const uint8_t* input, size_t length) override {
        if (length != DeviceIdentity::RECORD_SIZE)
            return DeviceIdentityStore::StorageResult::ERROR;
        const size_t index = slotIndex(slot);
        present[index] = true;
        for (size_t i = 0; i < length; ++i) bytes[index][i] = input[i];
        return DeviceIdentityStore::StorageResult::OK;
    }
    DeviceIdentityStore::StorageResult commit() override {
        return DeviceIdentityStore::StorageResult::OK;
    }
};

class EventStorageFixture final : public NodeEventStore::Storage,
                                  public HubEventLedger::Storage {
public:
    EventStorage::FixedCopy<EventRecords::NODE_METADATA_SIZE> nodeMetadata[2] = {};
    EventStorage::FixedCopy<EventRecords::NODE_RECORD_SIZE> nodeRecords[8][2] = {};
    EventStorage::FixedCopy<EventRecords::HUB_METADATA_SIZE> hubMetadata[2] = {};
    EventStorage::FixedCopy<EventRecords::HUB_RECORD_SIZE> hubRecords[8][2] = {};
    unsigned writes = 0;

    EventStorageFixture() {
        for (auto& x : nodeMetadata) x.status = EventStorage::FixedReadStatus::MISSING;
        for (auto& x : hubMetadata) x.status = EventStorage::FixedReadStatus::MISSING;
        for (auto& row : nodeRecords)
            for (auto& x : row) x.status = EventStorage::FixedReadStatus::MISSING;
        for (auto& row : hubRecords)
            for (auto& x : row) x.status = EventStorage::FixedReadStatus::MISSING;
    }

    EventIdentity::StorageResult read(EventStorage::CopySlot slot,
        uint8_t* out, size_t capacity, size_t& length) override {
        return readCopy(nodeMetadata[slotIndex(slot)], out, capacity, length);
    }
    EventIdentity::StorageResult write(EventStorage::CopySlot slot,
        const uint8_t* input, size_t length) override {
        return writeCopy(nodeMetadata[slotIndex(slot)], input, length);
    }
    EventIdentity::StorageResult readEventCopy(uint8_t slot,
        EventStorage::CopySlot copy, uint8_t* out, size_t capacity,
        size_t& length) override {
        return readCopy(nodeRecords[slot][slotIndex(copy)], out, capacity, length);
    }
    EventIdentity::StorageResult writeEventCopy(uint8_t slot,
        EventStorage::CopySlot copy, const uint8_t* input,
        size_t length) override {
        return writeCopy(nodeRecords[slot][slotIndex(copy)], input, length);
    }
    EventIdentity::StorageResult readHubMetadata(EventStorage::CopySlot copy,
        uint8_t* out, size_t capacity, size_t& length) override {
        return readCopy(hubMetadata[slotIndex(copy)], out, capacity, length);
    }
    EventIdentity::StorageResult writeHubMetadata(EventStorage::CopySlot copy,
        const uint8_t* input, size_t length) override {
        return writeCopy(hubMetadata[slotIndex(copy)], input, length);
    }
    EventIdentity::StorageResult readHubEventCopy(uint8_t slot,
        EventStorage::CopySlot copy, uint8_t* out, size_t capacity,
        size_t& length) override {
        return readCopy(hubRecords[slot][slotIndex(copy)], out, capacity, length);
    }
    EventIdentity::StorageResult writeHubEventCopy(uint8_t slot,
        EventStorage::CopySlot copy, const uint8_t* input,
        size_t length) override {
        return writeCopy(hubRecords[slot][slotIndex(copy)], input, length);
    }
    EventIdentity::StorageResult commit() override {
        return EventIdentity::StorageResult::OK;
    }

private:
    template<size_t N> static EventIdentity::StorageResult readCopy(
        const EventStorage::FixedCopy<N>& source, uint8_t* out,
        size_t capacity, size_t& length) {
        length = 0;
        if (source.status == EventStorage::FixedReadStatus::MISSING)
            return EventIdentity::StorageResult::MISSING;
        if (capacity < N) return EventIdentity::StorageResult::ERROR;
        for (size_t i = 0; i < N; ++i) out[i] = source.bytes[i];
        length = N;
        return EventIdentity::StorageResult::OK;
    }
    template<size_t N> EventIdentity::StorageResult writeCopy(
        EventStorage::FixedCopy<N>& target, const uint8_t* input,
        size_t length) {
        if (length != N) return EventIdentity::StorageResult::ERROR;
        ++writes;
        target.status = EventStorage::FixedReadStatus::OK;
        for (size_t i = 0; i < N; ++i) target.bytes[i] = input[i];
        return EventIdentity::StorageResult::OK;
    }
};

class EventEntropy final : public EventIdentity::EntropySource {
public:
    bool nextUint32(uint32_t& value) override { value = 0x1234; return true; }
};

class EventSequence final : public NodeEventDelivery::SequenceSource {
public:
    bool next(uint8_t& sequence) override { sequence = 5; return true; }
};

class EventJitter final : public NodeEventDelivery::JitterSource {
public:
    uint16_t nextMilliseconds() override { return 0; }
};

DeviceIdentity::Record record(DeviceIdentity::Role role, uint8_t local,
                              uint8_t peer) {
    DeviceIdentity::Record value = {};
    value.recordGeneration = 7;
    value.provisioningGeneration = 3;
    value.state = DeviceIdentity::DurableState::PROVISIONED;
    value.transaction = DeviceIdentity::TransactionKind::NONE;
    value.role = role;
    value.localDeviceId = local;
    value.peerDeviceId = peer;
    value.hardwareProfile = DeviceIdentity::HardwareProfile::HELTEC_V4;
    value.capabilityProfile = DeviceIdentity::CapabilityProfile::HELTEC_V4_BASE;
    value.networkId = 0x12345678;
    value.labelLength = 3;
    value.label[0] = 'R'; value.label[1] = 'E'; value.label[2] = 'D';
    return value;
}

DeviceIdentityStore::Snapshot recovered(const DeviceIdentity::Record& value) {
    IdentityStorage storage;
    storage.present[0] = true;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DeviceIdentity::CodecResult::OK),
        static_cast<uint8_t>(DeviceIdentity::encodeRecord(
            value, storage.bytes[0], sizeof(storage.bytes[0]))));
    DeviceIdentityStore::Store store;
    return store.recover(storage);
}

void assertOutcome(const DeviceIdentityStore::Snapshot& state,
                   ResolutionOutcome expected) {
    const Resolution result = Resolution::fromRecovery(state);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected),
                            static_cast<uint8_t>(result.outcome()));
    TEST_ASSERT_EQUAL(expected == ResolutionOutcome::PROVISIONED_HUB ||
                      expected == ResolutionOutcome::PROVISIONED_NODE,
                      result.radioEligible());
    TEST_ASSERT_EQUAL(result.radioEligible(), result.snapshot() != nullptr);
}

void testHubAndNodeResolveFromRecoveredBytes() {
    for (auto role : {DeviceIdentity::Role::HUB, DeviceIdentity::Role::NODE}) {
        const uint8_t local = role == DeviceIdentity::Role::HUB ? 1 : 0x10;
        const uint8_t peer = role == DeviceIdentity::Role::HUB ? 0x10 : 1;
        const auto state = recovered(record(role, local, peer));
        const Resolution resolved = Resolution::fromRecovery(state);
        TEST_ASSERT_TRUE(resolved.radioEligible());
        TEST_ASSERT_EQUAL_UINT8(local, resolved.snapshot()->localDeviceId());
        TEST_ASSERT_EQUAL_UINT8(peer, resolved.snapshot()->peerDeviceId());
        TEST_ASSERT_EQUAL_UINT32(0x12345678, resolved.snapshot()->networkId());
        TEST_ASSERT_EQUAL_UINT32(3, resolved.snapshot()->provisioningGeneration());
        TEST_ASSERT_EQUAL_UINT8(3, resolved.snapshot()->labelLength());
        TEST_ASSERT_EQUAL_UINT8('E', resolved.snapshot()->labelByte(1));
        TEST_ASSERT_EQUAL_UINT8(0, resolved.snapshot()->labelByte(3));
        TEST_ASSERT_EQUAL_UINT8(1,
            static_cast<uint8_t>(resolved.snapshot()->hardwareProfile()));
        TEST_ASSERT_EQUAL_UINT8(1,
            static_cast<uint8_t>(resolved.snapshot()->capabilityProfile()));
        TEST_ASSERT_EQUAL(role == DeviceIdentity::Role::HUB,
            resolved.snapshot()->hostRadioBridgeEnabled());
        TEST_ASSERT_EQUAL(role == DeviceIdentity::Role::HUB,
            resolved.snapshot()->hubEventServiceEnabled());
        const RuntimeState::State runtime(*resolved.snapshot());
        const auto host = HostOperationService::makeDeviceSnapshot(runtime, 12);
        TEST_ASSERT_EQUAL_UINT8(local, runtime.localId());
        TEST_ASSERT_EQUAL_UINT8(peer, runtime.peerId());
        TEST_ASSERT_EQUAL_UINT8(local, host.deviceId);
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(runtime.role()),
                                static_cast<uint8_t>(host.role));
        TEST_ASSERT_EQUAL_UINT8(role == DeviceIdentity::Role::HUB
            ? static_cast<uint8_t>(RuntimeState::DeviceRole::HUB)
            : static_cast<uint8_t>(RuntimeState::DeviceRole::NODE),
            static_cast<uint8_t>(runtime.role()));
    }
}

void testEveryNonProvisionedRecoveryOutcomeIsRadioOff() {
    const auto valid = recovered(record(DeviceIdentity::Role::HUB, 1, 0x10));
    struct Case { DeviceIdentityStore::Outcome store; ResolutionOutcome result; };
    const Case cases[] = {
        {DeviceIdentityStore::Outcome::VIRGIN_UNPROVISIONED,
            ResolutionOutcome::VIRGIN_UNPROVISIONED},
        {DeviceIdentityStore::Outcome::UNPROVISIONED,
            ResolutionOutcome::UNPROVISIONED},
        {DeviceIdentityStore::Outcome::PENDING, ResolutionOutcome::PENDING},
        {DeviceIdentityStore::Outcome::INVALID_PROVISIONING,
            ResolutionOutcome::INVALID_PROVISIONING},
        {DeviceIdentityStore::Outcome::STORAGE_UNAVAILABLE,
            ResolutionOutcome::STORAGE_UNAVAILABLE}
    };
    for (const Case& item : cases) {
        auto state = valid;
        state.outcome = item.store;
        assertOutcome(state, item.result);
    }
    IdentityStorage empty;
    DeviceIdentityStore::Store store;
    assertOutcome(store.recover(empty), ResolutionOutcome::VIRGIN_UNPROVISIONED);
    auto zero = DeviceIdentity::Record{};
    zero.state = DeviceIdentity::DurableState::UNPROVISIONED;
    zero.transaction = DeviceIdentity::TransactionKind::NONE;
    assertOutcome(recovered(zero), ResolutionOutcome::UNPROVISIONED);
    zero.state = DeviceIdentity::DurableState::PENDING;
    zero.transaction = DeviceIdentity::TransactionKind::FULL_RESET;
    assertOutcome(recovered(zero), ResolutionOutcome::PENDING);
    auto pending = record(DeviceIdentity::Role::NODE, 0x10, 1);
    pending.state = DeviceIdentity::DurableState::PENDING;
    pending.transaction = DeviceIdentity::TransactionKind::APPLY;
    assertOutcome(recovered(pending), ResolutionOutcome::PENDING);
}

void testInvalidAndUnsupportedRecordsFailClosed() {
    auto state = recovered(record(DeviceIdentity::Role::HUB, 1, 0x10));
    state.hasAuthority = false;
    assertOutcome(state, ResolutionOutcome::INVALID_PROVISIONING);
    state = recovered(record(DeviceIdentity::Role::HUB, 1, 0x10));
    state.record.peerDeviceId = 0x20;
    assertOutcome(state, ResolutionOutcome::INVALID_PROVISIONING);
    state = recovered(record(DeviceIdentity::Role::HUB, 1, 0x10));
    state.bytes[20] = 1;
    EventRecords::finishCrc(state.bytes, sizeof(state.bytes));
    assertOutcome(state, ResolutionOutcome::INVALID_PROVISIONING);
    state = recovered(record(DeviceIdentity::Role::HUB, 1, 0x10));
    state.bytes[21] = 2;
    EventRecords::finishCrc(state.bytes, sizeof(state.bytes));
    assertOutcome(state, ResolutionOutcome::UNSUPPORTED_HARDWARE_PROFILE);
    state = recovered(record(DeviceIdentity::Role::HUB, 1, 0x10));
    state.bytes[22] = 2;
    EventRecords::finishCrc(state.bytes, sizeof(state.bytes));
    assertOutcome(state, ResolutionOutcome::UNSUPPORTED_CAPABILITY_PROFILE);
    state = recovered(record(DeviceIdentity::Role::HUB, 1, 0x10));
    state.bytes[24] = 0; state.bytes[25] = 0;
    state.bytes[26] = 0; state.bytes[27] = 0;
    EventRecords::finishCrc(state.bytes, sizeof(state.bytes));
    assertOutcome(state, ResolutionOutcome::INVALID_PROVISIONING);
}

void testForgedProvisionedOutcomeCannotActivatePendingOrBadEndpoints() {
    auto state = recovered(record(DeviceIdentity::Role::HUB, 1, 0x10));
    state.bytes[16] = static_cast<uint8_t>(DeviceIdentity::DurableState::PENDING);
    state.bytes[17] = static_cast<uint8_t>(DeviceIdentity::TransactionKind::APPLY);
    EventRecords::finishCrc(state.bytes, sizeof(state.bytes));
    assertOutcome(state, ResolutionOutcome::INVALID_PROVISIONING);
    state = recovered(record(DeviceIdentity::Role::NODE, 0x10, 1));
    state.bytes[19] = 0;
    EventRecords::finishCrc(state.bytes, sizeof(state.bytes));
    assertOutcome(state, ResolutionOutcome::INVALID_PROVISIONING);
    state = recovered(record(DeviceIdentity::Role::NODE, 0x10, 1));
    state.bytes[20] = 0xFF;
    EventRecords::finishCrc(state.bytes, sizeof(state.bytes));
    assertOutcome(state, ResolutionOutcome::INVALID_PROVISIONING);
    state = recovered(record(DeviceIdentity::Role::NODE, 0x10, 1));
    state.bytes[18] = 0;
    EventRecords::finishCrc(state.bytes, sizeof(state.bytes));
    assertOutcome(state, ResolutionOutcome::INVALID_PROVISIONING);
}

void testRealStoreRecoveryReportsUnsupportedProfilesRadioOff() {
    for (uint8_t offset : {static_cast<uint8_t>(21),
                           static_cast<uint8_t>(22)}) {
        IdentityStorage storage;
        storage.present[0] = true;
        const auto value = record(DeviceIdentity::Role::HUB, 1, 0x10);
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DeviceIdentity::CodecResult::OK),
            static_cast<uint8_t>(DeviceIdentity::encodeRecord(
                value, storage.bytes[0], sizeof(storage.bytes[0]))));
        storage.bytes[0][offset] = 2;
        EventRecords::finishCrc(storage.bytes[0], sizeof(storage.bytes[0]));
        DeviceIdentityStore::Store store;
        const auto& state = store.recover(storage);
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DeviceIdentityStore::Outcome::INVALID_PROVISIONING),
            static_cast<uint8_t>(state.outcome));
        assertOutcome(state, offset == 21
            ? ResolutionOutcome::UNSUPPORTED_HARDWARE_PROFILE
            : ResolutionOutcome::UNSUPPORTED_CAPABILITY_PROFILE);
    }
}

void testUnknownSchemaRemainsInvalidEvenBesideUnsupportedProfile() {
    IdentityStorage storage;
    storage.present[0] = storage.present[1] = true;
    const auto value = record(DeviceIdentity::Role::HUB, 1, 0x10);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DeviceIdentity::CodecResult::OK),
        static_cast<uint8_t>(DeviceIdentity::encodeRecord(
            value, storage.bytes[0], sizeof(storage.bytes[0]))));
    for (size_t i = 0; i < DeviceIdentity::RECORD_SIZE; ++i) {
        storage.bytes[1][i] = storage.bytes[0][i];
    }
    storage.bytes[0][4] = 2;
    storage.bytes[1][21] = 2;
    EventRecords::finishCrc(storage.bytes[0], sizeof(storage.bytes[0]));
    EventRecords::finishCrc(storage.bytes[1], sizeof(storage.bytes[1]));
    DeviceIdentityStore::Store store;
    const auto& state = store.recover(storage);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DeviceIdentityStore::Selection::UNSUPPORTED_SCHEMA),
        static_cast<uint8_t>(state.selection));
    assertOutcome(state, ResolutionOutcome::INVALID_PROVISIONING);
}

void testSnapshotIsIndependentOfMutableStoreRecordAndBytes() {
    auto state = recovered(record(DeviceIdentity::Role::HUB, 0x21, 0x42));
    const Resolution resolved = Resolution::fromRecovery(state);
    const ProvisioningSnapshot& identity = *resolved.snapshot();
    state.record.localDeviceId = 1;
    state.record.peerDeviceId = 0x10;
    state.record.label[0] = 'X';
    state.bytes[19] = 1;
    state.bytes[20] = 0x10;
    state.bytes[28] = 'X';
    TEST_ASSERT_EQUAL_UINT8(0x21, identity.localDeviceId());
    TEST_ASSERT_EQUAL_UINT8(0x42, identity.peerDeviceId());
    TEST_ASSERT_EQUAL_UINT8('R', identity.labelByte(0));
}

void testNetworkIdNeverChangesWireOnePacketBytes() {
    auto first = record(DeviceIdentity::Role::HUB, 0x21, 0x42);
    auto second = first;
    second.networkId = 0x87654321;
    const Resolution a = Resolution::fromRecovery(recovered(first));
    const Resolution b = Resolution::fromRecovery(recovered(second));
    TEST_ASSERT_NOT_EQUAL(a.snapshot()->networkId(), b.snapshot()->networkId());
    WireOperations::Request request = {};
    request.value.type = static_cast<uint8_t>(DeviceCapabilities::ValueType::NONE);
    const auto packetA = WireOperations::makeCommand(*a.snapshot(), 4,
        static_cast<uint8_t>(Protocol::Opcode::PING), request);
    const auto packetB = WireOperations::makeCommand(*b.snapshot(), 4,
        static_cast<uint8_t>(Protocol::Opcode::PING), request);
    uint8_t bytesA[Protocol::MAX_PACKET_SIZE] = {};
    uint8_t bytesB[Protocol::MAX_PACKET_SIZE] = {};
    size_t lengthA = 0, lengthB = 0;
    TEST_ASSERT_TRUE(Protocol::encode(packetA, bytesA, sizeof(bytesA), lengthA));
    TEST_ASSERT_TRUE(Protocol::encode(packetB, bytesB, sizeof(bytesB), lengthB));
    TEST_ASSERT_EQUAL_UINT(lengthA, lengthB);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(bytesA, bytesB, lengthA);
}

void testHubWireAndTransactionRouting(uint8_t local, uint8_t peer) {
    const Resolution resolution = Resolution::fromRecovery(
        recovered(record(DeviceIdentity::Role::HUB, local, peer)));
    const auto& identity = *resolution.snapshot();
    WireOperations::Request request = {};
    request.value.type = static_cast<uint8_t>(DeviceCapabilities::ValueType::NONE);
    const Protocol::Packet command = WireOperations::makeCommand(identity, 7,
        static_cast<uint8_t>(Protocol::Opcode::PING), request);
    TEST_ASSERT_EQUAL_UINT8(local, command.source);
    TEST_ASSERT_EQUAL_UINT8(peer, command.destination);
    Protocol::Packet ack = TransactionEngine::makeAcknowledgment(
        command, Protocol::AckStatus::SUCCESS);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TransactionEngine::HubAckOutcome::MATCHING_ACK),
        static_cast<uint8_t>(TransactionEngine::evaluateHubAcknowledgment(
            ack, command, identity).outcome));
    ack.source = local;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TransactionEngine::HubAckOutcome::IGNORE_WRONG_SENDER),
        static_cast<uint8_t>(TransactionEngine::evaluateHubAcknowledgment(
            ack, command, identity).outcome));
    ack.source = peer; ack.destination = peer;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TransactionEngine::HubAckOutcome::IGNORE_WRONG_DESTINATION),
        static_cast<uint8_t>(TransactionEngine::evaluateHubAcknowledgment(
            ack, command, identity).outcome));
    TEST_ASSERT_EQUAL_UINT8(local, identity.localDeviceId());
    TEST_ASSERT_EQUAL_UINT8(peer, identity.peerDeviceId());
}

void testNodeWireAndTransactionRouting(uint8_t local, uint8_t peer) {
    const Resolution resolution = Resolution::fromRecovery(
        recovered(record(DeviceIdentity::Role::NODE, local, peer)));
    const auto& identity = *resolution.snapshot();
    Protocol::Packet command = {};
    command.type = Protocol::PacketType::COMMAND;
    command.source = peer; command.destination = local;
    command.opcode = Protocol::OPCODE_TEST;
    TransactionEngine::NodeDuplicateTracker duplicates;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TransactionEngine::NodeCommandOutcome::ACK_SUCCESS),
        static_cast<uint8_t>(TransactionEngine::evaluateNodeCommand(
            command, identity, duplicates).outcome));
    command.source = local;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TransactionEngine::NodeCommandOutcome::IGNORE_WRONG_SENDER),
        static_cast<uint8_t>(TransactionEngine::evaluateNodeCommand(
            command, identity, duplicates).outcome));
    command.source = peer; command.destination = peer;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TransactionEngine::NodeCommandOutcome::IGNORE_WRONG_DESTINATION),
        static_cast<uint8_t>(TransactionEngine::evaluateNodeCommand(
            command, identity, duplicates).outcome));
    TEST_ASSERT_EQUAL_UINT8(local, identity.localDeviceId());
    TEST_ASSERT_EQUAL_UINT8(peer, identity.peerDeviceId());
}

void testDefaultHubRouting() { testHubWireAndTransactionRouting(1, 0x10); }
void testAlternateHubRouting() { testHubWireAndTransactionRouting(0x21, 0x42); }
void testDefaultNodeRouting() { testNodeWireAndTransactionRouting(0x10, 1); }
void testAlternateNodeRouting() { testNodeWireAndTransactionRouting(0x42, 0x21); }

void testRadioRoleOperationsUseSnapshotWithoutMutation() {
    const Resolution hubResolution = Resolution::fromRecovery(
        recovered(record(DeviceIdentity::Role::HUB, 0x21, 0x42)));
    const Resolution nodeResolution = Resolution::fromRecovery(
        recovered(record(DeviceIdentity::Role::NODE, 0x42, 0x21)));
    const auto& hubIdentity = *hubResolution.snapshot();
    const auto& nodeIdentity = *nodeResolution.snapshot();
    RadioOperationBridge::HubStructuredOperationBridge hub;
    HostProtocol::OperationRequest request = {};
    request.category = HostProtocol::OperationCategory::DEVICE;
    request.operation = HostProtocol::OperationCode::PING;
    request.targetDeviceId = hubIdentity.peerDeviceId();
    HostProtocol::setNoneValue(request.value);
    const auto submitted = hub.submit(1, request, hubIdentity, 0, 100,
        1, true);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RadioOperationBridge::HubAction::TRANSMIT_COMMAND),
        static_cast<uint8_t>(submitted.action));
    TEST_ASSERT_EQUAL_UINT8(0x21, submitted.packet.source);
    TEST_ASSERT_EQUAL_UINT8(0x42, submitted.packet.destination);
    RadioOperationBridge::NodeStructuredOperationProcessor node;
    const auto admitted = node.admit(submitted.packet, nodeIdentity);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RadioOperationBridge::NodeAction::ACK_THEN_EXECUTE),
        static_cast<uint8_t>(admitted.action));
    TEST_ASSERT_EQUAL_UINT8(0x42, admitted.acknowledgment.source);
    TEST_ASSERT_EQUAL_UINT8(0x21, admitted.acknowledgment.destination);
    auto wrongHub = submitted.packet;
    wrongHub.source = 0x33;
    RadioOperationBridge::NodeStructuredOperationProcessor wrongSource;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RadioOperationBridge::NodeAction::IGNORE),
        static_cast<uint8_t>(wrongSource.admit(wrongHub, nodeIdentity).action));
    auto wrongDestination = submitted.packet;
    wrongDestination.destination = 0x33;
    RadioOperationBridge::NodeStructuredOperationProcessor wrongTarget;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RadioOperationBridge::NodeAction::IGNORE),
        static_cast<uint8_t>(wrongTarget.admit(wrongDestination, nodeIdentity).action));
    TEST_ASSERT_EQUAL_UINT8(0x21, hubIdentity.localDeviceId());
    TEST_ASSERT_EQUAL_UINT8(0x42, nodeIdentity.localDeviceId());
}

void testHubEventLedgerUsesSnapshotAndRetainsIdentity(uint8_t local,
                                                       uint8_t peer) {
    const Resolution resolution = Resolution::fromRecovery(
        recovered(record(DeviceIdentity::Role::HUB, local, peer)));
    const auto& identity = *resolution.snapshot();
    EventStorageFixture storage;
    HubEventLedger::Ledger ledger;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HubEventLedger::Status::READY),
        static_cast<uint8_t>(ledger.recover(storage, identity)));
    EventProtocol::Event event = {};
    event.source = peer; event.destination = local;
    event.sequence = 5; event.family = 0x40;
    event.epoch = 2; event.id = 3; event.lifetimeBudgetSeconds = 60;
    event.bodyLength = 1; event.body[0] = 1;
    uint8_t bytes[Protocol::MAX_PACKET_SIZE] = {};
    size_t length = 0;
    TEST_ASSERT_TRUE(EventProtocol::encodeEvent(event, bytes, sizeof(bytes), length));
    EventRadioIntegration::HubAdapter adapter;
    const auto result = adapter.process(bytes, length, identity, ledger);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EventRadioIntegration::HubAction::START_EVENT_ACK),
        static_cast<uint8_t>(result.action));
    TEST_ASSERT_EQUAL_UINT8(1, ledger.activeCount());
    Protocol::Packet wire = {};
    TEST_ASSERT_TRUE(Protocol::decode(bytes, length, wire));
    wire.source = local;
    TEST_ASSERT_TRUE(Protocol::encode(wire, bytes, sizeof(bytes), length));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EventRadioIntegration::HubAction::DISCARD),
        static_cast<uint8_t>(adapter.process(bytes, length, identity, ledger).action));
    wire.source = peer; wire.destination = peer;
    TEST_ASSERT_TRUE(Protocol::encode(wire, bytes, sizeof(bytes), length));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EventRadioIntegration::HubAction::DISCARD),
        static_cast<uint8_t>(adapter.process(bytes, length, identity, ledger).action));
    TEST_ASSERT_EQUAL_UINT8(local, identity.localDeviceId());
    TEST_ASSERT_EQUAL_UINT8(peer, identity.peerDeviceId());
}

void testDefaultHubEventIdentity() { testHubEventLedgerUsesSnapshotAndRetainsIdentity(1, 0x10); }
void testAlternateHubEventIdentity() { testHubEventLedgerUsesSnapshotAndRetainsIdentity(0x21, 0x42); }

void testNodeEventCustodyUsesSnapshot(uint8_t local, uint8_t peer) {
    const Resolution resolution = Resolution::fromRecovery(
        recovered(record(DeviceIdentity::Role::NODE, local, peer)));
    const auto& identity = *resolution.snapshot();
    EventStorageFixture storage;
    EventEntropy entropy;
    NodeEventStore::Store store;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NodeEventStore::Status::READY),
        static_cast<uint8_t>(store.recover(storage, entropy, identity)));
    NodeEventStore::EventInput event = {};
    event.family = 0x40;
    event.lifetimeBudgetSeconds = 60;
    event.bodyLength = 1; event.body[0] = 1;
    const auto enqueued = store.enqueue(event);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NodeEventStore::EnqueueStatus::ENQUEUED),
        static_cast<uint8_t>(enqueued.status));
    TEST_ASSERT_EQUAL_UINT8(local, enqueued.identity.sourceDeviceId);
    EventSequence sequence; EventJitter jitter;
    NodeEventDelivery::Controller delivery;
    const auto recoveredDelivery = delivery.recover(
        store, identity, sequence, jitter, 0);
    TEST_ASSERT_TRUE(recoveredDelivery.status == NodeEventDelivery::ControllerStatus::OK ||
        recoveredDelivery.status == NodeEventDelivery::ControllerStatus::NO_ACTIVE_EVENT);
    TEST_ASSERT_EQUAL_UINT8(local, identity.localDeviceId());
    TEST_ASSERT_EQUAL_UINT8(peer, identity.peerDeviceId());
}

void testDefaultNodeEventCustody() { testNodeEventCustodyUsesSnapshot(0x10, 1); }
void testAlternateNodeEventCustody() { testNodeEventCustodyUsesSnapshot(0x42, 0x21); }

void testWrongRoleCannotRunEventOrTransactionCoordinator() {
    const Resolution hub = Resolution::fromRecovery(
        recovered(record(DeviceIdentity::Role::HUB, 0x21, 0x42)));
    const Resolution node = Resolution::fromRecovery(
        recovered(record(DeviceIdentity::Role::NODE, 0x42, 0x21)));
    EventStorageFixture storage;
    EventEntropy entropy;
    NodeEventStore::Store nodeStore;
    HubEventLedger::Ledger ledger;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(NodeEventStore::Status::INVALID_SOURCE_DEVICE_ID),
        static_cast<uint8_t>(nodeStore.recover(storage, entropy, *hub.snapshot())));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HubEventLedger::Status::INVALID_CONFIGURATION),
        static_cast<uint8_t>(ledger.recover(storage, *node.snapshot())));
    TEST_ASSERT_EQUAL_UINT8(0, storage.writes);
    Protocol::Packet command = {};
    command.type = Protocol::PacketType::COMMAND;
    command.source = 0x21; command.destination = 0x42;
    command.opcode = Protocol::OPCODE_TEST;
    TransactionEngine::NodeDuplicateTracker duplicates;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TransactionEngine::NodeCommandOutcome::IGNORE_WRONG_SENDER),
        static_cast<uint8_t>(TransactionEngine::evaluateNodeCommand(
            command, *hub.snapshot(), duplicates).outcome));
    WireOperations::Request request = {};
    request.value.type = static_cast<uint8_t>(DeviceCapabilities::ValueType::NONE);
    TEST_ASSERT_EQUAL_UINT8(0, WireOperations::makeCommand(*node.snapshot(),
        1, static_cast<uint8_t>(Protocol::Opcode::PING), request).source);
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(testHubAndNodeResolveFromRecoveredBytes);
    RUN_TEST(testEveryNonProvisionedRecoveryOutcomeIsRadioOff);
    RUN_TEST(testInvalidAndUnsupportedRecordsFailClosed);
    RUN_TEST(testForgedProvisionedOutcomeCannotActivatePendingOrBadEndpoints);
    RUN_TEST(testRealStoreRecoveryReportsUnsupportedProfilesRadioOff);
    RUN_TEST(testUnknownSchemaRemainsInvalidEvenBesideUnsupportedProfile);
    RUN_TEST(testSnapshotIsIndependentOfMutableStoreRecordAndBytes);
    RUN_TEST(testNetworkIdNeverChangesWireOnePacketBytes);
    RUN_TEST(testDefaultHubRouting);
    RUN_TEST(testAlternateHubRouting);
    RUN_TEST(testDefaultNodeRouting);
    RUN_TEST(testAlternateNodeRouting);
    RUN_TEST(testRadioRoleOperationsUseSnapshotWithoutMutation);
    RUN_TEST(testDefaultHubEventIdentity);
    RUN_TEST(testAlternateHubEventIdentity);
    RUN_TEST(testDefaultNodeEventCustody);
    RUN_TEST(testAlternateNodeEventCustody);
    RUN_TEST(testWrongRoleCannotRunEventOrTransactionCoordinator);
    return UNITY_END();
}
