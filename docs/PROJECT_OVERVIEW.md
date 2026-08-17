# ARGUS REDLINE — What It Is

ARGUS REDLINE is an open-source, off-grid IoT, system-security, and structured-communications platform under active development. Its current published v0.5.1 firmware provides a validated direct radio/device foundation and bounded local capability abstraction; cryptographic security is planned for later pre-v1 milestones and is not implemented.

It is intended to securely connect operators, sensors, mobile devices, and controlled trigger mechanisms over resilient low-bandwidth radio when cellular service, internet access, or ordinary communications infrastructure is unavailable, unreliable, overloaded, or inappropriate for the task.

At its core, REDLINE provides a controlled communications path between a computer-connected Hub and independently configured field devices over LoRa radio.

It is being developed by **RaveGoat Labs** as part of the wider **RG Herd** privacy-first communications and coordination ecosystem.

> **Plain-language definition:** REDLINE is being developed toward a secure, configurable, off-grid device network for sending structured commands, status updates, check-ins, alerts, sensor data, and device events between an operator-facing Hub and field Nodes. Current v0.5.1 packets are not cryptographically authenticated or encrypted.

REDLINE is not a general-purpose chat application, arbitrary remote-control framework, finished mesh network, or replacement for emergency services. It is experimental embedded firmware and supporting infrastructure being developed toward a stable distributed-device platform.

The long-term host and transport boundary is defined in [ARGUS REDLINE — Host Transport Architecture](ARGUS_REDLINE_HOST_TRANSPORT_ARCHITECTURE.md). In short, applications own application meaning while REDLINE owns transport.

## Current development state

The current published firmware baseline is v0.5.1. The v0.5.x required hardware
qualification is complete. Documentation and design for `v0.6.0 — Structured
Operations, Responses, and Host Protocol` are active, and implementation is
unblocked from the hardware-qualification perspective, but v0.6.0 implementation
has **NOT STARTED**.

REDLINE remains a resilient field communications and capability substrate
whose current physical transport is LoRa. Firmware owns identity, framing,
radio mechanics, retries, duplicate handling, bounded hardware capabilities,
and constrained host-interface validation. Host software owns application
behavior, workflows, databases, rich UI, automation policy, third-party
integration, and higher-level interpretation. Those application concerns do
not move into the MCU as the Host Protocol is introduced.

---

## The Three Core Functions

### Off-grid IoT

REDLINE is intended to connect distributed devices that can:

- observe physical conditions;
- report measurements and events;
- receive configuration and policy;
- execute approved local procedures;
- operate approved outputs or trigger mechanisms;
- continue limited operation without internet or cellular service.

A Node may eventually represent a handheld device, environmental sensor, vehicle unit, check-in terminal, alert control, power monitor, signaling device, relay, or custom breadboard module.

### System security

Security means controlling who and what can participate in the system.

The target model includes:

- unique device identities;
- authenticated and encrypted traffic;
- controlled enrollment and provisioning;
- replay protection;
- restricted device capabilities;
- authenticated management commands;
- key rotation and device revocation;
- separation between public source code and private operational credentials.

Security does **not** mean offensive cybersecurity tooling. It means protecting the integrity, confidentiality, authorization, and lifecycle of the REDLINE network itself.

### Structured communications

REDLINE is not primarily based on unstructured conversation.

Its messages are intended to have defined semantics:

```text
known message type
+ authenticated sender
+ explicit destination
+ defined payload schema
+ expected response
```

Examples include:

```text
CHECK_IN
STATUS
ALERT
SENSOR_READING
GET_DEVICE_INFO
SET_POLICY
RUN_PROCEDURE
TRIGGER_OUTPUT
REPORT_EVENT
```

Structured messages allow devices and operator software to validate, route, display, store, and act on information consistently.

REDLINE's structured operations and future general application transport serve different purposes.

**REDLINE-defined structured operations** cover device management, transport and system control, capability discovery, and safe execution of approved local hardware behavior. Their semantics are defined by REDLINE because firmware must validate and execute them predictably.

