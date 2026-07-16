#include <Arduino.h>

void setup() {
    Serial.begin(115200);

    pinMode(LED_BUILTIN, OUTPUT);

    Serial.println("Heltec V4 TX booted");
}

void loop() {
    digitalWrite(LED_BUILTIN, HIGH);
    Serial.println("LED ON");
    delay(500);

    digitalWrite(LED_BUILTIN, LOW);
    Serial.println("LED OFF");
    delay(500);
}