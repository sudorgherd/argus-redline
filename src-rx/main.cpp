#include <Arduino.h>
#include <Wire.h>
#include "SSD1306Wire.h"
#include <RadioLib.h>
#include "protocol.h"
#include "device_config.h"
#include "redline_version.h"
#include "runtime_state.h"
#include "transaction_engine.h"
#include "device_input.h"
#include "device_ui.h"
#include "heltec_display.h"
#include "esp32_settings_storage.h"
#include "node_settings_integration.h"
#include "heltec_v4_capabilities.h"
#include "capability_role_integration.h"
#include "esp32_event_storage.h"
#include "node_event_delivery.h"
#include "node_event_radio.h"
#include <esp_system.h>
#if defined(ARGUS_HOST_MACHINE_STREAM)
#include "host_role_integration.h"
#endif
#if defined(ARGUS_HOST_MACHINE_STREAM) && defined(ARGUS_HOST_TEXT_STREAM)
#error "Host machine stream and characterization text stream are mutually exclusive"
#endif
#if !defined(ARGUS_HOST_MACHINE_STREAM) && !defined(ARGUS_HOST_TEXT_STREAM)
#error "Production role must select exactly one Host stream interpretation"
#endif
#if defined(ARGUS_HOST_MACHINE_STREAM) && defined(ARGUS_CAPABILITY_CHARACTERIZATION)
#error "Capability characterization cannot share the binary Host stream"
#endif
#if defined(ARGUS_CAPABILITY_CHARACTERIZATION)
#include "capability_characterization.h"
#include "capability_characterization_arduino.h"
#endif

SSD1306Wire display(0x3C, SDA_OLED, SCL_OLED);

SPIClass radioSPI(FSPI);
SX1262 radio = new Module(8, 14, 12, 13, radioSPI);

constexpr uint8_t APPLICATION_BUTTON_PIN = 0;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 30;
constexpr uint32_t BUTTON_LONG_PRESS_MS = 800;
constexpr uint32_t BUTTON_VERY_LONG_PRESS_MS = 3000;
constexpr uint32_t BUTTON_FEEDBACK_MS = 50;

volatile bool operationDone = false;

RuntimeState::State runtimeState(
    RuntimeState::DeviceRole::NODE,
    DeviceConfig::LOCAL_ID,
    DeviceConfig::PEER_ID
);
DeviceInput::Button applicationButton(
    BUTTON_DEBOUNCE_MS,
    BUTTON_LONG_PRESS_MS,
    BUTTON_VERY_LONG_PRESS_MS
);
DeviceUi::Controller uiController(0);
HeltecDisplay::Renderer displayRenderer(display);
DeviceUi::PeerState peerState = DeviceUi::PeerState::UNKNOWN;
Esp32SettingsStorage::PreferencesRecordStore settingsStore;
DeviceSettings::SettingsManager settingsManager;
DeviceSettings::Settings currentSettings = DeviceSettings::defaults();
NodeSettingsIntegration::ConfigurationState configurationState;
NodeSettingsIntegration::RequestQueue persistenceRequests;
HeltecV4Capabilities::ProfileState capabilityState;
HeltecV4Capabilities::HeltecV4CapabilityHandler capabilityHandler(
    capabilityState
);
DeviceCapabilities::CapabilityDiagnostics capabilityDiagnostics;
bool capabilityRegistryValid = false;
bool capabilitySummaryAvailable = false;

bool renderingPresentation = false;
bool radioLedActive = false;
bool feedbackLedActive = false;
uint32_t feedbackLedUntilMs = 0;
bool quietMaintenanceAttemptArmed = false;
bool maintenanceOwnershipActive = false;

class EspEventEntropy final : public EventIdentity::EntropySource {
public:
    bool nextUint32(uint32_t& value) override { value = esp_random(); return true; }
};
class EventSequence final : public NodeEventDelivery::SequenceSource {
public:
    bool next(uint8_t& value) override {
        ++value_; if (value_ == 0) ++value_; value = value_; return true;
    }
private:
    uint8_t value_ = 0;
};
class EventJitter final : public NodeEventDelivery::JitterSource {
public:
    uint16_t nextMilliseconds() override {
        return static_cast<uint16_t>(esp_random() % 251U);
    }
};

Esp32EventStorage::PreferencesStore eventStorage;
EspEventEntropy eventEntropy;
NodeEventStore::Store nodeEventStore;
EventSequence eventSequence;
EventJitter eventJitter;
NodeEventDelivery::Controller eventDelivery;
EventRadioIntegration::NodeArbiter eventRadio;
EventRadioIntegration::CommandPreAckTimer commandPreAck;
bool eventSubsystemReady = false;

uint8_t receiveBuffer[Protocol::MAX_PACKET_SIZE] = {};
uint8_t transmitBuffer[Protocol::MAX_PACKET_SIZE] = {};
size_t transmitLength = 0;

Protocol::Packet pendingAcknowledgment = {};
bool pendingAcknowledgmentDuplicate = false;

TransactionEngine::NodeDuplicateTracker duplicateTracker;

#if defined(ARGUS_HOST_MACHINE_STREAM)
HostRoleIntegration::Stack<decltype(Serial)> hostStack(Serial, false);
RadioOperationBridge::NodeStructuredOperationProcessor structuredProcessor;
bool structuredAckThenExecute = false;
bool structuredCachedResponseAfterAck = false;
Protocol::Packet structuredResponse = {};
#endif

#if defined(ARGUS_CAPABILITY_CHARACTERIZATION)
CapabilityCharacterization::AnalogAdapter characterizationAnalog;
CapabilityCharacterization::Action pendingCharacterizationAction =
    CapabilityCharacterization::Action::NONE;
char characterizationCommand[
    CapabilityCharacterization::COMMAND_BUFFER_CAPACITY
] = {};
uint8_t characterizationCommandLength = 0;
bool characterizationCommandOverflow = false;
#endif

#if defined(ARGUS_HOST_MACHINE_STREAM)
bool queryHostAvailability(const void* context,
    DeviceCapabilities::CapabilityId capabilityId, bool& available) {
    return HeltecV4Capabilities::capabilityAvailability(
        *static_cast<const HeltecV4Capabilities::ProfileState*>(context),
        capabilityId, available);
}