**Opaque application payloads** are a future `v1.1` direction for general data belonging to ARGUS, BLACKSHEEP, NIGHTWATCH, third-party software, and other host applications. REDLINE transports those payloads without embedding their application schemas or business meaning in firmware. Opaque general-purpose application transport is not implemented in the current `v0.5.1` firmware and is not part of the earlier Host Protocol foundation milestones.

Transport delivery also remains distinct from application outcome:

```text
application outcome != transport delivery status
```

REDLINE may be able to confirm that bytes reached a peer without knowing whether the receiving application accepted, displayed, persisted, or acted on them.

---

## Tiny Packets, Complex Behavior

One of REDLINE's central technical objectives is to determine how much useful device behavior can be represented through compact, authenticated, low-bandwidth instructions.

The system does not normally transmit C++ source code over LoRa. Instead, it transmits small instructions that select, configure, or combine behavior already available on the receiving device.

Conceptually:

```text
small packet
+ installed firmware
+ stored configuration
+ current device state
+ attached hardware
= complex local behavior
```

For example, a compact command could instruct a Node to apply a check-in policy containing an interval, retry limit, escalation rule, and delivery behavior. Only a few bytes may cross the radio link, while the Node performs timers, retries, local caching, display updates, sensor checks, and structured reporting.

The expected progression is:

```text
fixed operations
→ parameterized operations
→ multi-packet transactions
→ stored policies and procedures
→ compact workflow definitions
→ signed update packages
```

Future workflow definitions might resemble a small domain-specific language or bytecode. Any such system must remain constrained, authenticated, validated, and recoverable. REDLINE is not intended to expose arbitrary remote code execution.

---

## The System Model

The first supported topology is deliberately simple:

```text
Computer or operator service
          │
          │ USB / serial
          ▼
         Hub
     ┌────┼────┐
     ▼    ▼    ▼
   Node  Node  Node
```

The Hub coordinates directly reachable Nodes. Each Node has its own identity, configuration, transaction state, authorization, and registered capabilities.

The broader project is intended to become relay- and mesh-capable, but mesh routing is not the immediate architecture:

```text
direct Hub-to-Node network
→ stationary and mobile relays
→ store-and-forward behavior
→ controlled distributed routing
→ mesh-capable network
```

Direct communication must first become stable, usable, measurable, and secure. Mesh behavior adds routing loops, congestion, stale data, duplicate propagation, relay authorization, and recovery problems that should not be introduced prematurely.

---

## Controlled Capability Model

REDLINE may ultimately coordinate a wide range of devices, but “can do anything” does not mean unrestricted behavior.

A device can perform only actions permitted by:

- its attached hardware;
- installed firmware or signed procedures;
- registered capabilities;
- available radio bandwidth;
- device and operator authorization;
- safety and operational policy.

Possible capabilities include:

```text
TEMPERATURE_SENSOR
AIR_QUALITY_SENSOR
DIGITAL_INPUT
ANALOG_INPUT
ALERT_BUTTON
STATUS_DISPLAY
INDICATOR_OUTPUT
SIGNAL_OUTPUT
RELAY_OUTPUT
POWER_MONITOR
LOCATION_PROVIDER
LOCAL_STORAGE
```

Commands should operate on these capabilities or approved logical channels—not arbitrary GPIO numbers.

Examples:

```text
READ_SENSOR | AIR_QUALITY_SENSOR
SET_INDICATOR | mode=BLINK | duration=30
TRIGGER_OUTPUT | SIGNAL_OUTPUT | policy=3
RUN_PROCEDURE | procedure=CHECKIN_ESCALATION
```

Controlled trigger mechanisms may eventually include indicators, alarms, signaling devices, power controls, relays, or other approved actuators. Safety-critical or physically hazardous actions require stricter authorization, interlocks, validation, and local fail-safe behavior than ordinary status or telemetry operations.

---

## V1 Target

ARGUS REDLINE v1 is intended to provide a stable, secure, multi-device LoRa coordination platform.

### Reliable radio transport

- Bidirectional LoRa communication
- Compact, versioned binary packets
- Explicit source and destination addressing
- Sequence-matched acknowledgments
- Bounded retries and timeout handling
- Duplicate-command and duplicate-event suppression
- Structured errors, acknowledgments, responses, and events
- Per-device link statistics and transaction state

