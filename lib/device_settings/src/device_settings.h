#pragma once

#include <stdint.h>

namespace DeviceSettings {

constexpr uint16_t SCHEMA_VERSION = 1;
constexpr uint16_t DEFAULT_DISPLAY_TIMEOUT_SECONDS = 30;
constexpr uint8_t DEFAULT_DISPLAY_CONTRAST = 207;
constexpr bool DEFAULT_LED_ENABLED = true;
constexpr bool DEFAULT_DIAGNOSTICS_ENABLED = true;
constexpr bool DEFAULT_BUTTON_FEEDBACK_ENABLED = false;

enum class DefaultScreen : uint8_t {
    HOME = 0,
    RADIO = 1,
    DEVICE = 2,
    LAST_PACKET = 3,
    DIAGNOSTICS = 4,
    ABOUT = 5
};

constexpr DefaultScreen DEFAULT_SCREEN = DefaultScreen::HOME;

struct Settings {
    uint16_t displayTimeoutSeconds = DEFAULT_DISPLAY_TIMEOUT_SECONDS;
    uint8_t displayContrast = DEFAULT_DISPLAY_CONTRAST;
    bool ledEnabled = DEFAULT_LED_ENABLED;
    bool diagnosticsEnabled = DEFAULT_DIAGNOSTICS_ENABLED;
    DefaultScreen defaultScreen = DEFAULT_SCREEN;
    bool buttonFeedbackEnabled = DEFAULT_BUTTON_FEEDBACK_ENABLED;
};

enum class ValidationResult : uint8_t {
    UNCHANGED,
    REPAIRED
};

inline Settings defaults() {
    return Settings{};
}

inline bool operator==(const Settings& left, const Settings& right) {
    return (
        left.displayTimeoutSeconds == right.displayTimeoutSeconds &&
        left.displayContrast == right.displayContrast &&
        left.ledEnabled == right.ledEnabled &&
        left.diagnosticsEnabled == right.diagnosticsEnabled &&
        left.defaultScreen == right.defaultScreen &&
        left.buttonFeedbackEnabled == right.buttonFeedbackEnabled
    );
}

inline bool operator!=(const Settings& left, const Settings& right) {
    return !(left == right);
}

inline bool isValidDisplayTimeoutSeconds(uint16_t value) {
    return value == 0 || (value >= 5 && value <= 600);
}

inline bool isValidDisplayContrast(uint8_t value) {
    return value >= 16;
}

inline bool isValidDefaultScreen(DefaultScreen value) {
    switch (value) {
        case DefaultScreen::HOME:
        case DefaultScreen::RADIO:
        case DefaultScreen::DEVICE:
        case DefaultScreen::LAST_PACKET:
        case DefaultScreen::DIAGNOSTICS:
        case DefaultScreen::ABOUT:
            return true;
    }

    return false;
}

inline bool isValid(const Settings& settings) {
    return (
        isValidDisplayTimeoutSeconds(settings.displayTimeoutSeconds) &&
        isValidDisplayContrast(settings.displayContrast) &&
        isValidDefaultScreen(settings.defaultScreen)
    );
}

inline ValidationResult validateAndRepair(Settings& settings) {
    bool repaired = false;

    if (!isValidDisplayTimeoutSeconds(settings.displayTimeoutSeconds)) {
        settings.displayTimeoutSeconds = DEFAULT_DISPLAY_TIMEOUT_SECONDS;
        repaired = true;
    }

    if (!isValidDisplayContrast(settings.displayContrast)) {
        settings.displayContrast = DEFAULT_DISPLAY_CONTRAST;
        repaired = true;
    }

    if (!isValidDefaultScreen(settings.defaultScreen)) {
        settings.defaultScreen = DEFAULT_SCREEN;
        repaired = true;
    }

    return repaired
        ? ValidationResult::REPAIRED
        : ValidationResult::UNCHANGED;
}

}  // namespace DeviceSettings
