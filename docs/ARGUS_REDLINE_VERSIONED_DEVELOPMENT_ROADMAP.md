# ARGUS REDLINE — Versioned Development Roadmap

**Status:** Authoritative roadmap
**Current completed release:** `v0.4.0`
**Immediate next release:** `v0.5.0` — Capability Registry and Safe Hardware Abstraction
**Current wire format:** Wire Protocol `1`
**Primary v1 target:** Secure, capability-driven, direct Hub-to-Node off-grid IoT and structured communications platform

See [ARGUS REDLINE — What It Is](PROJECT_OVERVIEW.md) for the stable system vision and broad capability direction.

---

## 1. Roadmap Decision

ARGUS REDLINE will not attempt to reach mesh networking before it has a stable device platform.

The development order is:

```text
firmware foundation
→ device runtime and interface
→ persistent configuration
→ safe capability model
→ structured commands and responses
→ Node-originated events
→ persistent identity and provisioning
→ authenticated transport
→ multiple direct Nodes
→ power and lifecycle behavior
→ host control service
→ recovery and field qualification
→ v1.0.0
→ procedures, store-and-forward, relays, and mesh
```

The first stable release will be a secure direct network, not a finished mesh.

---

## 2. Versioning Model

The historical tags remain unchanged:

```text
v0.1.0
v0.1.01
v0.1.02
v0.1.03
```

No additional `v0.1.xx` releases are planned.

Beginning with `v0.2.0`, firmware releases use:

```text
MAJOR.MINOR.PATCH
```

Release policy:

```text
0.MINOR.0   coherent development milestone
0.MINOR.X   compatible fixes and milestone completion
1.0.0       first documented stable supported platform
2.0.0       first stable distributed or mesh-capable platform
```

A release does not automatically change every interface version.

Protocol v0.1 uses Wire Protocol version 1.

Track these independently:

```text
Firmware release        0.2.0, 0.3.0, 1.0.0
Wire protocol           1, 2, 3
Configuration schema    1, 2, 3
Host protocol/API       0.1, 0.2, 1.0
Hardware profile        HELTEC_V4
Procedure package       not defined before v1
```

### Compatibility rules

- Patch releases must not introduce incompatible wire, configuration, or host-interface changes.
- A wire-protocol version changes only when packet compatibility is actually broken.
- New opcodes, packet types, and payload schemas may remain on the same wire version when capability negotiation and safe rejection preserve compatibility.
- Configuration schemas must support validation, defaults, and migration.
- Host-interface changes must be documented independently from firmware releases.
- Historical development tags are preserved even when their numbering does not match the new policy.

---

# Pre-v1 Development

## `v0.2.0` — Firmware Architecture and Test Foundation — Complete

**Status:** Completed and released.

### Objective

Prepare the current working radio firmware for device UI, capabilities, storage, security, and multiple Nodes without continuing to expand two large role-specific source files.

### Scope

- Separate firmware version from wire-protocol version.
- Add centralized build and version metadata.
- Extract shared code into reusable components.
- Establish authoritative runtime and transaction-state types.
- Add native tests for hardware-independent behavior.
- Add a documented physical two-board regression test.
- Preserve the existing Wire Protocol `1` packet format.
- Preserve the current `TEST` command exchange.

### Suggested structure

```text
lib/
├── redline_protocol/
├── radio_link/
├── transaction_engine/
├── device_state/
├── diagnostics/
├── display/
├── input/
├── settings/
└── capabilities/

src-tx/
└── main.cpp

src-rx/
└── main.cpp

test/
├── test_protocol/
├── test_transactions/
├── test_duplicates/
└── test_configuration/
```

### Required tests

- Packet encode and decode
- Invalid packet length
- Invalid packet type
- Invalid protocol version
- Payload validation
- ACK matching
- Retry-state transitions
- Duplicate detection
- Sequence wraparound
- Device-configuration validation

### Release gate

