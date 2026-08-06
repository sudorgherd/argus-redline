#pragma once

#include <stddef.h>
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

constexpr size_t RECORD_SIZE = 24;
constexpr size_t CRC_INPUT_SIZE = 20;
constexpr uint8_t FLAG_LED_ENABLED = 0x01;
constexpr uint8_t FLAG_DIAGNOSTICS_ENABLED = 0x02;
constexpr uint8_t FLAG_BUTTON_FEEDBACK_ENABLED = 0x04;
constexpr uint8_t KNOWN_FLAGS =
    FLAG_LED_ENABLED |
    FLAG_DIAGNOSTICS_ENABLED |
    FLAG_BUTTON_FEEDBACK_ENABLED;

enum class CodecResult : uint8_t {
    OK,
    NULL_POINTER,
    WRONG_LENGTH,
    CORRUPT_MAGIC,
    CORRUPT_RECORD_LENGTH,
    CORRUPT_CRC,
    UNSUPPORTED_SCHEMA,
    UNKNOWN_FLAGS,
    NONZERO_RESERVED,
    INVALID_TIMEOUT,
    INVALID_CONTRAST,
    INVALID_SCREEN
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

inline void writeUint16Le(uint8_t* output, uint16_t value) {
    output[0] = static_cast<uint8_t>(value & 0xFFU);
    output[1] = static_cast<uint8_t>((value >> 8) & 0xFFU);
}

inline void writeUint32Le(uint8_t* output, uint32_t value) {
    output[0] = static_cast<uint8_t>(value & 0xFFU);
    output[1] = static_cast<uint8_t>((value >> 8) & 0xFFU);
    output[2] = static_cast<uint8_t>((value >> 16) & 0xFFU);
    output[3] = static_cast<uint8_t>((value >> 24) & 0xFFU);
}

inline uint16_t readUint16Le(const uint8_t* input) {
    return static_cast<uint16_t>(
        static_cast<uint16_t>(input[0]) |
        (static_cast<uint16_t>(input[1]) << 8)
    );
}

inline uint32_t readUint32Le(const uint8_t* input) {
    return (
        static_cast<uint32_t>(input[0]) |
        (static_cast<uint32_t>(input[1]) << 8) |
        (static_cast<uint32_t>(input[2]) << 16) |
        (static_cast<uint32_t>(input[3]) << 24)
    );
}

inline bool crc32IsoHdlc(
    const uint8_t* input,
    size_t length,
    uint32_t& crc
) {
    if (input == nullptr) {
        return false;
    }

    uint32_t value = 0xFFFFFFFFU;
    for (size_t index = 0; index < length; ++index) {
        value ^= input[index];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            value = (value & 1U) != 0U
                ? (value >> 1) ^ 0xEDB88320U
                : value >> 1;
        }
    }

    crc = value ^ 0xFFFFFFFFU;
    return true;
}

inline CodecResult validateCodecSettings(const Settings& settings) {
    if (!isValidDisplayTimeoutSeconds(settings.displayTimeoutSeconds)) {
        return CodecResult::INVALID_TIMEOUT;
    }
    if (!isValidDisplayContrast(settings.displayContrast)) {
        return CodecResult::INVALID_CONTRAST;
    }
    if (!isValidDefaultScreen(settings.defaultScreen)) {
        return CodecResult::INVALID_SCREEN;
    }
    return CodecResult::OK;
}

inline CodecResult encodeRecord(
    const Settings& settings,
    uint32_t generation,
    uint8_t* output,
    size_t outputLength
) {
    if (output == nullptr) {
        return CodecResult::NULL_POINTER;
    }
    if (outputLength != RECORD_SIZE) {
        return CodecResult::WRONG_LENGTH;
    }

    const CodecResult settingsResult = validateCodecSettings(settings);
    if (settingsResult != CodecResult::OK) {
        return settingsResult;
    }

    output[0] = 0x52;
    output[1] = 0x4C;
    output[2] = 0x43;
    output[3] = 0x46;
    writeUint16Le(output + 4, SCHEMA_VERSION);
    writeUint16Le(output + 6, static_cast<uint16_t>(RECORD_SIZE));
    writeUint32Le(output + 8, generation);
    writeUint16Le(output + 12, settings.displayTimeoutSeconds);
    output[14] = settings.displayContrast;
    output[15] = static_cast<uint8_t>(settings.defaultScreen);
    output[16] = static_cast<uint8_t>(
        (settings.ledEnabled ? FLAG_LED_ENABLED : 0U) |
        (settings.diagnosticsEnabled ? FLAG_DIAGNOSTICS_ENABLED : 0U) |
        (settings.buttonFeedbackEnabled
            ? FLAG_BUTTON_FEEDBACK_ENABLED
            : 0U)
    );
    output[17] = 0;
    output[18] = 0;
    output[19] = 0;

    uint32_t crc = 0;
    crc32IsoHdlc(output, CRC_INPUT_SIZE, crc);
    writeUint32Le(output + 20, crc);
    return CodecResult::OK;
}

inline CodecResult decodeRecord(
    const uint8_t* input,
    size_t inputLength,
    Settings* settings,
    uint32_t* generation
) {
    if (input == nullptr || settings == nullptr || generation == nullptr) {
        return CodecResult::NULL_POINTER;
    }
    if (inputLength != RECORD_SIZE) {
        return CodecResult::WRONG_LENGTH;
    }
    if (
        input[0] != 0x52 ||
        input[1] != 0x4C ||
        input[2] != 0x43 ||
        input[3] != 0x46
    ) {
        return CodecResult::CORRUPT_MAGIC;
    }
    if (readUint16Le(input + 6) != RECORD_SIZE) {
        return CodecResult::CORRUPT_RECORD_LENGTH;
    }

    uint32_t calculatedCrc = 0;
    crc32IsoHdlc(input, CRC_INPUT_SIZE, calculatedCrc);
    if (readUint32Le(input + 20) != calculatedCrc) {
        return CodecResult::CORRUPT_CRC;
    }
    if (readUint16Le(input + 4) != SCHEMA_VERSION) {
        return CodecResult::UNSUPPORTED_SCHEMA;
    }
    if ((input[16] & static_cast<uint8_t>(~KNOWN_FLAGS)) != 0U) {
        return CodecResult::UNKNOWN_FLAGS;
    }
    if (input[17] != 0 || input[18] != 0 || input[19] != 0) {
        return CodecResult::NONZERO_RESERVED;
    }

    Settings decoded;
    decoded.displayTimeoutSeconds = readUint16Le(input + 12);
    decoded.displayContrast = input[14];
    decoded.defaultScreen = static_cast<DefaultScreen>(input[15]);
    decoded.ledEnabled = (input[16] & FLAG_LED_ENABLED) != 0U;
    decoded.diagnosticsEnabled =
        (input[16] & FLAG_DIAGNOSTICS_ENABLED) != 0U;
    decoded.buttonFeedbackEnabled =
        (input[16] & FLAG_BUTTON_FEEDBACK_ENABLED) != 0U;

    const CodecResult settingsResult = validateCodecSettings(decoded);
    if (settingsResult != CodecResult::OK) {
        return settingsResult;
    }

    *settings = decoded;
    *generation = readUint32Le(input + 8);
    return CodecResult::OK;
}

inline CodecResult decodeRecord(
    const uint8_t* input,
    size_t inputLength,
    Settings& settings,
    uint32_t& generation
) {
    return decodeRecord(input, inputLength, &settings, &generation);
}

}  // namespace DeviceSettings
