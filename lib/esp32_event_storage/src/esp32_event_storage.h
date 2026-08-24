#pragma once

#include "hub_event_ledger.h"
#include "node_event_store.h"

namespace Esp32EventStorage {

class PreferencesStore final : public NodeEventStore::Storage,
                               public HubEventLedger::Storage {
public:
    EventIdentity::StorageResult read(
        EventStorage::CopySlot slot, uint8_t* output,
        size_t capacity, size_t& length) override;
    EventIdentity::StorageResult write(
        EventStorage::CopySlot slot, const uint8_t* input, size_t length) override;
    EventIdentity::StorageResult commit() override;
    EventIdentity::StorageResult readEventCopy(
        uint8_t logicalSlot, EventStorage::CopySlot copy,
        uint8_t* output, size_t capacity, size_t& length) override;
    EventIdentity::StorageResult writeEventCopy(
        uint8_t logicalSlot, EventStorage::CopySlot copy,
        const uint8_t* input, size_t length) override;
    EventIdentity::StorageResult readHubMetadata(
        EventStorage::CopySlot copy, uint8_t* output,
        size_t capacity, size_t& length) override;
    EventIdentity::StorageResult writeHubMetadata(
        EventStorage::CopySlot copy, const uint8_t* input, size_t length) override;
    EventIdentity::StorageResult readHubEventCopy(
        uint8_t logicalSlot, EventStorage::CopySlot copy,
        uint8_t* output, size_t capacity, size_t& length) override;
    EventIdentity::StorageResult writeHubEventCopy(
        uint8_t logicalSlot, EventStorage::CopySlot copy,
        const uint8_t* input, size_t length) override;
};

}  // namespace Esp32EventStorage
