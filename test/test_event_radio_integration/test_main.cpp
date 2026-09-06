#include <unity.h>

#include "hub_event_radio.h"
#include "node_event_radio.h"
#include "hub_receive_release.h"

using namespace EventRadioIntegration;

namespace {

class HubStorage final : public HubEventLedger::Storage {
public:
    EventStorage::FixedCopy<EventRecords::HUB_METADATA_SIZE> metadata[2] = {};
    EventStorage::FixedCopy<EventRecords::HUB_RECORD_SIZE> records[8][2] = {};
    bool failRecordWrites = false;
    unsigned recordWrites = 0;
    HubStorage() {
        metadata[0].status = metadata[1].status = EventStorage::FixedReadStatus::MISSING;
        for (auto& slot : records)
            slot[0].status = slot[1].status = EventStorage::FixedReadStatus::MISSING;
    }
    EventIdentity::StorageResult readHubMetadata(EventStorage::CopySlot c,
        uint8_t* o, size_t n, size_t& l) override { return read(metadata[idx(c)], o, n, l); }
    EventIdentity::StorageResult writeHubMetadata(EventStorage::CopySlot c,
        const uint8_t* i, size_t n) override { return write(metadata[idx(c)], i, n); }
    EventIdentity::StorageResult readHubEventCopy(uint8_t s, EventStorage::CopySlot c,
        uint8_t* o, size_t n, size_t& l) override { return read(records[s][idx(c)], o, n, l); }
    EventIdentity::StorageResult writeHubEventCopy(uint8_t s, EventStorage::CopySlot c,
        const uint8_t* i, size_t n) override {
        ++recordWrites;
        if (failRecordWrites) return EventIdentity::StorageResult::ERROR;
        return write(records[s][idx(c)], i, n);
    }
    EventIdentity::StorageResult commit() override { return EventIdentity::StorageResult::OK; }
private:
    static size_t idx(EventStorage::CopySlot c) { return c == EventStorage::CopySlot::A ? 0 : 1; }
    template<size_t N> static EventIdentity::StorageResult read(
        EventStorage::FixedCopy<N>& x, uint8_t* o, size_t n, size_t& l) {
        l = 0;
        if (x.status == EventStorage::FixedReadStatus::MISSING) return EventIdentity::StorageResult::MISSING;
        if (n < N) return EventIdentity::StorageResult::ERROR;
        for (size_t k=0;k<N;++k) o[k]=x.bytes[k]; l=N; return EventIdentity::StorageResult::OK;
    }
    template<size_t N> static EventIdentity::StorageResult write(
        EventStorage::FixedCopy<N>& x, const uint8_t* i, size_t n) {
        if (n != N) return EventIdentity::StorageResult::ERROR;
        x.status=EventStorage::FixedReadStatus::OK;
        for(size_t k=0;k<N;++k)x.bytes[k]=i[k]; return EventIdentity::StorageResult::OK;
    }
};

EventProtocol::Event event(uint32_t id=1) {
    EventProtocol::Event e={}; e.source=2; e.destination=1; e.sequence=7;
    e.family=0x40; e.epoch=9; e.id=id; e.lifetimeBudgetSeconds=600;
    e.bodyLength=1; e.body[0]=0x02; return e;
}
size_t encode(const EventProtocol::Event& e, uint8_t* bytes) {
    size_t length=0; TEST_ASSERT_TRUE(EventProtocol::encodeEvent(e,bytes,32,length)); return length;
}

void testNodeStandbyRecheckAndOwnership() {
    NodeArbiter a; NodeSafePoint p={true,true,false,false,false,false};
    TEST_ASSERT_EQUAL_UINT8((uint8_t)NodeAcquireResult::ACQUIRE_STANDBY,
        (uint8_t)a.requestEvent(p,NodeEventDelivery::RuntimeState::READY));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)NodeAcquireResult::RECEIVED_PACKET_WON,
        (uint8_t)a.finishStandby(true,true));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)NodeOwner::LISTENING,(uint8_t)a.owner());
    TEST_ASSERT_EQUAL_UINT8((uint8_t)NodeAcquireResult::ACQUIRE_STANDBY,
        (uint8_t)a.requestEvent(p,NodeEventDelivery::RuntimeState::READY));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)NodeAcquireResult::GRANT_EVENT_TX,
        (uint8_t)a.finishStandby(true,false));
    TEST_ASSERT_TRUE(a.eventOwnsRadio());
}

