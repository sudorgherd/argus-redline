# ARGUS REDLINE — Versioned Development Roadmap

**Status:** Authoritative roadmap
**Current release:** `v0.6.0` — Structured Operations, Responses, and Host Protocol
**Next planned milestone:** `v0.7.0` — Node-Originated Events and Reliable Delivery
**Current wire format:** Wire Protocol `1`
**Primary v1 target:** Secure, capability-driven, direct Hub-to-Node off-grid IoT and structured communications platform

**Lifecycle state:** v0.6.0 completes structured operations, Wire Protocol 1 RESPONSE semantics, developmental Host Protocol 0.1, the Arduino-ESP32 3.3.9 framework migration, and complete two-board physical qualification. Wire Protocol remains `1`, Configuration Schema remains `1`, and hardware profile remains `HELTEC_V4`. F-02 through F-04 remain deferred authenticated-transport requirements.

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
[x] Hub and Node both build
[x] Existing TEST exchange still succeeds
[x] Existing addressing and retry behavior remain intact
[x] Core protocol, transaction, and runtime policy is extracted from main.cpp
[x] Native tests pass
[x] Physical two-board regression passes
[x] Firmware and wire-protocol versions are reported separately
```

The physical regression used the behavior-complete `0.2.0-dev` build before the final identifier-only change. Startup metadata lines were not retained after USB CDC re-enumeration, and physical duplicate injection was not performed; source/build validation and native tests provide that evidence. These recorded limitations did not block the released v0.2.0 milestone.

### Explicit non-goals

- New application behavior
- Menus
- Persistent settings
- Multiple Nodes
- Encryption
- Mesh or repeaters

---

## `v0.3.0` — Device Runtime, Screens, and Input — Complete

**Status:** Completed and released. All core software and physical gates
passed. Controlled physical duplicate reproduction and physical
malformed/unsupported packet injection were not completed; those paths remain
covered by native tests and source audit.

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

**Status:** Complete.

Completed foundation: fixed pointer-free descriptors, a bounded immutable
registry, opaque logical IDs, typed values/results, authorization and
interlock gates, simulated vertical slice, Heltec V4 profile, role-safe
production integration, capability-owned diagnostics, DeviceInput snapshot and
LED-arbitration adapters, minimal DEVICE-screen summary, and retained
characterization tooling. Physical digital input and indicator/arbitration
behavior were validated on the V4.3 boards.

GPIO4/ADC1_CH3 physical electrical characterization passed in an August 16,
2026 v0.5.x follow-up over the tested 0.51–2.31 V range, together with a
post-characterization 30/30 TEST/ACK regression. The registered analog
capability remains `HARDWARE_UNAVAILABLE` in ordinary production firmware.

### v0.5.x qualification line

The v0.5.x line remains available for qualification and bounded corrective
patches before advancement to v0.6.0. The formal isolated radio exchange gate
passed during v0.5.1 qualification. The separate August 16 follow-up completed
controlled GPIO4/ADC1_CH3 electrical characterization and a formal
post-characterization 30/30 regression. Remaining documented broader work is
also complete: physical startup-held GPIO0 reproduction and the designated
endurance/broader fault qualification passed on August 16. The v0.5.x required
hardware entry-gate items are closed.

`v0.5.1` is the focused F-01 duplicate-cache security/correctness patch. It
uses exact canonical request identity (source, sequence, opcode, payload length,
and meaningful payload bytes) only after complete command admission. Native,
production-build, fixture, and two-board F-01 qualification passed without a
Wire Protocol version change. F-02 peer authentication, F-03
authentication-first discard/response governance, and F-04 authenticated
persistent freshness/replay protection remain deferred requirements; they are
not patched piecemeal into Wire Protocol 1.

### Objective

Make REDLINE a reusable off-grid IoT platform without exposing unrestricted hardware control.

### Scope

- Define a capability descriptor.
- Define bounded class vocabulary for approved sensors, inputs, outputs, and local procedures, including classes reserved for later milestones.
- Register only the explicitly approved low-risk v0.5.0 production subset defined by the implementation brief and hardware validation plan.
- Address hardware through logical capability IDs.
- Add capability discovery.
- Add per-capability authorization and validation hooks.
- Add local safety and interlock checks.
- Implement the first complete sensor/input/output vertical slice.

### Candidate capability classes

This is model vocabulary, not the production registered-capability set. A class may be defined now and remain unregistered until a later milestone explicitly approves its behavior.

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

Relay-like behavior may appear only in a simulated development/test fixture used to validate capability semantics. v0.5.0 does not register production relay hardware, switch external power, or make relay control part of the release.

`LOCAL_PROCEDURE` is reserved descriptor/class vocabulary in v0.5.0. The release does not require a registered production procedure or introduce policy automation, workflow execution, scripts, bytecode, transferred procedures, or remote procedure execution.

### Required behavior

```text
GET_CAPABILITIES
READ_INPUT
READ_SENSOR
SET_INDICATOR
TRIGGER_OUTPUT
```

These names describe local semantic capability operations and validation behavior in v0.5.0. They are not Wire Protocol opcodes, packet schemas, or Host Protocol commands. v0.5.0 proves bounded local discovery, validation, authorization, safety checks, and invocation while preserving Wire Protocol `1`. v0.6.0 owns formal operation registration, radio-visible structured commands and responses, and Host Protocol behavior.

### Release gate

```text
[x] Node reports registered capabilities through a bounded local tested API
[x] Unsupported capabilities are rejected
[x] Local operations use logical IDs rather than arbitrary GPIO numbers
[x] Input or sensor data can be returned structurally
[x] Approved output can be controlled
[x] Local authorization and safety checks can deny an operation
[x] Radio and UI remain responsive during observed hardware operations
[x] GPIO4 ADC electrical characterization passed in the August 16 follow-up; production remains fail-closed
```

### Explicit non-goals

- Arbitrary GPIO access
- General remote code execution
- Hazardous actuators
- Transferred procedures
- Mesh behavior

---

## `v0.6.0` — Structured Operations, Responses, and Host Protocol

**Documentation/design status:** Complete.

**Implementation status:** Complete — physical qualification passed.

**Release status:** Complete.

The normative design package is the
[v0.6.0 Architecture Baseline](V0.6.0_ARCHITECTURE_BASELINE.md),
[Host Protocol 0.1](HOST_PROTOCOL_0.1.md), and
[v0.6.0 Wire Operation and Response Design](V0.6.0_WIRE_OPERATION_RESPONSE_DESIGN.md).
The [implementation brief](V0.6.0_IMPLEMENTATION_BRIEF.md) retains the
implementation sequence and acceptance gates.

### Objective

Move beyond `TEST` by defining structured REDLINE operations, establishing a developmental Host Protocol `0.1`, and proving a complete computer-to-device transaction without making firmware the owner of host-application meaning.

### Scope

- Formal opcode and payload registry.
- Structured command schemas.
- Structured response schemas.
- Explicit error model.
- Add `RESPONSE` and, if required, reserved future packet types.
- Capability-aware operation validation.
- Define developmental Host Protocol `0.1` with an extensible computer-to-Hub boundary.
- Add explicit Host/device capability negotiation for the behavior implemented in this milestone.
- Validate the initial Host Protocol codec and streaming parser, including bounded input and recovery after malformed data.
- Add a bounded machine-readable USB/serial Host Protocol endpoint and a
  separate minimal reference/test utility; do not turn the firmware endpoint
  into a human-oriented command shell.
- Add compiled local procedure invocation.

Wire Protocol 1 remains unauthenticated at the v0.6 entry baseline. Initial
production radio authority is therefore read-only/non-side-effecting. General
remote `SET_INDICATOR` and `RUN_PROCEDURE` authority remains disabled. A
separately reviewed compile-time laboratory build may enable one explicitly
bounded qualification path; such enablement is unauthenticated and is not
production authenticated authority. Authenticated side-effect authority remains
a later security concern.

Host Protocol `0.1` is an initial development interface, not the final stable Host Protocol. This milestone keeps Host Protocol framing, Wire Protocol packets, structured REDLINE operations, transport status, and host-application meaning as separate concerns. It does not add the general opaque application transport planned for v1.1 or the stable host-service lifecycle planned for v0.12. Its interfaces must avoid assumptions that would prevent those later milestones.

### Initial operations

```text
PING
GET_DEVICE_INFO
GET_STATUS
GET_CAPABILITIES
DESCRIBE_CAPABILITY
READ_CAPABILITY
SET_INDICATOR
RUN_PROCEDURE
GET_DIAGNOSTICS
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
[x] Each operation has a documented payload schema
[x] ACK and RESPONSE semantics are distinct
[x] Unsupported operations fail predictably
[x] Malformed payloads cannot reach hardware handlers
[x] Host framing resynchronizes after bad input
[x] Host Protocol codec/parser validation covers malformed, truncated, oversized, and concatenated input
[x] Host/device capability negotiation reports supported behavior without relying only on firmware-version inference
[x] Host Protocol and Wire Protocol versions remain independently reported and documented
[x] REDLINE transport status is not represented as an application outcome
[x] Full PC-to-Node-to-PC transaction succeeds
[x] Existing Wire Protocol version is retained or deliberately incremented
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

