<p align="center">
  <img src="docs/assets/ARGUS_Redline.png"
       alt="ARGUS REDLINE"
       width="100%">
</p>

# ARGUS REDLINE

**Open-source off-grid IoT, system security, and structured communications over resilient low-bandwidth radio.**

## Connected Devices When the Grid Goes Down

ARGUS REDLINE is an open-source, off-grid network platform designed to connect computers, smartphones, sensors, devices, and field infrastructure when Wi-Fi, cellular service, and the internet aren't available.

Small, inexpensive radio nodes carry alerts, sensor readings, status updates, commands, and application data across resilient, long-range links without depending on commercial infrastructure.

Think remote property, farms, disaster response, community networks, field research, expeditions, events, mobile operations, or anywhere devices need to communicate beyond normal network coverage.

**One radio network. Whatever application you want to build on top of it.**

## The Architecture

REDLINE is built around a single, clean rule:

**The radio layer moves information reliably and safely. The application decides what that information means.**

```text
   [ Your Application ]
            |
            v
  [ Host Computer / Phone ]
            |
            v
     [ REDLINE Hub ]
            |
            v
 [ Off-Grid Radio Network ]
            |
            v
    [ REDLINE Node ]
            |
            v
[ Sensors / Field Devices ]
```

* **The Application Layer:** Your computer or smartphone handles the intelligence — user interfaces, mapping, databases, analytics, automation, operator interfaces, and business logic — without putting application-specific logic on the radio microcontroller.
* **The Infrastructure Layer:** REDLINE provides the physical bridge and owns constrained communications, device identity, bounded transactions, local runtime behavior, and radio transport between the host and field hardware. It packages data into small, efficient radio messages designed for long-range, low-bandwidth links where conventional network coverage may not exist.
* **The Field Layer:** Remote Nodes connect to independently provisioned field hardware and expose approved logical capabilities representing sensors, indicators, controls, storage, location providers, local procedures, and other physical interfaces rather than unrestricted raw GPIO or hardware-specific implementation details.

## What You Can Build On Top of It

Because REDLINE provides a shared off-grid network, completely different software systems can use the same underlying infrastructure.

* **Private Off-Grid Messaging:** Build encrypted local communications networks for families, neighborhood groups, or field teams to text and coordinate during emergencies.
* **Remote Sensor Networks:** Track environmental conditions, soil moisture, water levels, weather, solar systems, or equipment diagnostics across large areas.
* **Equipment Telemetry & Control:** Monitor distributed machinery, activate indicators, trigger equipment, or manage localized automation without cellular subscriptions.
* **Field Logistics & Operations:** Build location tracking, team check-ins, status reporting, panic/duress signaling, and coordination tools for expeditions, events, and remote operations.
* **Resilient Routing:** Extend coverage through repeaters, store-and-forward networking, and distributed routing to work around distance, terrain, and infrastructure gaps.

## One Substrate. Many Applications.

Most radio systems are built around one specific job. ARGUS REDLINE takes a different approach.

The network provides a common connection between computers and the physical world while applications decide what that connection is used for.

A REDLINE network can carry a weather reading, a check-in message, a location update, an equipment alert, a sensor measurement, a remote command, or data for an application that hasn't been invented yet.

The same underlying network can support entirely different tools without rebuilding the radio infrastructure for every new idea — and without turning the microcontroller firmware into a collection of application-specific features.

The goal is simple:

**You build the application. REDLINE provides the network.**

## Development Status

ARGUS REDLINE is an experimental embedded communications and device-control platform developed by **RaveGoat Labs** for the wider **RG Herd** coordination ecosystem.

> **Firmware identifier:** v0.5.1 — focused duplicate-cache security/correctness patch over the v0.5.0 capability substrate, preserving Wire Protocol 1.

**Current published baseline:** v0.5.1. Its F-01 remediation and focused qualification are complete. Remaining electrical, startup, and endurance qualification gates v0.6.0 implementation, while v0.6.0 architecture and protocol design work is active.

Configuration Schema remains `1` and the current hardware profile remains `HELTEC_V4`.

Current firmware uses small structured packets to invoke, configure, or coordinate substantially more complex behavior already available on each device. The packet is the instruction layer; the Node's firmware, stored state, attached hardware, authorization, and capability profile determine what the instruction can safely do.

