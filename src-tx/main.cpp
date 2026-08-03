#include <Arduino.h>
#include <Wire.h>
#include "SSD1306Wire.h"
#include <RadioLib.h>
#include "protocol.h"
#include "device_config.h"
#include "redline_version.h"
#include "runtime_state.h"
#include "transaction_engine.h"
#include "device_input.h"
#include "device_ui.h"
#include "heltec_display.h"

SSD1306Wire display(0x3C, SDA_OLED, SCL_OLED);

SPIClass radioSPI(FSPI);
SX1262 radio = new Module(8, 14, 12, 13, radioSPI);

constexpr unsigned long INITIAL_DELAY_MS = 2000;
constexpr unsigned long TRANSACTION_INTERVAL_MS = 3000;
constexpr unsigned long RETRY_DELAY_MS = 500;
constexpr uint8_t APPLICATION_BUTTON_PIN = 0;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 30;
constexpr uint32_t BUTTON_LONG_PRESS_MS = 800;

volatile bool operationDone = false;

RuntimeState::State runtimeState(
    RuntimeState::DeviceRole::HUB,
    DeviceConfig::LOCAL_ID,
    DeviceConfig::PEER_ID
);
DeviceInput::Button applicationButton(
    BUTTON_DEBOUNCE_MS,
    BUTTON_LONG_PRESS_MS
);
DeviceUi::Controller uiController(0);
HeltecDisplay::Renderer displayRenderer(display);
DeviceUi::PeerState peerState = DeviceUi::PeerState::UNKNOWN;

unsigned long nextTransmitAt = 0;

TransactionEngine::HubTransactionState transactionState;

Protocol::Packet currentCommand = {};
uint8_t transmitBuffer[Protocol::MAX_PACKET_SIZE] = {};
size_t transmitLength = 0;

void IRAM_ATTR setRadioFlag() {
    operationDone = true;
}

void markPresentationChanged() {
    uiController.markDirty();
}

void setRuntimePhase(RuntimeState::RuntimePhase phase) {
    if (runtimeState.phase() != phase) {
        runtimeState.setPhase(phase);
        markPresentationChanged();
    }
}

void setHealth(RuntimeState::Health health) {
    if (runtimeState.health() != health) {
        runtimeState.setHealth(health);
        markPresentationChanged();
    }
}

void recordError(RuntimeState::ErrorClass error) {
    runtimeState.recordError(error);
    markPresentationChanged();
}

void setPeerState(DeviceUi::PeerState state) {
    if (peerState != state) {
        peerState = state;
        markPresentationChanged();
    }
}

DeviceUi::PresentationInput buildPresentationInput() {
    DeviceUi::PresentationInput input;
    input.role = runtimeState.role();
    input.localId = runtimeState.localId();
    input.peerId = runtimeState.peerId();
    input.ready = runtimeState.isReady();
    input.health = runtimeState.health();
    input.phase = runtimeState.phase();
    input.firmwareVersion = RedlineVersion::FIRMWARE;
    input.wireProtocolVersion = RedlineVersion::WIRE_PROTOCOL;
    input.hardwareProfile = RedlineVersion::HARDWARE_PROFILE;
    input.radioMetricsAvailable = runtimeState.hasRadioMetrics();
    input.rssi = runtimeState.latestRssi();
    input.snr = runtimeState.latestSnr();
    input.peerState = peerState;
    input.lastInboundPacket = runtimeState.lastInboundPacket();
    input.counters = runtimeState.counters();
    input.lastError = runtimeState.lastError();
    return input;
}

void renderCurrentPresentation(uint32_t nowMs) {
    const DeviceUi::PresentationSnapshot snapshot =
        DeviceUi::buildPresentation(
            uiController.screen(),
            buildPresentationInput()
        );
    displayRenderer.render(snapshot);
    uiController.recordRendered(nowMs);
}

void serviceButton(uint32_t nowMs) {
    const bool pressed = digitalRead(APPLICATION_BUTTON_PIN) == LOW;
    const DeviceInput::ButtonEvents events =
        applicationButton.update(pressed, nowMs);
    uiController.handle(events.first, nowMs);
    uiController.handle(events.second, nowMs);
}

void serviceUi(uint32_t nowMs) {
    const DeviceUi::UiAction action = uiController.update(nowMs);

    switch (action) {
        case DeviceUi::UiAction::RENDER:
            renderCurrentPresentation(nowMs);
            break;

        case DeviceUi::UiAction::DISPLAY_ON:
            displayRenderer.displayOn();
            break;

        case DeviceUi::UiAction::DISPLAY_ON_AND_RENDER:
            displayRenderer.displayOn();
            renderCurrentPresentation(nowMs);
            break;

        case DeviceUi::UiAction::DISPLAY_OFF:
            displayRenderer.displayOff();
            break;

        case DeviceUi::UiAction::NONE:
            break;
    }
}

