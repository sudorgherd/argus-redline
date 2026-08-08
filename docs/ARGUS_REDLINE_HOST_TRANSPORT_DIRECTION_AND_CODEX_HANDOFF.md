# ARGUS REDLINE — Host Transport Direction & Codex Integration Handoff

**Status:** Architectural direction / implementation-planning handoff  
**Date:** 2026-08-08  
**Project:** ARGUS REDLINE  
**Purpose:** Preserve the clarified host-transport architecture, explain its relationship to the existing REDLINE foundation, and provide a safe basis for future repository documentation and roadmap updates.

> **Important:** This document records an architectural direction and a detailed candidate design. It is **not** a declaration that Host Protocol v1 is implemented, finalized, or frozen. Exact framing choices, opcodes, limits, and lifecycle semantics remain subject to validation when their implementation milestone is reached.

---

## 1. Executive Summary

ARGUS REDLINE has evolved from a small point-to-point `COMMAND / ACK / ERROR` LoRa control system toward a broader role:

> **A resilient, application-agnostic communications and capability substrate whose current physical transport is LoRa.**

The important architectural boundary is:

- **Host applications own meaning.**
- **REDLINE owns transport.**

Applications such as ARGUS, BLACKSHEEP, NIGHTWATCH, future command-line tools, sensor systems, or third-party software should not need to understand LoRa packet sizing, retry loops, fragmentation, physical radio configuration, route behavior, or transport security details.

Likewise, REDLINE firmware should not grow into a monolithic application platform containing ARGUS-specific dispatch logic, BLACKSHEEP-specific sensing semantics, NIGHTWATCH-specific evidence models, or arbitrary third-party business logic.

The long-term model is closer to an **off-grid network interface / transport peripheral**:

```text
┌────────────────────────────────────────────────────┐
│                    HOST SYSTEM                     │
│                                                    │
│  ARGUS / BLACKSHEEP / NIGHTWATCH / Third Party    │
│                       │                            │
│                  local IPC/API                     │
│                       ▼                            │
│                    redlined                        │
│          host-side driver / state service          │
└───────────────────────┬────────────────────────────┘
                        │
                  Host Protocol
                    over USB
                        │
┌───────────────────────▼────────────────────────────┐
│                  REDLINE HUB                      │
│                                                   │
│  transport framing / scheduling / fragmentation  │
│  retries / delivery / identity / crypto / radio  │
└───────────────────────┬────────────────────────────┘
                        │
                       LoRa
                        │
               REDLINE peers/nodes
```

This direction **does not invalidate the existing REDLINE work**. It clarifies what the upper layers eventually need to become.

The current runtime, radio, duplicate handling, diagnostics, persistent settings, device UI, capability registry, and implementation-boundary work remain directly useful.

The main future additions are:

1. opaque application transport;
2. a formal host-device protocol;
3. substrate-level fragmentation/reassembly;
4. transaction and delivery lifecycle semantics;
5. a small host-side service (`redlined`);
6. reconnect/reconciliation behavior;
7. simulator, fuzzing, and hardware-in-the-loop conformance testing;
8. eventual host API integration for ARGUS and other applications.

---

# 2. What Changed — and What Did Not

## 2.1 What did **not** change

The following principles remain intact:

- REDLINE remains an embedded communications substrate.
- LoRa remains the current physical transport.
- Radio operation, retry behavior, transport integrity, and constrained-device concerns belong primarily in firmware.
- Application behavior belongs on the host wherever practical.
- Existing releases remain valid historical milestones.
- Existing work should not be rewritten merely because the future architecture is clearer.
- Firmware size and conceptual scope should remain controlled.
- Heavy orchestration, databases, user-facing state, application policy, and long-lived records should live on the host.

The architecture should continue to protect the core constraint:

> **Do not migrate host/application complexity into firmware merely because the Host Protocol becomes more capable.**

---

## 2.2 What actually evolved

The original implementation model is roughly:

```text
Host/device action
      │
      ▼
COMMAND opcode
      │
      ▼
predefined firmware behavior
      │
      ▼
ACK / ERROR
```

That remains useful as an implementation stage, but it is too restrictive as the permanent application boundary.

The clarified future model is:

```text
application meaning
      │
      ▼
opaque application bytes
      │
      ▼
REDLINE transport substrate
      │
      ├── fragmentation
      ├── retries
      ├── delivery tracking
      ├── routing later
      ├── store-and-forward later
      └── security
```

