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
#include "hub_settings_integration.h"
#include "heltec_v4_capabilities.h"
#include "capability_role_integration.h"
#if defined(ARGUS_CAPABILITY_CHARACTERIZATION)
#include "capability_characterization.h"
#include "capability_characterization_arduino.h"
#endif

SSD1306Wire display(0x3C, SDA_OLED, SCL_OLED);

SPIClass radioSPI(FSPI);
SX1262 radio = new Module(8, 14, 12, 13, radioSPI);

constexpr unsigned long INITIAL_DELAY_MS = 2000;
constexpr unsigned long TRANSACTION_INTERVAL_MS = 3000;
constexpr unsigned long RETRY_DELAY_MS = 500;
constexpr uint8_t APPLICATION_BUTTON_PIN = 0;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 30;
constexpr uint32_t BUTTON_LONG_PRESS_MS = 800;
constexpr uint32_t BUTTON_VERY_LONG_PRESS_MS = 3000;
constexpr uint32_t BUTTON_FEEDBACK_MS = 50;

volatile bool operationDone = false;

RuntimeState::State runtimeState(
    RuntimeState::DeviceRole::HUB,
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
HubSettingsIntegration::ConfigurationState configurationState;
HubSettingsIntegration::RequestQueue persistenceRequests;
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

unsigned long nextTransmitAt = 0;

TransactionEngine::HubTransactionState transactionState;

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

Protocol::Packet currentCommand = {};
uint8_t transmitBuffer[Protocol::MAX_PACKET_SIZE] = {};
size_t transmitLength = 0;

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
    if (!HubSettingsIntegration::feedbackAllowed(currentSettings, event)) {
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
            break;
        case DeviceUi::EditorAction::FACTORY_RESET_REQUEST:
            persistenceRequests.queueFactoryReset();
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
    Serial.print(primary);

    if (secondary.length() > 0) {
        Serial.print(" | ");
        Serial.print(secondary);
    }

    Serial.println();
}

void logVersionMetadata() {
    Serial.print("Wire Protocol: ");
    Serial.println(RedlineVersion::WIRE_PROTOCOL);
    Serial.print("Hardware profile: ");
    Serial.println(RedlineVersion::HARDWARE_PROFILE);
}

void logPacket(const char* direction, const Protocol::Packet& packet) {
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
}

void applyCurrentSettings(uint32_t nowMs, bool applyDefaultScreen) {
    uiController.setCurrentSettings(currentSettings);
    uiController.setInactivityTimeoutMs(
        HubSettingsIntegration::timeoutMs(currentSettings),
        nowMs
    );
    displayRenderer.setContrast(currentSettings.displayContrast);
    if (applyDefaultScreen) {
        uiController.selectConfiguredScreen(
            HubSettingsIntegration::screen(currentSettings.defaultScreen)
        );
    }
    updateLedOutput();
    markPresentationChanged();
}

void loadSettings(uint32_t nowMs) {
    const DeviceSettings::LoadStatus result =
        settingsManager.load(settingsStore);
    currentSettings = settingsManager.settings();
    configurationState = HubSettingsIntegration::fromLoad(
        result,
        settingsManager
    );
    applyCurrentSettings(nowMs, true);
    if (settingsManager.repairPending()) {
        persistenceRequests.queueAutomaticRepair();
    }
}

bool persistenceSafe() {
    return HubSettingsIntegration::persistenceSafe(
        runtimeState.phase(),
        transactionState.isAwaitingAcknowledgment(),
        transactionState.retryCount() != 0,
        operationDone,
        renderingPresentation
    );
}

bool capabilitySafe(uint32_t nowMs) {
    CapabilityRoleIntegration::HubSafePointState safePoint;
    safePoint.runtimeReady = runtimeState.isReady();
    safePoint.registryValid = capabilityRegistryValid;
    safePoint.phase = runtimeState.phase();
    safePoint.awaitingAcknowledgment =
        transactionState.isAwaitingAcknowledgment();
    safePoint.retryCount = transactionState.retryCount();
    safePoint.radioEventPending = operationDone;
    safePoint.rendering = renderingPresentation;
    safePoint.persistencePending =
        persistenceRequests.pending() !=
        HubSettingsIntegration::Request::NONE;
    safePoint.transmissionDue =
        static_cast<int32_t>(nowMs - nextTransmitAt) >= 0;
    return CapabilityRoleIntegration::hubSafe(safePoint);
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
    Serial.print("CAPVAL role=HUB action=");
    Serial.print(action);
    Serial.print(" status=");
    Serial.print(CapabilityCharacterization::statusLabel(result.status));
    Serial.print(" type=");
    Serial.print(static_cast<uint8_t>(result.value.type));
    Serial.print(" value=");
    Serial.print(result.value.bits);
    Serial.print(" us=");
    Serial.println(durationUs);
}

void performCharacterizationAction(
    CapabilityCharacterization::Action action
) {
    using CapabilityCharacterization::Action;
    using namespace DeviceCapabilities;
    if (action == Action::HELP) {
        Serial.println(
            "CAPVAL commands=help,caps,digital,indicator-on,indicator-off,analog,deny-remote,deny-interlock,status"
        );
        return;
    }
    if (action == Action::CAPS) {
        const CapabilityRegistryView registry =
            HeltecV4Capabilities::registryView();
        Serial.print("CAPS count=");
        Serial.print(capabilityCount(registry));
        Serial.print(" valid=");
        Serial.println(capabilityRegistryValid ? 1 : 0);
        for (uint8_t index = 0; index < capabilityCount(registry); ++index) {
            CapabilityDescriptor descriptor = {};
            if (!getCapabilityByIndex(registry, index, descriptor)) continue;
            Serial.print("CAP index="); Serial.print(index);
            Serial.print(" id=0x"); Serial.print(descriptor.id, HEX);
            Serial.print(" class=");
            Serial.print(static_cast<uint8_t>(descriptor.capabilityClass));
            Serial.print(" ops="); Serial.println(descriptor.operationFlags);
        }
        return;
    }
    if (action == Action::STATUS) {
        Serial.print("CAPVAL role=HUB action=status registry=");
        Serial.print(capabilityRegistryValid ? 1 : 0);
        Serial.print(" indicator=");
        Serial.print(capabilityState.indicatorRequested ? 1 : 0);
        Serial.print(" digital_available=");
        Serial.print(capabilityState.digitalInputAvailable ? 1 : 0);
        Serial.print(" analog_available=");
        Serial.println(capabilityState.analogInputAvailable ? 1 : 0);
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
    bool priorIndicator = capabilityState.indicatorRequested;

    if (action == Action::INDICATOR_ON ||
        action == Action::INDICATOR_OFF) {
        id = HeltecV4Capabilities::APPLICATION_INDICATOR_ID;
        operation = Operation::SET;
        input = characterizationValue(
            ValueType::BOOLEAN,
            action == Action::INDICATOR_ON ? 1 : 0
        );
        name = action == Action::INDICATOR_ON
            ? "indicator-on" : "indicator-off";
    } else if (action == Action::ANALOG_INPUT) {
        id = HeltecV4Capabilities::ANALOG_INPUT_0_ID;
        name = "analog";
        analogAction = true;
    } else if (action == Action::DENY_REMOTE ||
               action == Action::DENY_INTERLOCK) {
        id = HeltecV4Capabilities::APPLICATION_INDICATOR_ID;
        operation = Operation::SET;
        input = characterizationValue(
            ValueType::BOOLEAN,
            priorIndicator ? 0 : 1
        );
        name = action == Action::DENY_REMOTE
            ? "deny-remote" : "deny-interlock";
        denialAction = true;
        if (action == Action::DENY_REMOTE) {
            callerClass = CallerClass::FUTURE_REMOTE;
        } else {
            interlock = InterlockState::ACTIVE;
        }
    }

    const uint32_t startedUs = micros();
    CapabilityCharacterization::AnalogSample sample;
    if (analogAction) {
        sample = characterizationAnalog.sampleOnce();
        capabilityState.analogInputAvailable = sample.available;
        capabilityState.analogInputNormalized = sample.normalized;
    }
    const OperationResult result = executeLocalCapabilityNow(
        id,
        operation,
        input,
        characterizationCaller(callerClass),
        interlock
    );
    const uint32_t durationUs = micros() - startedUs;
    if (analogAction) {
        Serial.print("CAPVAL role=HUB action=analog raw=");
        Serial.print(sample.raw);
        Serial.print(" norm=");
        Serial.print(sample.normalized);
        Serial.println(" calibrated_mv=UNAVAILABLE");
    }
    printCharacterizationResult(name, result, durationUs);
    if (denialAction) {
        Serial.print("CAPVAL side_effect_unchanged=");
        Serial.println(
            capabilityState.indicatorRequested == priorIndicator ? 1 : 0
        );
    }
}

void serviceCharacterizationInput() {
    while (Serial.available() > 0) {
        const char next = static_cast<char>(Serial.read());
        if (next == '\r') continue;
        if (next != '\n') {
            if (characterizationCommandLength + 1 <
                sizeof(characterizationCommand)) {
                characterizationCommand[characterizationCommandLength++] = next;
            } else {
                characterizationCommandOverflow = true;
            }
            continue;
        }

        characterizationCommand[characterizationCommandLength] = '\0';
        const CapabilityCharacterization::Action action =
            characterizationCommandOverflow
                ? CapabilityCharacterization::Action::NONE
                : CapabilityCharacterization::classifyCommand(
                    characterizationCommand
                );
        if (action == CapabilityCharacterization::Action::NONE) {
            Serial.println("CAPVAL command=INVALID");
        } else if (
            pendingCharacterizationAction !=
            CapabilityCharacterization::Action::NONE
        ) {
            Serial.println("CAPVAL command=BUSY");
        } else {
            pendingCharacterizationAction = action;
        }
        characterizationCommandLength = 0;
        characterizationCommandOverflow = false;
    }
}

void serviceCharacterization(uint32_t nowMs) {
    if (
        pendingCharacterizationAction ==
            CapabilityCharacterization::Action::NONE ||
        !capabilitySafe(nowMs)
    ) {
        return;
    }
    const CapabilityCharacterization::Action action =
        pendingCharacterizationAction;
    pendingCharacterizationAction = CapabilityCharacterization::Action::NONE;
    performCharacterizationAction(action);
}
#endif

void servicePersistence(uint32_t nowMs) {
    if (
        persistenceRequests.pending() ==
            HubSettingsIntegration::Request::NONE ||
        !persistenceSafe()
    ) {
        return;
    }

    const HubSettingsIntegration::Request request =
        persistenceRequests.pending();
    if (request == HubSettingsIntegration::Request::FACTORY_RESET) {
        const DeviceSettings::ResetResult result =
            settingsManager.factoryReset(settingsStore);
        configurationState = HubSettingsIntegration::fromReset(
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
        return;
    }

    const DeviceSettings::Settings& candidate =
        request == HubSettingsIntegration::Request::SAVE
            ? persistenceRequests.saveDraft()
            : currentSettings;
    const DeviceSettings::SaveStatus result =
        settingsManager.save(settingsStore, candidate);
    configurationState = HubSettingsIntegration::fromSave(
        result,
        settingsManager
    );
    if (HubSettingsIntegration::saveSucceeded(result)) {
        currentSettings = settingsManager.settings();
        applyCurrentSettings(nowMs, false);
    } else {
        markPresentationChanged();
    }
    persistenceRequests.clear();
}

void scheduleNextTransaction() {
    setRuntimePhase(RuntimeState::RuntimePhase::IDLE);
    nextTransmitAt = millis() + TRANSACTION_INTERVAL_MS;
}

void completeAndScheduleNextTransaction() {
    transactionState.completeTransaction();
    scheduleNextTransaction();
}

void scheduleRetryOrNext(const String& reason) {
    radio.standby();
    const TransactionEngine::HubTransactionAction action =
        transactionState.attemptFailed();

    if (action == TransactionEngine::HubTransactionAction::RETRANSMIT) {
        runtimeState.incrementRetransmissions();
        markPresentationChanged();
        showStatus(
            reason,
            String("retry ") + transactionState.retryCount() +
            "/" + transactionState.maximumRetries()
        );

        setRuntimePhase(RuntimeState::RuntimePhase::IDLE);
        nextTransmitAt = millis() + RETRY_DELAY_MS;
        return;
    }

    showStatus(
        "COMMAND FAILED",
        String("SEQ ") + currentCommand.sequence
    );

    setHealth(RuntimeState::Health::DEGRADED);
    setPeerState(DeviceUi::PeerState::DEGRADED);
    completeAndScheduleNextTransaction();
}

bool prepareCommand() {
    currentCommand = {};

    currentCommand.type = Protocol::PacketType::COMMAND;
    currentCommand.source = runtimeState.localId();
    currentCommand.destination = runtimeState.peerId();
    currentCommand.sequence = transactionState.currentSequence();
    currentCommand.opcode = Protocol::OPCODE_TEST;
    currentCommand.payloadLength = 0;

    return Protocol::encode(
        currentCommand,
        transmitBuffer,
        sizeof(transmitBuffer),
        transmitLength
    );
}

bool startCommandTransmission() {
    if (!prepareCommand()) {
        showStatus("TX FAILED", "encode error");
        completeAndScheduleNextTransaction();
        return false;
    }

    operationDone = false;
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
        recordError(RuntimeState::ErrorClass::RADIO_START_TRANSMIT);

        showStatus(
            "TX START FAILED",
            String("CODE ") + state
        );

        scheduleRetryOrNext("TX START FAILED");
        return false;
    }

    logPacket(
        transactionState.requestedTransmission() ==
                TransactionEngine::HubTransactionAction::TRANSMIT_INITIAL
            ? "TX"
            : "TX RETRY",
        currentCommand
    );

    showStatus(
        transactionState.requestedTransmission() ==
                TransactionEngine::HubTransactionAction::TRANSMIT_INITIAL
            ? "TX COMMAND"
            : "TX RETRY",
        String("SEQ ") + transactionState.currentSequence()
    );

    setRuntimePhase(RuntimeState::RuntimePhase::TRANSMITTING);
    return true;
}

bool startAckReceive(bool resetDeadline) {
    operationDone = false;

    const int state = radio.startReceive();

    if (state != RADIOLIB_ERR_NONE) {
        runtimeState.incrementRadioErrors();
        recordError(RuntimeState::ErrorClass::RADIO_START_RECEIVE);
        showStatus(
            "RX START FAILED",
            String("CODE ") + state
        );

        scheduleRetryOrNext("RX START FAILED");
        return false;
    }

    setRuntimePhase(RuntimeState::RuntimePhase::WAITING_FOR_ACK);

    if (resetDeadline) {
        transactionState.beginAcknowledgmentWait(
            static_cast<uint32_t>(millis())
        );
    }

    return true;
}

void handleAckTimeout() {
    runtimeState.incrementAcknowledgmentTimeouts();
    recordError(RuntimeState::ErrorClass::ACK_TIMEOUT);
    showStatus(
        "ACK TIMEOUT",
        String("SEQ ") + currentCommand.sequence
    );

    scheduleRetryOrNext("ACK TIMEOUT");
}

void continueWaitingForAck() {
    radio.standby();

    const TransactionEngine::HubTransactionAction action =
        transactionState.acknowledgmentWaitAction(
            static_cast<uint32_t>(millis())
        );

    if (action != TransactionEngine::HubTransactionAction::NO_ACTION) {
        handleAckTimeout();
        return;
    }

    startAckReceive(false);
}

void processAcknowledgment() {
    const size_t packetLength = radio.getPacketLength();

    if (
        packetLength < Protocol::HEADER_SIZE ||
        packetLength > Protocol::MAX_PACKET_SIZE
    ) {
        runtimeState.incrementMalformedPackets();
        recordError(RuntimeState::ErrorClass::PACKET_LENGTH);
        showStatus(
            "ACK MALFORMED",
            String("LEN ") + packetLength
        );

        continueWaitingForAck();
        return;
    }

    uint8_t receiveBuffer[Protocol::MAX_PACKET_SIZE] = {};

    const int state = radio.readData(
        receiveBuffer,
        packetLength
    );

    if (state == RADIOLIB_ERR_CRC_MISMATCH) {
        runtimeState.incrementRadioErrors();
        recordError(RuntimeState::ErrorClass::RADIO_READ);
        showStatus("BAD ACK", "CRC mismatch");
        continueWaitingForAck();
        return;
    }

    if (state != RADIOLIB_ERR_NONE) {
        runtimeState.incrementRadioErrors();
        recordError(RuntimeState::ErrorClass::RADIO_READ);
        showStatus(
            "ACK READ FAILED",
            String("CODE ") + state
        );

        continueWaitingForAck();
        return;
    }

    const float rssi = radio.getRSSI();
    const float snr = radio.getSNR();
    runtimeState.updateRadioMetrics(rssi, snr);
    runtimeState.recordActivity(static_cast<uint32_t>(millis()));
    markPresentationChanged();

    Protocol::Packet acknowledgment = {};

    if (
        !Protocol::decode(
            receiveBuffer,
            packetLength,
            acknowledgment
        )
    ) {
        runtimeState.incrementMalformedPackets();
        recordError(RuntimeState::ErrorClass::PACKET_DECODE);
        showStatus("ACK MALFORMED", "decode failed");
        continueWaitingForAck();
        return;
    }

    runtimeState.incrementDecodedPacketsReceived();
    runtimeState.recordInboundPacket(
        static_cast<uint8_t>(acknowledgment.type),
        acknowledgment.source,
        acknowledgment.destination,
        acknowledgment.sequence,
        acknowledgment.opcode,
        acknowledgment.payloadLength,
        acknowledgment.type == Protocol::PacketType::ACK &&
            acknowledgment.payloadLength == 1,
        acknowledgment.payloadLength == 1
            ? acknowledgment.payload[0]
            : 0,
        static_cast<uint32_t>(millis())
    );
    markPresentationChanged();

    logPacket("RX", acknowledgment);

    Serial.print("RSSI: ");
    Serial.print(runtimeState.latestRssi());
    Serial.print(" dBm | SNR: ");
    Serial.print(runtimeState.latestSnr());
    Serial.println(" dB");

    const TransactionEngine::HubAckEvaluation evaluation =
        TransactionEngine::evaluateHubAcknowledgment(
            acknowledgment,
            currentCommand,
            runtimeState.localId(),
            runtimeState.peerId()
        );

    if (
        evaluation.outcome !=
        TransactionEngine::HubAckOutcome::MATCHING_ACK
    ) {
        if (
            evaluation.outcome ==
            TransactionEngine::HubAckOutcome::IGNORE_MALFORMED_PAYLOAD
        ) {
            runtimeState.incrementMalformedPackets();
            recordError(RuntimeState::ErrorClass::ACK_STATUS);
        } else {
            runtimeState.incrementIgnoredPackets();
            recordError(RuntimeState::ErrorClass::PACKET_IGNORED);
        }
        showStatus(
            "ACK IGNORED",
            String("SEQ ") + acknowledgment.sequence
        );

        continueWaitingForAck();
        return;
    }

    const uint8_t rawStatus = evaluation.rawStatus;

    if (!Protocol::isValidAckStatus(rawStatus)) {
        runtimeState.incrementMalformedPackets();
        recordError(RuntimeState::ErrorClass::ACK_STATUS);
        showStatus(
            "ACK MALFORMED",
            String("STATUS ") + rawStatus
        );

        radio.standby();
        completeAndScheduleNextTransaction();
        return;
    }

    const Protocol::AckStatus status =
        static_cast<Protocol::AckStatus>(rawStatus);
    const TransactionEngine::HubTransactionAction completion =
        transactionState.acknowledgmentCompletionAction(status);

    if (
        completion ==
        TransactionEngine::HubTransactionAction::TRANSACTION_SUCCEEDED
    ) {
        runtimeState.incrementSuccessfulTransactions();
        setHealth(
            capabilityRegistryValid
                ? RuntimeState::Health::READY
                : RuntimeState::Health::DEGRADED
        );
        recordError(RuntimeState::ErrorClass::NONE);
        setPeerState(DeviceUi::PeerState::REACHABLE);
        showStatus(
            "ACK MATCHED",
            String("SEQ ") + currentCommand.sequence +
            " RSSI " + String(runtimeState.latestRssi(), 0) +
            " SNR " + String(runtimeState.latestSnr(), 1)
        );
    } else {
        setHealth(RuntimeState::Health::DEGRADED);
        recordError(RuntimeState::ErrorClass::REMOTE_ACK);
        setPeerState(DeviceUi::PeerState::DEGRADED);
        showStatus(
            "ACK ERROR",
            String("STATUS ") +
            static_cast<uint8_t>(status)
        );
    }

    radio.standby();
    completeAndScheduleNextTransaction();
}

void setup() {
    Serial.begin(115200);

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

    radioSPI.begin(9, 11, 10, 8);

    const int state = radio.begin(915.0);

    Serial.print("Radio init state: ");
    Serial.println(state);

    if (state != RADIOLIB_ERR_NONE) {
        runtimeState.setReady(false);
        setHealth(RuntimeState::Health::ERROR);
        recordError(RuntimeState::ErrorClass::RADIO_INITIALIZATION);
        serviceUi(static_cast<uint32_t>(millis()));
        showStatus("RADIO ERROR", String("CODE ") + state);
        return;
    }

    radio.setDio1Action(setRadioFlag);

#if defined(ARGUS_CAPABILITY_CHARACTERIZATION)
    const bool adcReady = characterizationAnalog.begin();
    Serial.print("CHARACTERIZATION BUILD role=HUB adc_ready=");
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
        runtimeState.role() == RuntimeState::DeviceRole::HUB
            ? "HUB READY"
            : "NODE READY",
        String("Firmware: ") + RedlineVersion::FIRMWARE
    );
    logVersionMetadata();

    nextTransmitAt = millis() + INITIAL_DELAY_MS;
}

void serviceHubTransport(uint32_t nowMs) {
    if (runtimeState.phase() == RuntimeState::RuntimePhase::IDLE) {
        if ((long)(millis() - nextTransmitAt) < 0) {
            return;
        }

        startCommandTransmission();
        return;
    }

    if (!operationDone) {
        if (
            runtimeState.phase() ==
                RuntimeState::RuntimePhase::WAITING_FOR_ACK &&
            transactionState.isAwaitingAcknowledgment()
        ) {
            const TransactionEngine::HubTransactionAction action =
                transactionState.acknowledgmentWaitAction(
                    static_cast<uint32_t>(millis())
                );

            if (
                action !=
                TransactionEngine::HubTransactionAction::NO_ACTION
            ) {
                handleAckTimeout();
            }
        }

        return;
    }

    operationDone = false;

    if (
        runtimeState.phase() ==
        RuntimeState::RuntimePhase::TRANSMITTING
    ) {
        radioLedActive = false;
        updateLedOutput();
        runtimeState.incrementTransmissionsCompleted();
        runtimeState.recordActivity(nowMs);
        markPresentationChanged();
        showStatus(
            "TX COMPLETE",
            String("SEQ ") + transactionState.currentSequence()
        );

        startAckReceive(true);
        return;
    }

    if (
        runtimeState.phase() ==
        RuntimeState::RuntimePhase::WAITING_FOR_ACK
    ) {
        processAcknowledgment();
    }
}

void loop() {
    const uint32_t nowMs = static_cast<uint32_t>(millis());
    serviceButton(nowMs);
    serviceButtonFeedback(nowMs);

#if defined(ARGUS_CAPABILITY_CHARACTERIZATION)
    serviceCharacterizationInput();
#endif

    servicePersistence(nowMs);

#if defined(ARGUS_CAPABILITY_CHARACTERIZATION)
    serviceCharacterization(nowMs);
#endif

    if (runtimeState.isReady()) {
        serviceHubTransport(nowMs);
    }

    serviceUi(nowMs);

    if (!runtimeState.isReady()) {
        yield();
    }
}