void showStatus(const String& primary, const String& secondary) {
    Serial.print(primary);

    if (secondary.length() > 0) {
        Serial.print(" | ");
        Serial.print(secondary);
    }

    Serial.println();
}

void logVersionMetadata() {
    Serial.print("Wire Protocol: ");
    Serial.println(RedlineVersion::WIRE_PROTOCOL);
    Serial.print("Hardware profile: ");
    Serial.println(RedlineVersion::HARDWARE_PROFILE);
}

void logPacket(const char* direction, const Protocol::Packet& packet) {
    Serial.print(direction);
    Serial.print(" type=");
    Serial.print(static_cast<uint8_t>(packet.type));
    Serial.print(" src=");
    Serial.print(packet.source);
    Serial.print(" dst=");
    Serial.print(packet.destination);
    Serial.print(" seq=");
    Serial.print(packet.sequence);
    Serial.print(" opcode=");
    Serial.print(packet.opcode);
    Serial.print(" payload=");
    Serial.println(packet.payloadLength);
}

void scheduleNextTransaction() {
    setRuntimePhase(RuntimeState::RuntimePhase::IDLE);
    nextTransmitAt = millis() + TRANSACTION_INTERVAL_MS;
}

void completeAndScheduleNextTransaction() {
    transactionState.completeTransaction();
    scheduleNextTransaction();
}

void scheduleRetryOrNext(const String& reason) {
    radio.standby();
    const TransactionEngine::HubTransactionAction action =
        transactionState.attemptFailed();

    if (action == TransactionEngine::HubTransactionAction::RETRANSMIT) {
        runtimeState.incrementRetransmissions();
        markPresentationChanged();
        showStatus(
            reason,
            String("retry ") + transactionState.retryCount() +
            "/" + transactionState.maximumRetries()
        );

        setRuntimePhase(RuntimeState::RuntimePhase::IDLE);
        nextTransmitAt = millis() + RETRY_DELAY_MS;
        return;
    }

    showStatus(
        "COMMAND FAILED",
        String("SEQ ") + currentCommand.sequence
    );

    setHealth(RuntimeState::Health::DEGRADED);
    setPeerState(DeviceUi::PeerState::DEGRADED);
    completeAndScheduleNextTransaction();
}

bool prepareCommand() {
    currentCommand = {};

    currentCommand.type = Protocol::PacketType::COMMAND;
    currentCommand.source = runtimeState.localId();
    currentCommand.destination = runtimeState.peerId();
    currentCommand.sequence = transactionState.currentSequence();
    currentCommand.opcode = Protocol::OPCODE_TEST;
    currentCommand.payloadLength = 0;

    return Protocol::encode(
        currentCommand,
        transmitBuffer,
        sizeof(transmitBuffer),
        transmitLength
    );
}

bool startCommandTransmission() {
    if (!prepareCommand()) {
        showStatus("TX FAILED", "encode error");
        completeAndScheduleNextTransaction();
        return false;
    }

    operationDone = false;
    digitalWrite(LED_BUILTIN, HIGH);

    const int state = radio.startTransmit(
        transmitBuffer,
        transmitLength
    );

    if (state != RADIOLIB_ERR_NONE) {
        digitalWrite(LED_BUILTIN, LOW);
        runtimeState.incrementRadioErrors();
        recordError(RuntimeState::ErrorClass::RADIO_START_TRANSMIT);

        showStatus(
            "TX START FAILED",
            String("CODE ") + state
        );

        scheduleRetryOrNext("TX START FAILED");
        return false;
    }

    logPacket(
        transactionState.requestedTransmission() ==
                TransactionEngine::HubTransactionAction::TRANSMIT_INITIAL
            ? "TX"
            : "TX RETRY",
        currentCommand
    );

    showStatus(
        transactionState.requestedTransmission() ==
                TransactionEngine::HubTransactionAction::TRANSMIT_INITIAL
            ? "TX COMMAND"
            : "TX RETRY",
        String("SEQ ") + transactionState.currentSequence()
    );

    setRuntimePhase(RuntimeState::RuntimePhase::TRANSMITTING);
    return true;
}

bool startAckReceive(bool resetDeadline) {
    operationDone = false;

    const int state = radio.startReceive();

    if (state != RADIOLIB_ERR_NONE) {
        runtimeState.incrementRadioErrors();
        recordError(RuntimeState::ErrorClass::RADIO_START_RECEIVE);
        showStatus(
            "RX START FAILED",
            String("CODE ") + state
        );

        scheduleRetryOrNext("RX START FAILED");
        return false;
    }

    setRuntimePhase(RuntimeState::RuntimePhase::WAITING_FOR_ACK);

    if (resetDeadline) {
        transactionState.beginAcknowledgmentWait(
            static_cast<uint32_t>(millis())
        );
    }

    return true;
}