The key shift is therefore **not** “replace everything.”

It is:

> **Generalize the upper transport boundary so REDLINE can carry multiple applications without embedding those applications into the firmware.**

---

# 3. Why This Direction Is Necessary

Once REDLINE is expected to behave as an application-neutral transport layer, several engineering problems become unavoidable.

If an application submits:

```text
SEND(peer=0x002A, payload=180 bytes)
```

while the physical radio frame can only carry a much smaller payload, some layer below the application must own:

- fragmentation;
- fragment numbering;
- retry behavior;
- missing-fragment recovery;
- reassembly;
- duplicate suppression;
- delivery confirmation;
- failure reporting;
- reconnect state.

Likewise, if the host sends a transaction to the Hub and the USB cable disappears, the system must define whether the transaction:

- continues;
- is cancelled;
- is unknown;
- is delivered;
- is retried;
- is safe to resubmit.

These are not problems introduced by the proposed Host Protocol.

They are inherent in the desired result.

The architecture work below is therefore best understood as **early discovery of requirements that the existing direction would eventually encounter**.

---

# 4. Proposed Component Boundaries

## 4.1 Host Applications

Examples:

- ARGUS
- BLACKSHEEP
- NIGHTWATCH
- CLI tooling
- diagnostics utilities
- future third-party applications

Responsibilities:

- application semantics;
- UI;
- databases;
- user/session policy;
- domain-specific behavior;
- long-lived application state;
- deciding what an opaque payload means;
- deciding whether an ambiguous transaction may be safely retried.

Applications should not need to understand:

- spreading factor;
- LoRa payload limits;
- physical fragmentation;
- radio retries;
- radio frame formats;
- route behavior;
- link-layer retransmission timing.

---

## 4.2 `redlined`

`redlined` is the proposed host-side REDLINE service/driver.

It should remain relatively small and application-neutral.

Potential responsibilities:

- serial device discovery/open/reopen;
- Host Protocol codec and validation;
- Hub handshake and capability negotiation;
- host session identity;
- transaction bookkeeping;
- reconnect and reconciliation workflow;
- client multiplexing;
- IPC/API exposure;
- local diagnostics;
- mapping Hub transport outcomes to application-facing state;
- retaining sufficient host-side history to reason about ambiguous resets.

It should **not**:

- interpret ARGUS alerts;
- understand BLACKSHEEP signal-event semantics;
- implement NIGHTWATCH evidence models;
- fragment payloads according to LoRa PHY settings;
- implement application business rules;
- duplicate radio transport logic that belongs in firmware.

Conceptually:

```text
Application
    │
    ▼
 redlined
(driver / host network service)
    │
    ▼
REDLINE Hub
(transport engine)
```

---

## 4.3 REDLINE Hub Firmware

The Hub should own transport behavior that depends on the physical network:

- radio framing;
- scheduling;
- fragmentation/reassembly;
- retransmission;
- duplicate suppression;
- transport delivery;
- radio/session security;
- peer/network identity mapping;
- store-and-forward behavior;
- route logic;
- constrained-device queues;
- current transport state.

This maintains the abstraction:

> The host submits application data. REDLINE gets it there according to negotiated transport capabilities.

---

# 5. Host Protocol Direction

A formal Host Protocol will eventually be required between `redlined` and the Hub.

The following is a **candidate design**, not a frozen implementation contract.

---

## 5.1 Candidate Frame Envelope

The strongest current candidate is:

```text
COBS_ENCODE(
    VERSION
    TYPE
    FLAGS
    TXN_ID
    PEER_ID
    LENGTH
    TYPE_SPECIFIC_BODY
    CRC32C
)
0x00
```

Rationale:

- serial is a byte stream;
- COBS provides deterministic frame delimitation;
- `0x00` becomes an unambiguous delimiter;
- parser recovery does not depend on scanning for a magic sequence inside arbitrary data;
- a corrupted `LENGTH` field cannot force the receiver to wait indefinitely for a fictional body length;
- stream recovery after garbage is straightforward.

Candidate raw frame layout:

```text
Offset  Size  Field
------  ----  ------------------
0       1     VERSION
1       1     TYPE
2       1     FLAGS
3       2     TXN_ID
5       2     PEER_ID
7       2     LENGTH
9       N     TYPE_SPECIFIC_BODY
9+N     4     CRC32C
```

