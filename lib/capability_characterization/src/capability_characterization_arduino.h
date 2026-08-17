#pragma once

#if !defined(ARGUS_CAPABILITY_CHARACTERIZATION)
#error "Characterization ADC adapter is validation-build only"
#endif

#include <Arduino.h>

#include "capability_characterization.h"
#include "heltec_v4_capabilities.h"

namespace CapabilityCharacterization {

struct AnalogSample {
    bool available = false;
    uint16_t raw = 0;
    uint16_t normalized = 0;
};

class AnalogAdapter {
public:
    bool begin() {
        analogReadResolution(12);
        analogSetPinAttenuation(
            HeltecV4Capabilities::Resources::EXTERNAL_ANALOG_GPIO,
            ADC_11db
        );
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
        // Arduino-ESP32 3.x removed adcAttachPin(). ADC one-shot
        // initialization is performed internally by the supported ADC API.
        initialized_ = true;
#else
        initialized_ = adcAttachPin(
            HeltecV4Capabilities::Resources::EXTERNAL_ANALOG_GPIO
        );
#endif
        return initialized_;
    }

    AnalogSample sampleOnce() const {
        AnalogSample sample;
        if (!initialized_) return sample;
        sample.raw = analogRead(
            HeltecV4Capabilities::Resources::EXTERNAL_ANALOG_GPIO
        );
        sample.normalized = normalizeRaw12ToU16(sample.raw);
        sample.available = true;
        return sample;
    }

    bool initialized() const {
        return initialized_;
    }

private:
    bool initialized_ = false;
};

}  // namespace CapabilityCharacterization