void handleAckTimeout() {
    runtimeState.incrementAcknowledgmentTimeouts();
    recordError(RuntimeState::ErrorClass::ACK_TIMEOUT);
    showStatus(
        "ACK TIMEOUT",
        String("SEQ ") + currentCommand.sequence
    );

    scheduleRetryOrNext("ACK TIMEOUT");
}

void continueWaitingForAck() {
    radio.standby();

    const TransactionEngine::HubTransactionAction action =
        transactionState.acknowledgmentWaitAction(
            static_cast<uint32_t>(millis())
        );

    if (action != TransactionEngine::HubTransactionAction::NO_ACTION) {
        handleAckTimeout();
        return;
    }

    startAckReceive(false);
}

void processAcknowledgment() {
    const size_t packetLength = radio.getPacketLength();

    if (
        packetLength < Protocol::HEADER_SIZE ||
        packetLength > Protocol::MAX_PACKET_SIZE
    ) {
        runtimeState.incrementMalformedPackets();
        recordError(RuntimeState::ErrorClass::PACKET_LENGTH);
        showStatus(
            "ACK MALFORMED",
            String("LEN ") + packetLength
        );

        continueWaitingForAck();
        return;
    }

    uint8_t receiveBuffer[Protocol::MAX_PACKET_SIZE] = {};

    const int state = radio.readData(
        receiveBuffer,
        packetLength
    );

    if (state == RADIOLIB_ERR_CRC_MISMATCH) {
        runtimeState.incrementRadioErrors();
        recordError(RuntimeState::ErrorClass::RADIO_READ);
        showStatus("BAD ACK", "CRC mismatch");
        continueWaitingForAck();
        return;
    }

    if (state != RADIOLIB_ERR_NONE) {
        runtimeState.incrementRadioErrors();
        recordError(RuntimeState::ErrorClass::RADIO_READ);
        showStatus(
            "ACK READ FAILED",
            String("CODE ") + state
        );

        continueWaitingForAck();
        return;
    }

    const float rssi = radio.getRSSI();
    const float snr = radio.getSNR();
    runtimeState.updateRadioMetrics(rssi, snr);
    runtimeState.recordActivity(static_cast<uint32_t>(millis()));
    markPresentationChanged();

    Protocol::Packet acknowledgment = {};

    if (
        !Protocol::decode(
            receiveBuffer,
            packetLength,
            acknowledgment
        )
    ) {
        runtimeState.incrementMalformedPackets();
        recordError(RuntimeState::ErrorClass::PACKET_DECODE);
        showStatus("ACK MALFORMED", "decode failed");
        continueWaitingForAck();
        return;
    }

    runtimeState.incrementDecodedPacketsReceived();
    runtimeState.recordInboundPacket(
        static_cast<uint8_t>(acknowledgment.type),
        acknowledgment.source,
        acknowledgment.destination,
        acknowledgment.sequence,
        acknowledgment.opcode,
        acknowledgment.payloadLength,
        acknowledgment.type == Protocol::PacketType::ACK &&
            acknowledgment.payloadLength == 1,
        acknowledgment.payloadLength == 1
            ? acknowledgment.payload[0]
            : 0,
        static_cast<uint32_t>(millis())
    );
    markPresentationChanged();

    logPacket("RX", acknowledgment);

    Serial.print("RSSI: ");
    Serial.print(runtimeState.latestRssi());
    Serial.print(" dBm | SNR: ");
    Serial.print(runtimeState.latestSnr());
    Serial.println(" dB");

    const TransactionEngine::HubAckEvaluation evaluation =
        TransactionEngine::evaluateHubAcknowledgment(
            acknowledgment,
            currentCommand,
            runtimeState.localId(),
            runtimeState.peerId()
        );

    if (
        evaluation.outcome !=
        TransactionEngine::HubAckOutcome::MATCHING_ACK
    ) {
        if (
            evaluation.outcome ==
            TransactionEngine::HubAckOutcome::IGNORE_MALFORMED_PAYLOAD
        ) {
            runtimeState.incrementMalformedPackets();
            recordError(RuntimeState::ErrorClass::ACK_STATUS);
        } else {
            runtimeState.incrementIgnoredPackets();
            recordError(RuntimeState::ErrorClass::PACKET_IGNORED);
        }
        showStatus(
            "ACK IGNORED",
            String("SEQ ") + acknowledgment.sequence
        );

        continueWaitingForAck();
        return;
    }

    const uint8_t rawStatus = evaluation.rawStatus;

    if (!Protocol::isValidAckStatus(rawStatus)) {
        runtimeState.incrementMalformedPackets();
        recordError(RuntimeState::ErrorClass::ACK_STATUS);
        showStatus(
            "ACK MALFORMED",
            String("STATUS ") + rawStatus
        );

        radio.standby();
        completeAndScheduleNextTransaction();
        return;
    }

    const Protocol::AckStatus status =
        static_cast<Protocol::AckStatus>(rawStatus);
    const TransactionEngine::HubTransactionAction completion =
        transactionState.acknowledgmentCompletionAction(status);

    if (
        completion ==
        TransactionEngine::HubTransactionAction::TRANSACTION_SUCCEEDED
    ) {
        runtimeState.incrementSuccessfulTransactions();
        setHealth(RuntimeState::Health::READY);
        recordError(RuntimeState::ErrorClass::NONE);
        setPeerState(DeviceUi::PeerState::REACHABLE);
        showStatus(
            "ACK MATCHED",
            String("SEQ ") + currentCommand.sequence +
            " RSSI " + String(runtimeState.latestRssi(), 0) +
            " SNR " + String(runtimeState.latestSnr(), 1)
        );
    } else {
        setHealth(RuntimeState::Health::DEGRADED);
        recordError(RuntimeState::ErrorClass::REMOTE_ACK);
        setPeerState(DeviceUi::PeerState::DEGRADED);
        showStatus(
            "ACK ERROR",
            String("STATUS ") +
            static_cast<uint8_t>(status)
        );
    }

    radio.standby();
    completeAndScheduleNextTransaction();
}