Provide a stable, self-hosted, application-neutral REDLINE service/driver boundary without embedding ARGUS or other host-application assumptions directly into firmware.

### Scope

- Reference REDLINE host service/driver; `redlined` may be used as a candidate implementation name but is not an interface requirement.
- Stable Host Protocol/service boundary, serial framing, and reconnect behavior.
- Host/device capability negotiation and compatibility reporting.
- Host transaction tracking with bounded, documented retention behavior.
- Authoritative reconciliation of accepted work after device reconnect.
- Explicit handling of device-reset ambiguity without inventing success or failure.
- Idempotent Host submission within a documented transaction scope so safe retries cannot create duplicate physical actions.
- Device registry persistence.
- Command submission.
- Response and event ingestion.
- Transaction and audit logging.
- Capability visibility.
- Presence and last-seen state.
- Narrow local API.
- Bounded support for multiple local clients where required by the narrow local API, without embedding client application schemas in REDLINE.
- Host Protocol/API `1.0` candidate.
- Documented ARGUS integration boundary.
- Stateful Hub simulation for Host Protocol and transaction-lifecycle validation.
- Virtual serial or equivalent fault injection for corruption, partial I/O, disconnect, reconnect, and device-reset scenarios.
- Host Protocol/service conformance tests and hardware-in-the-loop validation of the implemented Host/service behavior.