void testNodeSynchronousOwnersBlockEvent() {
    NodeArbiter a; NodeSafePoint p={true,true,false,true,false,false};
    TEST_ASSERT_EQUAL_UINT8((uint8_t)NodeAcquireResult::DENIED,
        (uint8_t)a.requestEvent(p,NodeEventDelivery::RuntimeState::READY));
    a.beginCommandPreAck(); p.synchronousWork=false;
    TEST_ASSERT_EQUAL_UINT8((uint8_t)NodeAcquireResult::DENIED,
        (uint8_t)a.requestEvent(p,NodeEventDelivery::RuntimeState::READY));
}

void testPreAckDeadlineIsNonblockingWrapSafeAndOneShot() {
    CommandPreAckTimer t; t.begin(0xFFFFFFF0U);
    TEST_ASSERT_FALSE(t.due(0x00000053U));
    TEST_ASSERT_TRUE(t.due(0x00000054U));
    TEST_ASSERT_FALSE(t.due(0x00000055U));
    t.clear(); TEST_ASSERT_FALSE(t.active());
}

void testHubNewAdmissionCommitBeforeAckAndDuplicate() {
    HubStorage s; HubEventLedger::Ledger l;
    TEST_ASSERT_EQUAL_UINT8((uint8_t)HubEventLedger::Status::READY,(uint8_t)l.recover(s,1,2));
    HubAdapter adapter; uint8_t bytes[32]; auto e=event(); size_t n=encode(e,bytes);
    HubResult first=adapter.process(bytes,n,1,2,l);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)HubAction::START_EVENT_ACK,(uint8_t)first.action);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)EventProtocol::AdmissionStatus::ADMITTED,(uint8_t)first.status);
    TEST_ASSERT_TRUE(first.ledgerMutated); TEST_ASSERT_EQUAL_UINT(1,l.activeCount());
    unsigned writes=s.recordWrites;
    HubResult duplicate=adapter.process(bytes,n,1,2,l);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)EventProtocol::AdmissionStatus::ADMITTED,(uint8_t)duplicate.status);
    TEST_ASSERT_FALSE(duplicate.ledgerMutated); TEST_ASSERT_EQUAL_UINT(writes,s.recordWrites);
}

void testHubStorageFailureCannotExposeAck() {
    HubStorage s; HubEventLedger::Ledger l; l.recover(s,1,2); s.failRecordWrites=true;
    uint8_t bytes[32]; size_t n=encode(event(),bytes);
    HubResult r=HubAdapter().process(bytes,n,1,2,l);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)HubAction::DISCARD,(uint8_t)r.action);
    TEST_ASSERT_EQUAL_UINT(0,l.activeCount());
}

void testHubUnsupportedMalformedAndUncorrelatable() {
    HubStorage s; HubEventLedger::Ledger l; l.recover(s,1,2); HubAdapter a;
    Protocol::Packet p={}; p.type=Protocol::PacketType::EVENT; p.source=2; p.destination=1;
    p.sequence=3; p.opcode=0x42; p.payloadLength=15; EventProtocol::writeUint32Le(p.payload,1);
    EventProtocol::writeUint32Le(p.payload+4,2); EventProtocol::writeUint32Le(p.payload+9,60);
    p.payload[13]=1; p.payload[14]=1; uint8_t bytes[32]; size_t n=0;
    TEST_ASSERT_TRUE(Protocol::encode(p,bytes,sizeof(bytes),n));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)EventProtocol::AdmissionStatus::UNSUPPORTED_EVENT,
        (uint8_t)a.process(bytes,n,1,2,l).status);
    p.opcode=0x40; p.payload[8]=0x80; Protocol::encode(p,bytes,sizeof(bytes),n);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)EventProtocol::AdmissionStatus::MALFORMED_EVENT,
        (uint8_t)a.process(bytes,n,1,2,l).status);
    EventProtocol::writeUint32Le(p.payload,0); Protocol::encode(p,bytes,sizeof(bytes),n);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)HubAction::DISCARD,(uint8_t)a.process(bytes,n,1,2,l).action);
    TEST_ASSERT_EQUAL_UINT(0,l.activeCount());
}