All multi-byte integer fields would be explicitly encoded in **little-endian** byte order.

The protocol must not describe this as “native endianness.” Wire order must be deterministic regardless of processor architecture.

---

## 5.2 Candidate Payload Limit Rules

The `uint16_t LENGTH` field can represent values from `0` through `65535`.

That does **not** imply every device must support a 65,535-byte body.

A future specification should state:

```text
LENGTH is uint16_t and represents 0..65535 bytes.

Each Hub advertises MAX_HOST_PAYLOAD during capability negotiation.

A sender MUST NOT submit a TYPE_SPECIFIC_BODY larger than the
negotiated MAX_HOST_PAYLOAD.

The protocol does not require an implementation to support the
full uint16_t representable range.
```

The current likely embedded limit might be 512 or 1024 bytes, but that should remain a **device capability**, not a permanent protocol-version ceiling.

---

# 6. Candidate Operational Flags

Potential v1 flags:

```text
bit 0  CONFIRMED_DELIVERY
bit 1  PRIORITY
bit 2  STORE_FORWARD_ALLOWED
bit 3  RESERVED
bit 4  RESERVED
bit 5  RESERVED
bit 6  RESERVED
bit 7  RESERVED
```

Proposed semantics:

### `CONFIRMED_DELIVERY`

Requests a terminal delivery outcome from the REDLINE substrate according to the capabilities negotiated for the destination peer.

This intentionally does **not** mean only “single-hop radio ACK.”

Future routing, store-and-forward, or other transports may define delivery differently.

### `PRIORITY`

Requests elevated scheduling priority according to Hub traffic policy.

It does not guarantee queue bypass and must not override:

- active physical operations;
- regulatory constraints;
- higher-priority traffic;
- safety limits;
- fairness policy.

### `STORE_FORWARD_ALLOWED`

Indicates that the application permits the substrate to retain the payload for delayed delivery according to configured policy.

### Reserved bits

Future specification should require:

```text
Transmitters MUST set unsupported/reserved bits to zero.

Receivers MUST reject unsupported non-zero reserved bits unless
capability negotiation has explicitly assigned semantics to them.
```

Silent ignoring of unknown transport flags should be avoided.

---

# 7. Candidate Opcode Namespace

The following map emerged during the design review and is useful as a planning candidate:

```text
0x01 - 0x0F : Session / system controls
0x10 - 0x1F : Host -> Hub actions
0x80 - 0x8F : Hub -> Host responses / indications
0x90 - 0x9F : Asynchronous network / telemetry events
```

Candidate operations:

```text
0x01 SYS_HELLO_REQ
0x02 TX_STATE_REQ

0x10 TX_DATA_REQ
0x12 TX_CANCEL_REQ
0x14 LINK_STATUS_REQ
0x16 QUEUE_STATUS_REQ
0x17 QUEUE_PURGE_REQ

0x80 TX_STATUS_IND
0x81 SYS_HELLO_RESP
0x82 TX_STATE_RESP
0x83 TX_CANCEL_RESP
0x84 LINK_STATUS_RESP
0x86 QUEUE_STATUS_RESP
0x87 QUEUE_PURGE_RESP

0x90 RX_DATA_IND
0x91 LINK_DIAG_IND
```

These values are **not yet authoritative**.

The important design requirement is stable ranges and non-collision.

Unknown `TYPE` values that pass framing and CRC checks should be treated as **unsupported operations**, not serial corruption.

The receiver should remain synchronized.

---

# 8. Capability Negotiation

The Host Protocol should not infer Hub capabilities from firmware version numbers.

A handshake should answer:

> **What can this device do right now?**

Candidate `SYS_HELLO_RESP` information:

- Host Protocol version;
- firmware version/build identifier;
- Hub `BOOT_ID`;
- maximum supported Host Protocol payload;
- queue capacity;
- supported feature flags;
- fragmentation capability;
- confirmed-delivery capability;
- store-and-forward capability;
- security state/capability;
- diagnostic capability.

This avoids brittle logic such as:

```text
firmware 0.8.2 => assume fragmentation exists
```

and replaces it with explicit capability discovery.

---

# 9. Transaction Lifecycle

A central design rule emerged:

> **Once the Hub accepts a transaction, the Hub owns it.**

The physical serial connection is no longer the transaction’s lifetime.