The v1 target includes multiple directly reachable Nodes, reliable commands and Node-originated events, persistent identity and settings, approved sensor and output capabilities, local device screens, a documented host interface, authenticated encryption, replay protection, and device revocation.

Mesh routing, repeaters, GNSS, final enclosures, general opaque application transport, and full ARGUS integration are later development layers rather than current operational features.

### Project Documentation

* **[ARGUS REDLINE — What It Is](docs/PROJECT_OVERVIEW.md)** — system purpose and broad development direction
* **[Versioned Development Roadmap](docs/ARGUS_REDLINE_VERSIONED_DEVELOPMENT_ROADMAP.md)** — authoritative release order, milestone scope, and release gates
* **[Host Transport Architecture](docs/ARGUS_REDLINE_HOST_TRANSPORT_ARCHITECTURE.md)** — responsibility boundaries between applications, host services, Hub firmware, and radio transport
* **[v0.6.0 Architecture Baseline](docs/V0.6.0_ARCHITECTURE_BASELINE.md)** — pre-implementation architectural baseline
* **[Host Protocol 0.1](docs/HOST_PROTOCOL_0.1.md)** — developmental computer-to-device protocol
* **[v0.6.0 Wire Operation and Response Design](docs/V0.6.0_WIRE_OPERATION_RESPONSE_DESIGN.md)** — structured radio operations and response semantics
* **[v0.6.0 Implementation Brief](docs/V0.6.0_IMPLEMENTATION_BRIEF.md)** — implementation sequence and acceptance gates
* **[August 14 Security Review](docs/ARGUS_REDLINE_SECURITY_REVIEW_2026-08-14.md)** — security findings and current dispositions
* **[v0.5.1 Implementation and Qualification Brief](docs/V0.5.1_IMPLEMENTATION_AND_QUALIFICATION_BRIEF.md)** — F-01 correction and acceptance gates
* **[v0.5.1 Qualification Results](docs/V0.5.1_QUALIFICATION_RESULTS.md)** — physical qualification evidence
* **[v0.5.1 Dependency and Source Audit](docs/V0.5.1_DEPENDENCY_AND_SOURCE_AUDIT.md)** — dependency and source verification
* **[v0.5.1 Development Handoff](docs/ARGUS_REDLINE_v0.5.1_Development_Handoff.md)** — patch, compatibility, validation, and remaining limitations

## Implemented now

The underlying v0.5.0 capability milestone adds three immutable `HELTEC_V4` logical capabilities; v0.5.1 does not change that registry behavior:

* Application indicator `0x0101`
* Digital input `0x0201`
* Analog input `0x0301`

Digital input and indicator/arbitration behavior were physically validated. Analog sampling remains disabled and reports `HARDWARE_UNAVAILABLE` pending GPIO4/ADC1_CH3 electrical characterization in a v0.5.x patch.

Verified behavior includes:

* Bidirectional LoRa communication
* Explicit Hub and Node addressing
* Versioned binary packet format
* Sequence-matched acknowledgments
* Bounded retransmission attempts
* Single-entry, in-memory duplicate detection using exact canonical identity: source, sequence, opcode, payload length, and meaningful payload bytes; this state is volatile across restart
* Complete command admission before semantic duplicate lookup or mutation, so invalid or inadmissible requests cannot influence cached duplicate state
* Duplicate ACK regeneration from cached transaction metadata and status
* Recovery after temporary node loss
* Packet-length and packet-type validation
* RSSI and SNR diagnostics
* Hardware-independent runtime health, error, metric, activity, inbound-packet, and saturating-counter state
* Deterministic active-low GPIO0 input using `INPUT_PULLUP`, 30 ms debounce, and 800 ms long press
* Six shared OLED screens: Home, Radio, Device, Last Packet, Diagnostics, and About
* Short-press navigation, long-press return Home, and a 30-second OLED inactivity timeout
* Wake without navigation while radio processing continues independently of the display
* Validated local settings with dual-slot generation recovery and a recoverable factory-reset policy
* A bounded nine-item settings editor for display, LED, diagnostics, default-screen, and feedback preferences
* Role-safe persistence service points for both Hub transactions and Node receive/ACK ownership
* A bounded logical capability registry separating approved device behavior from raw hardware access
* Local capability authorization and interlock enforcement
* Physical validation of indicator arbitration and digital-input capability behavior