void testHubCapacityAndMismatch() {
    HubStorage s; HubEventLedger::Ledger l; l.recover(s,1,2); HubAdapter a; uint8_t b[32];
    RuntimeState::State diagnostics(RuntimeState::DeviceRole::HUB, 1, 2);
    l.setDiagnostics(&diagnostics);
    for(uint32_t id=1;id<=8;++id){auto e=event(id);size_t n=encode(e,b);a.process(b,n,1,2,l,&diagnostics);}
    auto ninth=event(9);size_t n=encode(ninth,b);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)EventProtocol::AdmissionStatus::CAPACITY,
        (uint8_t)a.process(b,n,1,2,l,&diagnostics).status);
    auto mismatch=event(1);mismatch.body[0]=0x03;n=encode(mismatch,b);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)EventProtocol::AdmissionStatus::IDENTITY_CONTENT_MISMATCH,
        (uint8_t)a.process(b,n,1,2,l,&diagnostics).status);
    TEST_ASSERT_EQUAL_UINT32(8, diagnostics.eventSnapshot().counters.successfulAdmissions);
    TEST_ASSERT_EQUAL_UINT32(1, diagnostics.eventSnapshot().counters.hubCapacityRejections);
    TEST_ASSERT_EQUAL_UINT32(1, diagnostics.eventSnapshot().counters.identityContentMismatches);
}

void testHubArbiterPreservesOwnerAndDeadline() {
    HubArbiter a; a.setOwner(HubOwner::COMMAND_WAIT_RESPONSE,12345);
    TEST_ASSERT_TRUE(a.beginEventAck()); TEST_ASSERT_EQUAL_UINT8((uint8_t)HubOwner::EVENT_ACK_TX,(uint8_t)a.owner());
    TEST_ASSERT_EQUAL_UINT8((uint8_t)HubOwner::COMMAND_WAIT_RESPONSE,(uint8_t)a.finishEventAck());
    TEST_ASSERT_EQUAL_UINT32(12345,a.deadline());
    a.setOwner(HubOwner::IDLE_RECEIVE); TEST_ASSERT_FALSE(a.commandMayAcquire(true)); TEST_ASSERT_TRUE(a.commandMayAcquire(false));
}

void testWrongWireTypesNeverReachLedger() {
    HubStorage s; HubEventLedger::Ledger l; l.recover(s,1,2); HubAdapter a;
    Protocol::Packet p={}; p.type=Protocol::PacketType::ACK; p.source=2; p.destination=1;
    p.sequence=7; p.opcode=0x40; p.payloadLength=1; p.payload[0]=0;
    uint8_t bytes[32]; size_t n=0; TEST_ASSERT_TRUE(Protocol::encode(p,bytes,sizeof(bytes),n));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)HubAction::DISCARD,(uint8_t)a.process(bytes,n,1,2,l).action);
    p.type=Protocol::PacketType::RESPONSE; TEST_ASSERT_TRUE(Protocol::encode(p,bytes,sizeof(bytes),n));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)HubAction::DISCARD,(uint8_t)a.process(bytes,n,1,2,l).action);
    TEST_ASSERT_EQUAL_UINT(0,l.activeCount());
}

void testAckStartLossKeepsCustodyAndRetryRegeneratesAck() {
    HubStorage s; HubEventLedger::Ledger l; l.recover(s,1,2); HubAdapter adapter;
    uint8_t bytes[32]; size_t n=encode(event(),bytes);
    HubResult admitted=adapter.process(bytes,n,1,2,l);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)HubAction::START_EVENT_ACK,(uint8_t)admitted.action);
    // A future RadioLib start failure changes no ledger state.
    TEST_ASSERT_EQUAL_UINT(1,l.activeCount());
    HubResult retry=adapter.process(bytes,n,1,2,l);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)EventProtocol::AdmissionStatus::ADMITTED,(uint8_t)retry.status);
    TEST_ASSERT_FALSE(retry.ledgerMutated); TEST_ASSERT_EQUAL_UINT(1,l.activeCount());
}

void testDioDispatchObservationDetectsOwnershipOverwriteWithoutChangingIt() {
    NodeArbiter arbiter; EventTxDiagnostics::Observer observer;
    NodeSafePoint point = {true, true, false, false, false, false};
    arbiter.requestEvent(point, NodeEventDelivery::RuntimeState::READY);
    arbiter.finishStandby(true, false);
    observer.dispatchDio(arbiter.eventOwnsRadio(), true, 10);
    TEST_ASSERT_TRUE(arbiter.eventOwnsRadio());
    // Characterize existing production receive restoration overriding ownership.
    arbiter.restoreListening();
    observer.dispatchDio(arbiter.eventOwnsRadio(), true, 20);
    observer.clearingFlag(true, true, 21);
    observer.clearingFlag(false, true, 22);
    observer.clearingFlag(true, false, 23);
    using C = EventTxDiagnostics::Counter;
    TEST_ASSERT_EQUAL_UINT16(1, observer.snapshot().counters[(uint8_t)C::DIO_CLASSIFIED_EVENT]);
    TEST_ASSERT_EQUAL_UINT16(1, observer.snapshot().counters[(uint8_t)C::DIO_OTHER_PATH_DURING_TX]);
    TEST_ASSERT_EQUAL_UINT16(1, observer.snapshot().counters[(uint8_t)C::PENDING_FLAG_CLEARED_DURING_TX]);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)NodeOwner::LISTENING, (uint8_t)arbiter.owner());
}

