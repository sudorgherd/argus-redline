<p align="center">
  <img src="docs/assets/ARGUS_Redline.png"
       alt="ARGUS REDLINE"
       width="100%">
</p>

**Open-source off-grid IoT, system security, and structured communications over resilient low-bandwidth radio.**

ARGUS REDLINE is an experimental embedded communications and device-control project developed by **RaveGoat Labs** for the wider **RG Herd** coordination stack.

> **Firmware identifier:** v0.4.0 — persistent validated settings and local configuration recovery, preserving the Protocol v0.1 wire format.

## What is ARGUS REDLINE?

ARGUS REDLINE is intended to become a secure, configurable, off-grid platform connecting operators, sensors, mobile devices, and controlled trigger mechanisms through a computer-facing Hub and independently provisioned field Nodes.

REDLINE uses small structured packets to invoke, configure, or coordinate substantially more complex behavior already available on each device. The packet is the instruction layer; the Node's firmware, stored state, attached hardware, and authorization determine what the instruction can safely do.

The v1 target includes multiple directly reachable Nodes, reliable commands and Node-originated events, persistent identity and settings, approved sensor and output capabilities, local device screens, a documented host interface, authenticated encryption, replay protection, and device revocation. Mesh routing, repeaters, GNSS, final enclosures, and full ARGUS integration are later development layers rather than v1 requirements.

**[ARGUS REDLINE — What It Is](docs/PROJECT_OVERVIEW.md)** defines the system purpose and broad capability direction.

The **[Versioned Development Roadmap](docs/ARGUS_REDLINE_VERSIONED_DEVELOPMENT_ROADMAP.md)** defines the authoritative release order, milestone scope, and release gates.

The **[v0.4.0 Release Notes](docs/V0.4.0_RELEASE_NOTES.md)** summarize this milestone's changes, compatibility, validation, and known limitations. The [v0.4.0 storage characterization](docs/V0.4.0_STORAGE_CHARACTERIZATION.md) records the dedicated persistence evidence.

## Implemented now

Firmware v0.4.0 was validated on two physical Heltec WiFi LoRa 32 V4.3 boards before the final identity-only version change from `v0.3.0` to `v0.4.0`.

Verified behavior includes:

- Bidirectional LoRa communication
- Explicit Hub and Node addressing
- Versioned binary packet format
- Sequence-matched acknowledgments
- Bounded retransmission attempts
- Single-entry, in-memory duplicate detection based on the most recently received source, sequence, and opcode; this state is volatile across restart
- Duplicate ACK regeneration from cached transaction metadata and status
- Recovery after temporary node loss
- Packet-length and packet-type validation
- RSSI and SNR diagnostics
- Hardware-independent runtime health, error, metric, activity, inbound-packet, and saturating-counter state
- Deterministic active-low GPIO0 input using `INPUT_PULLUP`, 30 ms debounce, and 800 ms long press
- Six shared OLED screens: Home, Radio, Device, Last Packet, Diagnostics, and About
- Short-press navigation, long-press return Home, and a 30-second OLED inactivity timeout
- Wake without navigation while radio processing continues independently of the display
- Validated local settings with dual-slot generation recovery and a recoverable factory-reset policy
- A bounded nine-item settings editor for display, LED, diagnostics, default-screen, and feedback preferences
- Role-safe persistence service points for both Hub transactions and Node receive/ACK ownership

## Project

Developed by [RaveGoat Labs](https://ravegoat.com/) as part of the [RG Herd](https://rgherd.com/) privacy-first communications and coordination ecosystem.

Related public projects:

- [ARGUS](https://github.com/sudorgherd/rgherd-argus) — self-hosted dispatcher and operator coordination platform
- [RG Herd](https://rgherd.com/) — privacy-first communications and coordination infrastructure
- [ARGUS REDLINE v0.1.0](https://github.com/sudorgherd/argus-redline/releases/tag/v0.1.0) — first published radio protocol release

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

## Protocol v0.1 wire format

Packets use a six-byte header followed by an optional payload.

Protocol v0.1 identifies wire-format compatibility. Firmware v0.4.0 remains on Wire Protocol version 1. The v0.1.03 tag is preserved as historical release metadata.

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

The codec recognizes these packet types:

- `COMMAND`
- `ACK`
- `ERROR`

Current firmware actively uses only `COMMAND` and `ACK`. `ERROR` is recognized by the codec but is not currently emitted or handled as an active firmware transaction type.

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

`main` is the primary branch. Temporary development and documentation branches may exist, so this document does not maintain a branch inventory.

The first published radio protocol release remains:

- [ARGUS REDLINE v0.1.0](https://github.com/sudorgherd/argus-redline/releases/tag/v0.1.0)

The firmware identifier in the source is v0.4.0. The historical v0.1.03 tag is preserved, and both versions use the Protocol v0.1 wire format.

The history includes the original string-based exchange, binary protocol implementation, reliability testing, and Protocol v0.1 merge.

## Development direction

### V1 target

- Reliable radio transport with structured responses and events, duplicate suppression, and per-device transaction statistics
- Multiple directly reachable Nodes with persistent identities, controlled provisioning, and per-Node state
- A usable device interface with local screens, button input, persistent settings, and clear status feedback
- Initial commands and events including `PING`, `GET_DEVICE_INFO`, `GET_STATUS`, `SET_INDICATOR`, and `REPORT_EVENT`
- A documented computer-facing Hub interface with registry, transaction, capability, and configuration visibility
- Authenticated encryption, persistent counters, replay rejection, credential rotation, device revocation, and recovery after missed updates

These bullets describe complete v1 targets. Some radio-transport foundations are implemented now, but the listed v1 capabilities are not complete.

Persistent ordinary device settings are implemented in v0.4.0. Persistent identity, multi-Node coordination, repeaters, routing, store-and-forward, encryption and authentication, replay protection, provisioning, host/dispatcher integration, production alerts/check-ins, location sharing, panic/duress workflows, and sensor applications are not implemented.

### Exploratory and later concepts

- Device profiles for handheld, vehicle, sensor, actuator, and stationary roles
- Parameterized operations, policies, stored procedures, and compact workflow definitions
- Emergency signaling and operator check-ins
- Store-and-forward messaging
- Repeaters and mobile relay Nodes
- GNSS and sensor telemetry
- Full ARGUS operator-interface integration

These ideas may be explored where concrete requirements justify them. Their inclusion here is not a commitment to implement them in v1 or afterward, and they are not currently operational features.

## Security

The protocol and firmware are intended to remain open. Operational credentials are not.

Network keys, enrollment secrets, signing keys, access tokens, device certificates, deployment records, and private configuration files must never be committed.

REDLINE is not intended to provide arbitrary remote code execution or unrestricted GPIO access. Remote behavior must be limited to authenticated operations, installed procedures, and approved device capabilities.

## Experimental status

ARGUS REDLINE is active experimental firmware. It has not been independently audited and should not currently be relied upon for life-safety or other safety-critical communications.

Radio regulations, permitted frequencies, transmission-power limits, and operating requirements remain the responsibility of the operator.

## License

ARGUS REDLINE is licensed under the **GNU General Public License, version 3 or any later version** (`GPL-3.0-or-later`).

See [`LICENSE`](LICENSE) for the full license text.