```text
[ ] Hub and Node both build
[ ] Existing TEST exchange still succeeds
[ ] Existing addressing and retry behavior remain intact
[ ] Core logic is no longer concentrated in main.cpp
[ ] Native tests pass
[ ] Physical two-board regression passes
[ ] Firmware and wire-protocol versions are reported separately
```

### Explicit non-goals

- New application behavior
- Menus
- Persistent settings
- Multiple Nodes
- Encryption
- Mesh or repeaters

---

## `v0.3.0` — Device Runtime, Screens, and Input — Complete

**Status:** Completed release candidate; all core software and physical gates
passed. Controlled physical duplicate reproduction was not completed, while
duplicate behavior remains covered by native tests and source audit.

### Objective

Create a reusable device shell that remains responsive while radio operations continue.

### Scope

- One authoritative `DeviceState`.
- Shared screen framework for Hub and Node.
- Semantic button events.
- Button debouncing.
- Nonblocking display updates.
- Display timeout behavior.
- Device, packet, radio, and diagnostic screens.
- Runtime counters and error state.
- Consistent startup, ready, degraded, and error presentation.

### Initial screens

```text
Home
Radio
Device
Last Packet
Diagnostics
About
```

### Input events

```text
PRESS
RELEASE
SHORT_PRESS
LONG_PRESS
```

### Release gate

```text
[x] Screen navigation never interrupts receive mode
[x] Radio behavior does not depend on the visible screen
[x] Input is debounced
[x] Short and long presses are reliably distinguishable
[x] Runtime presentation state is the display's authoritative data source
[x] Home screen remains stable during ordinary activity
[x] Errors can be displayed without blocking the radio
```

### Explicit non-goals

- Persistent settings
- Provisioning
- Panic semantics
- Arbitrary menu control of radio or security parameters

---

## `v0.4.0` — Persistent Settings and Configuration Recovery

**Status:** Completed

### Objective

Allow safe local settings to survive reboot and power loss.

### Scope

- Define Configuration Schema `1`.
- Store local preferences in NVS.
- Validate stored values.
- Apply defaults on first boot.
- Detect invalid or unknown schemas.
- Support schema migration.
- Add factory reset.
- Separate ordinary settings from protected provisioning data.
- Avoid unnecessary flash writes.

### Initial local settings

```text
Display timeout
Display contrast
LED enabled
Diagnostics enabled
Default screen
Button feedback
```

### Protected values not exposed as ordinary settings

```text
Device identity
Network identity
Security credentials
Protocol version
Radio frequency
Transmit power
Raw GPIO assignments
```

### Release gate

```text
[x] Settings survive power removal
[x] Missing settings receive safe defaults
[x] Invalid values are rejected or repaired
[x] Factory reset restores documented defaults
[x] Schema version is visible
[x] Storage writes do not interrupt observed radio operation
[x] Repeated ordinary operation does not cause continuous flash writes
```

---

## `v0.5.0` — Capability Registry and Safe Hardware Abstraction

### Objective

Make REDLINE a reusable off-grid IoT platform without exposing unrestricted hardware control.

### Scope

- Define a capability descriptor.
- Register approved sensors, inputs, outputs, and local procedures.
- Address hardware through logical capability IDs.
- Add capability discovery.
- Add per-capability authorization and validation hooks.
- Add local safety and interlock checks.
- Implement the first complete sensor/input/output vertical slice.

### Candidate capability classes

```text
DIGITAL_INPUT
ANALOG_INPUT
SENSOR
INDICATOR_OUTPUT
SIGNAL_OUTPUT
RELAY_OUTPUT
POWER_MONITOR
LOCATION_PROVIDER
LOCAL_STORAGE
LOCAL_PROCEDURE
```

### First physical validation

Use low-risk hardware:

```text
button or digital input
+ LED or indicator output
+ resistor or analog input
```

A relay may be simulated before controlling external power.

### Required behavior

```text
GET_CAPABILITIES
READ_INPUT
READ_SENSOR
SET_INDICATOR
TRIGGER_OUTPUT
```

### Release gate