### Multiple direct Nodes

- One Hub coordinating multiple directly reachable Nodes
- Persistent device identities
- Controlled provisioning of device role and network membership
- Per-Node sequence, retry, presence, and last-seen state
- One active Hub transaction at a time initially
- Node-originated event delivery with acknowledgment and retry

### Usable device interface

- Stable OLED home screen
- Local device, radio, packet, and diagnostic screens
- Debounced button input with semantic press events
- Persistent local settings
- Clear delivery, error, and connectivity feedback
- Separation between normal user settings and protected provisioning values

### Useful commands, responses, and events

Initial operations should prove the full system rather than lock it to one application:

```text
PING
GET_DEVICE_INFO
GET_STATUS
SET_INDICATOR
REPORT_EVENT
```

The protocol may later expose approved sensors and outputs through capabilities such as:

```text
GET_CAPABILITIES
READ_DIGITAL
WRITE_DIGITAL
READ_ANALOG
READ_SENSOR
TRIGGER_OUTPUT
RUN_PROCEDURE
SET_POLICY
```

### Computer-facing Hub interface

- A documented USB/serial host protocol
- Commands submitted from a local computer
- Decoded responses and events returned to the host
- Device registry and transaction visibility
- Capability and configuration visibility
- A clean future integration boundary for ARGUS

The embedded firmware should not hard-code assumptions about the full ARGUS operator platform.

### Security

Before operational use, v1 should include:

- Unique device credentials
- Authenticated and encrypted packets
- Persistent message counters
- Replay rejection
- Controlled provisioning
- Credential rotation
- Device revocation
- Authenticated management commands
- Recovery after a device misses configuration or key updates

A revocation list alone does not cryptographically remove a device. The affected devices must move to a new key generation or security context that excludes the revoked unit.

---

## Intended Field Behaviors

REDLINE is being designed as a reusable platform rather than a single fixed appliance.

Potential behaviors include:

- Operator-reviewed check-ins
- Manual status updates
- Small addressed messages
- Digital-whistle or duress events
- Missed-check-in awareness
- Selective sensor telemetry
- Controlled trigger and signaling actions
- Controlled location requests
- Temporary offline operation
- Cached event delivery
- Store-and-forward synchronization
- Remote policy and procedure configuration

The first implementations should remain generic. For example, a physical button should initially prove reliable event delivery before being labeled or treated as a production panic function.

---

## Device Profiles

### Handheld Node

Potential capabilities:

- Simple local menus and status screens
- Manual check-ins and status events
- Deliberate multi-step alert activation
- Delivery confirmation
- Local caching
- Optional, tightly controlled GNSS behavior

### Sensor or Control Node

Potential capabilities:

- Registered digital, analog, or sensor inputs
- Environmental or equipment monitoring
- Event generation based on approved thresholds
- Indicators, signaling outputs, or controlled relays
- Locally enforced safety rules
- Stored policies and procedures

### Vehicle Node

Potential future capabilities:

- Larger power budget
- Mobile relay behavior
- Store-and-forward collection
- Optional position reporting when authorized
- Connection to a phone, tablet, or vehicle computer

### Stationary Relay or Repeater

Potential future capabilities:

- Fixed coverage extension
- Dead-drop synchronization
- Controlled relay behavior
- Environmental sensing
- Connection to a computer or networked Hub

### Hub

Target capabilities:

- Device registry
- Addressed command dispatch
- Response and event collection
- Presence and last-seen tracking
- Capability discovery and visibility
- Enrollment and provisioning
- Group membership and configuration management
- Rekeying and revocation distribution
- Computer and eventual ARGUS integration

---

## Implemented Now

The current v0.5.1 firmware foundation already provides:

- Two independently buildable Heltec firmware targets
- Bidirectional LoRa communication at 915 MHz
- Asynchronous Hub and Node radio state machines
- Protocol v0.1 wire-format support using Wire Protocol 1
- Explicit device addressing
- Sequence-matched acknowledgments
- Bounded retransmission
- Single-entry, in-memory duplicate detection based on the most recently received canonical request; this state is volatile across restart
- Duplicate ACK regeneration from cached transaction metadata and status
- Opcode and payload validation
- Configurable compile-time device identities
- RSSI and SNR diagnostics
- Hardware-independent runtime health, error, metric, activity, inbound-packet, and saturating-counter state
- A deterministic active-low GPIO0 input classifier with debounced short, long, and very-long presses
- Shared Home, Radio, Device, Last Packet, Diagnostics, and About OLED screens
- Nonblocking, dirty and rate-limited presentation with a 30-second timeout and wake-without-navigation behavior
- Shared bounded presentation snapshots and a Heltec OLED rendering adapter
- Validated Schema 1 local settings with dual-slot generation selection, fallback repair, and prepared-state-tested factory-reset recovery
- A bounded nine-item settings editor with role-safe persistence integration on both Hub and Node
- Physical two-board validation on two Heltec WiFi LoRa 32 V4.3 boards
- A fixed 20-byte capability descriptor, bounded 16-entry immutable registry,
  typed local values/results, authorization/interlock gates, and synchronous
  safe dispatch
- Three HELTEC_V4 logical capabilities: indicator `0x0101`, digital input
  `0x0201`, and analog input `0x0301`
- Role-safe capability diagnostics and a bounded DEVICE-screen CAPS summary
- Physical digital-input, indicator/arbitration, and GPIO4/ADC1_CH3 electrical
  characterization completed; ordinary production analog sampling remains
  disabled and returns `HARDWARE_UNAVAILABLE`

The codec recognizes `COMMAND`, `ACK`, and `ERROR` packet types. Current firmware actively uses only `COMMAND` and `ACK`; `ERROR` is recognized by the codec but is not currently emitted or handled as an active firmware transaction type.

The current application behavior remains intentionally narrow: a test command and acknowledgment exchange between one Hub and one Node.

Current packets are structured and validated but are **not yet cryptographically authenticated or encrypted**.

Persistent identity and provisioning, sensors, trigger mechanisms, transferred procedures, multiple active Nodes, security, host integration, repeaters, and mesh routing are targeted directions—not completed capabilities.

---

## Outside the Initial v1 Scope

The following ideas are not required for the first stable release. They may be explored later where concrete requirements justify them, but this list is not a commitment to implement them after v1:

- Mesh routing
- Repeaters
- City-scale relay networks
- Dead-drop synchronization
- GNSS as a standard feature
- Vehicle hardware
- Final handheld enclosure
- Full ARGUS integration
- Every sensor or actuator type
- General-purpose workflow bytecode
- Finalized panic workflow
- Firmware updates over LoRa

Keeping these outside the v1 requirement prevents speculative features from destabilizing the core platform.

The v1 capability model should still be designed so that later sensors, actuators, procedures, relays, and routing layers can be added without replacing the entire protocol.

---

## Development Principle

The project follows a simple rule:

> Build a reliable, reusable device platform first. Add specialized operational behavior only when a concrete need justifies it.

The conceptual development progression is:

```text
stable radio foundation
→ reusable device platform
→ secure direct-network platform
→ v1.0.0
→ optional distributed expansion
```

This sequence expresses broad product direction only. The [Versioned Development Roadmap](ARGUS_REDLINE_VERSIONED_DEVELOPMENT_ROADMAP.md) defines the authoritative version ordering, milestone scope, and release gates.

---

## Safety and Deployment Status

ARGUS REDLINE remains active experimental firmware.

It has not been independently audited and should not currently be relied upon for life-safety or safety-critical communications. Operators remain responsible for radio regulations, permitted frequencies, transmission-power limits, credential protection, attached-hardware safety, and deployment security.

Open-source firmware does not mean operational secrets are public. Network keys, enrollment secrets, signing keys, device credentials, deployment records, and private configuration must never be committed to the repository.

Any actuator or trigger mechanism must fail safely. Remote authorization does not replace local hardware interlocks, electrical protection, physical safeguards, or human review where those controls are appropriate.