Conceptually:

```text
HOST SUBMITS TX_DATA_REQ
        │
        ▼
Hub validates request
        │
        ├── rejected
        │
        └── ACCEPTED
              │
              ▼
        Hub owns transaction
              │
       ┌──────┼────────┐
       ▼      ▼        ▼
  DELIVERED FAILED  CANCELLED
```

---

# 10. Core Lifecycle Invariants

These principles are stronger than any particular opcode assignment and should be preserved even if the exact Host Protocol changes.

## 10.1 Sovereignty Invariant

> Once a transaction reaches `ACCEPTED`, loss of the host serial connection must not implicitly cancel or alter its execution over the transport.

A loose USB cable must not destroy a field transaction.

---

## 10.2 Authoritative Invariant

> The Hub is authoritative for transport outcomes.

`redlined` must not declare a radio delivery failure merely because a host-side timer expired.

Host watchdog state and radio delivery state are distinct.

For example:

```text
HUB_UNRESPONSIVE
```

does not imply:

```text
TX_FAILED
```

---

## 10.3 Explicit Reconciliation Invariant

> Reconnection must never convert unknown state into assumed success or assumed failure.

If the daemon loses contact with the Hub, transaction state must be explicitly reconciled where possible.

---

## 10.4 Provenance Invariant

A Hub that reboots and loses its volatile transaction table cannot claim knowledge it no longer has.

If an accepted transaction existed before the reboot and the Hub has no persistent record afterward:

```text
Hub response: NOT_FOUND
```

But the daemon may know:

- it submitted the transaction;
- it was accepted;
- the Hub boot identity changed;
- the Hub now has no record.

The host can therefore derive:

```text
OUTCOME_UNKNOWN_AFTER_RESET
```

This is an application-facing interpretation of known facts, **not a Hub transport result**.

---

# 11. Host Session and Boot Epoch Model

A useful conceptual identity emerged:

```text
Transaction Identity =
(
    HOST_SESSION_ID,
    BOOT_ID,
    TXN_ID
)
```

These values do not necessarily need to appear together in every frame.

They define the transaction epoch.

---

## 11.1 `HOST_SESSION_ID`

Generated when a logical `redlined` process/session starts.

Rules:

- daemon process starts -> generate a new random non-zero ID;
- USB disconnect -> retain ID;
- USB reconnect -> reuse ID;
- daemon crash/redeploy/new process -> generate new ID.

A new ID should represent a new logical host-session namespace.

---

## 11.2 `BOOT_ID`

Generated or otherwise uniquely changed when the Hub boots.

The daemon can use it to distinguish:

```text
same daemon + same BOOT_ID
=> transient wire interruption
```

from:

```text
same daemon + changed BOOT_ID
=> Hub execution epoch changed
```

That difference drives reconciliation behavior.

---

## 11.3 `TXN_ID`

Likely a host-generated `uint16_t`.

Possible rule:

```text
0x0000 reserved for unsolicited/no-request events
0x0001..0xFFFF host transaction identifiers
```

Reuse rules must be defined around active/cached transaction windows.

---

# 12. Idempotent Submission

This is a critical requirement.

Serial behavior may create the situation:

1. `redlined` writes `TX_DATA_REQ`;
2. Hub receives and accepts it;
3. serial stalls before `redlined` sees `ACCEPTED`;
4. host cannot tell whether the request was accepted.

A naive resend could cause duplicate physical delivery.

Therefore:

> Within the relevant transaction epoch, repeating an already accepted `TX_DATA_REQ` with the same transaction identity must not create a second radio transaction.

Candidate behavior:

```text
same transaction ID + same body:
    replay current/cached status

same transaction ID + different body:
    TXN_CONFLICT
```

This should eventually become a hard conformance requirement.

---

# 13. Cancellation and Queue Behavior

Disconnect must not imply cancellation.

Cancellation should always be explicit and best-effort.

Potential operation:

```text
TX_CANCEL_REQ
```

Possible outcomes:

```text
CANCELLED
TOO_LATE
NOT_FOUND
```

A Hub cannot unsend a payload that has already reached the destination.

Queue purge should likewise be explicit.

Potential modes:

```text
QUEUED_ONLY
ALL_CANCELABLE
```

A response should truthfully report what was actually removed and what could not be cancelled.

---

# 14. Reboot Semantics