```text
[ ] Node reports registered capabilities
[ ] Unsupported capabilities are rejected
[ ] Commands use logical IDs rather than arbitrary GPIO numbers
[ ] Input or sensor data can be returned structurally
[ ] Approved output can be controlled
[ ] Local authorization and safety checks can deny an operation
[ ] Radio and UI remain responsive during hardware operations
```

### Explicit non-goals

- Arbitrary GPIO access
- General remote code execution
- Hazardous actuators
- Transferred procedures
- Mesh behavior

---

## `v0.6.0` — Structured Operations, Responses, and Host Protocol

### Objective

Move beyond `TEST` into a real application protocol and prove a complete computer-to-device transaction.

### Scope

- Formal opcode and payload registry.
- Structured command schemas.
- Structured response schemas.
- Explicit error model.
- Add `RESPONSE` and, if required, reserved future packet types.
- Capability-aware operation validation.
- Define Host Protocol `0.1`.
- Add a simple USB/serial development console.
- Add compiled local procedure invocation.

### Initial operations

```text
PING
GET_DEVICE_INFO
GET_STATUS
GET_CAPABILITIES
READ_SENSOR
SET_INDICATOR
RUN_PROCEDURE
```

`RUN_PROCEDURE` invokes a known procedure already compiled into the Node. It does not install arbitrary code.

### Required vertical slice

```text
PC command
→ Hub serial parser
→ LoRa command
→ Node capability or procedure
→ LoRa response
→ Hub result
→ PC output
→ DeviceState and display update
```

### Release gate

```text
[ ] Each operation has a documented payload schema
[ ] ACK and RESPONSE semantics are distinct
[ ] Unsupported operations fail predictably
[ ] Malformed payloads cannot reach hardware handlers
[ ] Host framing resynchronizes after bad input
[ ] Full PC-to-Node-to-PC transaction succeeds
[ ] Existing Wire Protocol version is retained or deliberately incremented
```

---

## `v0.7.0` — Node-Originated Events and Reliable Delivery

### Objective

Allow Nodes to initiate structured traffic and preserve important events through temporary Hub unavailability.

### Scope

- Add `EVENT` semantics.
- Define persistent event identity.
- Add outbound event queue.
- Track queued, transmitting, acknowledged, failed, and expired states.
- Retry events without duplicate Hub processing.
- Persist important queued events across reboot.
- Add queue limits and overflow behavior.
- Support button and sensor-generated events.
- Add local compiled policies that may create events.

### Initial event sources

```text
button press
sensor threshold
local status change
device error
manual check-in
```

### Release gate

```text
[ ] Node can originate an event without a Hub command
[ ] Lost ACK causes retransmission
[ ] Hub processes a retransmitted event only once
[ ] Important queued events survive Node reboot
[ ] Queue-full behavior is visible and documented
[ ] Expired events are handled predictably
[ ] Hub-unavailable state is shown locally
```

### Explicit non-goals

- Production panic or duress workflow
- General workflow transfer
- Multi-hop forwarding

---

## `v0.8.0` — Persistent Identity and Controlled Provisioning

### Objective

Allow the same firmware image to become independently provisioned Hub and Node devices.

### Scope

- Remove operational identity from compile-time build flags.
- Define Identity Schema `1`.
- Add unprovisioned and provisioned states.
- Add controlled USB provisioning.
- Store device ID, role, label, network ID, authorized Hub, and hardware profile.
- Detect duplicate or invalid identities.
- Separate identity reset from ordinary settings reset.
- Define recovery and reprovisioning behavior.
- Freeze the initial security threat model and credential architecture.

### Provisioned properties

```text
Device ID
Device role
Human-readable label
Network ID
Authorized Hub
Hardware profile
Capability profile
Provisioning state
```

### Release gate

```text
[ ] Same firmware binary provisions multiple unique devices
[ ] Unprovisioned devices cannot join normal operations
[ ] Duplicate IDs are detected
[ ] Identity survives ordinary settings reset
[ ] Full factory reset clears identity deliberately
[ ] Provisioning state is visible locally
[ ] Identity and configuration schemas are documented separately
```

