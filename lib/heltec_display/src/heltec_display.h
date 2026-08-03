#pragma once

#include <SSD1306Wire.h>

#include "device_ui.h"

namespace HeltecDisplay {

class Renderer {
public:
    explicit Renderer(SSD1306Wire& display) : display_(display) {}

    // Rendering and power calls contain no scheduling or ownership policy.
    void render(const DeviceUi::PresentationSnapshot& snapshot);
    void displayOn();
    void displayOff();

private:
    void renderHome(const DeviceUi::PresentationSnapshot& snapshot);
    void renderInformation(const DeviceUi::PresentationSnapshot& snapshot);

    SSD1306Wire& display_;
};

}  // namespace HeltecDisplay