HostOperationService::AvailabilityProvider hostAvailability() {
    return HostOperationService::makeAvailabilityProvider(
        &capabilityState, queryHostAvailability);
}
#endif

void IRAM_ATTR setRadioFlag() {
    operationDone = true;
}

void markPresentationChanged() {
    uiController.markDirty();
}

void setRuntimePhase(RuntimeState::RuntimePhase phase) {
    if (runtimeState.phase() != phase) {
        runtimeState.setPhase(phase);
        markPresentationChanged();
    }
}

void setHealth(RuntimeState::Health health) {
    if (runtimeState.health() != health) {
        runtimeState.setHealth(health);
        markPresentationChanged();
    }
}

void recordError(RuntimeState::ErrorClass error) {
    runtimeState.recordError(error);
    markPresentationChanged();
}

void setPeerState(DeviceUi::PeerState state) {
    if (peerState != state) {
        peerState = state;
        markPresentationChanged();
    }
}

DeviceUi::PresentationInput buildPresentationInput() {
    DeviceUi::PresentationInput input;
    input.role = runtimeState.role();
    input.localId = runtimeState.localId();
    input.peerId = runtimeState.peerId();
    input.ready = runtimeState.isReady();
    input.health = runtimeState.health();
    input.phase = runtimeState.phase();
    input.firmwareVersion = RedlineVersion::FIRMWARE;
    input.wireProtocolVersion = RedlineVersion::WIRE_PROTOCOL;
    input.hardwareProfile = RedlineVersion::HARDWARE_PROFILE;
    input.radioMetricsAvailable = runtimeState.hasRadioMetrics();
    input.rssi = runtimeState.latestRssi();
    input.snr = runtimeState.latestSnr();
    input.peerState = peerState;
    input.lastInboundPacket = runtimeState.lastInboundPacket();
    input.counters = runtimeState.counters();
    input.lastError = runtimeState.lastError();
    input.diagnosticsEnabled = currentSettings.diagnosticsEnabled;
    input.configurationStatus = configurationState.status;
    input.configurationSource = configurationState.source;
    input.configurationGenerationAvailable =
        configurationState.generationAvailable;
    input.configurationGeneration = configurationState.generation;
    input.configurationRepairPending = configurationState.repairPending;
    input.unsupportedConfigurationPreserved =
        configurationState.unsupportedPreserved;
    input.capabilitySummaryAvailable = capabilitySummaryAvailable;
    input.capabilityRegistryValid = capabilityRegistryValid;
    input.registeredCapabilityCount = DeviceCapabilities::capabilityCount(
        HeltecV4Capabilities::registryView()
    );
    if (runtimeState.hasCapabilityDiagnostics()) {
        const DeviceCapabilities::CapabilityDiagnosticsSnapshot& diagnostics =
            runtimeState.capabilityDiagnostics();
        input.capabilityLastStatusAvailable =
            diagnostics.lastStatusAvailable == 1;
        input.capabilityLastStatus = diagnostics.lastStatus;
    }
    return input;
}

void renderCurrentPresentation(uint32_t nowMs) {
    renderingPresentation = true;
    if (uiController.editorActive()) {
        const DeviceUi::EditorPresentationSnapshot snapshot =
            DeviceUi::buildEditorSnapshot(
                uiController.editorPresentation()
            );
        displayRenderer.render(snapshot);
    } else {
        const DeviceUi::PresentationSnapshot snapshot =
            DeviceUi::buildPresentation(
                uiController.screen(),
                buildPresentationInput()
            );
        displayRenderer.render(snapshot);
    }
    renderingPresentation = false;
    uiController.recordRendered(nowMs);
}

void updateLedOutput() {
    digitalWrite(
        LED_BUILTIN,
        CapabilityRoleIntegration::ledOutputRequested(
            currentSettings.ledEnabled,
            radioLedActive,
            feedbackLedActive,
            capabilityState.indicatorRequested
        )
            ? HIGH
            : LOW
    );
}

void requestButtonFeedback(
    DeviceInput::ButtonEvent event,
    uint32_t nowMs
) {
    if (!NodeSettingsIntegration::feedbackAllowed(currentSettings, event)) {
        return;
    }
    feedbackLedActive = true;
    feedbackLedUntilMs = nowMs + BUTTON_FEEDBACK_MS;
    updateLedOutput();
}

void serviceButtonFeedback(uint32_t nowMs) {
    if (
        feedbackLedActive &&
        static_cast<int32_t>(nowMs - feedbackLedUntilMs) >= 0
    ) {
        feedbackLedActive = false;
        updateLedOutput();
    }
}

void queueEditorAction() {
    switch (uiController.takeEditorAction()) {
        case DeviceUi::EditorAction::SAVE_SETTINGS_REQUEST:
            persistenceRequests.queueSave(uiController.draftSettings());
            quietMaintenanceAttemptArmed = true;
            break;
        case DeviceUi::EditorAction::FACTORY_RESET_REQUEST:
            persistenceRequests.queueFactoryReset();
            quietMaintenanceAttemptArmed = true;
            break;
        case DeviceUi::EditorAction::NONE:
            break;
    }
}

void serviceButton(uint32_t nowMs) {
    const bool pressed = digitalRead(APPLICATION_BUTTON_PIN) == LOW;
    const DeviceInput::ButtonEvents events =
        applicationButton.update(pressed, nowMs);
    const DeviceInput::ButtonSnapshot snapshot =
        applicationButton.snapshot();
    capabilityState.digitalInputAvailable = snapshot.available;
    capabilityState.digitalInputActive = snapshot.pressed;
    uiController.handle(events.first, nowMs);
    requestButtonFeedback(events.first, nowMs);
    uiController.handle(events.second, nowMs);
    requestButtonFeedback(events.second, nowMs);
    queueEditorAction();
}

