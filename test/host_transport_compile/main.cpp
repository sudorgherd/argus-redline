#include <Arduino.h>

#include <host_transport.h>

HostTransport::Adapter<decltype(Serial)> hostTransportCompileFixture(Serial);

void compileHostTransportApi() {
    Serial.setTxTimeoutMs(0);
    Serial.setDebugOutput(false);
    const bool connected = static_cast<bool>(Serial);
    (void)connected;
    uint8_t frame[HostProtocol::MAX_ENCODED_FRAME_SIZE] = {};
    (void)hostTransportCompileFixture.serviceRx(0);
    (void)hostTransportCompileFixture.submit(frame, sizeof(frame));
    (void)hostTransportCompileFixture.serviceTx(0);
    hostTransportCompileFixture.reset();
}

void setup() {}
void loop() {}
