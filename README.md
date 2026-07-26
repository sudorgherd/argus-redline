# ARGUS REDLINE

**Open-source long-range radio firmware and off-grid coordination infrastructure.**

ARGUS REDLINE is an experimental communications project developed by **RaveGoat Labs** for the wider **RG Herd** coordination stack.

> **Current milestone:** Protocol v0.1 — verified reliable binary command exchange over LoRa.

## Current status

Protocol v0.1 has been built, flashed, and tested on two physical Heltec WiFi LoRa 32 V4 devices.

Verified behavior includes:

- Bidirectional LoRa communication
- Explicit Hub and Node addressing
- Versioned binary packet format
- Sequence-matched acknowledgments
- Bounded retransmission attempts
- Duplicate-command suppression
- Cached acknowledgment replay
- Recovery after temporary node loss
- Packet-length and packet-type validation
- RSSI and SNR diagnostics

## Project

Developed by **RaveGoat Labs** as part of the **RG Herd** privacy-first communications and coordination ecosystem.


## Hardware

Current development hardware:

- 2× Heltec WiFi LoRa 32 V4
- ESP32-S3
- Semtech SX1262 LoRa radio
- 915 MHz configuration
- Integrated OLED display
- USB serial debugging

Current logical device roles:

| Role | Device ID |
|---|---:|
| Hub | `0x01` |
| Node | `0x10` |

## Protocol v0.1

Packets use a six-byte header followed by an optional payload.

| Byte | Field |
|---:|---|
| 0 | Protocol version and packet type |
| 1 | Source device ID |
| 2 | Destination device ID |
| 3 | Sequence number |
| 4 | Opcode |
| 5 | Payload length |
| 6+ | Payload |

Maximum packet size is currently 32 bytes.

Supported packet types:

- `COMMAND`
- `ACK`
- `ERROR`

Protocol definitions and encoding logic are located in [`include/protocol.h`](include/protocol.h).

## Build

Build both firmware environments:

```powershell
pio run
```

Build individually:

```powershell
pio run -e tx
pio run -e rx
```

Upload individually:

```powershell
pio run -e tx -t upload
pio run -e rx -t upload
```

Upload ports may need to be changed in `platformio.ini`.

## Development history

This repository intentionally preserves its branches, tags, and commit history.

Current public branches:

- `main`
- `protocol-v0.1`
- `public-launch`

Current release tag:

- `v0.1.0`

The history includes the original string-based exchange, binary protocol implementation, reliability testing, and Protocol v0.1 merge.

## Development direction

Areas under consideration include:

- Multiple addressed Nodes
- Authenticated and encrypted packets
- Persistent transaction identity
- Handheld, vehicle, and stationary device profiles
- Emergency signaling and operator check-ins
- Store-and-forward messaging
- Repeaters and mobile relay Nodes
- GNSS and sensor telemetry
- Computer-connected Hub services
- ARGUS operator-interface integration
- Device revocation and key rotation

These are development directions, not completed features.

## Security

The protocol and firmware are intended to remain open. Operational credentials are not.

Network keys, enrollment secrets, signing keys, access tokens, device certificates, deployment records, and private configuration files must never be committed.

## Experimental status

ARGUS REDLINE is active experimental firmware. It has not been independently audited and should not currently be relied upon for life-safety or other safety-critical communications.

Radio regulations, permitted frequencies, transmission-power limits, and operating requirements remain the responsibility of the operator.