bool capabilitySafe(
    CapabilityRoleIntegration::NodeOwnership ownership,
    bool radioStandby
) {
    CapabilityRoleIntegration::NodeSafePointState safePoint;
    safePoint.runtimeReady = runtimeState.isReady();
    safePoint.registryValid = capabilityRegistryValid;
    safePoint.phase = runtimeState.phase();
    safePoint.ownership = ownership;
    safePoint.radioStandby = radioStandby;
    safePoint.radioEventPending = operationDone;
    safePoint.rendering = renderingPresentation;
    return CapabilityRoleIntegration::nodeSafe(safePoint);
}

DeviceCapabilities::OperationResult executeLocalCapabilityNow(
    DeviceCapabilities::CapabilityId capabilityId,
    DeviceCapabilities::Operation operation,
    const DeviceCapabilities::CapabilityValue& input,
    const DeviceCapabilities::CallerContext& caller,
    DeviceCapabilities::InterlockState interlock
) {
    const DeviceCapabilities::OperationResult result =
        CapabilityRoleIntegration::executeLocalCapabilityNow(
            capabilityRegistryValid,
            HeltecV4Capabilities::registryView(),
            capabilityHandler,
            capabilityId,
            operation,
            input,
            caller,
            interlock,
            capabilityDiagnostics,
            runtimeState
        );
    markPresentationChanged();
    if (
        result.status == DeviceCapabilities::OperationStatus::OK &&
        capabilityId ==
            HeltecV4Capabilities::APPLICATION_INDICATOR_ID &&
        operation == DeviceCapabilities::Operation::SET
    ) {
        updateLedOutput();
    }
    return result;
}

#if defined(ARGUS_CAPABILITY_CHARACTERIZATION)
bool startListening();

DeviceCapabilities::CapabilityValue characterizationValue(
    DeviceCapabilities::ValueType type,
    uint32_t bits
) {
    DeviceCapabilities::CapabilityValue value = {};
    value.type = type;
    value.bits = bits;
    return value;
}

DeviceCapabilities::CallerContext characterizationCaller(
    DeviceCapabilities::CallerClass callerClass
) {
    DeviceCapabilities::CallerContext caller = {};
    caller.callerClass = callerClass;
    return caller;
}

void printCharacterizationResult(
    const char* action,
    const DeviceCapabilities::OperationResult& result,
    uint32_t durationUs
) {
    Serial.print("CAPVAL role=NODE action="); Serial.print(action);
    Serial.print(" status=");
    Serial.print(CapabilityCharacterization::statusLabel(result.status));
    Serial.print(" type="); Serial.print(static_cast<uint8_t>(result.value.type));
    Serial.print(" value="); Serial.print(result.value.bits);
    Serial.print(" us="); Serial.println(durationUs);
}

void performCharacterizationAction(CapabilityCharacterization::Action action) {
    using CapabilityCharacterization::Action;
    using namespace DeviceCapabilities;
    if (action == Action::HELP) {
        Serial.println("CAPVAL commands=help,caps,digital,indicator-on,indicator-off,analog,deny-remote,deny-interlock,status");
        return;
    }
    if (action == Action::CAPS) {
        const CapabilityRegistryView registry = HeltecV4Capabilities::registryView();
        Serial.print("CAPS count="); Serial.print(capabilityCount(registry));
        Serial.print(" valid="); Serial.println(capabilityRegistryValid ? 1 : 0);
        for (uint8_t index = 0; index < capabilityCount(registry); ++index) {
            CapabilityDescriptor descriptor = {};
            if (!getCapabilityByIndex(registry, index, descriptor)) continue;
            Serial.print("CAP index="); Serial.print(index);
            Serial.print(" id=0x"); Serial.print(descriptor.id, HEX);
            Serial.print(" class="); Serial.print(static_cast<uint8_t>(descriptor.capabilityClass));
            Serial.print(" ops="); Serial.println(descriptor.operationFlags);
        }
        return;
    }
    if (action == Action::STATUS) {
        Serial.print("CAPVAL role=NODE action=status registry=");
        Serial.print(capabilityRegistryValid ? 1 : 0);
        Serial.print(" indicator="); Serial.print(capabilityState.indicatorRequested ? 1 : 0);
        Serial.print(" digital_available="); Serial.print(capabilityState.digitalInputAvailable ? 1 : 0);
        Serial.print(" analog_available="); Serial.println(capabilityState.analogInputAvailable ? 1 : 0);
        return;
    }

    CapabilityId id = HeltecV4Capabilities::DIGITAL_INPUT_ID;
    Operation operation = Operation::READ;
    CapabilityValue input = characterizationValue(ValueType::NONE, 0);
    CallerClass callerClass = CallerClass::TEST;
    InterlockState interlock = InterlockState::CLEAR;
    const char* name = "digital";
    bool analogAction = false;
    bool denialAction = false;
    const bool priorIndicator = capabilityState.indicatorRequested;
    if (action == Action::INDICATOR_ON || action == Action::INDICATOR_OFF) {
        id = HeltecV4Capabilities::APPLICATION_INDICATOR_ID;
        operation = Operation::SET;
        input = characterizationValue(ValueType::BOOLEAN, action == Action::INDICATOR_ON ? 1 : 0);
        name = action == Action::INDICATOR_ON ? "indicator-on" : "indicator-off";
    } else if (action == Action::ANALOG_INPUT) {
        id = HeltecV4Capabilities::ANALOG_INPUT_0_ID;
        name = "analog";
        analogAction = true;
    } else if (action == Action::DENY_REMOTE || action == Action::DENY_INTERLOCK) {
        id = HeltecV4Capabilities::APPLICATION_INDICATOR_ID;
        operation = Operation::SET;
        input = characterizationValue(ValueType::BOOLEAN, priorIndicator ? 0 : 1);
        name = action == Action::DENY_REMOTE ? "deny-remote" : "deny-interlock";
        denialAction = true;
        if (action == Action::DENY_REMOTE) callerClass = CallerClass::FUTURE_REMOTE;
        else interlock = InterlockState::ACTIVE;
    }

    const uint32_t startedUs = micros();
    CapabilityCharacterization::AnalogSample sample;
    if (analogAction) {
        sample = characterizationAnalog.sampleOnce();
        capabilityState.analogInputAvailable = sample.available;
        capabilityState.analogInputNormalized = sample.normalized;
    }
    const OperationResult result = executeLocalCapabilityNow(
        id, operation, input, characterizationCaller(callerClass), interlock
    );
    const uint32_t durationUs = micros() - startedUs;
    if (analogAction) {
        Serial.print("CAPVAL role=NODE action=analog raw="); Serial.print(sample.raw);
        Serial.print(" norm="); Serial.print(sample.normalized);
        Serial.println(" calibrated_mv=UNAVAILABLE");
    }
    printCharacterizationResult(name, result, durationUs);
    if (denialAction) {
        Serial.print("CAPVAL side_effect_unchanged=");
        Serial.println(capabilityState.indicatorRequested == priorIndicator ? 1 : 0);
    }
}