struct ReleaseRadio {
    volatile bool pending = false;
    bool irqDuringStandby = false, irqDuringReceive = false;
    int standbyResult = 0;
    bool receiveResult = true;
    unsigned standbyCalls = 0, receiveCalls = 0;
    bool listening = false;
    int standby() {
        ++standbyCalls;
        if (irqDuringStandby) pending = true;
        if (standbyResult == 0) listening = false;
        return standbyResult;
    }
    bool receive() {
        ++receiveCalls;
        if (irqDuringReceive) pending = true;
        listening = receiveResult;
        return receiveResult;
    }
};

ReleaseReceiveResult releaseReceive(ReleaseRadio& radio, const HubArbiter& arbiter,
                                    bool eventActive = false) {
    return restoreReleasedReceive(radio, radio.pending, eventActive, arbiter,
        [&] { return radio.receive(); });
}

void testLegacyReleasedBoundaryRestoresReceiveOnce() {
    ReleaseRadio radio; HubArbiter arbiter;
    arbiter.setOwner(HubOwner::COMMAND_WAIT_ACK, 2500);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ReleaseReceiveResult::RESTORED,
        (uint8_t)releaseReceive(radio, arbiter));
    TEST_ASSERT_TRUE(radio.listening);
    TEST_ASSERT_EQUAL_UINT(1, radio.standbyCalls);
    TEST_ASSERT_EQUAL_UINT(1, radio.receiveCalls);
    TEST_ASSERT_FALSE(radio.pending);
    // Release acquisition does not rewrite saved owner/deadline metadata.
    TEST_ASSERT_EQUAL_UINT8((uint8_t)HubOwner::COMMAND_WAIT_ACK, (uint8_t)arbiter.owner());
    TEST_ASSERT_EQUAL_UINT32(2500, arbiter.deadline());
}

void testStructuredReleasedBoundaryRestoresReceiveOnce() {
    ReleaseRadio radio; HubArbiter arbiter;
    arbiter.setOwner(HubOwner::COMMAND_WAIT_RESPONSE, 5000);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ReleaseReceiveResult::RESTORED,
        (uint8_t)releaseReceive(radio, arbiter));
    TEST_ASSERT_TRUE(radio.listening);
    TEST_ASSERT_EQUAL_UINT(1, radio.receiveCalls);
    TEST_ASSERT_EQUAL_UINT32(5000, arbiter.deadline());
}

void testReleasePreservesPendingIrqBeforeStandby() {
    ReleaseRadio radio; HubArbiter arbiter; radio.pending = true;
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ReleaseReceiveResult::PENDING_IRQ,
        (uint8_t)releaseReceive(radio, arbiter));
    TEST_ASSERT_TRUE(radio.pending);
    TEST_ASSERT_EQUAL_UINT(0, radio.standbyCalls);
    TEST_ASSERT_EQUAL_UINT(0, radio.receiveCalls);
}

void testReleaseRechecksIrqAfterStandby() {
    ReleaseRadio radio; HubArbiter arbiter; radio.irqDuringStandby = true;
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ReleaseReceiveResult::PENDING_IRQ,
        (uint8_t)releaseReceive(radio, arbiter));
    TEST_ASSERT_TRUE(radio.pending);
    TEST_ASSERT_EQUAL_UINT(1, radio.standbyCalls);
    TEST_ASSERT_EQUAL_UINT(0, radio.receiveCalls);
}

void testReleaseDoesNotClearIrqRaisedDuringReceiveStart() {
    ReleaseRadio radio; HubArbiter arbiter; radio.irqDuringReceive = true;
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ReleaseReceiveResult::RESTORED,
        (uint8_t)releaseReceive(radio, arbiter));
    TEST_ASSERT_TRUE(radio.pending);
    TEST_ASSERT_TRUE(radio.listening);
}

void testReleaseAfterConsumedCompletionRestoresReceive() {
    ReleaseRadio radio; HubArbiter arbiter;
    radio.pending = true;
    radio.pending = false; // Existing dispatcher consumes the completion first.
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ReleaseReceiveResult::RESTORED,
        (uint8_t)releaseReceive(radio, arbiter));
    TEST_ASSERT_EQUAL_UINT(1, radio.receiveCalls);
}

