#pragma once

#include <stdio.h>
#include <stdint.h>

#include "device_input.h"
#include "protocol.h"
#include "runtime_state.h"

namespace DeviceUi {

enum class Screen : uint8_t {
    HOME,
    RADIO,
    DEVICE,
    LAST_PACKET,
    DIAGNOSTICS,
    ABOUT
};

inline bool isLiveScreen(Screen screen) {
    return (
        screen == Screen::RADIO ||
        screen == Screen::LAST_PACKET ||
        screen == Screen::DIAGNOSTICS
    );
}

enum class UiAction : uint8_t {
    NONE,
    RENDER,
    DISPLAY_ON,
    DISPLAY_ON_AND_RENDER,
    DISPLAY_OFF
};

enum class PeerState : uint8_t {
    UNKNOWN,
    REACHABLE,
    DEGRADED,
    SEEN
};

constexpr uint8_t MAX_PRESENTATION_ROWS = 5;
constexpr size_t PRESENTATION_TITLE_CAPACITY = 17;
constexpr size_t PRESENTATION_LABEL_CAPACITY = 9;
constexpr size_t PRESENTATION_VALUE_CAPACITY = 17;

struct PresentationRow {
    char label[PRESENTATION_LABEL_CAPACITY] = {};
    char value[PRESENTATION_VALUE_CAPACITY] = {};
};

struct PresentationSnapshot {
    Screen screen = Screen::HOME;
    char title[PRESENTATION_TITLE_CAPACITY] = {};
    uint8_t rowCount = 0;
    PresentationRow rows[MAX_PRESENTATION_ROWS] = {};
};

struct PresentationInput {
    RuntimeState::DeviceRole role = RuntimeState::DeviceRole::HUB;
    uint8_t localId = 0;
    uint8_t peerId = 0;
    bool ready = false;
    RuntimeState::Health health = RuntimeState::Health::STARTING;
    RuntimeState::RuntimePhase phase = RuntimeState::RuntimePhase::IDLE;
    const char* firmwareVersion = "";
    uint8_t wireProtocolVersion = 0;
    const char* hardwareProfile = "";
    bool radioMetricsAvailable = false;
    float rssi = 0.0F;
    float snr = 0.0F;
    PeerState peerState = PeerState::UNKNOWN;
    RuntimeState::LastInboundPacket lastInboundPacket = {};
    RuntimeState::DiagnosticCounters counters = {};
    RuntimeState::ErrorClass lastError = RuntimeState::ErrorClass::NONE;
};

namespace PresentationDetail {

template <size_t Capacity>
inline void copyText(char (&destination)[Capacity], const char* source) {
    snprintf(destination, Capacity, "%s", source == nullptr ? "" : source);
}

inline void addRow(
    PresentationSnapshot& snapshot,
    const char* label,
    const char* value
) {
    if (snapshot.rowCount >= MAX_PRESENTATION_ROWS) {
        return;
    }

    PresentationRow& row = snapshot.rows[snapshot.rowCount++];
    copyText(row.label, label);
    copyText(row.value, value);
}

inline const char* roleLabel(RuntimeState::DeviceRole role) {
    switch (role) {
        case RuntimeState::DeviceRole::HUB:
            return "TX";
        case RuntimeState::DeviceRole::NODE:
            return "RX";
    }
    return "UNKNOWN";
}

inline const char* healthLabel(RuntimeState::Health health) {
    switch (health) {
        case RuntimeState::Health::STARTING:
            return "START";
        case RuntimeState::Health::READY:
            return "READY";
        case RuntimeState::Health::DEGRADED:
            return "DEGRADED";
        case RuntimeState::Health::ERROR:
            return "ERROR";
    }
    return "UNKNOWN";
}

inline const char* phaseLabel(RuntimeState::RuntimePhase phase) {
    switch (phase) {
        case RuntimeState::RuntimePhase::IDLE:
            return "IDLE";
        case RuntimeState::RuntimePhase::TRANSMITTING:
            return "TX";
        case RuntimeState::RuntimePhase::WAITING_FOR_ACK:
            return "WAIT ACK";
        case RuntimeState::RuntimePhase::LISTENING:
            return "LISTEN";
        case RuntimeState::RuntimePhase::TRANSMITTING_ACK:
            return "TX ACK";
    }
    return "UNKNOWN";
}

inline const char* peerLabel(
    RuntimeState::DeviceRole role,
    PeerState peerState
) {
    if (role == RuntimeState::DeviceRole::NODE) {
        return peerState == PeerState::SEEN ? "SEEN" : "UNKNOWN";
    }

    switch (peerState) {
        case PeerState::REACHABLE:
            return "REACHABLE";
        case PeerState::DEGRADED:
            return "DEGRADED";
        case PeerState::UNKNOWN:
        case PeerState::SEEN:
            return "UNKNOWN";
    }
    return "UNKNOWN";
}

inline const char* packetTypeLabel(uint8_t rawType) {
    switch (rawType) {
        case static_cast<uint8_t>(Protocol::PacketType::COMMAND):
            return "COMMAND";
        case static_cast<uint8_t>(Protocol::PacketType::ACK):
            return "ACK";
        case static_cast<uint8_t>(Protocol::PacketType::ERROR):
            return "ERROR";
    }
    return nullptr;
}

inline const char* ackStatusLabel(uint8_t rawStatus) {
    switch (rawStatus) {
        case static_cast<uint8_t>(Protocol::AckStatus::SUCCESS):
            return "SUCCESS";
        case static_cast<uint8_t>(Protocol::AckStatus::UNSUPPORTED_OPCODE):
            return "UNSUPPORTED";
        case static_cast<uint8_t>(Protocol::AckStatus::MALFORMED_PACKET):
            return "MALFORMED";
    }
    return nullptr;
}

inline const char* errorLabel(RuntimeState::ErrorClass error) {
    switch (error) {
        case RuntimeState::ErrorClass::NONE:
            return "NONE";
        case RuntimeState::ErrorClass::RADIO_INITIALIZATION:
            return "RADIO INIT";
        case RuntimeState::ErrorClass::RADIO_START_RECEIVE:
            return "START RECEIVE";
        case RuntimeState::ErrorClass::RADIO_START_TRANSMIT:
            return "START TRANSMIT";
        case RuntimeState::ErrorClass::RADIO_READ:
            return "RADIO READ";
        case RuntimeState::ErrorClass::PACKET_LENGTH:
            return "PACKET LENGTH";
        case RuntimeState::ErrorClass::PACKET_DECODE:
            return "PACKET DECODE";
        case RuntimeState::ErrorClass::PACKET_IGNORED:
            return "PACKET IGNORED";
        case RuntimeState::ErrorClass::ACK_TIMEOUT:
            return "ACK TIMEOUT";
        case RuntimeState::ErrorClass::REMOTE_ACK:
            return "REMOTE ACK";
        case RuntimeState::ErrorClass::ACK_STATUS:
            return "ACK STATUS";
    }
    return "UNKNOWN";
}

inline uint32_t saturatingAdd(uint32_t first, uint32_t second) {
    return second > UINT32_MAX - first ? UINT32_MAX : first + second;
}

inline void formatUnsigned(char* output, size_t capacity, uint32_t value) {
    snprintf(output, capacity, "%lu", static_cast<unsigned long>(value));
}

inline void formatPair(
    char* output,
    size_t capacity,
    uint32_t first,
    uint32_t second
) {
    snprintf(
        output,
        capacity,
        "%lu/%lu",
        static_cast<unsigned long>(first),
        static_cast<unsigned long>(second)
    );
}

}  // namespace PresentationDetail

inline PresentationSnapshot buildPresentation(
    Screen screen,
    const PresentationInput& input
) {
    PresentationSnapshot snapshot;
    snapshot.screen = screen;
    char value[PRESENTATION_VALUE_CAPACITY] = {};

    using namespace PresentationDetail;

    switch (screen) {
        case Screen::HOME:
            copyText(snapshot.title, "ARGUS REDLINE");
            addRow(snapshot, roleLabel(input.role), healthLabel(input.health));
            break;

        case Screen::RADIO:
            copyText(snapshot.title, "RADIO");
            addRow(snapshot, "ROLE", roleLabel(input.role));
            addRow(snapshot, "PHASE", phaseLabel(input.phase));
            if (input.radioMetricsAvailable) {
                snprintf(value, sizeof(value), "%.1f", input.rssi);
                addRow(snapshot, "RSSI", value);
                snprintf(value, sizeof(value), "%.1f", input.snr);
                addRow(snapshot, "SNR", value);
            } else {
                addRow(snapshot, "RSSI", "--");
                addRow(snapshot, "SNR", "--");
            }
            addRow(snapshot, "PEER", peerLabel(input.role, input.peerState));
            break;

        case Screen::DEVICE:
            copyText(snapshot.title, "DEVICE");
            addRow(snapshot, "ROLE", roleLabel(input.role));
            snprintf(value, sizeof(value), "0x%02X", input.localId);
            addRow(snapshot, "LOCAL", value);
            snprintf(value, sizeof(value), "0x%02X", input.peerId);
            addRow(snapshot, "PEER", value);
            addRow(
                snapshot,
                "STATUS",
                input.ready ? healthLabel(input.health) : "NOT READY"
            );
            addRow(snapshot, "HW", input.hardwareProfile);
            break;

        case Screen::LAST_PACKET: {
            copyText(snapshot.title, "LAST PACKET");
            const RuntimeState::LastInboundPacket& packet =
                input.lastInboundPacket;
            if (!packet.available) {
                addRow(snapshot, "RX", "NO PACKET");
                break;
            }

            const char* typeLabel = packetTypeLabel(packet.rawType);
            if (typeLabel == nullptr) {
                snprintf(value, sizeof(value), "TYPE %u", packet.rawType);
                addRow(snapshot, "RX", value);
            } else {
                addRow(snapshot, "RX", typeLabel);
            }
            snprintf(
                value,
                sizeof(value),
                "%02X>%02X",
                packet.source,
                packet.destination
            );
            addRow(snapshot, "SRC>DST", value);
            snprintf(
                value,
                sizeof(value),
                "%u/%u",
                packet.sequence,
                packet.opcode
            );
            addRow(snapshot, "SEQ/OP", value);
            snprintf(value, sizeof(value), "%u", packet.payloadLength);
            addRow(snapshot, "LEN", value);
            if (packet.ackStatusAvailable) {
                const char* statusLabel = ackStatusLabel(packet.rawAckStatus);
                if (statusLabel == nullptr) {
                    snprintf(
                        value,
                        sizeof(value),
                        "STATUS %u",
                        packet.rawAckStatus
                    );
                    addRow(snapshot, "ACK", value);
                } else {
                    addRow(snapshot, "ACK", statusLabel);
                }
            } else {
                addRow(snapshot, "ACK", "--");
            }
            break;
        }

        case Screen::DIAGNOSTICS: {
            copyText(snapshot.title, "DIAGNOSTICS");
            const RuntimeState::DiagnosticCounters& counters = input.counters;
            if (input.role == RuntimeState::DeviceRole::HUB) {
                formatUnsigned(
                    value,
                    sizeof(value),
                    counters.transmissionsCompleted
                );
                addRow(snapshot, "TX", value);
                formatUnsigned(
                    value,
                    sizeof(value),
                    counters.successfulTransactions
                );
                addRow(snapshot, "SUCCESS", value);
                formatPair(
                    value,
                    sizeof(value),
                    counters.retransmissions,
                    counters.acknowledgmentTimeouts
                );
                addRow(snapshot, "RETRY/TO", value);
            } else {
                formatUnsigned(
                    value,
                    sizeof(value),
                    counters.decodedPacketsReceived
                );
                addRow(snapshot, "RX", value);
                formatPair(
                    value,
                    sizeof(value),
                    counters.acceptedCommands,
                    counters.transmissionsCompleted
                );
                addRow(snapshot, "CMD/ACK", value);
                formatUnsigned(value, sizeof(value), counters.duplicates);
                addRow(snapshot, "DUP", value);
            }
            formatUnsigned(
                value,
                sizeof(value),
                saturatingAdd(
                    counters.malformedPackets,
                    counters.ignoredPackets
                )
            );
            addRow(snapshot, "BAD/IGN", value);
            addRow(snapshot, "ERROR", errorLabel(input.lastError));
            break;
        }

        case Screen::ABOUT:
            copyText(snapshot.title, "ARGUS REDLINE");
            addRow(snapshot, "FW", input.firmwareVersion);
            snprintf(value, sizeof(value), "%u", input.wireProtocolVersion);
            addRow(snapshot, "WIRE", value);
            addRow(snapshot, "HW", input.hardwareProfile);
            addRow(snapshot, "ROLE", roleLabel(input.role));
            break;
    }

    return snapshot;
}

class Controller {
public:
    static constexpr uint32_t DEFAULT_INACTIVITY_TIMEOUT_MS = 30000;
    static constexpr uint32_t DEFAULT_MINIMUM_RENDER_INTERVAL_MS = 100;
    static constexpr uint32_t DEFAULT_LIVE_REFRESH_INTERVAL_MS = 1000;