void serviceCharacterizationInput() {
    while (Serial.available() > 0) {
        const char next = static_cast<char>(Serial.read());
        if (next == '\r') continue;
        if (next != '\n') {
            if (characterizationCommandLength + 1 < sizeof(characterizationCommand)) {
                characterizationCommand[characterizationCommandLength++] = next;
            } else characterizationCommandOverflow = true;
            continue;
        }
        characterizationCommand[characterizationCommandLength] = '\0';
        const CapabilityCharacterization::Action action = characterizationCommandOverflow
            ? CapabilityCharacterization::Action::NONE
            : CapabilityCharacterization::classifyCommand(characterizationCommand);
        if (action == CapabilityCharacterization::Action::NONE) {
            Serial.println("CAPVAL command=INVALID");
        } else if (pendingCharacterizationAction != CapabilityCharacterization::Action::NONE) {
            Serial.println("CAPVAL command=BUSY");
        } else pendingCharacterizationAction = action;
        characterizationCommandLength = 0;
        characterizationCommandOverflow = false;
    }
}

void serviceCharacterization() {
    using CapabilityCharacterization::Action;
    if (pendingCharacterizationAction == Action::NONE ||
        persistenceRequests.pending() != NodeSettingsIntegration::Request::NONE ||
        maintenanceOwnershipActive || renderingPresentation || operationDone ||
        runtimeState.phase() != RuntimeState::RuntimePhase::LISTENING) return;

    maintenanceOwnershipActive = true;
    const int standbyState = radio.standby();
    const bool eventArrived = operationDone;
    const NodeSettingsIntegration::AcquisitionOutcome acquisition =
        NodeSettingsIntegration::classifyAcquisition(
            standbyState == RADIOLIB_ERR_NONE,
            eventArrived || operationDone
        );
    if (acquisition == NodeSettingsIntegration::AcquisitionOutcome::EVENT_PENDING) {
        maintenanceOwnershipActive = false;
        return;
    }
    if (acquisition == NodeSettingsIntegration::AcquisitionOutcome::STANDBY_FAILED) {
        maintenanceOwnershipActive = false;
        startListening();
        return;
    }
    if (!capabilitySafe(CapabilityRoleIntegration::NodeOwnership::QUIET_MAINTENANCE, true)) {
        maintenanceOwnershipActive = false;
        if (!operationDone) startListening();
        return;
    }
    const Action action = pendingCharacterizationAction;
    pendingCharacterizationAction = Action::NONE;
    performCharacterizationAction(action);
    maintenanceOwnershipActive = false;
    startListening();
}
#endif

void serviceUi(uint32_t nowMs) {
    const DeviceUi::UiAction action = uiController.update(nowMs);

    switch (action) {
        case DeviceUi::UiAction::RENDER:
            renderCurrentPresentation(nowMs);
            break;

        case DeviceUi::UiAction::DISPLAY_ON:
            displayRenderer.displayOn();
            break;

        case DeviceUi::UiAction::DISPLAY_ON_AND_RENDER:
            displayRenderer.displayOn();
            renderCurrentPresentation(nowMs);
            break;

        case DeviceUi::UiAction::DISPLAY_OFF:
            displayRenderer.displayOff();
            break;

        case DeviceUi::UiAction::NONE:
            break;
    }
}

void showStatus(const String& primary, const String& secondary) {
#if defined(ARGUS_HOST_TEXT_STREAM)
    Serial.print(primary);

    if (secondary.length() > 0) {
        Serial.print(" | ");
        Serial.print(secondary);
    }

    Serial.println();
#else
    (void)primary;
    (void)secondary;
#endif
}

void logVersionMetadata() {
#if defined(ARGUS_HOST_TEXT_STREAM)
    Serial.print("Wire Protocol: ");
    Serial.println(RedlineVersion::WIRE_PROTOCOL);
    Serial.print("Hardware profile: ");
    Serial.println(RedlineVersion::HARDWARE_PROFILE);
#endif
}

void logPacket(const char* direction, const Protocol::Packet& packet) {
#if defined(ARGUS_HOST_TEXT_STREAM)
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
#else
    (void)direction;
    (void)packet;
#endif
}

void applyCurrentSettings(uint32_t nowMs, bool applyDefaultScreen) {
    uiController.setCurrentSettings(currentSettings);
    uiController.setInactivityTimeoutMs(
        NodeSettingsIntegration::timeoutMs(currentSettings),
        nowMs
    );
    displayRenderer.setContrast(currentSettings.displayContrast);
    if (applyDefaultScreen) {
        uiController.selectConfiguredScreen(
            NodeSettingsIntegration::screen(currentSettings.defaultScreen)
        );
    }
    updateLedOutput();
    markPresentationChanged();
}

void loadSettings(uint32_t nowMs) {
    const DeviceSettings::LoadStatus result =
        settingsManager.load(settingsStore);
    currentSettings = settingsManager.settings();
    configurationState = NodeSettingsIntegration::fromLoad(
        result,
        settingsManager
    );
    applyCurrentSettings(nowMs, true);
    if (settingsManager.repairPending()) {
        persistenceRequests.queueAutomaticRepair();
        quietMaintenanceAttemptArmed = true;
    }
}

