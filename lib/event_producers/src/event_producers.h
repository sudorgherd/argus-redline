#pragma once

#include <stdint.h>

#include "device_input.h"
#include "event_protocol.h"
#include "node_event_store.h"

namespace EventProducers {

// Stage-11 local policy: all producer Events use ordinary (non-IMPORTANT)
// metadata and one hour of powered-runtime custody. Persistence is unchanged:
// NodeEventStore durably owns every admitted Event regardless of this flag.
constexpr uint8_t PRODUCER_FLAGS = 0;
constexpr uint32_t PRODUCER_LIFETIME_SECONDS = 3600U;

enum class CreationResult : uint8_t {
    ENQUEUED,
    QUEUE_FULL,
    INVALID_EVENT,
    STORAGE_FAILURE
};

class CreationSink {
public:
    virtual ~CreationSink() = default;
    virtual CreationResult create(const NodeEventStore::EventInput& input) = 0;
};

inline CreationResult normalize(NodeEventStore::EnqueueStatus status) {
    switch (status) {
        case NodeEventStore::EnqueueStatus::ENQUEUED:
            return CreationResult::ENQUEUED;
        case NodeEventStore::EnqueueStatus::QUEUE_FULL:
            return CreationResult::QUEUE_FULL;
        case NodeEventStore::EnqueueStatus::INVALID_EVENT:
            return CreationResult::INVALID_EVENT;
        case NodeEventStore::EnqueueStatus::STORAGE_FAILURE:
        case NodeEventStore::EnqueueStatus::IDENTITY_FAILURE:
        case NodeEventStore::EnqueueStatus::DEGRADED:
        default:
            return CreationResult::STORAGE_FAILURE;
    }
}

class StoreCreationSink final : public CreationSink {
public:
    explicit StoreCreationSink(NodeEventStore::Store& store) : store_(store) {}

    CreationResult create(const NodeEventStore::EventInput& input) override {
        return normalize(store_.enqueue(input).status);
    }

private:
    NodeEventStore::Store& store_;
};

struct Emission {
    bool attempted;
    CreationResult result;
};

inline Emission noEmission() {
    return {false, CreationResult::INVALID_EVENT};
}

inline NodeEventStore::EventInput makeInput(uint8_t family) {
    NodeEventStore::EventInput input = {};
    input.family = family;
    input.flags = PRODUCER_FLAGS;
    input.lifetimeBudgetSeconds = PRODUCER_LIFETIME_SECONDS;
    return input;
}

inline Emission emitButton(CreationSink& sink, DeviceInput::ButtonEvent event) {
    if (event == DeviceInput::ButtonEvent::NONE) return noEmission();
    const uint8_t value = static_cast<uint8_t>(event);
    if (!EventProtocol::isValidButtonEvent(value)) return noEmission();
    NodeEventStore::EventInput input = makeInput(
        static_cast<uint8_t>(EventProtocol::Family::BUTTON));
    input.bodyLength = 1;
    input.body[0] = value;
    return {true, sink.create(input)};
}

inline Emission emitManualCheckIn(CreationSink& sink) {
    NodeEventStore::EventInput input = makeInput(
        static_cast<uint8_t>(EventProtocol::Family::MANUAL_CHECK_IN));
    input.bodyLength = 1;
    input.body[0] = static_cast<uint8_t>(EventProtocol::ManualReason::USER_REQUEST);
    return {true, sink.create(input)};
}

struct ButtonContext {
    bool displayAwake;
    bool editorActive;
    bool homeScreen;
};

struct ButtonProductionResult {
    Emission button;
    Emission manualCheckIn;
};

// One gesture is admitted from its PRESS-time UI context. SHORT_PRESS is the
// sole BUTTON production stimulus, avoiding RELEASE+SHORT_PRESS duplication.
// A HOME long gesture becomes a manual check-in only on RELEASE, and is
// cancelled by VERY_LONG_PRESS so settings-editor entry cannot check in.
class ButtonProducer {
public:
    ButtonProductionResult observe(
        const DeviceInput::ButtonEvents& events,
        const ButtonContext& context,
        CreationSink& sink
    ) {
        ButtonProductionResult result = {noEmission(), noEmission()};
        observeOne(events.first, context, sink, result);
        observeOne(events.second, context, sink, result);
        return result;
    }

private:
    void observeOne(DeviceInput::ButtonEvent event, const ButtonContext& context,
                    CreationSink& sink, ButtonProductionResult& result) {
        switch (event) {
            case DeviceInput::ButtonEvent::PRESS:
                gestureEligible_ = context.displayAwake && !context.editorActive;
                manualEligible_ = gestureEligible_ && context.homeScreen;
                longObserved_ = false;
                veryLongObserved_ = false;
                break;
            case DeviceInput::ButtonEvent::SHORT_PRESS:
                if (gestureEligible_) result.button = emitButton(sink, event);
                gestureEligible_ = false;
                manualEligible_ = false;
                break;
            case DeviceInput::ButtonEvent::LONG_PRESS:
                if (manualEligible_) longObserved_ = true;
                break;
            case DeviceInput::ButtonEvent::VERY_LONG_PRESS:
                veryLongObserved_ = true;
                manualEligible_ = false;
                break;
            case DeviceInput::ButtonEvent::RELEASE:
                if (manualEligible_ && longObserved_ && !veryLongObserved_)
                    result.manualCheckIn = emitManualCheckIn(sink);
                if (longObserved_) gestureEligible_ = false;
                manualEligible_ = false;
                longObserved_ = false;
                veryLongObserved_ = false;
                break;
            case DeviceInput::ButtonEvent::NONE:
                break;
        }
    }