### Possible service flow

```text
ARGUS or local operator tool
→ REDLINE host API
→ application-neutral REDLINE host service / driver
→ Hub radio
→ field Node
```

The service boundary must remain capable of supporting the general opaque application transport planned for v1.1, but v0.12 does not introduce that complete transport, its fragmentation/reassembly system, or a stable third-party application-transport interface.

### Release gate

```text
[ ] Service reconnects after Hub USB disconnect
[ ] Serial corruption does not permanently desynchronize framing
[ ] Reconnect reconciles accepted transactions against authoritative Hub state where that state remains available
[ ] A Hub reset or lost authoritative record produces an explicit ambiguous/unknown outcome rather than assumed success or failure
[ ] Commands and events have stable host-side identities
[ ] Duplicate Host submissions are idempotent within the documented transaction scope and do not create duplicate physical actions
[ ] Host/device capabilities are negotiated and exposed through the service boundary
[ ] Simulator and virtual-serial fault scenarios cover connection loss, reconnect, reset, stale responses, and duplicate submissions
[ ] The implemented Host Protocol/service behavior passes hardware-in-the-loop conformance validation
[ ] Device registry persists across service restart
[ ] API schemas are documented
[ ] Firmware can operate without the full ARGUS platform
```

### Explicit non-goal

Full production ARGUS workflow integration is not required for v1. General opaque application payload transport, substrate fragmentation/reassembly for that transport, and the stable third-party application interface remain v1.1 work.

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

Post-v1 ordering is directional and may change as field use identifies real needs. These milestones describe architectural direction, not implemented behavior or a binding release commitment. REDLINE v1 remains the stable direct-network substrate; post-v1 work expands it in layers without moving modules, relays, routing, or mesh into the v1 promise.