---

## `v0.9.0` — Authenticated Transport and Replay Protection

### Objective

Protect direct Hub-to-Node communication before expanding to multiple active Nodes.

### Scope

- Implement unique device credentials.
- Establish pairwise Hub-to-Node authenticated encryption.
- Define nonce and persistent-counter construction.
- Reject replayed packets.
- Authenticate management operations.
- Add key rotation.
- Add direct-network device revocation.
- Define behavior after storage loss or counter rollback.
- Protect credentials in persistent storage.
- Add tamper, replay, and reboot tests.
- Document production security provisioning.

### Security boundary

The first stable direct network should prefer pairwise device relationships over one universal shared network key.

Group keys and generation-based group rekeying become necessary later for repeaters, group traffic, and mesh behavior.

### Release gate

```text
[ ] Modified ciphertext is rejected
[ ] Replayed packets are rejected
[ ] Counter state survives reboot
[ ] Counter rollback is detected or safely recovered
[ ] Revoked device traffic is rejected
[ ] Management commands require stronger authorization
[ ] Credential loss has a documented recovery process
[ ] Security failures do not trigger device capabilities
```

### Hardware-security boundary

Secure Boot, flash encryption, encrypted NVS, and irreversible eFuse provisioning are designed and tested only on designated hardware. They are not casually enabled on the current development boards.

---

## `v0.10.0` — Secure Multi-Node Direct Network

### Objective

Operate one Hub with multiple independently secured, directly reachable Nodes.

### Hardware requirement

```text
1 Hub
+ at least 2 physical Nodes
```

Two physical Nodes are required to test independent state, contention, and collisions.

### Scope

- Hub device registry.
- Per-Node sequence and counter state.
- Per-Node retries and timeouts.
- Per-Node presence and last-seen status.
- Secure destination selection.
- One active Hub transaction at a time initially.
- Round-robin or policy-based polling.
- Node event contention and randomized backoff.
- Explicit broadcast policy.
- Collision and airtime logging.
- Offline and stale-device handling.

### Release gate

```text
[ ] Hub communicates independently with at least two Nodes
[ ] Per-Node transaction state never crosses devices
[ ] Simultaneous Node events recover through backoff
[ ] One missing Node does not block others
[ ] Revoking one Node does not disable authorized Nodes
[ ] Presence and last-seen state are accurate
[ ] Broadcast behavior is documented and restricted
[ ] Packet-loss and collision behavior are measured
```

### Explicit non-goals

- Repeaters
- Routing
- Mesh
- Multiple simultaneous Hub transactions
- Network-wide shared-key architecture

---

## `v0.11.0` — Power, Sleep, and Device Lifecycle

### Objective

Make field devices behave predictably under battery use, restart, radio loss, and power interruption.

### Scope

- Battery and supply-state reporting.
- Display sleep.
- Radio-aware sleep states.
- Wake sources.
- Hub scheduling for sleeping Nodes.
- Brownout and reset-reason reporting.
- Safe restart behavior.
- Queue and counter persistence before sleep.
- Low-battery policy hooks.
- Current-consumption measurements.
- Hardware UART or alternative diagnostics where USB CDC conflicts with sleep.

### Release gate

```text
[ ] Sleeping Nodes wake predictably
[ ] Hub does not treat scheduled sleep as unexplained failure
[ ] Queued events and security counters survive sleep and reset
[ ] Low-battery state is reported
[ ] Brownout or reset reason is visible
[ ] Receive availability and power consumption are measured
[ ] Device cannot enter sleep during an unsafe storage or transmit operation
```

---

## `v0.12.0` — Hub Control Service and Stable Integration Boundary

### Objective

Provide a self-hosted computer-facing control plane without embedding ARGUS assumptions directly into firmware.

### Scope