## Project

Developed by [RaveGoat Labs](https://ravegoat.com/) as part of the [RG Herd](https://rgherd.com/) privacy-first communications and coordination ecosystem.

Learn more about the ARGUS coordination platform at [argus.rgherd.com](https://argus.rgherd.com/).

Related public projects:

* [ARGUS](https://github.com/sudorgherd/rgherd-argus) — self-hosted dispatcher and operator coordination platform
* [RG Herd](https://rgherd.com/) — privacy-first communications and coordination infrastructure
* [ARGUS REDLINE v0.1.0](https://github.com/sudorgherd/argus-redline/releases/tag/v0.1.0) — first published radio protocol release

## Hardware

Current development hardware:

* 2× Heltec WiFi LoRa 32 V4
* ESP32-S3
* Semtech SX1262 LoRa radio
* 915 MHz configuration
* Integrated OLED display
* USB serial debugging

Current logical device roles:

| Role | Device ID |
| ---- | --------: |
| Hub  |    `0x01` |
| Node |    `0x10` |

The current Heltec development hardware is the reference platform, not the intended limit of the REDLINE architecture. Logical capability abstraction is designed to keep higher-level applications independent of raw hardware-specific GPIO assignments.

## Protocol v0.1 wire format

Packets use a six-byte header followed by an optional payload.

Protocol v0.1 identifies wire-format compatibility. Firmware v0.5.1 remains on Wire Protocol version 1. The v0.1.03 tag is preserved as historical release metadata.

| Byte | Field                            |
| ---: | -------------------------------- |
|    0 | Protocol version and packet type |
|    1 | Source device ID                 |
|    2 | Destination device ID            |
|    3 | Sequence number                  |
|    4 | Opcode                           |
|    5 | Payload length                   |
|   6+ | Payload                          |

Maximum packet size is currently 32 bytes.

The codec recognizes these packet types:

* `COMMAND`
* `ACK`
* `ERROR`

Current firmware actively uses only `COMMAND` and `ACK`. `ERROR` is recognized by the codec but is not currently emitted or handled as an active firmware transaction type.

Protocol definitions and encoding logic are located in [`include/protocol.h`](include/protocol.h).

## Build

Build both firmware environments:

```powershell
platformio run
```

Build individually:

```powershell
platformio run -e tx
platformio run -e rx
```

Upload individually:

```powershell
platformio run -e tx -t upload
platformio run -e rx -t upload
```

Upload ports may need to be changed in `platformio.ini`.

## Local validation

Run the complete native Unity suite:

```powershell
platformio test -e native
```

Build the production Hub and Node firmware:

```powershell
platformio run -e tx
platformio run -e rx
```

Build the two valid device-configuration fixtures:

```powershell
platformio run -e config_hub_valid
platformio run -e config_node_valid
```

The three negative fixtures must fail compilation. A nonzero exit is the expected successful test result; verify the corresponding exact diagnostic shown below:

```powershell
platformio run -e config_missing_local # ARGUS_LOCAL_DEVICE_ID must be defined by the build environment
platformio run -e config_missing_peer  # ARGUS_PEER_DEVICE_ID must be defined by the build environment
platformio run -e config_equal_ids     # Local and peer device IDs must be different
```

## Development history

This repository intentionally preserves its branches, tags, and commit history.

`main` is the primary branch. Temporary development and documentation branches may exist, so this document does not maintain a branch inventory.

The first published radio protocol release remains:

* [ARGUS REDLINE v0.1.0](https://github.com/sudorgherd/argus-redline/releases/tag/v0.1.0)

The firmware identifier in the source is v0.5.1. The historical v0.1.03 tag is preserved, and both versions use the Protocol v0.1 wire format.

The history includes the original string-based exchange, binary protocol implementation, reliability testing, device runtime and UI work, persistent settings, the bounded capability abstraction introduced through v0.5.0, and the focused v0.5.1 duplicate-cache correction.

## Development direction

The product architecture described above is being implemented as a layered system in which applications interact with a stable, application-neutral host boundary rather than directly managing embedded transport details.

At the technical level, the intended direction is:

```text
Host applications
      ↓
Application-neutral REDLINE host service / driver
      ↓
Host Protocol
      ↓
REDLINE Hub firmware
      ↓
Wire Protocol
      ↓
Remote REDLINE Nodes
      ↓
Approved physical capabilities
```

Application meaning remains above this boundary.

The embedded layer is responsible for bounded communication and physical capability access. Host-side software remains responsible for business logic, user interfaces, databases, analytics, automation, integrations, and interpretation of the information being transported.

Current firmware uses structured `COMMAND`/`ACK`-style operations. The future architecture adds the computer-facing Host/service boundary while preserving this separation of responsibilities.

General opaque application payload transport is planned for v1.1 and is not implemented today.

See **[Host Transport Architecture](docs/ARGUS_REDLINE_HOST_TRANSPORT_ARCHITECTURE.md)** for the stable responsibility boundaries and **[Host Protocol 0.1](docs/HOST_PROTOCOL_0.1.md)** for the developmental v0.6 framing specification.

The next planned milestone is **`v0.6.0 — Structured Operations, Responses, and Host Protocol`**.

Its **[implementation brief](docs/V0.6.0_IMPLEMENTATION_BRIEF.md)** is planning, not active firmware work. Documentation/design is active; implementation entry remains gated on successful completion of the remaining required hardware qualification.

### V1 target

* Reliable radio transport with structured responses and events, duplicate suppression, and per-device transaction statistics
* Multiple directly reachable Nodes with persistent identities, controlled provisioning, and per-Node state
* A usable device interface with local screens, button input, persistent settings, and clear status feedback
* Initial commands and events including `PING`, `GET_DEVICE_INFO`, `GET_STATUS`, `SET_INDICATOR`, and `REPORT_EVENT`
* A documented computer-facing Hub interface with registry, transaction, capability, and configuration visibility
* Authenticated encryption, persistent counters, replay rejection, credential rotation, device revocation, and recovery after missed updates

These bullets describe complete v1 targets. Some radio-transport foundations are implemented now, but the listed v1 capabilities are not complete.

Persistent ordinary device settings and the bounded local capability foundation are implemented through v0.5.0.

Persistent identity, multi-Node coordination, repeaters, routing, store-and-forward, encryption and authentication, replay protection, provisioning, stable host/dispatcher integration, production alerts/check-ins, location sharing, panic/duress workflows, and sensor applications are not implemented.

### Exploratory and later concepts

* Device profiles for handheld, vehicle, sensor, actuator, and stationary roles
* Parameterized operations, policies, stored procedures, and compact workflow definitions
* Emergency signaling and operator check-ins
* Store-and-forward messaging
* Repeaters and mobile relay Nodes
* GNSS and sensor telemetry
* Detachable field modules and third-party hardware profiles
* Application-neutral host SDKs and adapters
* Full ARGUS operator-interface integration

These ideas may be explored where concrete requirements justify them. Their inclusion here is not a commitment to implement them in v1 or afterward, and they are not currently operational features.

## Security

The protocol and firmware are intended to remain open. Operational credentials are not.

Network keys, enrollment secrets, signing keys, access tokens, device certificates, deployment records, and private configuration files must never be committed.

REDLINE is not intended to provide arbitrary remote code execution or unrestricted GPIO access.

Remote embedded device operations must remain limited to operations authorized for the current trust context and approved device capabilities. Wire Protocol 1 radio peers are not authenticated; the v0.6 production design therefore permits only documented read-only/non-side-effecting REMOTE operations according to policy and does not grant general remote `SET_INDICATOR` or `RUN_PROCEDURE` authority. This device-execution restriction does not prohibit the future application-neutral opaque transport layer.

The current firmware does not yet implement the authenticated encrypted transport planned for the v1 development path.

## Experimental status

ARGUS REDLINE is active experimental firmware.

It has not been independently audited and should not currently be relied upon for life-safety or other safety-critical communications.

Radio regulations, permitted frequencies, transmission-power limits, and operating requirements remain the responsibility of the operator.

## License

ARGUS REDLINE is licensed under the **GNU General Public License, version 3 or any later version** (`GPL-3.0-or-later`).

See [`LICENSE`](LICENSE) for the full license text.