Normal volatile transactions should not be treated as persistent unless explicitly accepted into a durable store-and-forward system.

A future implementation might distinguish:

```text
volatile active transaction
persistent store-forward transaction
```

On an uncontrolled Hub reset:

```text
volatile state:
    may disappear

persistent store-forward state:
    may be recovered if integrity can be verified
```

The system must not fabricate certainty where none exists.

`FAILED_DEVICE_RESET` should only be used if the Hub can authoritatively establish that the reset caused a failure.

Otherwise the host should preserve ambiguity.

---

# 15. Fragmentation Ownership

The host application should not fragment application payloads into LoRa-sized chunks.

`redlined` should also normally avoid radio-PHY-specific fragmentation.

Preferred boundary:

```text
Application
    │
    │ opaque payload
    ▼
redlined
    │
    │ Host Protocol payload
    ▼
REDLINE Hub
    │
    ├── fragmentation
    ├── scheduling
    ├── retry
    ├── fragment ACK/recovery
    └── transport delivery
```

This ensures that future physical transports can evolve without requiring every host application or daemon layer to understand their packet geometry.

---

# 16. Link Diagnostics

Link diagnostics should be based on actual observations, not continuous polling of a silent peer.

RSSI/SNR are meaningful when packets are received.

Preferred model:

- attach receive metrics to inbound data;
- maintain a bounded peer diagnostic cache;
- allow on-demand snapshot queries;
- emit asynchronous events only for meaningful state changes.

Potential cached fields:

```text
PEER_ID
LAST_RX_RSSI
LAST_RX_SNR
LAST_RX_TIME
LAST_TX_RESULT
LAST_SEEN_TIME
RX_COUNT
TX_SUCCESS_COUNT
TX_FAILURE_COUNT
RETRY_COUNT
```

---

## 16.1 Candidate `RX_DATA_IND` typed body

Instead of appending metadata outside the Host Protocol payload, use a typed body:

```text
RX_FLAGS        uint8
RSSI_DBM        int16
SNR_DB_QUARTER  int16
APP_LENGTH      uint16
APP_DATA        byte[APP_LENGTH]
```

The outer Host Protocol `LENGTH` represents the entire typed body.

Only `APP_DATA` is opaque application data.

`SNR_DB_QUARTER` means:

```text
1 integer unit = 0.25 dB

11.25 dB => 45
-3.50 dB => -14
```

This avoids floating-point wire representations.

---

# 17. Parser Safety

The Host Protocol parser should treat serial input as untrusted.

Candidate validation order:

```text
accumulate bytes until 0x00
        │
        ▼
encoded-size bounds check
        │
        ▼
COBS decode
        │
        ▼
minimum structural length
        │
        ▼
VERSION validation
        │
        ▼
FLAGS validation
        │
        ▼
LENGTH validation
        │
        ▼
actual decoded size matches LENGTH
        │
        ▼
CRC32C validation
        │
        ▼
TYPE dispatch
```

A corrupted frame must destroy only itself.

The next valid frame must remain recoverable.

---

# 18. Proposed Testing Architecture

The Host Protocol should be testable extensively before physical hardware is involved.

---

## 18.1 Tier 1 — Pure Codec Tests

No serial port, no asyncio, no hardware.

Validate:

```text
decode(encode(frame)) == frame
```

Test:

- empty bodies;
- maximum negotiated bodies;
- all-zero bodies;
- `0x00`-heavy data;
- `0xFF`-heavy data;
- every byte value;
- bad CRC;
- bad version;
- malformed lengths;
- unsupported flags;
- unsupported types;
- truncated CRC;
- trailing/extra data.

Property-based testing with Python Hypothesis is a strong candidate.

---

## 18.2 Tier 2 — Streaming Parser Torture

Feed valid frames:

- one byte at a time;
- two bytes at a time;
- random chunk sizes;
- full-frame reads;
- multiple concatenated frames.

Inject:

```text
garbage || 0x00 || valid_frame || 0x00 || garbage
```

Core requirement:

> A corrupted frame may destroy itself, but must not poison the next valid frame.

---

## 18.3 Tier 3 — Virtual Serial Fault Harness

Use a Linux pseudo-terminal pair.

```text
redlined
   │
 /dev/pts/X
   │
PTY pair
   │
Hub simulator
```

Fault injection should include:

- dropped bytes;
- duplicated bytes;
- arbitrary garbage;
- partial writes;
- paused reads;
- paused writes;
- serial close/reopen;
- Hub reboot;
- changed `BOOT_ID`;
- same `BOOT_ID`;
- delayed status events;
- stale responses;
- CRC corruption.

---

## 18.4 Tier 4 — Stateful Hub Simulator

The simulator should implement the abstract Host Protocol lifecycle rather than acting as a simple echo endpoint.

Useful test controls:

```text
hub.disconnect_serial()
hub.reconnect_serial()
hub.reboot()
hub.accept(txn)
hub.deliver(txn)
hub.fail(txn)
hub.drop_next_frame()
hub.corrupt_next_frame()
```

---

## 18.5 Mandatory Stateful Failure Scenarios

### Scenario A — Cable Loss, Hub Survives, Delivery Succeeds

```text
TX_DATA_REQ
Hub -> ACCEPTED
serial disconnect
Hub completes delivery
serial reconnect
same BOOT_ID
TX_STATE_REQ
Hub -> DELIVERED
```

Expected application state:

```text
DELIVERED
```

---

### Scenario B — Cable Loss, Hub Survives, Delivery Fails

```text
TX_DATA_REQ
Hub -> ACCEPTED
serial disconnect
Hub exhausts transport retries
serial reconnect
same BOOT_ID
TX_STATE_REQ
Hub -> FAILED_TIMEOUT
```

---

### Scenario C — Hub Reboot During Ambiguous Transaction

```text
TX_DATA_REQ
Hub -> ACCEPTED
Hub hard reset
serial reconnect
new BOOT_ID
TX_STATE_REQ
Hub -> NOT_FOUND
```

`redlined` derives:

```text
OUTCOME_UNKNOWN_AFTER_RESET
```

from its own history + changed `BOOT_ID` + authoritative Hub `NOT_FOUND`.

---

### Scenario D — Daemon Restart, Hub Survives

```text
daemon A submits transaction
Hub -> ACCEPTED
daemon A crashes
daemon B starts
new HOST_SESSION_ID
Hub remains alive
```

The new session must not alias or overwrite the old transaction namespace.

---

### Scenario E — Duplicate Submission

```text
TX_DATA_REQ txn=500
serial stalls
same request is retransmitted
```

Hub must not schedule duplicate physical delivery.

Expected behavior:

```text
same body => replay status
different body => TXN_CONFLICT
```

---

### Scenario F — Stale Status Race

A delayed status associated with an obsolete host/Hub transaction epoch must not be applied to a later transaction that happens to reuse a numeric `TXN_ID`.

---

# 19. Fuzzing Invariants

For arbitrary hostile byte input:

1. parser must not crash;
2. parser must not allocate unbounded memory;
3. parser must not deadlock;
4. parser must retain bounded state;
5. parser must recover at the next valid frame boundary.

Strong invariant:

```text
[random hostile bytes]
0x00
[valid COBS frame]
0x00
```

must eventually result in:

```text
valid frame dispatched successfully
```

The Python side can use Hypothesis initially.

A future C/C++ implementation may benefit from libFuzzer/AFL++ or equivalent fuzzing.

---

# 20. Relationship to Existing REDLINE Work

The proposed host transport model is **not** grounds to rewrite completed milestones.

Existing work remains valuable.

Examples include:

- point-to-point radio bring-up;
- explicit addressing;
- duplicate suppression;
- cached ACK replay;
- radio recovery behavior;
- RuntimeState;
- health/error modeling;
- diagnostic counters;
- DeviceInput;
- DeviceUi;
- nonblocking runtime behavior;
- persistent settings;
- settings validation;
- dual-slot persistence;
- recovery/factory-reset logic;
- capability registry and implementation-boundary work.

Many of these are exactly the disciplines needed underneath a more general transport layer.

The future architecture should be layered on top of that foundation rather than used as justification to discard it.

---

# 21. Expected Roadmap Impact

The existing roadmap should **not** be rewritten from scratch.

The future sections should be audited and updated to reflect the clarified architecture.

Likely explicit tracks/milestones include:

```text
current capability/runtime foundation
        │
        ▼
opaque application transport semantics
        │
        ▼
Host Protocol definition
        │
        ▼
substrate fragmentation / reassembly
        │
        ▼
transaction lifecycle / reconciliation
        │
        ▼
redlined host service
        │
        ▼
simulator + conformance harness
        │
        ▼
ARGUS integration
        │
        ▼
third-party application interface
        │
        ▼
routing / store-forward / crypto evolution
```

