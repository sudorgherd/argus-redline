#include <Arduino.h>
#include <Wire.h>
#include "SSD1306Wire.h"
#include <RadioLib.h>
#include "protocol.h"

SSD1306Wire display(0x3C, SDA_OLED, SCL_OLED);

SPIClass radioSPI(FSPI);
SX1262 radio = new Module(8, 14, 12, 13, radioSPI);

constexpr char COMMAND[] = "100";
constexpr char EXPECTED_ACK[] = "ACK 100";

bool radioReady = false;

volatile bool operationDone = false;

enum class HubRadioState : uint8_t {
    IDLE,
    TRANSMITTING,
    WAITING_FOR_ACK
};

HubRadioState hubState = HubRadioState::IDLE;

unsigned long nextTransmitAt = 0;
unsigned long ackDeadline = 0;

int transmissionState = RADIOLIB_ERR_NONE;

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

    radio.setDio1Action(setRadioFlag);
    radioReady = true;
    showHomeScreen("TX | READY");
    showStatus("HUB READY", "first TX in 2 sec");
    Serial.println("Hub ready");
    nextTransmitAt = millis() + 2000;
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

        operationDone = false;

        digitalWrite(LED_BUILTIN, HIGH);
        showStatus("TX 100", "sending command");

        transmissionState = radio.startTransmit(
            reinterpret_cast<const uint8_t*>(COMMAND),
            sizeof(COMMAND) - 1
        );

        if (transmissionState != RADIOLIB_ERR_NONE) {
            digitalWrite(LED_BUILTIN, LOW);

            Serial.print("TX start failed, code ");
            Serial.println(transmissionState);

            hubState = HubRadioState::IDLE;
            nextTransmitAt = millis() + 3000;
            return;
        }

        hubState = HubRadioState::TRANSMITTING;
        return;
    }

    if (!operationDone) {
        if (
            hubState == HubRadioState::WAITING_FOR_ACK &&
            (long)(millis() - ackDeadline) >= 0
        ) {
            radio.standby();

            Serial.println("ACK timeout");
            showStatus("ACK TIMEOUT", "retry in 3 sec");

            hubState = HubRadioState::IDLE;
            nextTransmitAt = millis() + 3000;
        }

        return;
    }

    operationDone = false;

    if (hubState == HubRadioState::TRANSMITTING) {
        digitalWrite(LED_BUILTIN, LOW);

        Serial.println("TX: 100");
        showStatus("TX 100", "waiting for ACK");

        int state = radio.startReceive();

        if (state != RADIOLIB_ERR_NONE) {
            Serial.print("RX start failed, code ");
            Serial.println(state);

            hubState = HubRadioState::IDLE;
            nextTransmitAt = millis() + 3000;
            return;
        }

        hubState = HubRadioState::WAITING_FOR_ACK;
        ackDeadline = millis() + 2500;
        return;
    }

    if (hubState == HubRadioState::WAITING_FOR_ACK) {
        String acknowledgment;

        int state = radio.readData(acknowledgment);

        if (state == RADIOLIB_ERR_NONE) {
            float rssi = radio.getRSSI();
            float snr = radio.getSNR();

            Serial.print("RX: ");
            Serial.println(acknowledgment);
            Serial.print("RSSI: ");
            Serial.print(rssi);
            Serial.print(" dBm | SNR: ");
            Serial.print(snr);
            Serial.println(" dB");

            if (acknowledgment == EXPECTED_ACK) {
                showStatus(
                    "ACK 100",
                    String("RSSI ") + String(rssi, 0) +
                    " SNR " + String(snr, 1)
                );
            } else {
                showStatus("BAD RESPONSE", acknowledgment);
            }
        } else if (state == RADIOLIB_ERR_CRC_MISMATCH) {
            Serial.println("ACK CRC mismatch");
            showStatus("BAD ACK", "CRC mismatch");
        } else {
            Serial.print("ACK read failed, code ");
            Serial.println(state);
            showStatus("RX FAILED", String("CODE ") + state);
        }

        radio.standby();

        hubState = HubRadioState::IDLE;
        nextTransmitAt = millis() + 3000;
    }
}
