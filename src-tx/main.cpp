#include <Arduino.h>
#include <Wire.h>
#include "SSD1306Wire.h"
#include <RadioLib.h>
#include "protocol.h"
#include "device_config.h"
#include "redline_version.h"
#include "transaction_engine.h"

SSD1306Wire display(0x3C, SDA_OLED, SCL_OLED);

SPIClass radioSPI(FSPI);
SX1262 radio = new Module(8, 14, 12, 13, radioSPI);

constexpr unsigned long INITIAL_DELAY_MS = 2000;
constexpr unsigned long TRANSACTION_INTERVAL_MS = 3000;
constexpr unsigned long RETRY_DELAY_MS = 500;

bool radioReady = false;
volatile bool operationDone = false;

enum class HubRadioState : uint8_t {
    IDLE,
    TRANSMITTING,
    WAITING_FOR_ACK
};

HubRadioState hubState = HubRadioState::IDLE;

unsigned long nextTransmitAt = 0;

TransactionEngine::HubTransactionState transactionState;

Protocol::Packet currentCommand = {};
uint8_t transmitBuffer[Protocol::MAX_PACKET_SIZE] = {};
size_t transmitLength = 0;

void IRAM_ATTR setRadioFlag() {
    operationDone = true;
}

const unsigned char rgLogo26x16[] PROGMEM = {
0xff,0x00,0xfc,0x01,0xff,0x03,0xff,0x01,0xff,0x87,0xff,0x01,0x07,0xc7,0x03,
0x00,0x07,0xef,0x01,0x00,0x07,0xe7,0x00,0x00,0x07,0xe7,0x00,0x00,0x87,0xe7,
0xf0,0x03,0xff,0xe3,0xf0,0x03,0xff,0xe1,0xf0,0x03,0xc7,0xe1,0x80,0x03,0x87,
0xe3,0x81,0x03,0x87,0xc7,0x83,0x03,0x07,0xc7,0xff,0x03,0x07,0x8f,0xff,0x03,
0x07,0x0e,0xfe,0x00
};

void showHomeScreen(const String& status) {
    display.clear();

    display.setColor(WHITE);
    display.fillRect(48, 0, 32, 32);

    display.setColor(BLACK);
    display.drawXbm(51, 8, 26, 16, rgLogo26x16);

    display.setColor(WHITE);
    display.setTextAlignment(TEXT_ALIGN_CENTER);

    display.setFont(ArialMT_Plain_16);
    display.drawString(64, 34, "RaveGoat Labs");

    display.setFont(ArialMT_Plain_10);
    display.drawString(64, 53, status);

    display.display();
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
    hubState = HubRadioState::IDLE;
    nextTransmitAt = millis() + TRANSACTION_INTERVAL_MS;
}

void scheduleRetryOrNext(const String& reason) {
    radio.standby();
    const TransactionEngine::HubTransactionAction action =
        transactionState.attemptFailed();

    if (action == TransactionEngine::HubTransactionAction::RETRANSMIT) {
        showStatus(
            reason,
            String("retry ") + transactionState.retryCount() +
            "/" + transactionState.maximumRetries()
        );

        hubState = HubRadioState::IDLE;
        nextTransmitAt = millis() + RETRY_DELAY_MS;
        return;
    }

    showStatus(
        "COMMAND FAILED",
        String("SEQ ") + currentCommand.sequence
    );

    transactionState.completeTransaction();
    scheduleNextTransaction();
}

bool prepareCommand() {
    currentCommand = {};

    currentCommand.type = Protocol::PacketType::COMMAND;
    currentCommand.source = DeviceConfig::LOCAL_ID;
    currentCommand.destination = DeviceConfig::PEER_ID;
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
        transactionState.completeTransaction();
        scheduleNextTransaction();
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

    hubState = HubRadioState::TRANSMITTING;
    return true;
}

