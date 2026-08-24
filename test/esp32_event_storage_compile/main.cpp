#include <esp32_event_storage.h>

void setup() {
    Esp32EventStorage::PreferencesStore storage;
    NodeEventStore::Storage* nodeStorage = &storage;
    HubEventLedger::Storage* hubStorage = &storage;
    (void)nodeStorage;
    (void)hubStorage;
    (void)storage;
}

void loop() {}