- Reference Hub serial service.
- Stable serial framing and reconnect behavior.
- Device registry persistence.
- Command submission.
- Response and event ingestion.
- Transaction and audit logging.
- Capability visibility.
- Presence and last-seen state.
- Narrow local API.
- Host Protocol/API `1.0` candidate.
- Documented ARGUS integration boundary.

### Possible service flow

```text
ARGUS or local operator tool
→ REDLINE host API
→ Hub serial service
→ Hub radio
→ field Node
```

### Release gate

```text
[ ] Service reconnects after Hub USB disconnect
[ ] Serial corruption does not permanently desynchronize framing
[ ] Commands and events have stable host-side identities
[ ] Duplicate host submissions are handled safely
[ ] Device registry persists across service restart
[ ] API schemas are documented
[ ] Firmware can operate without the full ARGUS platform
```

### Explicit non-goal

Full production ARGUS workflow integration is not required for v1.

---

## `v0.13.0` — Update, Recovery, and Field Qualification

### Objective

Stop adding major features and prove the system can survive expected failures.

### Scope

- Documented firmware update process.
- Recovery mode.
- Configuration migration tests.
- Failed-update recovery and rollback.
- Power-loss testing during writes and updates.
- Malformed-packet and fuzz testing.
- Queue overflow testing.
- Counter and sequence wrap testing.
- Long-duration operation.
- Forced radio failures.
- RF range and packet-loss measurements.
- Antenna-orientation records.
- Current-consumption records.
- Storage wear review.
- Production security-profile validation.
- Complete operator and developer documentation.

LoRa firmware transfer is not required. Initial updates may remain USB-based or use a separately validated local update path.

### Release gate

```text
[ ] Bad firmware can be recovered or rolled back
[ ] Configuration migration preserves valid settings
[ ] Power loss does not silently corrupt identity or counters
[ ] Long-duration direct network test passes
[ ] Forced Node and Hub failures recover predictably
[ ] RF and power behavior are measured and documented
[ ] Security and capability failure tests pass
[ ] No unresolved release-blocking defects remain
```

---

# v1 Release Process

## `v1.0.0-alpha.1` — Feature Complete

- All required v1 features are present.
- No new feature families are accepted.
- Known defects and unfinished documentation are allowed.
- Wire, configuration, and host interfaces are candidates rather than final.

## `v1.0.0-beta.1` — Field Test Candidate

- Primary behavior is complete.
- Field testing and failure testing are active.
- Compatibility changes require explicit review.
- Operator-facing documentation is substantially complete.

Additional beta releases may be created as necessary:

```text
v1.0.0-beta.2
v1.0.0-beta.3
```

## `v1.0.0-rc.1` — Release Candidate

- No known release-blocking defects.
- Interfaces are frozen.
- Update and recovery process is verified.
- Security configuration is documented.
- Final regression and clean-install tests pass.

## `v1.0.0` — Stable Direct-Network Platform

### v1 promise

```text
Stable Heltec V4 firmware
One Hub with multiple secure direct Nodes
Persistent identity and provisioning
Local screens and persistent settings
Capability registry and safe logical hardware access
Approved sensors, inputs, outputs, and trigger mechanisms
Structured commands, responses, errors, and events
Reliable Node-originated event delivery
Compiled procedure invocation
Versioned host service and API
Authenticated encrypted communication
Replay protection and direct-device revocation
Power, sleep, update, and recovery behavior
Measured RF and power performance
Documented wire, configuration, identity, and host schemas
```

### v1 does not promise

```text
Mesh routing
Repeaters
Store-and-forward relays
Dead-drop synchronization
Transferable workflow bytecode
Firmware transfer over LoRa
Full ARGUS workflow integration
GNSS as a standard feature
Vehicle hardware
Final handheld enclosure
Every sensor or actuator
Final life-safety panic workflow
```

---

# Post-v1 Expansion

Post-v1 ordering is directional and may change as field use identifies real needs.

## `v1.1.0` — Capability and Hardware Module Expansion

- Additional sensor drivers
- Additional approved outputs
- Capability module SDK
- Hardware profile abstraction
- Additional ESP32 and LoRa board profiles
- Capability conformance tests

