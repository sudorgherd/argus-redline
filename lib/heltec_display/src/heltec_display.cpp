#include "heltec_display.h"

#include <OLEDDisplayFonts.h>

#include <stdio.h>

namespace {

// Canonical RG asset and legacy Home geometry shared by both firmware roles.
const unsigned char RG_LOGO_26X16[] PROGMEM = {
    0xff, 0x00, 0xfc, 0x01, 0xff, 0x03, 0xff, 0x01,
    0xff, 0x87, 0xff, 0x01, 0x07, 0xc7, 0x03, 0x00,
    0x07, 0xef, 0x01, 0x00, 0x07, 0xe7, 0x00, 0x00,
    0x07, 0xe7, 0x00, 0x00, 0x87, 0xe7, 0xf0, 0x03,
    0xff, 0xe3, 0xf0, 0x03, 0xff, 0xe1, 0xf0, 0x03,
    0xc7, 0xe1, 0x80, 0x03, 0x87, 0xe3, 0x81, 0x03,
    0x87, 0xc7, 0x83, 0x03, 0x07, 0xc7, 0xff, 0x03,
    0x07, 0x8f, 0xff, 0x03, 0x07, 0x0e, 0xfe, 0x00
};

constexpr int16_t DISPLAY_RIGHT = 127;
constexpr int16_t DISPLAY_CENTER = 64;
constexpr int16_t TITLE_Y = 0;
constexpr int16_t FIRST_ROW_Y = 10;
constexpr int16_t ROW_SPACING = 10;
constexpr size_t HOME_STATUS_CAPACITY =
    DeviceUi::PRESENTATION_LABEL_CAPACITY +
    DeviceUi::PRESENTATION_VALUE_CAPACITY +
    3;

}  // namespace

namespace HeltecDisplay {

void Renderer::render(const DeviceUi::PresentationSnapshot& snapshot) {
    display_.clear();

    if (snapshot.screen == DeviceUi::Screen::HOME) {
        renderHome(snapshot);
    } else {
        renderInformation(snapshot);
    }

    display_.display();
}

void Renderer::displayOn() {
    display_.displayOn();
}

void Renderer::displayOff() {
    display_.displayOff();
}

void Renderer::renderHome(
    const DeviceUi::PresentationSnapshot& snapshot
) {
    display_.setColor(WHITE);
    display_.fillRect(48, 0, 32, 32);

    display_.setColor(BLACK);
    display_.drawXbm(51, 8, 26, 16, RG_LOGO_26X16);

    display_.setColor(WHITE);
    display_.setTextAlignment(TEXT_ALIGN_CENTER);
    display_.setFont(ArialMT_Plain_16);
    display_.drawString(DISPLAY_CENTER, 34, "RaveGoat Labs");

    char status[HOME_STATUS_CAPACITY] = {};
    if (snapshot.rowCount > 0) {
        snprintf(
            status,
            sizeof(status),
            "%s | %s",
            snapshot.rows[0].label,
            snapshot.rows[0].value
        );
    }

    display_.setFont(ArialMT_Plain_10);
    display_.drawString(DISPLAY_CENTER, 53, status);
}

void Renderer::renderInformation(
    const DeviceUi::PresentationSnapshot& snapshot
) {
    display_.setColor(WHITE);
    display_.setFont(ArialMT_Plain_10);
    display_.setTextAlignment(TEXT_ALIGN_CENTER);
    display_.drawString(DISPLAY_CENTER, TITLE_Y, snapshot.title);

    // The shared 128x64 layout supports at most five bounded content rows.
    const uint8_t rowCount = snapshot.rowCount < DeviceUi::MAX_PRESENTATION_ROWS
        ? snapshot.rowCount
        : DeviceUi::MAX_PRESENTATION_ROWS;

    for (uint8_t index = 0; index < rowCount; ++index) {
        const int16_t y = FIRST_ROW_Y + index * ROW_SPACING;
        const DeviceUi::PresentationRow& row = snapshot.rows[index];

        display_.setTextAlignment(TEXT_ALIGN_LEFT);
        display_.drawString(0, y, row.label);
        display_.setTextAlignment(TEXT_ALIGN_RIGHT);
        display_.drawString(DISPLAY_RIGHT, y, row.value);
    }
}

}  // namespace HeltecDisplay