> Do not implement modules before v1. Do not design v1 in a way that makes modules impossible after v1.

The directional expansion order is:

```text
open application transport
→ detachable module protocol
→ detachable field hardware
→ open module ecosystem
→ bounded field automation
→ disconnected/store-and-forward operation
→ group and distributed security
→ deliberate relays
→ experimental routing
→ stable distributed v2 platform
```

## `v1.1.0` — Open Application Transport Expansion

### Objective

Expand the stable computer-facing REDLINE interface into a general application transport without moving application logic into embedded firmware.

### Scope and direction

- Application namespaces or endpoints above the device transport.
- Multiple local applications sharing one Hub through explicit endpoint registration.
- Durable host-side queues and restart recovery.
- General opaque application payload submission without embedding application schemas in REDLINE firmware.
- Optional host/application-level chunking or streaming when an application divides its own logical objects for application-level reasons.
- REDLINE substrate-owned transport fragmentation/reassembly when physical packet geometry, MTU, or link constraints require a submitted payload to be divided.
- Resumable transfers where message class, storage, and link behavior justify them.
- Delivery classes with documented reliability semantics.
- Priority, TTL, expiration, and bounded embedded queue behavior.
- Clean separation between REDLINE transport status and application-defined status.
- An application SDK or reference client against the documented host API.
- Richer local endpoint registration, lifecycle, and error reporting.
- ARGUS as an important reference consumer of the interface, not a firmware dependency.

### Architectural boundary

```text
embedded REDLINE
  identity, authentication, radio scheduling, bounded queues,
  retries, transport state, and physical-transport
  fragmentation/reassembly

host service
  application-neutral payload submission, durable host queues,
  endpoint registration, client multiplexing, and local API

application
  payload meaning, optional application-object chunking/streaming,
  workflow, databases, automation,
  permissions, and operator UI
```

REDLINE must remain useful to applications other than ARGUS. Embedded firmware does not acquire application databases, workflow engines, human-facing schemas, or ARGUS-specific assumptions.

Host/application contracts must not permanently encode assumptions tied to LoRa frame geometry. The host submits a logical application payload without managing LoRa fragments; REDLINE divides and reassembles it when required by the active physical transport. A future physical transport with different framing or MTU must not require application protocol redesign. Any application-level chunking remains distinct from this transport function.

### Directional release criteria

- Multiple local applications can register and use isolated namespaces through the host service.
- Host restart does not silently discard messages whose delivery class promises durability.
- Applications can submit logical payloads without understanding physical-transport fragment geometry.
- Transport fragmentation/reassembly is bounded, substrate-owned, and validated without moving application meaning into firmware.
- Transport completion and application acceptance remain distinguishable.
- Existing direct-network security, bounded queues, and radio timing remain authoritative.

## `v1.2.0` — REDLINE Module Protocol Foundation

### Objective

Define the first transport-independent detachable smart-module protocol without requiring finished custom handheld hardware.

### Scope and direction

Introduce the REDLINE Module Protocol (RMP) with an initial vocabulary such as:

```text
DISCOVER
IDENTIFY
CAPABILITIES
CONFIG
START
STOP
STATUS
SAMPLE
EVENT
ERROR
```

RMP direction includes:

- Module discovery and stable module identity.
- Bounded capability manifests using the established capability model.
- Protocol and capability compatibility/version negotiation.
- Explicit module permissions and lifecycle states.
- Attach, identify, register, operate, stop, detach, and reconnect semantics.
- Module failure isolation and bounded malformed-input handling.
- Generic module events and structured module errors.
- No direct module access to LoRa credentials or unrestricted radio control.
- No arbitrary filesystem access or unrestricted GPIO access.
- Explicit policy and permission before location access.
- Continued REDLINE operation when a module is absent, incompatible, malformed, unresponsive, or removed.

RMP is defined independently of its physical transport. The first proof should use a simple development transport such as UART rather than waiting for final USB-host hardware.

The reference module should be deliberately simple: temperature/humidity or another bounded sensor, one button/input, and one LED/output. The proof tests protocol lifecycle and isolation rather than application novelty.

### Directional release criteria

