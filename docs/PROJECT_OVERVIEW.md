# ARGUS REDLINE — What It Is

ARGUS REDLINE is an open-source, long-range radio coordination platform for situations where cellular service, internet access, or ordinary communications infrastructure is unavailable, unreliable, overloaded, or inappropriate for the task.

At its core, REDLINE is intended to provide a controlled communications path between a computer-connected Hub and independently configured field devices over LoRa radio.

It is being developed by **RaveGoat Labs** as part of the wider **RG Herd** privacy-first communications and coordination ecosystem.

> **Plain-language definition:** REDLINE is a secure, configurable, off-grid radio system for sending small commands, status updates, check-ins, alerts, and device events between a Hub and field Nodes.

REDLINE is not a general-purpose chat application, a finished mesh network, or a replacement for emergency services. It is experimental embedded firmware and supporting infrastructure being developed toward a stable coordination platform.

---

## The System Model

The initial supported topology is deliberately simple:

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

The Hub coordinates directly reachable Nodes. Each Node has its own identity, configuration, transaction state, and authorization.

The design avoids jumping immediately into routing, repeaters, or mesh behavior. Those features can be added later after direct Hub-to-Node operation is stable, usable, measurable, and secure.

---

## Targeted v1 Capabilities

ARGUS REDLINE v1 is intended to provide a stable, secure, multi-device LoRa coordination platform with the following capabilities.

### Reliable radio transport

- Bidirectional LoRa communication
- Compact, versioned binary packets
- Explicit source and destination addressing
- Sequence-matched acknowledgments
- Bounded retries and timeout handling
- Duplicate-command and duplicate-event suppression
- Structured error and response handling
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

### Useful commands and events

Initial operations should prove the full system rather than lock it to one application. Likely core operations include:

```text
PING
GET_DEVICE_INFO
GET_STATUS
SET_INDICATOR
REPORT_EVENT
```

The protocol may later expose approved sensors and outputs through logical capabilities such as:

```text
GET_CAPABILITIES
READ_DIGITAL
WRITE_DIGITAL
READ_ANALOG
```

Remote arbitrary GPIO access is not a target. Hardware access must use known-safe logical channels or registered capabilities.

### Computer-facing Hub interface

- A documented USB/serial host protocol
- Commands submitted from a local computer
- Decoded responses and events returned to the host
- Device registry and transaction visibility
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

REDLINE is being designed as a reusable platform rather than a single fixed appliance. Potential behaviors include:

- Operator-reviewed check-ins
- Manual status updates
- Small addressed messages
- Digital-whistle or duress events
- Missed-check-in awareness
- Selective sensor telemetry
- Controlled location requests
- Temporary offline operation
- Cached event delivery
- Store-and-forward synchronization

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
- Enrollment and provisioning
- Group membership and configuration management
- Rekeying and revocation distribution
- Computer and eventual ARGUS integration

---

## What Exists Today

The current firmware foundation already provides:

- Two independently buildable Heltec firmware targets
- Bidirectional LoRa communication at 915 MHz
- Asynchronous Hub and Node radio state machines
- Protocol v0.1 binary packet encoding
- Explicit device addressing
- Sequence-matched acknowledgments
- Bounded retransmission
- Duplicate-command suppression
- Cached acknowledgment replay
- Opcode and payload validation
- Configurable compile-time device identities
- RSSI and SNR diagnostics
- Physical two-board validation

The current application behavior remains intentionally narrow: a test command and acknowledgment exchange.

Current packets are structured and validated but are **not yet cryptographically authenticated or encrypted**.

---

## What v1 Does Not Need

The following ideas are legitimate future directions, but they are not required for the first stable release:

- Mesh routing
- Repeaters
- City-scale relay networks
- Dead-drop synchronization
- GNSS as a standard feature
- Vehicle hardware
- Final handheld enclosure
- Full ARGUS integration
- Every sensor or actuator type
- Finalized panic workflow
- Firmware updates over LoRa

Keeping these outside the v1 requirement prevents speculative features from destabilizing the core platform.

---

## Development Principle

The project follows a simple rule:

> Build a reliable, reusable device platform first. Add specialized operational behavior only when a concrete need justifies it.

That means the progression is:

```text
stable radio foundation
→ usable device shell
→ persistent configuration
→ useful commands and responses
→ reliable Node-originated events
→ provisioning and identity
→ multiple direct Nodes
→ authentication and encryption
→ field qualification
→ v1.0.0
```

---

## Safety and Deployment Status

ARGUS REDLINE remains active experimental firmware.

It has not been independently audited and should not currently be relied upon for life-safety or safety-critical communications. Operators remain responsible for radio regulations, permitted frequencies, transmission-power limits, credential protection, and deployment security.

Open-source firmware does not mean operational secrets are public. Network keys, enrollment secrets, signing keys, device credentials, deployment records, and private configuration must never be committed to the repository.
