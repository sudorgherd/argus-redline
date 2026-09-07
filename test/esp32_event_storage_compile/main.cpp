#include <Arduino.h>
#include <esp32_event_storage.h>
#include <node_event_creation.h>

void setup() {
    Esp32EventStorage::PreferencesStore storage;
    NodeEventStore::Storage* nodeStorage = &storage;
    HubEventLedger::Storage* hubStorage = &storage;
    (void)nodeStorage;
    (void)hubStorage;
    (void)storage;
    NodeEventStore::Store nodeStore;
    NodeEventDelivery::Controller controller;
    NodeEventDelivery::TrackedCreationSink<decltype(&millis)> sink(
        nodeStore, controller, &millis);
    (void)sink;
}

void loop() {}