- Attach, identify, capability registration, operation, disconnect, and reconnect complete deterministically.
- Incompatible versions and malformed module behavior fail safely and visibly.
- Module disappearance cannot destabilize radio, UI, settings, or onboard capabilities.
- A module cannot obtain credentials, unrestricted radio, filesystem, GPIO, or location access through RMP.
- The same logical protocol works independently of the initial UART proof transport.

## `v1.3.0` — Detachable Smart Module Platform

### Objective

Turn the RMP proof into a field-usable detachable hardware architecture.

### Scope and direction

- Select and validate a production module transport, such as USB host or another appropriate physical interface.
- Hot attach/detach and deterministic module-present detection.
- Switched module power, current limiting, and power monitoring.
- ESD, overcurrent, reverse-power, and fault protection appropriate to the interface.
- Mechanical retention, connector-cycle, environmental, and field-service requirements.
- Module lifecycle, status, configuration, firmware version, and compatibility visibility in device/host UI.
- A finished handheld hardware profile and custom PCB/enclosure work where required.
- Antenna, battery, charging, thermal, and power-budget documentation for the module-host device.
- Vehicle power, mounting, transient protection, antenna, and mobile-gateway considerations where justified.

Current Heltec development boards do not need to become the final module-host hardware. The production interface must support detachable operation and must not assume permanent attachment.

```text
Capability Registry
    |
    +-- native/onboard capability
    |
    +-- RMP smart-module capability
```

REDLINE consumes the bounded capability/event contract. It does not need to understand whether a module internally uses SPI, I2C, UART, a proprietary sensor bus, or another implementation detail.

### Directional release criteria

- Hot attach/detach and module power faults do not reset or destabilize REDLINE.
- Onboard and external capabilities appear through one higher-level model where practical.
- Module status and incompatibility remain inspectable without a full application-specific UI.
- Electrical protection, current limits, power behavior, connector retention, and recovery are physically characterized.

## `v1.4.0` — Module SDK and Open Hardware Ecosystem

### Objective

Allow third-party developers to create REDLINE-compatible modules without modifying REDLINE core firmware.

### Scope and direction

- Publish the RMP specification and compatibility rules.
- Provide a module developer SDK and reference module firmware.
- Publish reference schematics and electrical/interface requirements.
- Define the module manifest and capability/event schema rules.
- Provide compatibility and conformance test suites.
- Provide module validation, lifecycle, and self-test tooling.
- Document module permissions, failure isolation, update expectations, and review boundaries.
- Publish example sensor, input, output, and storage modules without privileging one application.
- Document a path for future environmental sensing, GNSS, radiation, storage, networking, RF sensing, and currently unknown module classes.

The architectural test is that a developer unfamiliar with RaveGoat Labs can implement the documented interface, attach a module, and have its approved capabilities appear without changing REDLINE core firmware.

Specialized projects such as BLACKSHEEP may consume this platform as an application/capability family. REDLINE must not contain BLACKSHEEP-specific application logic. The core interface remains intentionally generic; specialized behavior belongs in modules and host applications.

### Directional release criteria

- At least one independently implemented module passes published conformance tests.
- Module manifests and events are bounded, versioned, and rejected safely when incompatible.
- Adding a conforming module requires no REDLINE core firmware modification.
- SDK examples do not grant unrestricted GPIO, filesystem, radio, credential, or location access.

## `v1.5.0` — Signed Policies, Procedures, and Field Automation

### Objective

Allow approved native and module-provided capabilities to participate in bounded local automation without enabling arbitrary remote native-code execution.

### Scope and direction

- A constrained declarative policy, workflow representation, DSL, or bounded bytecode.
- Signed procedure or policy packages.
- Explicit compatibility and capability requirements.
- Chunk validation and recovery for multi-part installation.
- Transactional installation, activation, rollback, and removal.
- Execution-time, instruction, memory, storage, and event-rate limits.
- Capability permissions and policy-specific authorization.
- Audit records for installation, activation, denial, execution, and failure.
- Local event → approved procedure invocation.
- Approved capability operation → structured event generation.
- Consistent interaction between native and RMP module capabilities.