void servicePendingPersistence(uint32_t nowMs) {
    const NodeSettingsIntegration::Request request =
        persistenceRequests.pending();
    if (request == NodeSettingsIntegration::Request::FACTORY_RESET) {
        const DeviceSettings::ResetResult result =
            settingsManager.factoryReset(settingsStore);
        configurationState = NodeSettingsIntegration::fromReset(
            result,
            settingsManager
        );
        if (result == DeviceSettings::ResetResult::RESET_COMPLETED) {
            currentSettings = settingsManager.settings();
            applyCurrentSettings(nowMs, true);
        } else {
            markPresentationChanged();
        }
        persistenceRequests.clear();
        quietMaintenanceAttemptArmed = false;
        return;
    }

    const DeviceSettings::Settings& candidate =
        request == NodeSettingsIntegration::Request::SAVE
            ? persistenceRequests.saveDraft()
            : currentSettings;
    const DeviceSettings::SaveStatus result =
        settingsManager.save(settingsStore, candidate);
    configurationState = NodeSettingsIntegration::fromSave(
        result,
        settingsManager
    );
    if (NodeSettingsIntegration::saveSucceeded(result)) {
        currentSettings = settingsManager.settings();
        applyCurrentSettings(nowMs, false);
    } else {
        markPresentationChanged();
    }
    persistenceRequests.clear();
    quietMaintenanceAttemptArmed = false;
}

void servicePersistenceAfterAcknowledgment(
    uint32_t nowMs,
    bool radioStandby
) {
    if (
        persistenceRequests.pending() ==
            NodeSettingsIntegration::Request::NONE
    ) {
        return;
    }

    NodeSettingsIntegration::SafePointState safePoint;
    safePoint.acknowledgmentCompleted = true;
    safePoint.radioEventPending = operationDone;
    safePoint.radioStandby = radioStandby;
    safePoint.rendering = renderingPresentation;
    if (!NodeSettingsIntegration::persistenceSafe(safePoint)) {
        if (!radioStandby) {
            quietMaintenanceAttemptArmed = false;
        }
        return;
    }
    servicePendingPersistence(nowMs);
}

bool startListening() {
    operationDone = false;
    setRuntimePhase(RuntimeState::RuntimePhase::LISTENING);

    const int state = radio.startReceive();

    if (state != RADIOLIB_ERR_NONE) {
        runtimeState.incrementRadioErrors();
        setHealth(RuntimeState::Health::DEGRADED);
        recordError(RuntimeState::ErrorClass::RADIO_START_RECEIVE);
        showStatus(
            "RX START FAILED",
            String("CODE ") + state
        );
        return false;
    }

    eventRadio.restoreListening();

    return true;
}

void resumeListening() {
    radio.standby();
    if (startListening()) {
        setHealth(
            capabilityRegistryValid
                ? RuntimeState::Health::READY
                : RuntimeState::Health::DEGRADED
        );
    }
}

void serviceQuietMaintenance(uint32_t nowMs) {
    if (!NodeSettingsIntegration::quietMaintenanceAllowed(
            persistenceRequests.pending(),
            quietMaintenanceAttemptArmed,
            runtimeState.phase(),
            operationDone,
            false,
            false,
            renderingPresentation
        )) {
        return;
    }

    maintenanceOwnershipActive = true;
    // standby() is the ownership boundary. Recheck the ISR flag after it;
    // an event that wins this race remains set for normal packet processing.
    const int standbyState = radio.standby();
    const bool eventArrivedDuringAcquisition = operationDone;
    const NodeSettingsIntegration::AcquisitionOutcome acquisition =
        NodeSettingsIntegration::classifyAcquisition(
            standbyState == RADIOLIB_ERR_NONE,
            eventArrivedDuringAcquisition || operationDone
        );
    if (
        acquisition ==
            NodeSettingsIntegration::AcquisitionOutcome::EVENT_PENDING
    ) {
        maintenanceOwnershipActive = false;
        return;
    }
    if (
        acquisition ==
            NodeSettingsIntegration::AcquisitionOutcome::STANDBY_FAILED
    ) {
        maintenanceOwnershipActive = false;
        quietMaintenanceAttemptArmed = false;
        startListening();
        return;
    }

    NodeSettingsIntegration::SafePointState safePoint;
    safePoint.quietListeningMaintenance = true;
    safePoint.radioEventPending = operationDone;
    safePoint.radioStandby = true;
    safePoint.rendering = renderingPresentation;
    if (!NodeSettingsIntegration::persistenceSafe(safePoint)) {
        maintenanceOwnershipActive = false;
        if (!operationDone) {
            startListening();
        }
        return;
    }
    servicePendingPersistence(nowMs);
    maintenanceOwnershipActive = false;
    startListening();
}

bool startAcknowledgment(
    const Protocol::Packet& command,
    Protocol::AckStatus status,
    bool duplicate
) {
    pendingAcknowledgment =
        TransactionEngine::makeAcknowledgment(command, status);

    transmitLength = 0;

    if (
        !Protocol::encode(
            pendingAcknowledgment,
            transmitBuffer,
            sizeof(transmitBuffer),
            transmitLength
        )
    ) {
        showStatus("ACK FAILED", "encode error");
        startListening();
        return false;
    }

    pendingAcknowledgmentDuplicate = duplicate;

    if (radio.standby() != RADIOLIB_ERR_NONE) {
        startListening();
        return false;
    }
    eventRadio.beginCommandPreAck();
    commandPreAck.begin(static_cast<uint32_t>(millis()));
    setRuntimePhase(RuntimeState::RuntimePhase::TRANSMITTING_ACK);
    return true;
}

void serviceCommandPreAck(uint32_t nowMs) {
    if (!commandPreAck.due(nowMs)) return;
    commandPreAck.clear();
    operationDone = false;
    eventRadio.beginCommandAckTx();
    radioLedActive = true;
    updateLedOutput();

    const int state = radio.startTransmit(
        transmitBuffer,
        transmitLength
    );

    if (state != RADIOLIB_ERR_NONE) {
        radioLedActive = false;
        updateLedOutput();
        runtimeState.incrementRadioErrors();
        setHealth(RuntimeState::Health::DEGRADED);
        recordError(RuntimeState::ErrorClass::RADIO_START_TRANSMIT);

        showStatus(
            "ACK FAILED",
            String("CODE ") + state
        );
#if defined(ARGUS_HOST_MACHINE_STREAM)
        structuredAckThenExecute = false;
        structuredCachedResponseAfterAck = false;
#endif
        startListening();
        return;
    }
}