    Controller(
        uint32_t initialTimeMs,
        uint32_t inactivityTimeoutMs = DEFAULT_INACTIVITY_TIMEOUT_MS,
        uint32_t minimumRenderIntervalMs =
            DEFAULT_MINIMUM_RENDER_INTERVAL_MS,
        uint32_t liveRefreshIntervalMs = DEFAULT_LIVE_REFRESH_INTERVAL_MS
    ) :
        inactivityTimeoutMs_(inactivityTimeoutMs),
        minimumRenderIntervalMs_(minimumRenderIntervalMs),
        liveRefreshIntervalMs_(liveRefreshIntervalMs),
        lastInputAtMs_(initialTimeMs),
        lastLiveRefreshAtMs_(initialTimeMs) {}

    Screen screen() const {
        return screen_;
    }

    bool displayAwake() const {
        return displayAwake_;
    }

    bool dirty() const {
        return dirty_;
    }

    void markDirty() {
        invalidate();
    }

    void handle(DeviceInput::ButtonEvent event, uint32_t nowMs) {
        if (!displayAwake_) {
            if (event == DeviceInput::ButtonEvent::PRESS) {
                displayAwake_ = true;
                displayOnPending_ = true;
                suppressWakeGesture_ = true;
                wakeGestureWasLong_ = false;
                lastInputAtMs_ = nowMs;
                invalidate();
            }
            return;
        }

        if (suppressWakeGesture_) {
            handleSuppressedWakeGesture(event);
            return;
        }

        switch (event) {
            case DeviceInput::ButtonEvent::PRESS:
                lastInputAtMs_ = nowMs;
                break;

            case DeviceInput::ButtonEvent::SHORT_PRESS:
                screen_ = nextScreen(screen_);
                lastInputAtMs_ = nowMs;
                invalidate();
                break;

            case DeviceInput::ButtonEvent::LONG_PRESS:
                lastInputAtMs_ = nowMs;
                if (screen_ != Screen::HOME) {
                    screen_ = Screen::HOME;
                    invalidate();
                }
                break;

            case DeviceInput::ButtonEvent::NONE:
            case DeviceInput::ButtonEvent::RELEASE:
                break;
        }
    }