This milestone never transfers unrestricted native executable code. Sandboxed host logic or WebAssembly may be explored later, but is not a v1.5.0 requirement and does not weaken embedded execution limits.

### Directional release criteria

- Unsigned, incompatible, over-quota, or unauthorized packages are rejected transactionally.
- Interrupted installation preserves the previous working state or recovers predictably.
- Execution cannot address undeclared capabilities or exceed documented limits.
- Native and module capabilities use the same authorization, safety, audit, and result boundaries.

## `v1.6.0` — Store-and-Forward and Disconnected Operation

### Objective

Preserve and eventually deliver useful information through temporary network partitions.

### Scope and direction

- Persistent message custody with explicit ownership and restart recovery.
- TTL/expiration, delivery receipts, and delayed-event classification.
- Queue priority, storage quotas, pressure behavior, and garbage collection.
- Duplicate-safe delayed delivery and idempotent receipt handling.
- Dead-drop synchronization and synchronization after missed updates.
- Data-mule and mobile collection behavior for physically separated areas.
- Historical handling of data received from devices later revoked.
- Bounded recovery after restart, interrupted synchronization, or partial transfer.
- Inspectable custody, age, expiration, and failure state.

This is the directional milestone where handheld or mobile Nodes may physically carry queued information between disconnected areas. Custody does not imply that expired, revoked, unauthenticated, or policy-denied data becomes actionable.

### Directional release criteria

- Restart and interrupted synchronization preserve custody invariants.
- TTL, priority, duplicate handling, quotas, and garbage collection are deterministic under storage pressure.
- Delivery receipts cannot be confused with application acceptance.
- Later revocation and delayed historical data have documented handling.

## `v1.7.0` — Group Security and Delegated Relay Authorization

### Objective

Introduce the distributed security prerequisites required by disconnected delivery, forwarding, and future group traffic before general relay or routing behavior.

### Scope and direction

- Group or event credentials only where the communication model requires them.
- Key-generation or epoch model and explicit group membership.
- Rekeying and disconnected-device resynchronization.
- Stale-generation rejection and replay boundaries.
- Delegated relay authorization with bounded forwarding authority.
- Management-authority signatures and credential rotation.
- Revocation behavior across delayed or disconnected network segments.
- Recovery for devices that miss group/key updates.
- Auditable distinction between origin authentication, relay custody, and delivery.

Pairwise security remains appropriate for direct relationships. Group/distributed mechanisms are added only where forwarding, group traffic, or disconnected operation requires them; they do not replace direct-device security without evidence.

### Directional release criteria

- Unauthorized relays cannot acquire forwarding authority by observing traffic.
- Stale epochs, removed members, and replayed group traffic fail predictably.
- Disconnected rejoin/rekey and revocation behavior are documented and field-tested.
- Origin, relay, and management authority remain cryptographically distinguishable.

## `v1.8.0` — Stationary and Mobile Relays / Gateways

### Objective

Introduce deliberate constrained forwarding before general mesh routing.

### Scope and direction

- Stationary relay Nodes and mobile relay/gateway Nodes.
- Controlled forwarding under explicit relay identity and authorization.
- Hop limits, duplicate-propagation suppression, and loop prevention.
- Bounded forwarding queues, custody records, priority, and expiration.
- Congestion controls and airtime governance.
- Relay health, capacity, authorization, queue, and link status.
- Vehicle/mobile gateway power, mounting, antenna, connectivity, and field profiles where justified.
- Measured stationary and mobile relay field testing.

The first forwarding topology should be simple and known:

```text
Node
→ Relay
→ Hub
```

This is not general mesh routing. Dynamic path selection, arbitrary forwarding, and unbounded broadcast remain excluded.

### Directional release criteria

- One-hop relay forwarding is authorized, bounded, duplicate-safe, and loop-safe.
- Relay loss or queue pressure produces documented recovery rather than network-wide instability.
- Airtime and congestion behavior are measured under representative field loads.
- Mobile gateway behavior does not bypass custody, identity, authorization, or privacy policy.

## `v1.9.0` — Experimental Distributed Routing

### Objective

Experimentally extend the proven relay model into dynamic distributed routing.