#if defined(ARGUS_HOST_MACHINE_STREAM)
bool startStructuredResponse(const Protocol::Packet& response) {
    size_t length = 0;
    if (!Protocol::encode(response, transmitBuffer, sizeof(transmitBuffer), length))
        return false;
    structuredResponse = response;
    transmitLength = length;
    operationDone = false;
    setRuntimePhase(RuntimeState::RuntimePhase::TRANSMITTING_RESPONSE);
    eventRadio.beginCommandResponseTx();
    radioLedActive = true;
    updateLedOutput();
    if (radio.startTransmit(transmitBuffer, transmitLength) != RADIOLIB_ERR_NONE) {
        radioLedActive = false;
        updateLedOutput();
        runtimeState.incrementRadioErrors();
        recordError(RuntimeState::ErrorClass::RADIO_START_TRANSMIT);
        startListening();
        return false;
    }
    return true;
}

bool beginStructuredResponseAfterAck() {
    Protocol::Packet response = structuredResponse;
    if (structuredAckThenExecute) {
        const HostOperationService::DeviceSnapshot snapshot =
            HostOperationService::makeDeviceSnapshot(runtimeState,
                static_cast<uint32_t>(millis()) / 1000U);
        const RadioOperationBridge::NodeResult executed =
            structuredProcessor.executeAdmittedOperation(snapshot,
                capabilityRegistryValid, HeltecV4Capabilities::registryView(),
                capabilityHandler, DeviceCapabilities::InterlockState::CLEAR,
                capabilityDiagnostics, runtimeState, hostAvailability());
        runtimeState.updateCapabilityDiagnostics(capabilityDiagnostics.snapshot());
        if (executed.action != RadioOperationBridge::NodeAction::RESPONSE_READY)
            return false;
        response = executed.response;
    }
    structuredAckThenExecute = false;
    structuredCachedResponseAfterAck = false;
    updateLedOutput();
    return startStructuredResponse(response);
}

void finishStructuredResponse() {
    radioLedActive = false;
    updateLedOutput();
    runtimeState.incrementTransmissionsCompleted();
    runtimeState.recordActivity(static_cast<uint32_t>(millis()));
    markPresentationChanged();
    startListening();
}

void serviceProductionHost(uint32_t nowMs) {
    hostStack.observeConnection(static_cast<bool>(Serial));
    hostStack.serviceTx();
    hostStack.servicePendingDelivery();
    if (!runtimeState.isReady() || operationDone ||
        runtimeState.phase() != RuntimeState::RuntimePhase::LISTENING ||
        Serial.available() <= 0) return;
    if (radio.standby() != RADIOLIB_ERR_NONE || operationDone) {
        startListening();
        return;
    }
    const HostOperationService::DeviceSnapshot snapshot =
        HostOperationService::makeDeviceSnapshot(runtimeState, nowMs / 1000U);
    hostStack.serviceRx(snapshot, runtimeState.peerId(), capabilityRegistryValid,
        HeltecV4Capabilities::registryView(), capabilityHandler,
        DeviceCapabilities::InterlockState::CLEAR, capabilityDiagnostics,
        runtimeState, hostAvailability(), nowMs, 2500U, false);
    runtimeState.updateCapabilityDiagnostics(capabilityDiagnostics.snapshot());
    updateLedOutput();
    startListening();
}
#endif

void finishAcknowledgment() {
    radioLedActive = false;
    updateLedOutput();
    const int standbyState = radio.standby();

    logPacket("TX", pendingAcknowledgment);

    showStatus(
        pendingAcknowledgmentDuplicate ? "DUPLICATE" : "COMMAND OK",
        String("ACK SEQ ") + pendingAcknowledgment.sequence
    );

#if defined(ARGUS_HOST_MACHINE_STREAM)
    if (structuredAckThenExecute || structuredCachedResponseAfterAck) {
        runtimeState.incrementTransmissionsCompleted();
        runtimeState.recordActivity(static_cast<uint32_t>(millis()));
        markPresentationChanged();
        beginStructuredResponseAfterAck();
        return;
    }
#endif
    servicePersistenceAfterAcknowledgment(
        static_cast<uint32_t>(millis()),
        standbyState == RADIOLIB_ERR_NONE
    );
    const bool listening = startListening();
    runtimeState.incrementTransmissionsCompleted();
    runtimeState.recordActivity(static_cast<uint32_t>(millis()));
    markPresentationChanged();

    if (listening) {
        setHealth(
            capabilityRegistryValid
                ? RuntimeState::Health::READY
                : RuntimeState::Health::DEGRADED
        );
    }
}

