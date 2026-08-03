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

constexpr uint8_t APPLICATION_BUTTON_PIN = 0;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 30;
constexpr uint32_t BUTTON_LONG_PRESS_MS = 800;

volatile bool operationDone = false;

RuntimeState::State runtimeState(
    RuntimeState::DeviceRole::NODE,
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

uint8_t receiveBuffer[Protocol::MAX_PACKET_SIZE] = {};
uint8_t transmitBuffer[Protocol::MAX_PACKET_SIZE] = {};
size_t transmitLength = 0;

Protocol::Packet pendingAcknowledgment = {};
bool pendingAcknowledgmentDuplicate = false;

TransactionEngine::NodeDuplicateTracker duplicateTracker;

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

bool startListening() {
    operationDone = false;
    setRuntimePhase(RuntimeState::RuntimePhase::LISTENING);

    const int state = radio.startReceive();

    if (state != RADIOLIB_ERR_NONE) {
        runtimeState.incrementRadioErrors();
        setHealth(RuntimeState::Health::DEGRADED);
        recordError(RuntimeState::ErrorClass::RADIO_START_RECEIVE);
        showStatus(
            "RX START FAILED",
            String("CODE ") + state
        );
        return false;
    }

    return true;
}

void resumeListening() {
    radio.standby();
    if (startListening()) {
        setHealth(RuntimeState::Health::READY);
    }
}

bool startAcknowledgment(
    const Protocol::Packet& command,
    Protocol::AckStatus status,
    bool duplicate
) {
    pendingAcknowledgment =
        TransactionEngine::makeAcknowledgment(command, status);

    transmitLength = 0;

    if (
        !Protocol::encode(
            pendingAcknowledgment,
            transmitBuffer,
            sizeof(transmitBuffer),
            transmitLength
        )
    ) {
        showStatus("ACK FAILED", "encode error");
        startListening();
        return false;
    }

    pendingAcknowledgmentDuplicate = duplicate;

    radio.standby();
    delay(100);

    operationDone = false;
    setRuntimePhase(RuntimeState::RuntimePhase::TRANSMITTING_ACK);
    digitalWrite(LED_BUILTIN, HIGH);

    const int state = radio.startTransmit(
        transmitBuffer,
        transmitLength
    );

    if (state != RADIOLIB_ERR_NONE) {
        digitalWrite(LED_BUILTIN, LOW);
        runtimeState.incrementRadioErrors();
        setHealth(RuntimeState::Health::DEGRADED);
        recordError(RuntimeState::ErrorClass::RADIO_START_TRANSMIT);

        showStatus(
            "ACK FAILED",
            String("CODE ") + state
        );

        startListening();
        return false;
    }

    return true;
}

void finishAcknowledgment() {
    digitalWrite(LED_BUILTIN, LOW);
    radio.standby();

    logPacket("TX", pendingAcknowledgment);

    showStatus(
        pendingAcknowledgmentDuplicate ? "DUPLICATE" : "COMMAND OK",
        String("ACK SEQ ") + pendingAcknowledgment.sequence
    );

    const bool listening = startListening();
    runtimeState.incrementTransmissionsCompleted();
    runtimeState.recordActivity(static_cast<uint32_t>(millis()));
    markPresentationChanged();

    if (listening) {
        setHealth(RuntimeState::Health::READY);
    }
}

void handleReceivedPacket() {
    const size_t packetLength = radio.getPacketLength();

    if (
        packetLength < Protocol::HEADER_SIZE ||
        packetLength > Protocol::MAX_PACKET_SIZE
    ) {
        runtimeState.incrementMalformedPackets();
        recordError(RuntimeState::ErrorClass::PACKET_LENGTH);
        showStatus(
            "RX MALFORMED",
            String("LEN ") + packetLength
        );

        resumeListening();
        return;
    }

    const int readState = radio.readData(
        receiveBuffer,
        packetLength
    );

    if (readState != RADIOLIB_ERR_NONE) {
        runtimeState.incrementRadioErrors();
        setHealth(RuntimeState::Health::DEGRADED);
        recordError(RuntimeState::ErrorClass::RADIO_READ);
        showStatus(
            "RX FAILED",
            String("CODE ") + readState
        );

        resumeListening();
        return;
    }

    const uint32_t observedAtMs = static_cast<uint32_t>(millis());
    const float rssi = radio.getRSSI();
    const float snr = radio.getSNR();
    runtimeState.updateRadioMetrics(rssi, snr);
    runtimeState.recordActivity(observedAtMs);
    markPresentationChanged();

    Protocol::Packet command = {};

    if (!Protocol::decode(receiveBuffer, packetLength, command)) {
        runtimeState.incrementMalformedPackets();
        recordError(RuntimeState::ErrorClass::PACKET_DECODE);
        showStatus("RX MALFORMED", "decode failed");
        resumeListening();
        return;
    }

    runtimeState.incrementDecodedPacketsReceived();
    runtimeState.recordInboundPacket(
        static_cast<uint8_t>(command.type),
        command.source,
        command.destination,
        command.sequence,
        command.opcode,
        command.payloadLength,
        false,
        0,
        observedAtMs
    );
    markPresentationChanged();

    logPacket("RX", command);

    Serial.print("RSSI: ");
    Serial.print(runtimeState.latestRssi());
    Serial.print(" dBm | SNR: ");
    Serial.print(runtimeState.latestSnr());
    Serial.println(" dB");

    const TransactionEngine::NodeCommandEvaluation evaluation =
        TransactionEngine::evaluateNodeCommand(
            command,
            runtimeState.localId(),
            runtimeState.peerId(),
            duplicateTracker
        );

    switch (evaluation.outcome) {
        case TransactionEngine::NodeCommandOutcome::
            IGNORE_WRONG_DESTINATION:
            runtimeState.incrementIgnoredPackets();
            recordError(RuntimeState::ErrorClass::PACKET_IGNORED);
            showStatus(
                "RX IGNORED",
                String("DST ") + command.destination
            );
            resumeListening();
            return;

        case TransactionEngine::NodeCommandOutcome::
            IGNORE_WRONG_SENDER:
        case TransactionEngine::NodeCommandOutcome::
            IGNORE_WRONG_PACKET_TYPE:
            runtimeState.incrementIgnoredPackets();
            recordError(RuntimeState::ErrorClass::PACKET_IGNORED);
            showStatus("RX IGNORED", "invalid sender/type");
            resumeListening();
            return;

        case TransactionEngine::NodeCommandOutcome::DUPLICATE:
            runtimeState.incrementDuplicates();
            markPresentationChanged();
            startAcknowledgment(command, evaluation.status, true);
            return;

        case TransactionEngine::NodeCommandOutcome::ACK_SUCCESS:
            runtimeState.incrementAcceptedCommands();
            setHealth(RuntimeState::Health::READY);
            recordError(RuntimeState::ErrorClass::NONE);
            setPeerState(DeviceUi::PeerState::SEEN);
            markPresentationChanged();
            startAcknowledgment(command, evaluation.status, false);
            return;

        case TransactionEngine::NodeCommandOutcome::
            ACK_UNSUPPORTED_OPCODE:
            startAcknowledgment(command, evaluation.status, false);
            return;

        case TransactionEngine::NodeCommandOutcome::
            ACK_MALFORMED_PACKET:
            runtimeState.incrementMalformedPackets();
            markPresentationChanged();
            startAcknowledgment(command, evaluation.status, false);
            return;
    }
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

    if (!startListening()) {
        runtimeState.setReady(false);
        setHealth(RuntimeState::Health::ERROR);
        serviceUi(static_cast<uint32_t>(millis()));
        return;
    }

    runtimeState.setReady(true);
    setHealth(RuntimeState::Health::READY);
    markPresentationChanged();
    serviceUi(static_cast<uint32_t>(millis()));
    showStatus(
        runtimeState.role() == RuntimeState::DeviceRole::NODE
            ? "NODE READY"
            : "HUB READY",
        String("Firmware: ") + RedlineVersion::FIRMWARE
    );
    logVersionMetadata();
}

void loop() {
    const uint32_t nowMs = static_cast<uint32_t>(millis());
    serviceButton(nowMs);

    if (runtimeState.isReady() && operationDone) {
        operationDone = false;

        if (
            runtimeState.phase() ==
            RuntimeState::RuntimePhase::TRANSMITTING_ACK
        ) {
            finishAcknowledgment();
        } else {
            handleReceivedPacket();
        }
    }

    // Defer OLED I/O until an active ACK has completed and receive restarted.
    if (
        runtimeState.phase() !=
        RuntimeState::RuntimePhase::TRANSMITTING_ACK
    ) {
        serviceUi(nowMs);
    }

    if (!runtimeState.isReady()) {
        yield();
    }
}
