#include <Arduino.h>
#include <Wire.h>
#include "SSD1306Wire.h"
#include <RadioLib.h>
#include "protocol.h"

SSD1306Wire display(0x3C, SDA_OLED, SCL_OLED);

SPIClass radioSPI(FSPI);
SX1262 radio = new Module(8, 14, 12, 13, radioSPI);

bool radioReady = false;

bool hasLastCommand = false;
uint8_t lastSource = 0;
uint8_t lastSequence = 0;
uint8_t lastOpcode = 0;
Protocol::AckStatus lastAckStatus = Protocol::AckStatus::SUCCESS;

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

bool sendAcknowledgment(
    const Protocol::Packet& command,
    Protocol::AckStatus status,
    bool duplicate
) {
    Protocol::Packet acknowledgment = {};

    acknowledgment.type = Protocol::PacketType::ACK;
    acknowledgment.source = Protocol::NODE_ID;
    acknowledgment.destination = command.source;
    acknowledgment.sequence = command.sequence;
    acknowledgment.opcode = command.opcode;
    acknowledgment.payloadLength = 1;
    acknowledgment.payload[0] = static_cast<uint8_t>(status);

    uint8_t transmitBuffer[Protocol::MAX_PACKET_SIZE] = {};
    size_t transmitLength = 0;

    if (
        !Protocol::encode(
            acknowledgment,
            transmitBuffer,
            sizeof(transmitBuffer),
            transmitLength
        )
    ) {
        showStatus("ACK FAILED", "encode error");
        return false;
    }

    delay(100);
    digitalWrite(LED_BUILTIN, HIGH);

    const int state = radio.transmit(
        transmitBuffer,
        transmitLength
    );

    digitalWrite(LED_BUILTIN, LOW);

    if (state != RADIOLIB_ERR_NONE) {
        showStatus("ACK FAILED", String("CODE ") + state);
        return false;
    }

    logPacket("TX", acknowledgment);

    showStatus(
        duplicate ? "DUPLICATE" : "COMMAND OK",
        String("ACK SEQ ") + acknowledgment.sequence
    );

    return true;
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

    radioReady = true;
    showHomeScreen("RX | READY");
    showStatus("NODE READY", "Protocol v0.1");
}

void loop() {
    if (!radioReady) {
        delay(1000);
        return;
    }

    uint8_t receiveBuffer[Protocol::MAX_PACKET_SIZE] = {};

    const int state = radio.receive(
        receiveBuffer,
        sizeof(receiveBuffer),
        10000
    );

    if (state == RADIOLIB_ERR_RX_TIMEOUT) {
        digitalWrite(LED_BUILTIN, HIGH);
        delay(75);
        digitalWrite(LED_BUILTIN, LOW);
        return;
    }

    if (state != RADIOLIB_ERR_NONE) {
        showStatus("RX FAILED", String("CODE ") + state);
        return;
    }

    const size_t packetLength = radio.getPacketLength();

    if (
        packetLength < Protocol::HEADER_SIZE ||
        packetLength > Protocol::MAX_PACKET_SIZE
    ) {
        showStatus(
            "RX MALFORMED",
            String("LEN ") + packetLength
        );
        return;
    }

    Protocol::Packet command = {};

    if (!Protocol::decode(receiveBuffer, packetLength, command)) {
        showStatus("RX MALFORMED", "decode failed");
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

    if (!Protocol::isAddressedTo(command, Protocol::NODE_ID)) {
        showStatus(
            "RX IGNORED",
            String("DST ") + command.destination
        );
        return;
    }

    if (
        command.type != Protocol::PacketType::COMMAND ||
        command.source != Protocol::HUB_ID
    ) {
        showStatus("RX IGNORED", "invalid sender/type");
        return;
    }

    if (isDuplicateCommand(command)) {
        sendAcknowledgment(command, lastAckStatus, true);
        return;
    }

    Protocol::AckStatus status;

    if (command.opcode == Protocol::OPCODE_TEST) {
        status = Protocol::AckStatus::SUCCESS;
    } else {
        status = Protocol::AckStatus::UNSUPPORTED_OPCODE;
    }

    rememberCommand(command, status);
    sendAcknowledgment(command, status, false);
}
