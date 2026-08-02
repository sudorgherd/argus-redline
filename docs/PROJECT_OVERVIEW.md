# ARGUS REDLINE — What It Is

ARGUS REDLINE is an open-source, off-grid IoT, system-security, and structured-communications platform under active development.

It is intended to securely connect operators, sensors, mobile devices, and controlled trigger mechanisms over resilient low-bandwidth radio when cellular service, internet access, or ordinary communications infrastructure is unavailable, unreliable, overloaded, or inappropriate for the task.

At its core, REDLINE provides a controlled communications path between a computer-connected Hub and independently configured field devices over LoRa radio.

It is being developed by **RaveGoat Labs** as part of the wider **RG Herd** privacy-first communications and coordination ecosystem.

> **Plain-language definition:** REDLINE is a secure, configurable, off-grid device network for sending structured commands, status updates, check-ins, alerts, sensor data, and device events between an operator-facing Hub and field Nodes.

REDLINE is not a general-purpose chat application, arbitrary remote-control framework, finished mesh network, or replacement for emergency services. It is experimental embedded firmware and supporting infrastructure being developed toward a stable distributed-device platform.

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

The current firmware foundation already provides:

- Two independently buildable Heltec firmware targets
- Bidirectional LoRa communication at 915 MHz
- Asynchronous Hub and Node radio state machines
- Protocol v0.1 wire-format support; the firmware source identifies itself as v0.1.03
- Explicit device addressing
- Sequence-matched acknowledgments
- Bounded retransmission
- Single-entry, in-memory duplicate detection based on the most recently received source, sequence, and opcode; this state is volatile across restart
- Duplicate ACK regeneration from cached transaction metadata and status
- Opcode and payload validation
- Configurable compile-time device identities
- RSSI and SNR diagnostics
- Physical two-board validation

The codec recognizes `COMMAND`, `ACK`, and `ERROR` packet types. Current firmware actively uses only `COMMAND` and `ACK`; `ERROR` is recognized by the codec but is not currently emitted or handled as an active firmware transaction type.

The current application behavior remains intentionally narrow: a test command and acknowledgment exchange between one Hub and one Node.

Current packets are structured and validated but are **not yet cryptographically authenticated or encrypted**.

Sensors, trigger mechanisms, transferred procedures, multiple active Nodes, repeaters, and mesh routing are targeted directions—not completed capabilities.

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