    bool gestureEligible_ = false;
    bool manualEligible_ = false;
    bool longObserved_ = false;
    bool veryLongObserved_ = false;
};

struct ThresholdPolicy {
    uint16_t capabilityId;
    EventProtocol::SensorValueType valueType;
    uint32_t thresholdBits;
    uint32_t hysteresisBits;
    EventProtocol::ThresholdRelation relation;
};

class SensorThresholdProducer {
public:
    explicit SensorThresholdProducer(const ThresholdPolicy& policy) : policy_(policy) {}

    Emission observe(bool available, uint32_t observedBits, CreationSink& sink) {
        if (!available || !validPolicy() || !validObserved(observedBits))
            return noEmission();

        const bool active = isActive(observedBits);
        if (!initialized_) {
            initialized_ = true;
            armed_ = !active;
            return noEmission();
        }
        if (!armed_) {
            if (isRearmed(observedBits)) armed_ = true;
            return noEmission();
        }
        if (!active) return noEmission();

        armed_ = false;
        NodeEventStore::EventInput input = makeInput(
            static_cast<uint8_t>(EventProtocol::Family::SENSOR_THRESHOLD));
        input.bodyLength = 8;
        input.body[0] = static_cast<uint8_t>(policy_.capabilityId & 0xFFU);
        input.body[1] = static_cast<uint8_t>(policy_.capabilityId >> 8);
        input.body[2] = static_cast<uint8_t>(policy_.valueType);
        EventProtocol::writeUint32Le(input.body + 3, observedBits);
        input.body[7] = static_cast<uint8_t>(policy_.relation);
        return {true, sink.create(input)};
    }

private:
    bool validPolicy() const {
        if (policy_.capabilityId == 0 ||
            !EventProtocol::isValidSensorValueType(static_cast<uint8_t>(policy_.valueType)) ||
            !EventProtocol::isValidThresholdRelation(static_cast<uint8_t>(policy_.relation)))
            return false;
        return validObserved(policy_.thresholdBits);
    }
    bool validObserved(uint32_t bits) const {
        return (policy_.valueType != EventProtocol::SensorValueType::NORMALIZED_U16 &&
                policy_.valueType != EventProtocol::SensorValueType::ENUM_U16) ||
            (bits & 0xFFFF0000U) == 0;
    }
    bool isActive(uint32_t value) const {
        const int64_t observed = numericValue(value);
        const int64_t threshold = numericValue(policy_.thresholdBits);
        return policy_.relation == EventProtocol::ThresholdRelation::CROSSED_ABOVE
            ? observed > threshold : observed < threshold;
    }
    bool isRearmed(uint32_t value) const {
        const int64_t observed = numericValue(value);
        const int64_t threshold = numericValue(policy_.thresholdBits);
        const int64_t hysteresis = static_cast<int64_t>(policy_.hysteresisBits);
        if (policy_.relation == EventProtocol::ThresholdRelation::CROSSED_ABOVE)
            return observed <= threshold - hysteresis;
        return observed >= threshold + hysteresis;
    }
    int64_t numericValue(uint32_t bits) const {
        if (policy_.valueType == EventProtocol::SensorValueType::SIGNED_32 ||
            policy_.valueType == EventProtocol::SensorValueType::FIXED_Q16_16) {
            return static_cast<int32_t>(bits);
        }
        return static_cast<int64_t>(bits);
    }

    ThresholdPolicy policy_;
    bool initialized_ = false;
    bool armed_ = false;
};

}  // namespace EventProducers