    UiAction update(uint32_t nowMs) {
        if (
            displayAwake_ &&
            elapsed(nowMs, lastInputAtMs_) >= inactivityTimeoutMs_
        ) {
            displayAwake_ = false;
            displayOnPending_ = false;
            renderPending_ = false;
            dirtyWhileRenderPending_ = false;
            return UiAction::DISPLAY_OFF;
        }

        if (!displayAwake_) {
            return UiAction::NONE;
        }

        if (
            !dirty_ &&
            isLiveScreen(screen_) &&
            elapsed(nowMs, lastLiveRefreshAtMs_) >= liveRefreshIntervalMs_
        ) {
            invalidate();
        }

        const bool renderEligible =
            !hasRendered_ ||
            elapsed(nowMs, lastRenderAtMs_) >= minimumRenderIntervalMs_;

        if (displayOnPending_) {
            displayOnPending_ = false;
            if (dirty_ && !renderPending_ && renderEligible) {
                beginRenderRequest();
                return UiAction::DISPLAY_ON_AND_RENDER;
            }
            return UiAction::DISPLAY_ON;
        }

        if (dirty_ && !renderPending_ && renderEligible) {
            beginRenderRequest();
            return UiAction::RENDER;
        }

        return UiAction::NONE;
    }

    // Acknowledges completion of the most recently requested render.
    void recordRendered(uint32_t nowMs) {
        if (!renderPending_) {
            return;
        }

        renderPending_ = false;
        dirty_ = dirtyWhileRenderPending_;
        dirtyWhileRenderPending_ = false;
        hasRendered_ = true;
        lastRenderAtMs_ = nowMs;
        if (isLiveScreen(screen_)) {
            lastLiveRefreshAtMs_ = nowMs;
        }
    }

private:
    static uint32_t elapsed(uint32_t nowMs, uint32_t sinceMs) {
        return nowMs - sinceMs;
    }

