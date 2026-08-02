#include <Arduino.h>
#include <Wire.h>
#include "SSD1306Wire.h"
#include <RadioLib.h>
#include "protocol.h"
#include "device_config.h"
#include "redline_version.h"

SSD1306Wire display(0x3C, SDA_OLED, SCL_OLED);

SPIClass radioSPI(FSPI);
SX1262 radio = new Module(8, 14, 12, 13, radioSPI);

bool radioReady = false;
volatile bool operationDone = false;

enum class NodeRadioState : uint8_t {
    LISTENING,
    TRANSMITTING_ACK
};

NodeRadioState nodeState = NodeRadioState::LISTENING;

uint8_t receiveBuffer[Protocol::MAX_PACKET_SIZE] = {};
uint8_t transmitBuffer[Protocol::MAX_PACKET_SIZE] = {};
size_t transmitLength = 0;

Protocol::Packet pendingAcknowledgment = {};
bool pendingAcknowledgmentDuplicate = false;

bool hasLastCommand = false;
uint8_t lastSource = 0;
uint8_t lastSequence = 0;
uint8_t lastOpcode = 0;
Protocol::AckStatus lastAckStatus = Protocol::AckStatus::SUCCESS;

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

bool isDuplicateCommand(const Protocol::Packet& packet) {
    return (
        hasLastCommand &&
        packet.source == lastSource &&
        packet.sequence == lastSequence &&
        packet.opcode == lastOpcode
    );
}

void rememberCommand(
    const Protocol::Packet& packet,
    Protocol::AckStatus status
) {
    hasLastCommand = true;
    lastSource = packet.source;
    lastSequence = packet.sequence;
    lastOpcode = packet.opcode;
    lastAckStatus = status;
}

bool startListening() {
    operationDone = false;
    nodeState = NodeRadioState::LISTENING;

    const int state = radio.startReceive();

    if (state != RADIOLIB_ERR_NONE) {
        showStatus(
            "RX START FAILED",
            String("CODE ") + state
        );
        return false;
    }

    return true;
}

bool startAcknowledgment(
    const Protocol::Packet& command,
    Protocol::AckStatus status,
    bool duplicate
) {
    pendingAcknowledgment = {};

    pendingAcknowledgment.type = Protocol::PacketType::ACK;
    pendingAcknowledgment.source = DeviceConfig::LOCAL_ID;
    pendingAcknowledgment.destination = command.source;
    pendingAcknowledgment.sequence = command.sequence;
    pendingAcknowledgment.opcode = command.opcode;
    pendingAcknowledgment.payloadLength = 1;
    pendingAcknowledgment.payload[0] = static_cast<uint8_t>(status);

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
    nodeState = NodeRadioState::TRANSMITTING_ACK;
    digitalWrite(LED_BUILTIN, HIGH);

    const int state = radio.startTransmit(
        transmitBuffer,
        transmitLength
    );

    if (state != RADIOLIB_ERR_NONE) {
        digitalWrite(LED_BUILTIN, LOW);

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

    startListening();
}

void handleReceivedPacket() {
    const size_t packetLength = radio.getPacketLength();

    if (
        packetLength < Protocol::HEADER_SIZE ||
        packetLength > Protocol::MAX_PACKET_SIZE
    ) {
        showStatus(
            "RX MALFORMED",
            String("LEN ") + packetLength
        );

        radio.standby();
        startListening();
        return;
    }

    const int readState = radio.readData(
        receiveBuffer,
        packetLength
    );

    if (readState != RADIOLIB_ERR_NONE) {
        showStatus(
            "RX FAILED",
            String("CODE ") + readState
        );

        radio.standby();
        startListening();
        return;
    }

    Protocol::Packet command = {};

    if (!Protocol::decode(receiveBuffer, packetLength, command)) {
        showStatus("RX MALFORMED", "decode failed");
        radio.standby();
        startListening();
        return;
    }

    logPacket("RX", command);

    const float rssi = radio.getRSSI();
    const float snr = radio.getSNR();

    Serial.print("RSSI: ");
    Serial.print(rssi);
    Serial.print(" dBm | SNR: ");
    Serial.print(snr);
    Serial.println(" dB");

    if (!Protocol::isAddressedTo(command, DeviceConfig::LOCAL_ID)) {
        showStatus(
            "RX IGNORED",
            String("DST ") + command.destination
        );

        radio.standby();
        startListening();
        return;
    }

    if (
        command.type != Protocol::PacketType::COMMAND ||
        command.source != DeviceConfig::PEER_ID
    ) {
        showStatus("RX IGNORED", "invalid sender/type");
        radio.standby();
        startListening();
        return;
    }

    if (isDuplicateCommand(command)) {
        startAcknowledgment(command, lastAckStatus, true);
        return;
    }

    Protocol::AckStatus status;

    if (!Protocol::isSupportedCommandOpcode(command.opcode)) {
        status = Protocol::AckStatus::UNSUPPORTED_OPCODE;
    } else if (
        !Protocol::isValidCommandPayload(
            command.opcode,
            command.payloadLength
        )
    ) {
        status = Protocol::AckStatus::MALFORMED_PACKET;
    } else {
        status = Protocol::AckStatus::SUCCESS;
    }

    rememberCommand(command, status);
    startAcknowledgment(command, status, false);
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
    showHomeScreen("RX | READY");
    showStatus(
        "NODE READY",
        String("Firmware: ") + RedlineVersion::FIRMWARE
    );
    logVersionMetadata();

    if (!startListening()) {
        radioReady = false;
        showHomeScreen("RX START ERR");
    }
}

void loop() {
    if (!radioReady) {
        delay(1000);
        return;
    }

    if (!operationDone) {
        return;
    }

    operationDone = false;

    if (nodeState == NodeRadioState::TRANSMITTING_ACK) {
        finishAcknowledgment();
        return;
    }

    handleReceivedPacket();
}