void setup() {
    Serial.begin(115200);

    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(Vext, OUTPUT);
    pinMode(RST_OLED, OUTPUT);
    // GPIO0 is also a boot strap; holding it during reset may enter download mode.
    pinMode(APPLICATION_BUTTON_PIN, INPUT_PULLUP);

    digitalWrite(LED_BUILTIN, LOW);
    digitalWrite(Vext, LOW);
    delay(100);

    digitalWrite(RST_OLED, LOW);
    delay(20);
    digitalWrite(RST_OLED, HIGH);
    delay(20);

    display.init();

    radioSPI.begin(9, 11, 10, 8);

    const int state = radio.begin(915.0);

    Serial.print("Radio init state: ");
    Serial.println(state);

    if (state != RADIOLIB_ERR_NONE) {
        runtimeState.setReady(false);
        setHealth(RuntimeState::Health::ERROR);
        recordError(RuntimeState::ErrorClass::RADIO_INITIALIZATION);
        serviceUi(static_cast<uint32_t>(millis()));
        showStatus("RADIO ERROR", String("CODE ") + state);
        return;
    }

    radio.setDio1Action(setRadioFlag);

    runtimeState.setReady(true);
    setHealth(RuntimeState::Health::READY);
    markPresentationChanged();
    serviceUi(static_cast<uint32_t>(millis()));
    showStatus(
        runtimeState.role() == RuntimeState::DeviceRole::HUB
            ? "HUB READY"
            : "NODE READY",
        String("Firmware: ") + RedlineVersion::FIRMWARE
    );
    logVersionMetadata();

    nextTransmitAt = millis() + INITIAL_DELAY_MS;
}

void serviceHubTransport(uint32_t nowMs) {
    if (runtimeState.phase() == RuntimeState::RuntimePhase::IDLE) {
        if ((long)(millis() - nextTransmitAt) < 0) {
            return;
        }

        startCommandTransmission();
        return;
    }

    if (!operationDone) {
        if (
            runtimeState.phase() ==
                RuntimeState::RuntimePhase::WAITING_FOR_ACK &&
            transactionState.isAwaitingAcknowledgment()
        ) {
            const TransactionEngine::HubTransactionAction action =
                transactionState.acknowledgmentWaitAction(
                    static_cast<uint32_t>(millis())
                );

            if (
                action !=
                TransactionEngine::HubTransactionAction::NO_ACTION
            ) {
                handleAckTimeout();
            }
        }

        return;
    }

    operationDone = false;

    if (
        runtimeState.phase() ==
        RuntimeState::RuntimePhase::TRANSMITTING
    ) {
        digitalWrite(LED_BUILTIN, LOW);
        runtimeState.incrementTransmissionsCompleted();
        runtimeState.recordActivity(nowMs);
        markPresentationChanged();
        showStatus(
            "TX COMPLETE",
            String("SEQ ") + transactionState.currentSequence()
        );

        startAckReceive(true);
        return;
    }

    if (
        runtimeState.phase() ==
        RuntimeState::RuntimePhase::WAITING_FOR_ACK
    ) {
        processAcknowledgment();
    }
}

void loop() {
    const uint32_t nowMs = static_cast<uint32_t>(millis());
    serviceButton(nowMs);

    if (runtimeState.isReady()) {
        serviceHubTransport(nowMs);
    }

    serviceUi(nowMs);

    if (!runtimeState.isReady()) {
        yield();
    }
}