    static Screen nextScreen(Screen screen) {
        switch (screen) {
            case Screen::HOME:
                return Screen::RADIO;
            case Screen::RADIO:
                return Screen::DEVICE;
            case Screen::DEVICE:
                return Screen::LAST_PACKET;
            case Screen::LAST_PACKET:
                return Screen::DIAGNOSTICS;
            case Screen::DIAGNOSTICS:
                return Screen::ABOUT;
            case Screen::ABOUT:
                return Screen::HOME;
        }

        return Screen::HOME;
    }

    void invalidate() {
        dirty_ = true;
        if (renderPending_) {
            dirtyWhileRenderPending_ = true;
        }
    }

    void beginRenderRequest() {
        renderPending_ = true;
        dirtyWhileRenderPending_ = false;
    }

    void handleSuppressedWakeGesture(DeviceInput::ButtonEvent event) {
        if (event == DeviceInput::ButtonEvent::LONG_PRESS) {
            wakeGestureWasLong_ = true;
        } else if (event == DeviceInput::ButtonEvent::SHORT_PRESS) {
            suppressWakeGesture_ = false;
        } else if (
            event == DeviceInput::ButtonEvent::RELEASE &&
            wakeGestureWasLong_
        ) {
            suppressWakeGesture_ = false;
        }
    }

    const uint32_t inactivityTimeoutMs_;
    const uint32_t minimumRenderIntervalMs_;
    const uint32_t liveRefreshIntervalMs_;
    Screen screen_ = Screen::HOME;
    bool displayAwake_ = true;
    bool dirty_ = true;
    bool renderPending_ = false;
    bool dirtyWhileRenderPending_ = false;
    bool displayOnPending_ = false;
    bool suppressWakeGesture_ = false;
    bool wakeGestureWasLong_ = false;
    bool hasRendered_ = false;
    uint32_t lastInputAtMs_;
    uint32_t lastRenderAtMs_ = 0;
    uint32_t lastLiveRefreshAtMs_;
};

}  // namespace DeviceUi