bool startAckReceive(bool resetDeadline) {
    operationDone = false;

    const int state = radio.startReceive();

    if (state != RADIOLIB_ERR_NONE) {
        showStatus(
            "RX START FAILED",
            String("CODE ") + state
        );

        scheduleRetryOrNext("RX START FAILED");
        return false;
    }

    hubState = HubRadioState::WAITING_FOR_ACK;

    if (resetDeadline) {
        transactionState.beginAcknowledgmentWait(
            static_cast<uint32_t>(millis())
        );
    }

    return true;
}

void handleAckTimeout() {
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
        showStatus("BAD ACK", "CRC mismatch");
        continueWaitingForAck();
        return;
    }

    if (state != RADIOLIB_ERR_NONE) {
        showStatus(
            "ACK READ FAILED",
            String("CODE ") + state
        );

        continueWaitingForAck();
        return;
    }

    const float rssi = radio.getRSSI();
    const float snr = radio.getSNR();

    Protocol::Packet acknowledgment = {};

    if (
        !Protocol::decode(
            receiveBuffer,
            packetLength,
            acknowledgment
        )
    ) {
        showStatus("ACK MALFORMED", "decode failed");
        continueWaitingForAck();
        return;
    }

    logPacket("RX", acknowledgment);

    Serial.print("RSSI: ");
    Serial.print(rssi);
    Serial.print(" dBm | SNR: ");
    Serial.print(snr);
    Serial.println(" dB");

    const TransactionEngine::HubAckEvaluation evaluation =
        TransactionEngine::evaluateHubAcknowledgment(
            acknowledgment,
            currentCommand,
            DeviceConfig::LOCAL_ID,
            DeviceConfig::PEER_ID
        );

    if (
        evaluation.outcome !=
        TransactionEngine::HubAckOutcome::MATCHING_ACK
    ) {
        showStatus(
            "ACK IGNORED",
            String("SEQ ") + acknowledgment.sequence
        );

        continueWaitingForAck();
        return;
    }

    const uint8_t rawStatus = evaluation.rawStatus;

    if (!Protocol::isValidAckStatus(rawStatus)) {
        showStatus(
            "ACK MALFORMED",
            String("STATUS ") + rawStatus
        );

        radio.standby();
        transactionState.completeTransaction();
        scheduleNextTransaction();
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
        showStatus(
            "ACK MATCHED",
            String("SEQ ") + currentCommand.sequence +
            " RSSI " + String(rssi, 0) +
            " SNR " + String(snr, 1)
        );
    } else {
        showStatus(
            "ACK ERROR",
            String("STATUS ") +
            static_cast<uint8_t>(status)
        );
    }

    radio.standby();
    transactionState.completeTransaction();
    scheduleNextTransaction();
}

void setup() {
    Serial.begin(115200);

    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(Vext, OUTPUT);
    pinMode(RST_OLED, OUTPUT);

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
        showHomeScreen(String("RADIO ERR ") + state);
        showStatus("RADIO ERROR", String("CODE ") + state);
        return;
    }

    radio.setDio1Action(setRadioFlag);

    radioReady = true;
    showHomeScreen("TX | READY");
    showStatus(
        "HUB READY",
        String("Firmware: ") + RedlineVersion::FIRMWARE
    );
    logVersionMetadata();

    nextTransmitAt = millis() + INITIAL_DELAY_MS;
}

void loop() {
    if (!radioReady) {
        delay(1000);
        return;
    }

    if (hubState == HubRadioState::IDLE) {
        if ((long)(millis() - nextTransmitAt) < 0) {
            return;
        }

        startCommandTransmission();
        return;
    }

    if (!operationDone) {
        if (
            hubState == HubRadioState::WAITING_FOR_ACK &&
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

    if (hubState == HubRadioState::TRANSMITTING) {
        digitalWrite(LED_BUILTIN, LOW);
        showStatus(
            "TX COMPLETE",
            String("SEQ ") + transactionState.currentSequence()
        );

        startAckReceive(true);
        return;
    }

    if (hubState == HubRadioState::WAITING_FOR_ACK) {
        processAcknowledgment();
    }
}