This sequence may not map one-to-one onto version numbers yet.

The repository should be audited before assigning exact release milestones.

---

# 22. Recommended Development Sequencing

A possible implementation sequence, once the roadmap reaches this work:

## Phase 1 — Protocol model only

- frame structs;
- COBS candidate;
- CRC32C;
- capability model;
- error model;
- type-specific body definitions.

## Phase 2 — Pure host codec

- encode/decode;
- validation;
- property tests;
- fuzz target.

## Phase 3 — Stateful Hub simulator

- session handling;
- boot epochs;
- transaction table;
- idempotency;
- status cache;
- cancellation;
- reconnect semantics.

## Phase 4 — Virtual serial harness

- PTY backend;
- fault injection;
- connection loss;
- delayed frames;
- reboot testing.

## Phase 5 — `redlined` transport core

- asyncio serial layer;
- codec integration;
- handshake;
- transaction registry;
- reconciliation engine.

## Phase 6 — Local IPC/API

- Unix socket or localhost API;
- client multiplexing;
- diagnostic exposure;
- application-neutral data interface.

## Phase 7 — Firmware Host Protocol

- embedded parser;
- capability negotiation;
- Hub transaction ownership;
- bounded queues;
- conformance behavior.

## Phase 8 — Substrate fragmentation/reassembly

- radio-fragment model;
- fragment retries;
- reassembly;
- delivery result mapping.

## Phase 9 — Hardware-in-the-loop conformance

Reuse the simulator scenario suite against physical Hub firmware wherever practical.

## Phase 10 — ARGUS integration

ARGUS becomes a client of `redlined`.

Only after the substrate interface is stable should application-specific integrations expand.

---

# 23. Repository Documentation Integration Plan

The next repository task should be an **audit**, not an immediate rewrite.

Codex should inspect the repository and classify current documentation.

Suggested categories:

### A. Historical implementation docs

These should generally remain unchanged except for factual errors.

Completed release documentation should continue to describe what that release actually implemented.

### B. Current milestone docs

Check whether current implementation boundaries conflict with the clarified application-neutral transport direction.

Make the smallest corrections necessary.

### C. Future roadmap docs

These are the primary target for updates.

Clarify:

- host/application meaning stays off-device;
- transport semantics become application-neutral;
- Host Protocol is a distinct lifecycle;
- `redlined` is host-side;
- fragmentation is substrate-owned;
- LoRa is the first physical transport, not necessarily REDLINE's permanent identity;
- future transports should not require application rewrites.

### D. README / overview material

Update only enough to accurately communicate REDLINE's architecture and current maturity.

Do not imply unimplemented capabilities already exist.

---

# 24. Codex Repository Audit Instructions

When this document is handed to Codex, the first task should be:

> **Audit the current ARGUS REDLINE repository against this architectural direction. Do not implement code yet. Do not rewrite completed historical milestones. Identify documentation and roadmap impacts first.**

Codex should produce:

1. a list of relevant files;
2. the current statement each file makes;
3. whether it conflicts with, already supports, or is silent about this direction;
4. the smallest required correction;
5. future roadmap additions;
6. milestone dependencies;
7. any implementation assumption that needs an explicit decision before coding.

Codex should pay particular attention to:

- `README.md`;
- project overview documentation;
- versioned development roadmap;
- current/future implementation briefs;
- capability model;
- Host Protocol ownership references;
- wire-protocol lifecycle documentation;
- transport/application boundary language;
- persistence/state ownership;
- future routing/crypto/store-forward milestones.

---

# 25. Codex Guardrails

Codex should be explicitly instructed:

## Do not

- implement the Host Protocol merely because this document describes one;
- rename existing release semantics retroactively;
- rewrite completed v0.x documentation into future architecture language;
- move application logic into firmware;
- treat COBS/CRC32C/opcode values as final without an implementation milestone decision;
- introduce large firmware abstractions before their roadmap milestone;
- assume the current 32-byte radio frame must become the Host Protocol message limit;
- assume Matrix is the REDLINE transport gateway;
- make `redlined` understand ARGUS/BLACKSHEEP/NIGHTWATCH application payload meaning.

## Do