## `v1.2.0` — Signed Policies and Transferable Procedures

### Objective

Expand the tiny-packet/complex-behavior model beyond compiled procedure IDs.

### Scope

- Compact policy or workflow format
- Multi-packet transfer
- Chunk validation and recovery
- Signed procedure packages
- Compatibility declaration
- Transactional installation
- Rollback
- Execution limits
- Storage quotas
- Procedure permissions
- Audit record

This is not unrestricted native-code transfer. The preferred model is a constrained domain-specific language or bytecode with bounded operations.

## `v1.3.0` — Store-and-Forward and Dead-Drop Synchronization

- Persistent message custody
- Expiration and packet lifetime
- Delivery receipts
- Delayed-event classification
- Synchronization after missed updates
- Storage limits and garbage collection
- Historical data from later-revoked devices
- Dead-drop Node profile

## `v1.4.0` — Group Security and Delegated Relay Authorization

- Group or event credentials
- Key-generation or epoch model
- Group membership
- Rekeying
- Disconnected-device resynchronization
- Relay authorization
- Management-authority signatures
- Stale-generation rejection

## `v1.5.0` — Stationary and Mobile Relay Nodes

- Controlled repeaters
- Mobile collection
- Duplicate propagation control
- Hop limits
- Relay custody records
- Loop prevention
- Congestion controls
- Relay health reporting

## `v1.6.0` — ARGUS Integration

- ARGUS device registry synchronization
- Operator command submission
- Structured event intake
- Check-in workflows
- Alert review and escalation
- Zones and assignments
- Audit integration
- Role-based access
- Offline Hub operation

## `v1.7.0` — Handheld, Vehicle, and GNSS Profiles

- Handheld interaction profile
- Vehicle power and mounting profile
- Controlled GNSS request and reporting
- Privacy-aware location policy
- Mobile relay behavior
- Field enclosure requirements
- Antenna and power profile documentation

## `v1.8.0` — Experimental Distributed Routing

- Route discovery
- Route metrics
- Hop limits
- Path failure recovery
- Network partition behavior
- Congestion and airtime controls
- Routed event identity
- Routed replay protection
- Multi-relay field testing

This remains experimental and does not yet constitute a stable mesh promise.

---

# `v2.0.0` — Stable Distributed and Mesh-Capable Platform

`v2.0.0` is earned when REDLINE supports a documented, secured, field-tested distributed network rather than only direct Hub-to-Node operation.

### v2 promise

```text
Direct and relayed communication
Controlled multi-hop routing
Store-and-forward delivery
Stationary and mobile relays
Group membership and epoch-based rekeying
Revocation across disconnected network segments
Signed transferable procedures
ARGUS operational integration
Network-wide diagnostics
Measured congestion and airtime behavior
Documented partition and recovery behavior
Stable routing, security, and procedure-package interfaces
```

---

## 3. Development Rules

1. Every release has a written scope and release gate.
2. No feature is called complete until it passes physical hardware validation.
3. New packet behavior requires protocol documentation and tests.
4. New persistent data requires a schema and migration strategy.
5. New hardware control requires a registered capability and local safety validation.
6. Security primitives are selected from established implementations; REDLINE does not invent cryptography.
7. Mesh features do not enter pre-v1 work unless required to protect an earlier architectural decision.
8. A release number represents a coherent system milestone, not every coding session.
9. Patch releases correct or complete the current milestone; they do not become hidden feature releases.
10. Current capabilities and future targets remain clearly separated in public documentation.

---

## 4. Immediate Next Release

The next planned release is:

```text
v0.5.0
Capability Registry and Safe Hardware Abstraction
```

Immediate work:

```text
[ ] Define a bounded capability descriptor and logical capability IDs
[ ] Add capability discovery without unrestricted hardware access
[ ] Add per-capability authorization, validation, safety, and interlock hooks
[ ] Implement the first complete sensor/input/output vertical slice
[ ] Preserve protocol, radio, and role ownership while capabilities are introduced
```
