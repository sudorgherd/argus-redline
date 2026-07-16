#include <Arduino.h>
#include <Wire.h>
#include "SSD1306Wire.h"
#include <RadioLib.h>

SSD1306Wire display(0x3C, SDA_OLED, SCL_OLED);

SPIClass radioSPI(FSPI);
SX1262 radio = new Module(8, 14, 12, 13, radioSPI);

constexpr char COMMAND[] = "100";
constexpr char ACKNOWLEDGMENT[] = "ACK 100";

bool radioReady = false;

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

    int state = radio.begin(915.0);

    Serial.print("Radio init state: ");
    Serial.println(state);

    if (state != RADIOLIB_ERR_NONE) {
        showHomeScreen(String("RADIO ERR ") + state);
        showStatus("RADIO ERROR", String("CODE ") + state);
        return;
    }

    radioReady = true;
    showHomeScreen("RX | READY");
    showStatus("NODE READY", "listening for 100");
    Serial.println("Node listening");
}

void loop() {
    if (!radioReady) {
        delay(1000);
        return;
    }

    uint8_t commandBuffer[sizeof(COMMAND)] = {0};

    int state = radio.receive(
        commandBuffer,
        sizeof(COMMAND) - 1,
        10000
    );

    if (state == RADIOLIB_ERR_RX_TIMEOUT) {
        digitalWrite(LED_BUILTIN, HIGH);
        delay(75);
        digitalWrite(LED_BUILTIN, LOW);
        return;
    }

    if (state != RADIOLIB_ERR_NONE) {
        Serial.print("RX failed, code ");
        Serial.println(state);
        showStatus("RX FAILED", String("CODE ") + state);
        delay(1000);
        showStatus("NODE READY", "listening for 100");
        return;
    }

    String command(
        reinterpret_cast<char*>(commandBuffer)
    );

    float rssi = radio.getRSSI();
    float snr = radio.getSNR();

    Serial.print("RX: ");
    Serial.println(command);
    Serial.print("RSSI: ");
    Serial.print(rssi);
    Serial.print(" dBm | SNR: ");
    Serial.print(snr);
    Serial.println(" dB");

    if (command != COMMAND) {
        showStatus("RX UNKNOWN", command);
        delay(1000);
        showStatus("NODE READY", "listening for 100");
        return;
    }

    showStatus(
        "RX 100",
        String("RSSI ") + String(rssi, 0) +
        " SNR " + String(snr, 1)
    );

    delay(100);

    digitalWrite(LED_BUILTIN, HIGH);

    state = radio.transmit(
        reinterpret_cast<const uint8_t*>(ACKNOWLEDGMENT),
        sizeof(ACKNOWLEDGMENT) - 1
    );

    digitalWrite(LED_BUILTIN, LOW);

    if (state == RADIOLIB_ERR_NONE) {
        Serial.println("TX: ACK 100");
        showStatus("RX 100", "ACK SENT");
    } else {
        Serial.print("ACK failed, code ");
        Serial.println(state);
        showStatus("ACK FAILED", String("CODE ") + state);
    }

    delay(1000);
    showStatus("NODE READY", "listening for 100");
}