- preserve historical implementation truth;
- update future-facing architecture carefully;
- keep firmware narrow;
- preserve host/application separation;
- make lifecycle ownership explicit;
- separate firmware, wire protocol, config, and Host API versioning;
- add new milestones only where justified;
- identify dependencies before implementation;
- prefer incremental commits.

---

# 26. Suggested Documentation Artifacts

This handoff may later be split into dedicated repository documents.

Possible future structure:

```text
docs/
  ARGUS_REDLINE_HOST_TRANSPORT_DIRECTION.md
  HOST_PROTOCOL_V1_DESIGN_NOTES.md
  HOST_PROTOCOL_V1_SPEC.md
  HOST_PROTOCOL_V1_TEST_PLAN.md
  REDLINED_ARCHITECTURE.md
```

Recommended lifecycle:

```text
direction document
    ↓
design notes
    ↓
implementation audit
    ↓
formal spec
    ↓
test plan
    ↓
code
```

Do not skip directly from brainstorming to a binding specification.

---

# 27. Current Decision Status

## Architectural direction — strong

The following should be treated as established project direction unless repository review reveals a contradiction:

- REDLINE is an application-neutral communications/capability substrate.
- Host applications own application meaning.
- Firmware owns transport behavior.
- Host-side heavy state/orchestration should remain off-device.
- A formal host/device boundary will eventually be required.
- Fragmentation should be owned below the application layer.
- Serial disconnect must not implicitly cancel accepted radio work.
- Transport outcomes must come from the Hub.
- Ambiguous outcomes must remain ambiguous until evidence resolves them.
- The architecture should support future applications without embedding their logic in firmware.

## Candidate implementation details — not frozen

The following remain candidate choices:

- COBS;
- CRC32C;
- exact frame layout;
- exact opcode values;
- exact flag bits;
- 16-bit peer IDs;
- 16-bit transaction IDs;
- `BOOT_ID` representation;
- `HOST_SESSION_ID` representation;
- exact `MAX_HOST_PAYLOAD`;
- exact queue sizes;
- exact diagnostic body layouts;
- exact IPC mechanism for `redlined`;
- exact programming language for `redlined`;
- exact version milestone in which each feature lands.

These should be validated when implementation approaches.

---

# 28. Recommended Immediate Next Action

Do **not** begin coding `redlined` immediately.

The next action should be a repository-wide documentation/roadmap audit.

Recommended sequence:

```text
1. Commit/preserve this architecture handoff
2. Ask Codex to audit current repository docs
3. Review Codex impact report
4. Update future roadmap and architecture docs incrementally
5. Preserve completed milestone history
6. Assign Host Protocol work to an explicit future milestone
7. Write formal implementation spec only when that milestone approaches
```

This preserves the value of the architecture review without allowing a speculative future design to derail current development.

---

# 29. Bottom Line

The recent architecture discussion did **not** reveal that REDLINE's existing base is wrong.

It clarified the consequences of a direction the project was already moving toward:

> **REDLINE should behave like a resilient off-grid communications substrate rather than a collection of application-specific radio commands.**

The existing firmware remains the lower-layer foundation.

The future work is primarily about building a clean, durable boundary above that foundation:

```text
applications
    ↓
host service
    ↓
Host Protocol
    ↓
REDLINE transport
    ↓
physical radio
```

The architecture review surfaced the distributed-systems problems early—before they were embedded into thousands of lines of code.

That is an advantage.

The correct response is not a rewrite.

It is a controlled roadmap update, a repository documentation audit, and incremental implementation when the project reaches the relevant milestones.

---

## Handoff Note for Future ChatGPT / Codex

When continuing this work:

1. Inspect the repository before proposing edits.
2. Treat this document as architectural context, not implemented truth.
3. Preserve completed release history.
4. Keep current milestone scope stable unless a real dependency requires change.
5. Update future roadmap language to reflect the application-neutral transport substrate model.
6. Do not promote candidate Host Protocol details into a frozen spec without explicit review.
7. Keep embedded complexity bounded.
8. Keep application semantics on the host.
9. Maintain explicit ownership of transport state and outcomes.
10. Build tests and simulation before implementing complex serial/radio lifecycle behavior.

The goal is not to make REDLINE larger for its own sake.

The goal is to make REDLINE **boringly reliable, reusable, and difficult to misuse** while keeping the embedded core small enough to remain understandable.
