"""Source wiring checks complement native tests of the shared production seams.

These do not simulate Arduino/RadioLib or claim hardware IRQ/timing coverage.
Run: python -m unittest discover -s test -p test_event_production_wiring.py
"""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


def body(source, name):
    match = re.search(r"^(?:void|bool) " + name + r"\([^;]*?\)\s*\{", source, re.M)
    if match is None:
        raise AssertionError(f"Production function missing: {name}")
    start = match.end()
    depth = 1
    for end in range(start, len(source)):
        depth += (source[end] == "{") - (source[end] == "}")
        if depth == 0:
            return source[start:end]
    raise AssertionError(f"Unclosed production function: {name}")


class EventProductionWiringTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.node = (ROOT / "src-rx/main.cpp").read_text()
        cls.hub = (ROOT / "src-tx/main.cpp").read_text()

    def test_node_service_samples_after_packet_handling_without_loop_time_argument(self):
        loop = body(self.node, "loop")
        self.assertLess(loop.index("handleReceivedPacket();"), loop.index("serviceNodeEvents();"))
        self.assertIn("void serviceNodeEvents()", self.node)
        service = body(self.node, "serviceNodeEvents")
        self.assertIn("NodeEventDelivery::serviceNow(", service)
        self.assertIn("eventDelivery, synchronousWork, [] { return millis(); }", service)
        self.assertIn("const uint32_t nowMs = tick.now;", service)
        self.assertNotIn("eventDelivery.service(nowMs", service)

    def test_node_producers_share_tracked_creation_and_fail_closed(self):
        self.assertIn("NodeEventDelivery::TrackedCreationSink<decltype(&millis)> eventCreationSink(\n"
                      "    nodeEventStore, eventDelivery, &millis);", self.node)
        self.assertNotIn("EventProducers::StoreCreationSink eventCreationSink", self.node)
        self.assertNotIn("nodeEventStore.enqueue(", self.node)
        for name in ("serviceButton", "serviceSensorThresholdProducer"):
            producer = body(self.node, name)
            self.assertIn("eventCreationSink", producer)
            self.assertIn("CreationResult::STORAGE_FAILURE", producer)
            self.assertIn("eventSubsystemReady = false;", producer)

    def test_node_completion_samples_after_producers_without_loop_time_argument(self):
        loop = body(self.node, "loop")
        completion = "NodeEventDelivery::txCompletedNow(eventDelivery, [] { return millis(); })"
        self.assertLess(loop.index("serviceButton(nowMs);"), loop.index(completion))
        self.assertLess(loop.index("serviceSensorThresholdProducer();"), loop.index(completion))
        self.assertNotIn("eventDelivery.txCompleted(nowMs)", self.node)
        self.assertEqual(1, self.node.count("NodeEventDelivery::txCompletedNow("))

    def test_structured_commands_use_existing_hub_arbiter_ownership(self):
        start = body(self.hub, "startHostStructuredTransmission")
        self.assertLess(start.index("HubOwner::COMMAND_TX"),
                        start.index("radio.startTransmit("))
        self.assertIn("hubRadioArbiter.setOwner(priorOwner, priorDeadline);", start)
        service = body(self.hub, "serviceProductionHost")
        self.assertLess(service.index("hubRadioArbiter.commandMayAcquire(operationDone)"),
                        service.index("radio.standby()"))
        release = body(self.hub, "finishHostStructuredOwnership")
        self.assertIn("HubOwner::IDLE_RECEIVE", release)
        acknowledgment = body(self.hub, "processAcknowledgment")
        self.assertIn("HubOwner::COMMAND_WAIT_RESPONSE", acknowledgment)
        transport = body(self.hub, "serviceHubTransport")
        self.assertIn("HubOwner::COMMAND_WAIT_ACK", transport)

    def test_legacy_completion_and_abort_share_one_shot_idle_restoration(self):
        completion = body(self.hub, "completeAndScheduleNextTransaction")
        self.assertLess(completion.index("transactionState.completeTransaction();"),
                        completion.index("scheduleNextTransaction();"))
        schedule = body(self.hub, "scheduleNextTransaction")
        self.assertLess(schedule.index("RuntimePhase::IDLE"), schedule.index("nextTransmitAt ="))
        self.assertLess(schedule.index("nextTransmitAt = millis() + TRANSACTION_INTERVAL_MS;"),
                        schedule.index("restoreReleasedTransactionReceive();"))
        self.assertEqual(1, schedule.count("restoreReleasedTransactionReceive();"))
        self.assertIn("completeAndScheduleNextTransaction();", body(self.hub, "startCommandTransmission"))
        ack = body(self.hub, "processAcknowledgment")
        self.assertEqual(2, ack.count("completeAndScheduleNextTransaction();"))
        self.assertNotIn("radio.standby();", ack)

    def test_legacy_retry_and_exhaustion_keep_scheduling_and_restore_receive(self):
        retry = body(self.hub, "scheduleRetryOrNext")
        self.assertIn("transactionState.attemptFailed();", retry)
        self.assertIn("nextTransmitAt = millis() + RETRY_DELAY_MS;\n"
                      "        restoreReleasedTransactionReceive();", retry)
        self.assertIn("completeAndScheduleNextTransaction();", retry)
        self.assertNotIn("radio.standby();", retry)
        self.assertIn("TRANSACTION_INTERVAL_MS = 3000;", self.hub)
        self.assertIn("RETRY_DELAY_MS = 500;", self.hub)

    def test_structured_terminal_and_timeout_release_restore_after_owner_release(self):
        release = body(self.hub, "finishHostStructuredOwnership")
        self.assertLess(release.index("hostStructuredOwner = false;"),
                        release.index("restoreReleasedTransactionReceive();"))
        self.assertIn("nextTransmitAt = millis() + TRANSACTION_INTERVAL_MS;", release)
        self.assertEqual(1, release.count("restoreReleasedTransactionReceive();"))
        self.assertNotIn("radio.standby();", release)
        self.assertIn("finishHostStructuredOwnership();", body(self.hub, "processAcknowledgment"))
        self.assertIn("finishHostStructuredOwnership();", body(self.hub, "serviceHubTransport"))

    def test_release_uses_existing_receive_without_clearing_pending_irq(self):
        release = body(self.hub, "restoreReleasedTransactionReceive")
        self.assertIn("EventRadioIntegration::restoreReleasedReceive(radio, operationDone,", release)
        self.assertIn("eventAckTxActive, hubRadioArbiter", release)
        self.assertIn("restoreHubReceive(RuntimeState::RuntimePhase::IDLE, false)", release)
        self.assertIn("if (clearPending) operationDone = false;", body(self.hub, "restoreHubReceive"))

    def test_release_failure_is_reported_without_retry_loop(self):
        release = body(self.hub, "restoreReleasedTransactionReceive")
        for required in ("Result::STANDBY_FAILED", "Result::RECEIVE_FAILED",
                         "runtimeState.incrementRadioErrors();", "Health::DEGRADED",
                         "ErrorClass::RADIO_START_RECEIVE"):
            self.assertIn(required, release)
        self.assertNotIn("scheduleRetryOrNext(", release)
        self.assertNotIn("while (", release)

    def test_idle_loop_has_no_continuous_release_receive_restart(self):
        transport = body(self.hub, "serviceHubTransport")
        self.assertNotIn("restoreReleasedTransactionReceive(", transport)
        idle = transport[transport.index("if (runtimeState.phase() == RuntimeState::RuntimePhase::IDLE)"):]
        before_deadline = idle[:idle.index("if (radio.standby()")]
        self.assertIn("if ((long)(millis() - nextTransmitAt) < 0)", before_deadline)
        self.assertNotIn("restoreHubReceive(", before_deadline)
        self.assertNotIn("radio.startReceive(", before_deadline)

    def test_event_ack_completion_path_is_unchanged_and_has_priority(self):
        self.assertEqual("eventAckTxActive = false; hubRadioArbiter.finishEventAck(); "
                         "restoreHubReceive(eventAckPriorPhase);",
                         " ".join(body(self.hub, "finishEventAdmissionAck").split()))
        transport = body(self.hub, "serviceHubTransport")
        self.assertTrue(transport.lstrip().startswith("if (eventAckTxActive)"))
        self.assertIn("if (operationDone) { operationDone = false; finishEventAdmissionAck(); }", transport)


if __name__ == "__main__":
    unittest.main()