void handleReceivedPacket() {
    const size_t packetLength = radio.getPacketLength();

    if (
        packetLength < Protocol::HEADER_SIZE ||
        packetLength > Protocol::MAX_PACKET_SIZE
    ) {
        runtimeState.incrementMalformedPackets();
        recordError(RuntimeState::ErrorClass::PACKET_LENGTH);
        showStatus(
            "RX MALFORMED",
            String("LEN ") + packetLength
        );

        resumeListening();
        return;
    }

    const int readState = radio.readData(
        receiveBuffer,
        packetLength
    );

    if (readState != RADIOLIB_ERR_NONE) {
        runtimeState.incrementRadioErrors();
        setHealth(RuntimeState::Health::DEGRADED);
        recordError(RuntimeState::ErrorClass::RADIO_READ);
        showStatus(
            "RX FAILED",
            String("CODE ") + readState
        );

        resumeListening();
        return;
    }

    const uint32_t observedAtMs = static_cast<uint32_t>(millis());
    const float rssi = radio.getRSSI();
    const float snr = radio.getSNR();
    runtimeState.updateRadioMetrics(rssi, snr);
    runtimeState.recordActivity(observedAtMs);
    markPresentationChanged();

    if (eventSubsystemReady &&
        eventDelivery.state() == NodeEventDelivery::RuntimeState::WAIT_ADMISSION) {
        (void)eventDelivery.admissionCandidate(
            receiveBuffer, packetLength, observedAtMs);
    }

    Protocol::Packet command = {};

    if (!Protocol::decode(receiveBuffer, packetLength, command)) {
        runtimeState.incrementMalformedPackets();
        recordError(RuntimeState::ErrorClass::PACKET_DECODE);
        showStatus("RX MALFORMED", "decode failed");
        resumeListening();
        return;
    }

    runtimeState.incrementDecodedPacketsReceived();
    runtimeState.recordInboundPacket(
        static_cast<uint8_t>(command.type),
        command.source,
        command.destination,
        command.sequence,
        command.opcode,
        command.payloadLength,
        false,
        0,
        observedAtMs
    );
    markPresentationChanged();

    logPacket("RX", command);

#if defined(ARGUS_HOST_TEXT_STREAM)
    Serial.print("RSSI: ");
    Serial.print(runtimeState.latestRssi());
    Serial.print(" dBm | SNR: ");
    Serial.print(runtimeState.latestSnr());
    Serial.println(" dB");
#endif

#if defined(ARGUS_HOST_MACHINE_STREAM)
    if (command.type == Protocol::PacketType::COMMAND &&
        WireOperations::isStructuredOpcode(command.opcode)) {
        const RadioOperationBridge::NodeResult structured =
            structuredProcessor.admit(command, runtimeState.localId(),
                runtimeState.peerId());
        switch (structured.action) {
            case RadioOperationBridge::NodeAction::ACK_THEN_EXECUTE:
                runtimeState.incrementAcceptedCommands();
                structuredAckThenExecute = true;
                structuredCachedResponseAfterAck = false;
                startAcknowledgment(command, Protocol::AckStatus::SUCCESS, false);
                return;
            case RadioOperationBridge::NodeAction::SEND_ACK:
                runtimeState.incrementDuplicates();
                startAcknowledgment(command, Protocol::AckStatus::SUCCESS, true);
                return;
            case RadioOperationBridge::NodeAction::SEND_ACK_AND_RESPONSE:
                runtimeState.incrementDuplicates();
                structuredResponse = structured.response;
                structuredCachedResponseAfterAck = true;
                structuredAckThenExecute = false;
                startAcknowledgment(command, Protocol::AckStatus::SUCCESS, true);
                return;
            case RadioOperationBridge::NodeAction::SEND_REJECTION_ACK:
                startAcknowledgment(command,
                    static_cast<Protocol::AckStatus>(structured.acknowledgment.payload[0]),
                    false);
                return;
            case RadioOperationBridge::NodeAction::ACTIVE_BUSY:
            case RadioOperationBridge::NodeAction::IGNORE:
            case RadioOperationBridge::NodeAction::RESPONSE_READY:
                resumeListening();
                return;
        }
    }
#endif

    const TransactionEngine::NodeCommandEvaluation evaluation =
        TransactionEngine::evaluateNodeCommand(
            command,
            runtimeState.localId(),
            runtimeState.peerId(),
            duplicateTracker
        );

    switch (evaluation.outcome) {
        case TransactionEngine::NodeCommandOutcome::
            IGNORE_WRONG_DESTINATION:
            runtimeState.incrementIgnoredPackets();
            recordError(RuntimeState::ErrorClass::PACKET_IGNORED);
            showStatus(
                "RX IGNORED",
                String("DST ") + command.destination
            );
            resumeListening();
            return;

        case TransactionEngine::NodeCommandOutcome::
            IGNORE_WRONG_SENDER:
        case TransactionEngine::NodeCommandOutcome::
            IGNORE_WRONG_PACKET_TYPE:
            runtimeState.incrementIgnoredPackets();
            recordError(RuntimeState::ErrorClass::PACKET_IGNORED);
            showStatus("RX IGNORED", "invalid sender/type");
            resumeListening();
            return;

        case TransactionEngine::NodeCommandOutcome::DUPLICATE:
            runtimeState.incrementDuplicates();
            markPresentationChanged();
            startAcknowledgment(command, evaluation.status, true);
            return;

        case TransactionEngine::NodeCommandOutcome::ACK_SUCCESS:
            runtimeState.incrementAcceptedCommands();
            setHealth(
                capabilityRegistryValid
                    ? RuntimeState::Health::READY
                    : RuntimeState::Health::DEGRADED
            );
            recordError(RuntimeState::ErrorClass::NONE);
            setPeerState(DeviceUi::PeerState::SEEN);
            markPresentationChanged();
            startAcknowledgment(command, evaluation.status, false);
            return;

        case TransactionEngine::NodeCommandOutcome::
            ACK_UNSUPPORTED_OPCODE:
            startAcknowledgment(command, evaluation.status, false);
            return;

        case TransactionEngine::NodeCommandOutcome::
            ACK_MALFORMED_PACKET:
            runtimeState.incrementMalformedPackets();
            markPresentationChanged();
            startAcknowledgment(command, evaluation.status, false);
            return;
    }
}

