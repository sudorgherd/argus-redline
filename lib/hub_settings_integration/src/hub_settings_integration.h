#pragma once

#include <stdint.h>

#include "device_settings.h"
#include "device_ui.h"
#include "runtime_state.h"

namespace HubSettingsIntegration {

enum class Request : uint8_t {
    NONE,
    AUTOMATIC_REPAIR,
    SAVE,
    FACTORY_RESET
};

class RequestQueue {
public:
    void queueAutomaticRepair() {
        if (pending_ == Request::NONE) {
            pending_ = Request::AUTOMATIC_REPAIR;
        }
    }

    void queueSave(const DeviceSettings::Settings& draft) {
        if (pending_ == Request::FACTORY_RESET) {
            return;
        }
        saveDraft_ = draft;
        pending_ = Request::SAVE;
    }

    void queueFactoryReset() {
        pending_ = Request::FACTORY_RESET;
    }

    Request pending() const {
        return pending_;
    }

    const DeviceSettings::Settings& saveDraft() const {
        return saveDraft_;
    }

    void clear() {
        pending_ = Request::NONE;
    }

private:
    Request pending_ = Request::NONE;
    DeviceSettings::Settings saveDraft_ = DeviceSettings::defaults();
};

inline bool persistenceSafe(
    RuntimeState::RuntimePhase phase,
    bool awaitingAcknowledgment,
    bool retryScheduled,
    bool radioEventPending,
    bool rendering
) {
    return phase == RuntimeState::RuntimePhase::IDLE &&
        !awaitingAcknowledgment &&
        !retryScheduled &&
        !radioEventPending &&
        !rendering;
}

inline uint32_t timeoutMs(const DeviceSettings::Settings& settings) {
    return static_cast<uint32_t>(settings.displayTimeoutSeconds) * 1000U;
}

inline DeviceUi::Screen screen(DeviceSettings::DefaultScreen value) {
    switch (value) {
        case DeviceSettings::DefaultScreen::HOME:
            return DeviceUi::Screen::HOME;
        case DeviceSettings::DefaultScreen::RADIO:
            return DeviceUi::Screen::RADIO;
        case DeviceSettings::DefaultScreen::DEVICE:
            return DeviceUi::Screen::DEVICE;
        case DeviceSettings::DefaultScreen::LAST_PACKET:
            return DeviceUi::Screen::LAST_PACKET;
        case DeviceSettings::DefaultScreen::DIAGNOSTICS:
            return DeviceUi::Screen::DIAGNOSTICS;
        case DeviceSettings::DefaultScreen::ABOUT:
            return DeviceUi::Screen::ABOUT;
    }
    return DeviceUi::Screen::HOME;
}

inline DeviceUi::ConfigurationSource source(
    const DeviceSettings::SettingsManager& manager
) {
    if (!manager.hasActiveSlot()) {
        return DeviceUi::ConfigurationSource::DEFAULTS;
    }
    return manager.activeSlot() == DeviceSettings::RecordSlot::A
        ? DeviceUi::ConfigurationSource::SLOT_A
        : DeviceUi::ConfigurationSource::SLOT_B;
}

struct ConfigurationState {
    DeviceUi::ConfigurationStatus status =
        DeviceUi::ConfigurationStatus::NOT_SUPPLIED;
    DeviceUi::ConfigurationSource source =
        DeviceUi::ConfigurationSource::DEFAULTS;
    bool generationAvailable = false;
    uint32_t generation = 0;
    bool repairPending = false;
    bool unsupportedPreserved = false;
};

inline ConfigurationState fromLoad(
    DeviceSettings::LoadStatus result,
    const DeviceSettings::SettingsManager& manager
) {
    ConfigurationState state;
    state.source = source(manager);
    state.generationAvailable = manager.hasActiveSlot();
    state.generation = manager.generation();
    state.repairPending = manager.repairPending();
    state.unsupportedPreserved = manager.unsupportedSchema();
    switch (result) {
        case DeviceSettings::LoadStatus::LOADED:
            state.status = DeviceUi::ConfigurationStatus::LOADED;
            break;
        case DeviceSettings::LoadStatus::LOADED_FALLBACK_SLOT:
            state.status = DeviceUi::ConfigurationStatus::FALLBACK;
            break;
        case DeviceSettings::LoadStatus::REPAIRED_SCHEMA_1:
            state.status = DeviceUi::ConfigurationStatus::REPAIRED;
            break;
        case DeviceSettings::LoadStatus::DEFAULTED_MISSING:
        case DeviceSettings::LoadStatus::DEFAULTED_CORRUPT:
            state.status = DeviceUi::ConfigurationStatus::DEFAULTED;
            break;
        case DeviceSettings::LoadStatus::UNSUPPORTED_SCHEMA:
            state.status = DeviceUi::ConfigurationStatus::UNSUPPORTED;
            state.unsupportedPreserved = true;
            break;
        case DeviceSettings::LoadStatus::STORAGE_UNAVAILABLE:
            state.status = DeviceUi::ConfigurationStatus::UNAVAILABLE;
            break;
        case DeviceSettings::LoadStatus::RESET_COMPLETED:
            state.status = DeviceUi::ConfigurationStatus::RESET_COMPLETED;
            break;
        case DeviceSettings::LoadStatus::RESET_RECOVERY_FAILED:
            state.status = DeviceUi::ConfigurationStatus::RESET_FAILED;
            break;
    }
    return state;
}

inline ConfigurationState fromSave(
    DeviceSettings::SaveStatus result,
    const DeviceSettings::SettingsManager& manager
) {
    ConfigurationState state;
    state.source = source(manager);
    state.generationAvailable = manager.hasActiveSlot();
    state.generation = manager.generation();
    state.repairPending = manager.repairPending();
    state.unsupportedPreserved = manager.unsupportedSchema();
    switch (result) {
        case DeviceSettings::SaveStatus::SAVED:
        case DeviceSettings::SaveStatus::REPAIRED_SAVED:
            state.status = DeviceUi::ConfigurationStatus::SAVED;
            break;
        case DeviceSettings::SaveStatus::UNCHANGED:
        case DeviceSettings::SaveStatus::REPAIRED_UNCHANGED:
            state.status = DeviceUi::ConfigurationStatus::UNCHANGED;
            break;
        case DeviceSettings::SaveStatus::UNSUPPORTED_SCHEMA:
            state.status = DeviceUi::ConfigurationStatus::UNSUPPORTED;
            state.unsupportedPreserved = true;
            break;
        case DeviceSettings::SaveStatus::STORAGE_UNAVAILABLE:
        case DeviceSettings::SaveStatus::WRITE_FAILED:
        case DeviceSettings::SaveStatus::READ_BACK_FAILED:
        case DeviceSettings::SaveStatus::VERIFICATION_FAILED:
            state.status = DeviceUi::ConfigurationStatus::SAVE_FAILED;
            break;
    }
    return state;
}

inline ConfigurationState fromReset(
    DeviceSettings::ResetResult result,
    const DeviceSettings::SettingsManager& manager
) {
    ConfigurationState state;
    state.source = source(manager);
    state.generationAvailable = manager.hasActiveSlot();
    state.generation = manager.generation();
    state.repairPending = manager.repairPending();
    state.unsupportedPreserved = manager.unsupportedSchema();
    state.status = result == DeviceSettings::ResetResult::RESET_COMPLETED
        ? DeviceUi::ConfigurationStatus::RESET_COMPLETED
        : DeviceUi::ConfigurationStatus::RESET_FAILED;
    return state;
}

inline bool saveSucceeded(DeviceSettings::SaveStatus result) {
    return result == DeviceSettings::SaveStatus::SAVED ||
        result == DeviceSettings::SaveStatus::REPAIRED_SAVED;
}

inline bool saveUnchanged(DeviceSettings::SaveStatus result) {
    return result == DeviceSettings::SaveStatus::UNCHANGED ||
        result == DeviceSettings::SaveStatus::REPAIRED_UNCHANGED;
}

inline bool feedbackAllowed(
    const DeviceSettings::Settings& settings,
    DeviceInput::ButtonEvent event
) {
    return settings.ledEnabled && settings.buttonFeedbackEnabled &&
        (event == DeviceInput::ButtonEvent::SHORT_PRESS ||
         event == DeviceInput::ButtonEvent::LONG_PRESS ||
         event == DeviceInput::ButtonEvent::VERY_LONG_PRESS);
}

}  // namespace HubSettingsIntegration
