#include <Arduino.h>
#include <esp32_identity_storage.h>

void setup() {
    Esp32IdentityStorage::NvsStore storage;
    DeviceIdentityStore::Storage* interface = &storage;
    DeviceIdentityStore::Store store;
    (void)interface;
    (void)store;
}

void loop() {}