void setup() {
    Serial.begin(115200);
#if defined(ARGUS_HOST_MACHINE_STREAM)
    Serial.setTxTimeoutMs(0);
    Serial.setDebugOutput(false);
#endif

    HeltecV4Capabilities::initializeProfileState(capabilityState);
    capabilityRegistryValid =
        DeviceCapabilities::isValidCapabilityRegistry(
            HeltecV4Capabilities::registryView()
        );
    capabilitySummaryAvailable = true;

    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(Vext, OUTPUT);
    pinMode(RST_OLED, OUTPUT);
    // GPIO0 is also a boot strap; holding it during reset may enter download mode.
    pinMode(APPLICATION_BUTTON_PIN, INPUT_PULLUP);

    digitalWrite(LED_BUILTIN, LOW);
    digitalWrite(Vext, LOW);
    delay(100);

    digitalWrite(RST_OLED, LOW);
    delay(20);
    digitalWrite(RST_OLED, HIGH);
    delay(20);

    display.init();

    loadSettings(static_cast<uint32_t>(millis()));

    const NodeEventStore::Status eventStoreStatus = nodeEventStore.recover(
        eventStorage, eventEntropy, DeviceConfig::LOCAL_ID);
    if (eventStoreStatus == NodeEventStore::Status::READY) {
        const NodeEventDelivery::ControllerResult recovered = eventDelivery.recover(
            nodeEventStore, DeviceConfig::LOCAL_ID, DeviceConfig::PEER_ID,
            eventSequence, eventJitter, static_cast<uint32_t>(millis()));
        eventSubsystemReady =
            recovered.status == NodeEventDelivery::ControllerStatus::OK ||
            recovered.status == NodeEventDelivery::ControllerStatus::NO_ACTIVE_EVENT;
    }

    radioSPI.begin(9, 11, 10, 8);

    const int state = radio.begin(915.0);

#if defined(ARGUS_HOST_TEXT_STREAM)
    Serial.print("Radio init state: ");
    Serial.println(state);
#endif

    if (state != RADIOLIB_ERR_NONE) {
        runtimeState.setReady(false);
        setHealth(RuntimeState::Health::ERROR);
        recordError(RuntimeState::ErrorClass::RADIO_INITIALIZATION);
        serviceUi(static_cast<uint32_t>(millis()));
        showStatus("RADIO ERROR", String("CODE ") + state);
        return;
    }

    radio.setDio1Action(setRadioFlag);

    if (!startListening()) {
        runtimeState.setReady(false);
        setHealth(RuntimeState::Health::ERROR);
        serviceUi(static_cast<uint32_t>(millis()));
        return;
    }

#if defined(ARGUS_CAPABILITY_CHARACTERIZATION)
    const bool adcReady = characterizationAnalog.begin();
    Serial.print("CHARACTERIZATION BUILD role=NODE adc_ready=");
    Serial.println(adcReady ? 1 : 0);
#endif

    runtimeState.setReady(true);
    setHealth(
        capabilityRegistryValid
            ? RuntimeState::Health::READY
            : RuntimeState::Health::DEGRADED
    );
    markPresentationChanged();
    serviceUi(static_cast<uint32_t>(millis()));
    showStatus(
        runtimeState.role() == RuntimeState::DeviceRole::NODE
            ? "NODE READY"
            : "HUB READY",
        String("Firmware: ") + RedlineVersion::FIRMWARE
    );
    logVersionMetadata();
}

void serviceNodeEvents(uint32_t nowMs) {
    if (!eventSubsystemReady) return;
    const bool synchronousWork = commandPreAck.active() ||
        eventRadio.owner() != EventRadioIntegration::NodeOwner::LISTENING ||
        runtimeState.phase() != RuntimeState::RuntimePhase::LISTENING ||
        operationDone || maintenanceOwnershipActive || renderingPresentation;
    const NodeEventDelivery::ControllerResult serviced =
        eventDelivery.service(nowMs, synchronousWork);
    if (serviced.status == NodeEventDelivery::ControllerStatus::DEGRADED ||
        serviced.status == NodeEventDelivery::ControllerStatus::POLICY_FAILURE ||
        serviced.status == NodeEventDelivery::ControllerStatus::STORAGE_FAILURE) {
        eventSubsystemReady = false;
        return;
    }
    EventRadioIntegration::NodeSafePoint point = {
        runtimeState.isReady(),
        runtimeState.phase() == RuntimeState::RuntimePhase::LISTENING,
        operationDone,
        synchronousWork,
        maintenanceOwnershipActive,
        renderingPresentation
    };
    if (eventRadio.requestEvent(point, eventDelivery.state()) !=
        EventRadioIntegration::NodeAcquireResult::ACQUIRE_STANDBY) return;
    const bool standbyOk = radio.standby() == RADIOLIB_ERR_NONE;
    const EventRadioIntegration::NodeAcquireResult acquired =
        eventRadio.finishStandby(standbyOk, operationDone);
    if (acquired == EventRadioIntegration::NodeAcquireResult::RECEIVED_PACKET_WON) return;
    if (acquired != EventRadioIntegration::NodeAcquireResult::GRANT_EVENT_TX) {
        startListening(); return;
    }
    const NodeEventDelivery::ControllerResult grant = eventDelivery.grantTransmit(nowMs);
    if (grant.action.type != NodeEventDelivery::RadioActionType::TRANSMIT) {
        startListening(); return;
    }
    operationDone = false;
    if (radio.startTransmit(grant.action.bytes, grant.action.length) != RADIOLIB_ERR_NONE) {
        (void)eventDelivery.txStartFailed(nowMs);
        startListening();
        return;
    }
    (void)eventDelivery.txStarted(nowMs);
}

void loop() {
    const uint32_t nowMs = static_cast<uint32_t>(millis());
    serviceButton(nowMs);
    serviceButtonFeedback(nowMs);
    serviceCommandPreAck(nowMs);

#if defined(ARGUS_CAPABILITY_CHARACTERIZATION)
    serviceCharacterizationInput();
#endif

    if (runtimeState.isReady() && operationDone) {
        operationDone = false;

        if (eventRadio.eventOwnsRadio()) {
            (void)eventDelivery.txCompleted(nowMs);
            startListening();
        } else if (
            runtimeState.phase() ==
            RuntimeState::RuntimePhase::TRANSMITTING_ACK
        ) {
            finishAcknowledgment();
#if defined(ARGUS_HOST_MACHINE_STREAM)
        } else if (runtimeState.phase() ==
            RuntimeState::RuntimePhase::TRANSMITTING_RESPONSE) {
            finishStructuredResponse();
#endif
        } else {
            handleReceivedPacket();
        }
    }

    if (runtimeState.isReady()) {
#if defined(ARGUS_HOST_MACHINE_STREAM)
        serviceProductionHost(nowMs);
#endif
        serviceQuietMaintenance(nowMs);
#if defined(ARGUS_CAPABILITY_CHARACTERIZATION)
        serviceCharacterization();
#endif
    }

    serviceNodeEvents(nowMs);

    // Defer OLED I/O until an active ACK has completed and receive restarted.
    if (
        runtimeState.phase() !=
            RuntimeState::RuntimePhase::TRANSMITTING_ACK &&
#if defined(ARGUS_HOST_MACHINE_STREAM)
        runtimeState.phase() != RuntimeState::RuntimePhase::TRANSMITTING_RESPONSE &&
#endif
        !operationDone &&
        !maintenanceOwnershipActive
    ) {
        serviceUi(nowMs);
    }

    if (!runtimeState.isReady()) {
        yield();
    }
}