void testReleaseDoesNotPreemptEitherEventOwnershipIndicator() {
    ReleaseRadio radio; HubArbiter arbiter;
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ReleaseReceiveResult::EVENT_OWNED,
        (uint8_t)releaseReceive(radio, arbiter, true));
    arbiter.setOwner(HubOwner::COMMAND_WAIT_ACK, 1234);
    TEST_ASSERT_TRUE(arbiter.beginEventAck());
    radio.pending = true; // The pending Event TX completion must remain intact.
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ReleaseReceiveResult::EVENT_OWNED,
        (uint8_t)releaseReceive(radio, arbiter));
    TEST_ASSERT_TRUE(radio.pending);
    TEST_ASSERT_EQUAL_UINT(0, radio.standbyCalls);
    TEST_ASSERT_EQUAL_UINT(0, radio.receiveCalls);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)HubOwner::COMMAND_WAIT_ACK, (uint8_t)arbiter.finishEventAck());
    TEST_ASSERT_EQUAL_UINT32(1234, arbiter.deadline());
}

void testReleaseStandbyFailureIsExplicitWithoutReceive() {
    ReleaseRadio radio; HubArbiter arbiter; radio.standbyResult = -1;
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ReleaseReceiveResult::STANDBY_FAILED,
        (uint8_t)releaseReceive(radio, arbiter));
    TEST_ASSERT_FALSE(radio.listening);
    TEST_ASSERT_EQUAL_UINT(1, radio.standbyCalls);
    TEST_ASSERT_EQUAL_UINT(0, radio.receiveCalls);
}

void testReleaseReceiveFailureIsExplicitWithoutAutomaticRetry() {
    ReleaseRadio radio; HubArbiter arbiter; radio.receiveResult = false;
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ReleaseReceiveResult::RECEIVE_FAILED,
        (uint8_t)releaseReceive(radio, arbiter));
    TEST_ASSERT_FALSE(radio.listening);
    TEST_ASSERT_EQUAL_UINT(1, radio.standbyCalls);
    TEST_ASSERT_EQUAL_UINT(1, radio.receiveCalls);
}

void testReleaseReportsStandbyFailureEvenWhenIrqAlsoArrives() {
    ReleaseRadio radio; HubArbiter arbiter;
    radio.standbyResult = -1; radio.irqDuringStandby = true;
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ReleaseReceiveResult::STANDBY_FAILED,
        (uint8_t)releaseReceive(radio, arbiter));
    TEST_ASSERT_TRUE(radio.pending);
    TEST_ASSERT_EQUAL_UINT(0, radio.receiveCalls);
}

} // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(testLegacyReleasedBoundaryRestoresReceiveOnce);
    RUN_TEST(testStructuredReleasedBoundaryRestoresReceiveOnce);
    RUN_TEST(testReleasePreservesPendingIrqBeforeStandby);
    RUN_TEST(testReleaseRechecksIrqAfterStandby);
    RUN_TEST(testReleaseDoesNotClearIrqRaisedDuringReceiveStart);
    RUN_TEST(testReleaseAfterConsumedCompletionRestoresReceive);
    RUN_TEST(testReleaseDoesNotPreemptEitherEventOwnershipIndicator);
    RUN_TEST(testReleaseStandbyFailureIsExplicitWithoutReceive);
    RUN_TEST(testReleaseReceiveFailureIsExplicitWithoutAutomaticRetry);
    RUN_TEST(testReleaseReportsStandbyFailureEvenWhenIrqAlsoArrives);
    RUN_TEST(testDioDispatchObservationDetectsOwnershipOverwriteWithoutChangingIt);
    RUN_TEST(testNodeStandbyRecheckAndOwnership);
    RUN_TEST(testNodeSynchronousOwnersBlockEvent);
    RUN_TEST(testPreAckDeadlineIsNonblockingWrapSafeAndOneShot);
    RUN_TEST(testHubNewAdmissionCommitBeforeAckAndDuplicate);
    RUN_TEST(testHubStorageFailureCannotExposeAck);
    RUN_TEST(testHubUnsupportedMalformedAndUncorrelatable);
    RUN_TEST(testHubCapacityAndMismatch);
    RUN_TEST(testHubArbiterPreservesOwnerAndDeadline);
    RUN_TEST(testWrongWireTypesNeverReachLedger);
    RUN_TEST(testAckStartLossKeepsCustodyAndRetryRegeneratesAck);
    return UNITY_END();
}