### Scope and direction

- Neighbor state and bounded route discovery.
- Explicit route representation, metrics, path selection, and expiration.
- Loop prevention, hop limits, and path-failure recovery.
- Network-partition and merge behavior.
- Congestion, broadcast, and airtime controls.
- Routed event/message identity and duplicate suppression.
- Routed replay protection and security through forwarding Nodes.
- Multi-relay laboratory and field testing.
- Diagnostics sufficient to explain route choice, failure, congestion, and recovery.

No routing algorithm is selected by this roadmap. Algorithm choice must be informed by measured LoRa behavior, security analysis, failure testing, and actual deployment geometry.

This milestone remains explicitly experimental and does not constitute a stable mesh promise.

### Directional release criteria

- Multiple candidate routing approaches are evaluated against documented measurements.
- Loops, stale routes, partitions, congestion, replay, and relay compromise have bounded tested behavior.
- Multi-relay field evidence is sufficient to decide whether a stable v2 routing contract is supportable.

## Cross-cutting post-v1 directions

### ARGUS and other host applications

ARGUS is an important reference/operator application consuming the stable Host API and open application transport. Device-registry synchronization, command submission, event intake, check-ins, alert review/escalation, zones, assignments, audit integration, role-based access, and offline Hub operation belong in ARGUS or host services—not embedded REDLINE assumptions.

REDLINE remains application-agnostic. NIGHTWATCH, if explored, is another application that may transport compact evidence metadata or status through REDLINE; this does not imply media transfer over LoRa.

### Handheld, vehicle, and module-host hardware

Finished handheld enclosure, connector, antenna, battery, charging, thermal, and interaction requirements belong primarily to the detachable module platform. Vehicle power, transient protection, mounting, antenna, and mobile-gateway requirements belong to the module-host and relay/gateway milestones. These profiles consume the stable REDLINE substrate rather than expanding the v1 promise retroactively.

### GNSS and location privacy

GNSS/location is an approved `LOCATION_PROVIDER` capability that may be onboard or module-provided. Access requires explicit authorization, privacy policy, lifecycle, and visibility. Continuous tracking is not a core REDLINE requirement, and location data is not exposed to a module or application merely because hardware is present.

### Specialized module families

BLACKSHEEP may be an example specialized future module/capability family. It is neither a firmware dependency nor the architectural reason for RMP. Environmental sensing, radiation, storage, networking, RF sensing, and unknown future modules use the same generic manifest, capability, permission, lifecycle, and failure-isolation rules.

---

# `v2.0.0` — Stable Distributed and Mesh-Capable Platform

`v2.0.0` is earned rather than scheduled. It represents the point where the secure, application-agnostic, capability-driven direct platform created in v1 operates reliably across a documented, secured, field-tested distributed network.

### v2 promise

```text
Stable direct and relayed communication
Controlled multi-hop routing
Store-and-forward delivery
Stationary and mobile relays
Group membership and epoch-based rekeying where required
Revocation across disconnected network segments
Signed and bounded transferable procedures
Open application transport
Stable module and capability architecture
ARGUS operational integration through documented interfaces
Network-wide diagnostics
Measured congestion and airtime behavior
Documented partition and recovery behavior
Stable routing, security, module, application-transport, and procedure-package interfaces
```

v2.0.0 does not promise every possible module, application, sensor, workflow, or deployment profile. REDLINE remains the network and capability substrate; host applications and modules retain their own semantics and release lifecycles.

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

After publication of the completed v0.6.0 release candidate, the next planned milestone is:

```text
v0.7.0
Node-Originated Events and Reliable Delivery
```

Immediate direction:

```text
[x] Complete v0.6.0 implementation and physical qualification
[x] Preserve F-02, F-03, and F-04 as authenticated-transport requirements
[x] Complete v0.6.0 lifecycle and release documentation
[x] Publish v0.6.0
[ ] Begin v0.7.0 design and implementation through its own approved scope and gates
```

v0.7.0 introduces Node-originated structured traffic and reliable delivery. It does not pull forward persistent identity, authenticated transport, multi-Node networking, routing, repeaters, or mesh behavior.
