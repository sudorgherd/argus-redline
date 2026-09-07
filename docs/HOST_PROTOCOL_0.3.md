# ARGUS REDLINE Host Protocol 0.3

**Status:** Normative developmental specification for v0.8.0; not a stable API

**Version:** major `0`, minor `3`

**Predecessors:** Host Protocol `0.1`, frozen for minor-1 frames, and Host
Protocol `0.2`, frozen for minor-2 frames

**Milestone authority:** `V0.8.0_IMPLEMENTATION_BRIEF.md`

## 1. Status and normative authority

This document freezes the exact Host Protocol 0.3 byte contract and lifecycle
needed by v0.8.0 persistent identity and controlled provisioning. It is the
normative Host-boundary authority for implementation, tests, bench integration,
physical qualification, and release closeout.

Normative words MUST, MUST NOT, SHOULD, and MAY are requirements. Multi-byte
integers are unsigned little-endian unless stated otherwise. Tables of offsets
refer to the first byte of the named structure, not the enclosing Host frame.

This specification does not implement firmware, authorize a version promotion,
or change Host Protocol 0.1, Host Protocol 0.2, Wire Protocol 1, Configuration
Schema 1, Event persistence schemas, or HELTEC_V4 hardware behavior.

## 2. Scope

Host Protocol 0.3 adds a bounded local provisioning category through which a
physically present operator and directly attached USB Host can:

- read non-secret provisioning/identity status;
- apply one complete first-provisioning or reprovisioning proposal;
- request the defined full factory reset; and
- cancel a volatile provisioning window before a durable transaction begins.

Provisioning operations remain Host-initiated and synchronous. No unsolicited
frame, second framing mechanism, general management API, interactive shell,
arbitrary blob store, credential format, or RF provisioning path is introduced.

## 3. Compatibility with Host Protocol 0.1 and 0.2

A provisioned 0.3 implementation supports:

```text
frame major 0 / minor 1 -> exact Host Protocol 0.1 semantics
frame major 0 / minor 2 -> exact Host Protocol 0.2 semantics
frame major 0 / minor 3 -> Host Protocol 0.3 semantics in this document
```

The minor byte in every frame is authoritative. HELLO is discovery only; it
creates no negotiated connection state. Every accepted request retains its own
frame minor, and every immediate, deferred, or retained response uses that
minor. A later frame is interpreted solely under its encoded minor.

Shared codec knowledge is not permission to extend an older minor:

- minor 1 knows only the 0.1 categories, features, operations, roles, values,
  results, validation rules, and HELLO range;
- minor 2 knows exactly the 0.2 additions and no provisioning vocabulary; and
- only minor 3 knows the assignments in Sections 7–22.

A non-operational device (UNPROVISIONED, INVALID, PENDING/COMMITTING, or
REBOOT_REQUIRED) has no truthful Hub/Node identity for a legacy HELLO. It
therefore exposes an operational supported-minor set of `{3}`. A syntactically
valid minor-1 or minor-2 HELLO receives that minor's existing
`PROTOCOL_ERROR/UNSUPPORTED_MINOR`, detail zero. This is lifecycle availability,
not a reinterpretation of either legacy frame vocabulary. Other syntactically
valid minor-1/2 operations have no local device ID to target and return the
existing `REQUEST_REJECTED/BAD_TARGET` with NONE. They never receive a new
category, role, target, value, or result value.

### 3.1 Compatibility table

| Property | Minor 1 | Minor 2 | Minor 3 |
|---|---|---|---|
| Framing/message types | exact 0.1 | exact 0.2/0.1 | unchanged |
| HELLO request range | exactly `1..1` | bounded range within `1..2` | bounded range within `1..3` |
| DEVICE/CAPABILITY/PROCEDURE/DIAGNOSTIC | exact 0.1 | exact 0.2 | retained when operational |
| EVENT category/operations/result | prohibited | exact 0.2 | exact 0.2 retained |
| PROVISIONING category | prohibited | prohibited | additive |
| request STRUCTURE | prohibited | only exact 0.2 uses | exact 0.2 plus Sections 14 and 18 |
| no-active-role value `0x00` | invalid | invalid | valid only under Section 6 |
| Host-local target `0x00` | invalid | invalid | provisioning category only |
| PROVISIONING_RESULT | invalid | invalid | provisioning operations only |

## 4. Framing and bounds

Each frame remains:

```text
COBS(decoded frame) || 0x00
```

Decoded geometry remains:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | protocol major |
| 1 | 1 | protocol minor |
| 2 | 1 | message type |
| 3 | 1 | flags |
| 4 | 2 | request ID |
| 6 | 2 | payload length `N` |
| 8 | `N` | payload, `0..128` bytes |
| `8+N` | 2 | CRC-16/CCITT-FALSE |

Decoded length is exactly `10+N`, 10..138 bytes. Maximum Host payload remains
128 bytes. The maximum non-delimiter COBS candidate remains 139 bytes and the
maximum encoded frame including delimiter remains 140 bytes. Compile-time
bounded buffers are mandatory.

CRC remains CRC-16/CCITT-FALSE: polynomial `0x1021`, initial `0xFFFF`, no
reflection, final XOR `0x0000`. It covers major through the final payload byte
and is stored low byte then high byte. CRC detects corruption; it authenticates
nothing.

Standard COBS/delimiter recovery, partial-frame handling, oversized-candidate
discard, and USB byte-stream independence remain unchanged.

## 5. Message types, flags, and request IDs

No message type is added:

| Value | Name | Direction |
|---:|---|---|
| `0x01` | `HELLO_REQUEST` | Host -> device |
| `0x02` | `HELLO_RESPONSE` | device -> Host |
| `0x10` | `OPERATION_REQUEST` | Host -> device |
| `0x11` | `OPERATION_RESPONSE` | device -> Host |
| `0x7F` | `PROTOCOL_ERROR` | device -> Host |

Flags remain exactly `0x00`. Request ID `0x0000` remains reserved. Hosts use
`0x0001..0xFFFF`; correlated responses copy the request ID.

Request IDs remain volatile Host-link correlation values. The single-active,
single-retained completion rules from 0.1/0.2 remain. A retained Host request
identity is request ID plus the byte-exact accepted OPERATION_REQUEST payload.
Request ID alone is never a persistent idempotency token.

## 6. Version handling and HELLO

### 6.1 HELLO request and selection

HELLO_REQUEST remains exactly two bytes:

```text
minimum minor   uint8
maximum minor   uint8
```

- A minor-1 frame requires exactly `1,1`.
- A minor-2 frame uses the exact 0.2 rule and may request only a valid range
  within `1..2`.
- A minor-3 frame requires nonzero minimum/maximum, minimum <= maximum, and
  both values <= 3. A provisioned device selects the highest value in the
  intersection with `{1,2,3}`. A non-operational device selects from `{3}`.
- No overlap returns `PROTOCOL_ERROR/UNSUPPORTED_MINOR`, detail zero.
- The HELLO_RESPONSE frame minor always equals the request frame minor even
  when `selected minor` is lower.

When a minor-3 HELLO selects 1 or 2 on a provisioned device, the response
payload follows the selected minor's role, category, feature, and reserved-bit
vocabulary exactly. Provisioning bits remain zero. This does not change the
minor-3 response envelope or create session state.

### 6.2 HELLO response

The existing 16-byte geometry remains sufficient:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | selected minor |
| 1 | 1 | firmware major |
| 2 | 1 | firmware minor |
| 3 | 1 | firmware patch |
| 4 | 1 | Wire Protocol |
| 5 | 1 | Configuration Schema |
| 6 | 1 | compiled hardware profile (`HELTEC_V4=0x01`) |
| 7 | 1 | active role |
| 8 | 1 | active REDLINE device ID |
| 9 | 1 | maximum Host payload (`128`) |
| 10 | 2 | operation-category bitmap |
| 12 | 2 | feature bitmap |
| 14 | 1 | maximum outstanding operations (`1`) |
| 15 | 1 | reserved zero |

Minor-3 role values are:

| Value | Meaning |
|---:|---|
| `0x00` | `NO_ACTIVE_ROLE` |
| `0x01` | `HUB` |
| `0x02` | `NODE` |

`NO_ACTIVE_ROLE` is valid only in a selected-minor-3 response and requires
device ID `0x00`, category bitmap exactly `0x0020`, and feature bitmap exactly
`0x0008`. It is used for UNPROVISIONED, INVALID, PENDING/COMMITTING, and
REBOOT_REQUIRED. GET_PROVISIONING_STATUS distinguishes those states. It is not
a Wire role or device ID.

A selected-minor-3 operational Hub/Node reports its provisioned local ID
(`0x01..0xFE`), its implemented legacy category bits under the existing rules,
and the provisioning category bit. A complete Hub Event service continues to
require equal EVENT category/Event-service feature advertisement. A Node still
does not advertise EVENT service. Codec support alone sets no service bit.

INVALID provisioning produces a normal minor-3 HELLO with NO_ACTIVE_ROLE and
provisioning-only advertisement; it does not fabricate an ID, overwrite
storage, or enable radio. Status then returns the exact invalid/storage reason.

Firmware version, Wire version, Configuration Schema, and hardware profile
remain independent and MUST NOT substitute for category/feature advertisement.

## 7. Category registry

| Value | Category | Bitmap bit/value | Minor introduced |
|---:|---|---:|---:|
| `0x01` | DEVICE | bit 0 / `0x0001` | 1 |
| `0x02` | CAPABILITY | bit 1 / `0x0002` | 1 |
| `0x03` | PROCEDURE | bit 2 / `0x0004` | 1 |
| `0x04` | DIAGNOSTIC | bit 3 / `0x0008` | 1 |
| `0x05` | EVENT | bit 4 / `0x0010` | 2 |
| `0x06` | PROVISIONING | bit 5 / `0x0020` | 3 |

For selected minor 3, bits 6..15 are reserved zero (`reserved mask 0xFFC0`).
PROVISIONING is local to either directly attached board. It is advertised by a
0.3 implementation in every provisioning lifecycle state, including normal
provisioned operation. It is never a Wire operation category.

## 8. Feature bitmap

| Bit/value | Feature | Minor introduced |
|---:|---|---:|
| bit 0 / `0x0001` | local operations | 1 |
| bit 1 / `0x0002` | structured radio-operation bridge | 1 |
| bit 2 / `0x0004` | persistent local Hub Event service | 2 |
| bit 3 / `0x0008` | controlled local provisioning service | 3 |

For selected minor 3, bits 4..15 are reserved zero (`reserved mask 0xFFF0`).
Provisioning feature bit 3 MUST equal category bit 5. Both are set for a
complete 0.3 provisioning/status service and both clear otherwise.

NO_ACTIVE_ROLE sets only provisioning/category feature bits as specified in
Section 6. An operational role retains applicable existing features and adds
provisioning. Minor-1 responses keep bits 2..15 zero. Minor-2 responses keep
bits 3..15 zero.

## 9. Operation registry

Existing operations `0x20..0x2B` and their exact category pairings remain
unchanged. Minor 3 adds:

| Category | Opcode | Operation | Dispatch | Physical authorization | Reboot |
|---|---:|---|---|---|---|
| PROVISIONING | `0x2C` | `GET_PROVISIONING_STATUS` | local Hub/Node/no-active-role | never required | never |
| PROVISIONING | `0x2D` | `APPLY_PROVISIONING` | local only | required for a new mutation; not for a proven no-op/retry | required after committed change |
| PROVISIONING | `0x2E` | `FULL_FACTORY_RESET` | local only | required for a new mutation; not for a proven completed retry | required after committed reset |
| PROVISIONING | `0x2F` | `CANCEL_PROVISIONING_WINDOW` | local only | existing open window is sufficient | no |

Only PROVISIONING may pair with `0x2C..0x2F`. These opcodes are not added to
Wire Protocol or the Wire operation registry. A provisioning operation is
never submitted through the Hub radio bridge, regardless of target fields or
role.

## 10. Value vocabulary

All existing value assignments remain:

```text
NONE           0x00
BOOLEAN        0x01
UNSIGNED_32    0x02
SIGNED_32      0x03
NORMALIZED_U16 0x04
FIXED_Q16_16   0x05
ENUM_U16       0x06
STRUCTURE      0x7F
```

STRUCTURE remains operation-specific, never an arbitrary byte array. Minor 3
adds exactly:

- a 72-byte provisioning-status response structure;
- a 40-byte APPLY request structure; and
- a 16-byte FULL_FACTORY_RESET request structure.

GET status and CANCEL requests use NONE/length zero. Every provisioning result
response uses NONE/length zero except successful GET status, which uses the
72-byte STRUCTURE. No string type is added; the bounded label is a field inside
the APPLY structure.

## 11. Local target semantics

Host-local provisioning target sentinel is exactly:

```text
HOST_LOCAL_PROVISIONING_TARGET = 0x00
```

For minor-3 PROVISIONING requests and responses:

- target device ID MUST be `0x00`;
- target ID MUST be `0x0000`; and
- the response echoes both values.

This sentinel identifies the one device physically attached to the Host
transport. It is not a REDLINE device ID, not a Wire source/destination, and
not a broadcast. It is valid under no category other than minor-3
PROVISIONING, and provisioning rejects every other target device/target ID as
`REQUEST_REJECTED/BAD_TARGET` before service state or storage access.

The same sentinel is used after provisioning. Provisioning management never
depends on guessing the current or replacement REDLINE ID. All existing
minor-1/2 and minor-3 non-provisioning operations retain their current local or
remote target rules and reject `0x00`.

The APPLY/FULL_RESET structures additionally name the six-byte hardware
identity, preventing a command prepared for one attached board from mutating a
different board. That public value is a wrong-device guard, not authentication.

## 12. Provisioning lifecycle vocabulary

Lifecycle values used in the status structure are:

| Value | State | Ordinary RF |
|---:|---|---|
| `0x00` | `UNPROVISIONED` | prohibited |
| `0x01` | `PROVISIONING_ALLOWED` | quiesced/prohibited |
| `0x02` | `PROVISIONING_COMMITTING` | prohibited |
| `0x03` | `PROVISIONED_HUB` | permitted when otherwise ready |
| `0x04` | `PROVISIONED_NODE` | permitted when otherwise ready |
| `0x05` | `INVALID_PROVISIONING` | prohibited |
| `0x06` | `RESET_COMMITTING` | prohibited |
| `0x07` | `REBOOT_REQUIRED` | prohibited |

Identity storage-state values are:

| Value | State |
|---:|---|
| `0x00` | `MISSING` |
| `0x01` | `VALID_PRIMARY` |
| `0x02` | `VALID_FALLBACK` |
| `0x03` | `PENDING` |
| `0x04` | `INVALID_OR_CONFLICT` |
| `0x05` | `UNSUPPORTED_SCHEMA` |
| `0x06` | `UNAVAILABLE` |

Roles in the status structure are `NONE=0`, `HUB=1`, `NODE=2`. Authorization
scopes and pending transaction kinds are both `NONE=0`, `APPLY=1`, and
`FULL_RESET=2`.

Event-domain summary states are `UNKNOWN=0`, `EMPTY=1`, `PRESENT=2`,
`INVALID=3`, and `UNAVAILABLE=4`. PRESENT means at least one owned record or
allocator/ordinal metadata key exists. INVALID/UNAVAILABLE requires the
erasure acknowledgement before a destructive operation and prevents a claim
that the domain was empty.

## 13. Identity/provisioning status structure

GET_PROVISIONING_STATUS returns exact structure schema 1, length 72:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | structure schema (`1`) |
| 1 | 1 | provisioning lifecycle (Section 12) |
| 2 | 1 | identity storage state |
| 3 | 1 | reported identity/proposal role (`0..2`) |
| 4 | 1 | reported identity/proposal local device ID |
| 5 | 1 | reported identity/proposal peer device ID |
| 6 | 1 | compiled hardware profile (`HELTEC_V4=1`) |
| 7 | 1 | reported identity/proposal capability profile, or zero |
| 8 | 2 | recognized Identity Schema version, or zero |
| 10 | 2 | status flags |
| 12 | 4 | provisioning generation, or zero |
| 16 | 4 | selected protected-record generation, or zero |
| 20 | 4 | reported identity/proposal network ID, or zero |
| 24 | 2 | provisioning-window whole seconds remaining, rounded down |
| 26 | 1 | last provisioning result available (`0`/`1`) |
| 27 | 1 | last PROVISIONING_RESULT code, or zero |
| 28 | 1 | reported identity/proposal label length, `0..16` |
| 29 | 16 | reported identity/proposal label; unused tail zero |
| 45 | 6 | hardware identity in displayed MAC/USB-serial byte order |
| 51 | 1 | recoverable Node custody count, `0..8`, otherwise zero |
| 52 | 1 | recoverable Hub ACTIVE count, `0..8`, otherwise zero |
| 53 | 1 | recoverable Hub CONSUMED count, `0..8`, otherwise zero |
| 54 | 1 | Event-domain summary state |
| 55 | 1 | current physical authorization scope |
| 56 | 1 | durable pending transaction kind |
| 57 | 3 | reserved zero |
| 60 | 2 | provisioning windows opened (saturating) |
| 62 | 2 | provisioning windows expired/cancelled (saturating) |
| 64 | 2 | successful APPLY commits (saturating) |
| 66 | 2 | successful full-reset commits (saturating) |
| 68 | 2 | pending-recovery failures (saturating) |
| 70 | 2 | identity/provisioning storage failures (saturating) |

Status flag assignments are:

| Bit/value | Meaning |
|---:|---|
| bit 0 / `0x0001` | identity/proposal fields available |
| bit 1 / `0x0002` | physical authorization armed |
| bit 2 / `0x0004` | reboot required |
| bit 3 / `0x0008` | durable PENDING transaction exists |
| bit 4 / `0x0010` | ordinary radio operation permitted |
| bit 5 / `0x0020` | Node Event-domain data present |
| bit 6 / `0x0040` | Hub Event-domain data present |
| bit 7 / `0x0080` | unbound/legacy v0.7 Event-domain data present |
| bit 8 / `0x0100` | Event-domain summary/counts valid |
| bit 9 / `0x0200` | provisioning storage/recovery degraded |

Bits 10..15 are reserved zero. Counts are authoritative only when bit 8 is
set. PRESENT/INVALID/UNAVAILABLE and bits 5..7 cannot be used to infer Event
identity or content.

Flag bit 0 and the identity/proposal fields follow one exact rule:

- an active PROVISIONED authority, a provisioning window based on that
  authority, or REBOOT_REQUIRED after APPLY reports the complete committed
  identity and sets bit 0;
- a recognized durable PENDING/APPLY reports the complete carried target
  proposal and sets bits 0 and 3; and
- virgin/canonical UNPROVISIONED, PENDING/FULL_RESET, REBOOT_REQUIRED after
  FULL_RESET, or an invalid/unavailable authority clears bit 0 and encodes
  bytes 3..5, 7..9, 12..23, and 28..44 as canonical zero.

The compiled hardware profile and physical hardware identity remain available
in all cases. PROVISIONING_ALLOWED based on UNPROVISIONED/invalid authority
does not fabricate identity fields.

The last result and all six counters are volatile, diagnostic-only, and clear
on reboot. A query does not change them. Saturation is 65535.

## 14. APPLY provisioning request structure

APPLY_PROVISIONING requires STRUCTURE type, schema 1, exact length 40:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | request/Identity Schema (`1`) |
| 1 | 1 | requested role (`HUB=1`, `NODE=2`) |
| 2 | 1 | requested local device ID |
| 3 | 1 | requested peer device ID |
| 4 | 4 | requested network ID |
| 8 | 1 | requested hardware profile (`HELTEC_V4=1`) |
| 9 | 1 | requested capability profile (`HELTEC_V4_BASE=1`) |
| 10 | 1 | label length, `0..16` |
| 11 | 1 | request flags |
| 12 | 16 | label bytes; unused tail zero |
| 28 | 4 | expected current provisioning generation |
| 32 | 6 | expected hardware identity |
| 38 | 2 | reserved zero |

Request flag bit 0/value `0x01` is `ACK_EVENT_ERASURE`. Bits 1..7 are reserved
zero. The acknowledgement covers Node custody/allocator metadata and Hub
ACTIVE/CONSUMED/ordinal metadata. It grants no general NVS erase authority.

Valid device IDs are `0x01..0xFE`; `0x00` and `0xFF` are reserved. Local and
peer IDs MUST differ. Network ID is nonzero. Labels use bytes `0x20..0x7E` and
unused bytes are zero. Hardware/capability profiles must match supported
compiled profiles.

Expected generation is:

- zero when the Host requires virgin/canonical UNPROVISIONED authority;
- the exact current nonzero generation for a new reprovisioning change; or
- the preceding generation for the exact durable retry case in Section 21.

`0xFFFFFFFF` is reserved for FULL_FACTORY_RESET invalid-state recovery and is
invalid in APPLY. New successful generations are limited to
`1..0xFFFFFFFE`; exhaustion fails closed until full reset.

### 14.1 Validation order

Before any mutation:

```text
frame COBS/length/CRC/version/type/flags/request ID
-> category/operation pair
-> Host-local target 0x00 and target ID 0
-> STRUCTURE type and exact length 40
-> schema, known flags, zero reserved/tail, label geometry
-> ordinary single-active/retained Host lifecycle classification
-> hardware identity
-> role, IDs, network, hardware/capability profiles, label semantics
-> current/pending lifecycle and expected-generation classification
-> exact durable no-op/retry classification
-> physical authorization scope for any new mutation
-> Event-domain erasure acknowledgement when required
-> foreground safe-point acquisition
-> durable transaction
```

Syntactic/geometry/schema/reserved/tail violations are
`REQUEST_REJECTED/MALFORMED_REQUEST`. Semantic field, policy, generation,
hardware, storage, and lifecycle outcomes use Section 19. No rejected request
changes identity/Event/settings storage.

## 15. GET_PROVISIONING_STATUS

Request:

```text
category          PROVISIONING (0x06)
operation         GET_PROVISIONING_STATUS (0x2C)
target device ID  HOST_LOCAL_PROVISIONING_TARGET (0x00)
target ID         0
value type        NONE (0x00)
value length      0
```

It is available in every minor-3 lifecycle state without physical
authorization. It never opens/extends/consumes a window, changes a diagnostic,
initializes radio, repairs storage, clears an Event, or writes NVS.

Success is `SUCCESS/OK` with STRUCTURE length 72. If the status snapshot cannot
be safely formed, return `PROVISIONING_RESULT/STORAGE_FAILURE` with NONE; do
not synthesize an unprovisioned state.

## 16. APPLY_PROVISIONING

APPLY uses the Section 14 request and is the only first-provision/reprovision
operation. There is no separate “write field,” “commit,” or role-change opcode.

For a new mutation it requires authorization scope APPLY. At a foreground safe
point the implementation:

```text
quiesces ordinary radio/Host work
-> commits and verifies PENDING/APPLY carrying the complete target
-> consumes the physical authorization/window
-> clears and verifies both Node and Hub Event domains
-> commits and verifies the target PROVISIONED record
-> enters REBOOT_REQUIRED with radio disabled
-> returns PROVISIONING_RESULT/APPLIED_REBOOT_REQUIRED
```

Ordinary settings are preserved. Event clear occurs only after verified
PENDING and before new identity authority. A failure before verified PENDING
leaves the old authority and authorization open until timeout. A failure after
verified PENDING consumes authorization, preserves PENDING, keeps radio off,
and returns STORAGE_FAILURE; boot recovery must complete the carried
transaction. Success is not reported until final record readback/byte
verification.

The old identity stops being ordinary runtime authority when verified PENDING
begins; it remains only recovery provenance. The new identity becomes durable
authority after final verified PROVISIONED commit but is not activated as a
radio role until reboot.

An exact proposal equal to current active identity with expected generation
equal to current generation returns `UNCHANGED`, with no physical authorization,
write, erase, generation advance, or reboot. Divergent proposals require an
armed APPLY window.

## 17. Reprovision behavior

A provisioned device enters reprovisioning only through the locally confirmed
Device Management UI action defined by the implementation brief. Opening the
window waits for no active radio owner, incomplete Host response, or persistent
write, then blocks new ordinary RF work. Powered Event lifetime continues
normally while the window is open; it is never refreshed.

During PROVISIONING_ALLOWED only HELLO, GET_PROVISIONING_STATUS, the operation
matching the selected physical scope, and CANCEL are serviced. Other valid
Host operations return existing `REQUEST_REJECTED/BUSY`. Timeout/cancel resumes
the unchanged provisioned role if no PENDING record exists.

CANCEL_PROVISIONING_WINDOW uses category PROVISIONING, opcode `0x2F`, the
Section 11 local target, and NONE/length zero. In PROVISIONING_ALLOWED it closes
the volatile window, increments the expired/cancelled diagnostic counter, and
returns `PROVISIONING_RESULT/WINDOW_CANCELLED` with NONE. In every other state
it returns `PROVISIONING_RESULT/INVALID_LIFECYCLE_STATE`. It MUST NOT cancel,
roll back, or modify a durable PENDING transaction, and it requires no
additional physical authorization beyond the open window.

Every actual change—including role, local/peer ID, network, profile, or label—
uses the full Section 16 transaction and clears both Event domains. Mixed
old/new identity state is never operational. Any clear/write failure after
PENDING leaves a recoverable non-operational device, not an old-role fallback.

## 18. FULL_FACTORY_RESET

FULL_FACTORY_RESET requires STRUCTURE schema 1, exact length 16:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | reset request schema (`1`) |
| 1 | 1 | flags |
| 2 | 4 | expected current provisioning generation |
| 6 | 6 | expected hardware identity |
| 12 | 4 | confirmation bytes exactly ASCII `FULL` (`46 55 4C 4C`) |

Flag bit 0 is ACK_EVENT_ERASURE; bits 1..7 are reserved zero. A provisioned
device requires its exact current generation. Canonical/virgin UNPROVISIONED
uses zero. INVALID_PROVISIONING recovery uses reserved value `0xFFFFFFFF`.
Other values produce GENERATION_MISMATCH.

For a new mutation, authorization scope FULL_RESET is mandatory. The sequence
is:

```text
commit/verify PENDING/FULL_RESET
-> consume authorization/window
-> complete the existing recoverable ordinary-settings reset
-> clear/verify both Event domains
-> commit/verify canonical UNPROVISIONED identity
-> enter REBOOT_REQUIRED with radio disabled
-> return PROVISIONING_RESULT/RESET_REBOOT_REQUIRED
```

The operation removes only ARGUS-owned settings, identity, and Event keys; it
does not erase the NVS partition, firmware, bootloader, hardware identity, or
future credentials. Failure before PENDING preserves current authority and
authorization until timeout. Failure after PENDING remains non-operational and
recoverable, returns STORAGE_FAILURE, and never reports reset success.

The confirmation bytes, exact hardware target, physical FULL_RESET scope, and
Event-erasure acknowledgement are all required; no ordinary USB command can
accidentally become a full reset. The existing settings-only reset is not a
Host 0.3 provisioning operation and continues to preserve identity/Event data.

## 19. Result classes and codes

All 0.1 result classes/codes and 0.2 EVENT_RESULT remain exact. Minor 3 adds:

```text
PROVISIONING_RESULT = 0x06
```

| Code | Name | Meaning / allowed operation |
|---:|---|---|
| `0x00` | `APPLIED_REBOOT_REQUIRED` | APPLY final commit verified; reboot required |
| `0x01` | `ALREADY_APPLIED_REBOOT_REQUIRED` | exact APPLY retry proves successor record; same boot still requires reboot |
| `0x02` | `ALREADY_ACTIVE` | exact APPLY retry after reboot proves requested successor is active |
| `0x03` | `RESET_REBOOT_REQUIRED` | FULL_RESET final canonical UNPROVISIONED commit verified |
| `0x04` | `ALREADY_UNPROVISIONED` | reset retry proves canonical/virgin unprovisioned state; no mutation |
| `0x05` | `WINDOW_CANCELLED` | CANCEL closed an open pre-PENDING window |
| `0x06` | `UNCHANGED` | APPLY exactly equals current active record/current generation; no mutation |
| `0x10` | `PHYSICAL_PRESENCE_REQUIRED` | requested mutation lacks matching armed scope |
| `0x11` | `INVALID_LIFECYCLE_STATE` | operation is not permitted in current/pending state |
| `0x12` | `INVALID_ROLE` | requested role is not HUB/NODE |
| `0x13` | `RESERVED_DEVICE_ID` | local or peer is `0x00`/`0xFF` |
| `0x14` | `DUPLICATE_LOCAL_PEER_ID` | local ID equals peer ID |
| `0x15` | `INVALID_NETWORK_ID` | network ID is zero |
| `0x16` | `UNSUPPORTED_HARDWARE_PROFILE` | requested profile does not equal compiled profile |
| `0x17` | `UNSUPPORTED_CAPABILITY_PROFILE` | requested capability profile is absent |
| `0x18` | `INVALID_LABEL` | label length/content/tail violates Section 14 |
| `0x19` | `GENERATION_MISMATCH` | expected authority is neither current nor exact retry predecessor |
| `0x1A` | `EVENT_ERASURE_ACK_REQUIRED` | any Node/Hub Event state exists or cannot be proven empty and bit 0 is clear |
| `0x1B` | `HARDWARE_ID_MISMATCH` | expected six-byte hardware identity differs |
| `0x1C` | `STORAGE_FAILURE` | protected/settings/Event storage cannot complete or be proven |
| `0x1D` | `UNSUPPORTED_IDENTITY_SCHEMA` | structurally valid request names a schema unsupported for provisioning |

All PROVISIONING_RESULT responses use NONE/length zero. Codes `0x07..0x0F`
and `0x1E..0xFF` are reserved.

Wrong framing/payload geometry, unknown flags/reserved bytes, invalid
confirmation token, prohibited value type, or an unknown category/operation
encoding remains `REQUEST_REJECTED/MALFORMED_REQUEST`. A known category paired
with an operation not registered for it, or a known provisioning operation
unavailable on the implementation, uses the existing
`REQUEST_REJECTED/UNSUPPORTED_OPERATION`. Wrong Host-local target is
BAD_TARGET. Single-active BUSY and retained-request MISMATCH use existing
REQUEST_REJECTED codes. These are not duplicated in PROVISIONING_RESULT.

## 20. Physical-presence authorization

Authorization is volatile, local, scope-specific, and non-cryptographic:

- status field 55 reports NONE/APPLY/FULL_RESET;
- status flag bit 1 reports armed;
- field 24 reports whole seconds remaining;
- an awake two-step physical UI confirmation opens a 120-second window;
- startup-held/wake-only input cannot arm it;
- reboot always clears it;
- read-only status neither requires nor affects it;
- malformed, wrong-target, wrong-hardware, invalid-field, generation-mismatch,
  missing-erasure-acknowledgement, safe-point-BUSY, or pre-PENDING storage
  failure does not consume or extend it;
- CANCEL or expiry consumes it without persistent mutation;
- verified PENDING consumes it immediately and permanently for that boot; and
- successful APPLY/reset therefore also has it consumed.

The window is one-shot with respect to durable mutation: after PENDING, no
second provisioning request is accepted. Exact durable no-op/retry outcomes in
Section 21 do not mutate storage and may be returned without newly armed
physical presence. A divergent request always requires a new physical window.

## 21. Persistent retry and idempotency semantics

The Host retry cache remains volatile and handles byte-exact same-request
replay while available. Durable semantics come from the expected generation
and complete committed record, never request ID.

For APPLY with expected generation `E` and exact proposal `P`:

| Durable/current state | Outcome |
|---|---|
| UNPROVISIONED and `E=0`, armed | commit generation 1 from P |
| active generation `E`, P equals active | UNCHANGED, no write |
| active generation `E`, P differs, armed | commit successor `E+1` from P |
| current generation `E+1`, P equals current, REBOOT_REQUIRED | ALREADY_APPLIED_REBOOT_REQUIRED |
| current generation `E+1`, P equals current and active after reboot | ALREADY_ACTIVE |
| any other generation/content relation | GENERATION_MISMATCH |
| PENDING | INVALID_LIFECYCLE_STATE; recovery owns completion |

Successor arithmetic does not wrap and excludes `0xFFFFFFFF`.

If an APPLY response is lost after final commit, the retained Host cache may
re-emit it. If that cache is lost, the table above proves the same outcome from
durable state. A reboot after commit activates P and returns ALREADY_ACTIVE for
the exact retry. A differing second request cannot masquerade as the retry.

For FULL_RESET, exact retained replay re-emits the response. After cache loss
or reboot, a syntactically valid reset request naming the matching hardware,
the `FULL` confirmation, and Event acknowledgement returns
ALREADY_UNPROVISIONED without mutation only when all three terminal effects are
provable: durable identity is canonical/virgin UNPROVISIONED, ordinary settings
are canonical defaults, and both Event domains are proven empty. Its expected-
generation field is ignored only for this safe fully-reset outcome. This
permits deterministic retry after reset without pretending request ID is
durable. An unprovisioned device retaining migrated settings or any legacy,
invalid, or unavailable Event state is not an already-completed reset. A new
destructive reset of that device or a provisioned/invalid device still requires
the appropriate generation (`0`, current, or invalid sentinel), Event
acknowledgement, confirmation, and physical scope.

Host timeout before verified PENDING proves nothing; status determines whether
authority remained old, PENDING recovery is active, or the successor committed.
Hosts MUST query status before deciding whether to retry. They MUST NOT use a
fresh request ID as evidence that an old mutation did not occur.

## 22. Migration and legacy Event-state behavior

Installing v0.8 over v0.7 does not create Identity Schema 1. The device boots
UNPROVISIONED, preserves ordinary Configuration Schema 1 settings, disables
radio, and inspects `red_evt` read-only for migration status. It never infers a
role/ID from old build flags, hardware identity, Node Event metadata, or Hub
ledger records.

Status bit 7 marks unbound/legacy Event state whenever no active Identity
Schema authority exists and any Node or Hub Event-domain data/key cannot be
proven empty. Bits 5 and 6 distinguish Node and Hub domains; counts are
reported only when safely recoverable. INVALID/UNAVAILABLE is treated as
potential data, not empty.

If either domain is present, invalid, or unavailable, APPLY/FULL_RESET with
ACK_EVENT_ERASURE clear returns EVENT_ERASURE_ACK_REQUIRED without mutation.
Bit 0 acknowledges destruction of **both** Node and Hub Event domains,
including custody, allocator metadata, ACTIVE/CONSUMED proofs, and ordinal
metadata. After verified PENDING, both domains are cleared and verified before
new PROVISIONED/canonical UNPROVISIONED authority is committed. A clear failure
returns STORAGE_FAILURE, retains PENDING, and keeps radio off.

Acknowledgement is consent, not a statement that data exists; setting it when
both domains are proven empty is valid. It does not erase ordinary settings.

## 23. Role/state operation matrix

| Lifecycle | HELLO | GET status | APPLY | FULL reset | CANCEL | Existing operations |
|---|---|---|---|---|---|---|
| UNPROVISIONED | minor 3; no active role | yes | mutation only with armed APPLY; exact retry/no-op per Section 21 | armed for mutation; completed retry allowed | only while armed | unavailable/BAD_TARGET |
| PROVISIONING_ALLOWED | yes | yes | only if scope APPLY | only if scope FULL_RESET | yes | BUSY |
| PROVISIONING_COMMITTING | yes | yes where safe | no new request; exact retained response only | no | no | BUSY |
| PROVISIONED_HUB | minors 1/2/3 | yes | unchanged/retry without arm; change with armed APPLY | with armed FULL_RESET | only while armed | exact inherited Hub behavior when no window |
| PROVISIONED_NODE | minors 1/2/3 | yes | unchanged/retry without arm; change with armed APPLY | with armed FULL_RESET | only while armed | exact inherited Node behavior when no window |
| INVALID_PROVISIONING | minor 3; no active role | yes | prohibited | armed FULL_RESET with expected generation `0xFFFFFFFF` | only while armed | unavailable/BAD_TARGET |
| RESET_COMMITTING | yes | yes where safe | no | no new request; exact retained response only | no | BUSY |
| REBOOT_REQUIRED | minor 3; no active role | yes | exact durable retry only | exact completed reset retry only | no | BUSY/unavailable |

Read-only status is never RF-bridged. During committing, ordinary single-active
Host lifecycle may return BUSY until the accepted operation reaches a retained
terminal response. If Host disconnect clears only partial framing, accepted
work continues under existing lifecycle rules.

## 24. Security boundary

Persistent REDLINE identity is routing/configuration identity. A provisioned
peer ID is not cryptographic authentication, confidentiality, authenticated
freshness, anti-spoofing, or proof that an RF packet came from that peer.

Provisioning relies on local physical confirmation, direct USB attachment, and
wrong-board targeting. Host CRC is corruption detection, not Host identity.
A malicious attached computer during an intentionally opened window remains in
the trust boundary. No credentials or secrets exist in these structures.

Wire Protocol 1 remains unauthenticated and unencrypted. F-02 forgeable peer
metadata, F-03 unauthenticated response work, and F-04 replay after short
sequence reuse remain open for v0.9 authenticated transport.

## 25. Parser and buffer limits

| Addition | Value bytes | Host payload | Decoded frame | Maximum COBS frame including delimiter |
|---|---:|---:|---:|---:|
| GET status request / CANCEL request | 0 | 7 | 17 | 19 |
| provisioning result response | 0 | 9 | 19 | 21 |
| FULL_FACTORY_RESET request | 16 | 23 | 33 | 35 |
| APPLY_PROVISIONING request | 40 | 47 | 57 | 59 |
| GET status response | 72 | 81 | 91 | 93 |

The new worst case is the status response: payload 81, decoded 91, maximum 93
bytes after COBS plus delimiter. It is below payload 128, decoded 138, and
encoded 140. Existing Host 0.2's 111-byte Event-diagnostics value remains
larger than every new 0.3 value and is unchanged. No dynamic/unbounded value is
permitted.

## 26. Normative golden vectors

All vectors use firmware target `0.8.0`, Wire Protocol 1, Configuration Schema
1, HELTEC_V4 profile 1, and little-endian fields. Hardware identities are shown
in the same byte order as `F8:5B:1B:A2:A0:E4` and
`F8:5B:1B:A1:0A:E0`. Network ID is `0x11223344`. APPLY labels are `HUB-A` and
`NODE-A`. Every CRC and COBS line was generated and independently decoded with
the released reference utility's CRC/COBS routines during documentation
freeze. “Before CRC” includes the complete decoded header and payload.

### 26.1 HELLO selects 3

```text
HELLO_REQUEST, range 1..3, request 0x3001
before CRC: 00 03 01 00 01 30 02 00 01 03
CRC: 2CDA (field DA 2C)
decoded: 00 03 01 00 01 30 02 00 01 03 DA 2C
COBS + delimiter: 01 03 03 01 04 01 30 02 05 01 03 DA 2C 00

HELLO_RESPONSE, unprovisioned/no active role, provisioning only
before CRC: 00 03 02 00 01 30 10 00 03 00 08 00 01 01 01 00 00 80 20 00 08 00 01 00
CRC: 93D0 (field D0 93)
decoded: 00 03 02 00 01 30 10 00 03 00 08 00 01 01 01 00 00 80 20 00 08 00 01 00 D0 93
COBS + delimiter: 01 03 03 02 04 01 30 10 02 03 02 08 04 01 01 01 01 03 80 20 02 08 02 01 03 D0 93 00
```

### 26.2 Provisioning status

The response represents virgin UNPROVISIONED, missing identity records,
compiled HELTEC_V4, valid empty Event summary, and no volatile counters.

```text
GET_PROVISIONING_STATUS request, request 0x3002
before CRC: 00 03 10 00 02 30 07 00 06 2C 00 00 00 00 00
CRC: F699 (field 99 F6)
decoded: 00 03 10 00 02 30 07 00 06 2C 00 00 00 00 00 99 F6
COBS + delimiter: 01 03 03 10 04 02 30 07 03 06 2C 01 01 01 01 03 99 F6 00

GET_PROVISIONING_STATUS SUCCESS/OK, 72-byte schema 1
before CRC: 00 03 11 00 02 30 51 00 06 2C 00 00 00 00 00 7F 48 01 00 00 00 00 00 01 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 F8 5B 1B A2 A0 E4 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
CRC: 4180 (field 80 41)
decoded: 00 03 11 00 02 30 51 00 06 2C 00 00 00 00 00 7F 48 01 00 00 00 00 00 01 00 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 F8 5B 1B A2 A0 E4 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 80 41
COBS + delimiter: 01 03 03 11 04 02 30 51 03 06 2C 01 01 01 01 04 7F 48 01 01 01 01 01 02 01 01 01 01 02 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 07 F8 5B 1B A2 A0 E4 01 01 02 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 03 80 41 00
```

### 26.3 Valid Hub and Node APPLY requests

Both acknowledge Event erasure and expect unprovisioned generation zero.

```text
APPLY Hub request 0x3003
before CRC: 00 03 10 00 03 30 2F 00 06 2D 00 00 00 7F 28 01 01 01 10 44 33 22 11 01 01 05 01 48 55 42 2D 41 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 F8 5B 1B A2 A0 E4 00 00
CRC: F545 (field 45 F5)
decoded: 00 03 10 00 03 30 2F 00 06 2D 00 00 00 7F 28 01 01 01 10 44 33 22 11 01 01 05 01 48 55 42 2D 41 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 F8 5B 1B A2 A0 E4 00 00 45 F5
COBS + delimiter: 01 03 03 10 04 03 30 2F 03 06 2D 01 01 14 7F 28 01 01 01 10 44 33 22 11 01 01 05 01 48 55 42 2D 41 01 01 01 01 01 01 01 01 01 01 01 01 01 01 07 F8 5B 1B A2 A0 E4 01 03 45 F5 00

APPLY Node request 0x3004
before CRC: 00 03 10 00 04 30 2F 00 06 2D 00 00 00 7F 28 01 02 10 01 44 33 22 11 01 01 06 01 4E 4F 44 45 2D 41 00 00 00 00 00 00 00 00 00 00 00 00 00 00 F8 5B 1B A1 0A E0 00 00
CRC: 246B (field 6B 24)
decoded: 00 03 10 00 04 30 2F 00 06 2D 00 00 00 7F 28 01 02 10 01 44 33 22 11 01 01 06 01 4E 4F 44 45 2D 41 00 00 00 00 00 00 00 00 00 00 00 00 00 00 F8 5B 1B A1 0A E0 00 00 6B 24
COBS + delimiter: 01 03 03 10 04 04 30 2F 03 06 2D 01 01 15 7F 28 01 02 10 01 44 33 22 11 01 01 06 01 4E 4F 44 45 2D 41 01 01 01 01 01 01 01 01 01 01 01 01 01 07 F8 5B 1B A1 0A E0 01 03 6B 24 00

APPLY committed response for 0x3003
before CRC: 00 03 11 00 03 30 09 00 06 2D 00 00 00 06 00 00 00
CRC: 7358 (field 58 73)
decoded: 00 03 11 00 03 30 09 00 06 2D 00 00 00 06 00 00 00 58 73
COBS + delimiter: 01 03 03 11 04 03 30 09 03 06 2D 01 01 02 06 01 01 03 58 73 00
```

### 26.4 Physical presence required

The request is structurally identical to the Hub proposal but has request ID
0x3005 and no armed APPLY window.

```text
request before CRC: 00 03 10 00 05 30 2F 00 06 2D 00 00 00 7F 28 01 01 01 10 44 33 22 11 01 01 05 01 48 55 42 2D 41 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 F8 5B 1B A2 A0 E4 00 00
CRC: 7C4E (field 4E 7C)
decoded: 00 03 10 00 05 30 2F 00 06 2D 00 00 00 7F 28 01 01 01 10 44 33 22 11 01 01 05 01 48 55 42 2D 41 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 F8 5B 1B A2 A0 E4 00 00 4E 7C
COBS + delimiter: 01 03 03 10 04 05 30 2F 03 06 2D 01 01 14 7F 28 01 01 01 10 44 33 22 11 01 01 05 01 48 55 42 2D 41 01 01 01 01 01 01 01 01 01 01 01 01 01 01 07 F8 5B 1B A2 A0 E4 01 03 4E 7C 00

response before CRC: 00 03 11 00 05 30 09 00 06 2D 00 00 00 06 10 00 00
CRC: BF50 (field 50 BF)
decoded: 00 03 11 00 05 30 09 00 06 2D 00 00 00 06 10 00 00 50 BF
COBS + delimiter: 01 03 03 11 04 05 30 09 03 06 2D 01 01 03 06 10 01 03 50 BF 00
```

### 26.5 Malformed APPLY structure

The value length is 39 rather than 40. No field is interpreted or mutated.

```text
request before CRC: 00 03 10 00 06 30 2E 00 06 2D 00 00 00 7F 27 01 01 01 10 44 33 22 11 01 01 05 01 48 55 42 2D 41 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 F8 5B 1B A2 A0 E4 00
CRC: F60F (field 0F F6)
decoded: 00 03 10 00 06 30 2E 00 06 2D 00 00 00 7F 27 01 01 01 10 44 33 22 11 01 01 05 01 48 55 42 2D 41 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 F8 5B 1B A2 A0 E4 00 0F F6
COBS + delimiter: 01 03 03 10 04 06 30 2E 03 06 2D 01 01 14 7F 27 01 01 01 10 44 33 22 11 01 01 05 01 48 55 42 2D 41 01 01 01 01 01 01 01 01 01 01 01 01 01 01 07 F8 5B 1B A2 A0 E4 03 0F F6 00

MALFORMED_REQUEST response before CRC: 00 03 11 00 06 30 09 00 06 2D 00 00 00 01 01 00 00
CRC: 558B (field 8B 55)
decoded: 00 03 11 00 06 30 09 00 06 2D 00 00 00 01 01 00 00 8B 55
COBS + delimiter: 01 03 03 11 04 06 30 09 03 06 2D 01 01 03 01 01 01 03 8B 55 00
```

### 26.6 Equal local/peer ID

```text
request 0x3007 before CRC: 00 03 10 00 07 30 2F 00 06 2D 00 00 00 7F 28 01 01 01 01 44 33 22 11 01 01 05 01 48 55 42 2D 41 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 F8 5B 1B A2 A0 E4 00 00
CRC: B5BC (field BC B5)
decoded: 00 03 10 00 07 30 2F 00 06 2D 00 00 00 7F 28 01 01 01 01 44 33 22 11 01 01 05 01 48 55 42 2D 41 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 F8 5B 1B A2 A0 E4 00 00 BC B5
COBS + delimiter: 01 03 03 10 04 07 30 2F 03 06 2D 01 01 14 7F 28 01 01 01 01 44 33 22 11 01 01 05 01 48 55 42 2D 41 01 01 01 01 01 01 01 01 01 01 01 01 01 01 07 F8 5B 1B A2 A0 E4 01 03 BC B5 00

response before CRC: 00 03 11 00 07 30 09 00 06 2D 00 00 00 06 14 00 00
CRC: E956 (field 56 E9)
decoded: 00 03 11 00 07 30 09 00 06 2D 00 00 00 06 14 00 00 56 E9
COBS + delimiter: 01 03 03 11 04 07 30 09 03 06 2D 01 01 03 06 14 01 03 56 E9 00
```

### 26.7 Legacy Event acknowledgement required

This valid proposal has request flags zero while status proves legacy Event
state exists.

```text
request 0x3008 before CRC: 00 03 10 00 08 30 2F 00 06 2D 00 00 00 7F 28 01 01 01 10 44 33 22 11 01 01 05 00 48 55 42 2D 41 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 F8 5B 1B A2 A0 E4 00 00
CRC: CAEC (field EC CA)
decoded: 00 03 10 00 08 30 2F 00 06 2D 00 00 00 7F 28 01 01 01 10 44 33 22 11 01 01 05 00 48 55 42 2D 41 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 F8 5B 1B A2 A0 E4 00 00 EC CA
COBS + delimiter: 01 03 03 10 04 08 30 2F 03 06 2D 01 01 0E 7F 28 01 01 01 10 44 33 22 11 01 01 05 06 48 55 42 2D 41 01 01 01 01 01 01 01 01 01 01 01 01 01 01 07 F8 5B 1B A2 A0 E4 01 03 EC CA 00

response before CRC: 00 03 11 00 08 30 09 00 06 2D 00 00 00 06 1A 00 00
CRC: 3305 (field 05 33)
decoded: 00 03 11 00 08 30 09 00 06 2D 00 00 00 06 1A 00 00 05 33
COBS + delimiter: 01 03 03 11 04 08 30 09 03 06 2D 01 01 03 06 1A 01 03 05 33 00
```

### 26.8 Generation mismatch

The request incorrectly expects generation 7.

```text
request 0x3009 before CRC: 00 03 10 00 09 30 2F 00 06 2D 00 00 00 7F 28 01 01 01 10 44 33 22 11 01 01 05 01 48 55 42 2D 41 00 00 00 00 00 00 00 00 00 00 00 07 00 00 00 F8 5B 1B A2 A0 E4 00 00
CRC: 7632 (field 32 76)
decoded: 00 03 10 00 09 30 2F 00 06 2D 00 00 00 7F 28 01 01 01 10 44 33 22 11 01 01 05 01 48 55 42 2D 41 00 00 00 00 00 00 00 00 00 00 00 07 00 00 00 F8 5B 1B A2 A0 E4 00 00 32 76
COBS + delimiter: 01 03 03 10 04 09 30 2F 03 06 2D 01 01 14 7F 28 01 01 01 10 44 33 22 11 01 01 05 01 48 55 42 2D 41 01 01 01 01 01 01 01 01 01 01 02 07 01 01 07 F8 5B 1B A2 A0 E4 01 03 32 76 00

response before CRC: 00 03 11 00 09 30 09 00 06 2D 00 00 00 06 19 00 00
CRC: 2F36 (field 36 2F)
decoded: 00 03 11 00 09 30 09 00 06 2D 00 00 00 06 19 00 00 36 2F
COBS + delimiter: 01 03 03 11 04 09 30 09 03 06 2D 01 01 03 06 19 01 03 36 2F 00
```

### 26.9 Full factory reset

The request expects generation 1, acknowledges Event erasure, targets the Hub
hardware identity, and contains literal `FULL`.

```text
FULL_FACTORY_RESET request 0x300A
before CRC: 00 03 10 00 0A 30 17 00 06 2E 00 00 00 7F 10 01 01 01 00 00 00 F8 5B 1B A2 A0 E4 46 55 4C 4C
CRC: 1E41 (field 41 1E)
decoded: 00 03 10 00 0A 30 17 00 06 2E 00 00 00 7F 10 01 01 01 00 00 00 F8 5B 1B A2 A0 E4 46 55 4C 4C 41 1E
COBS + delimiter: 01 03 03 10 04 0A 30 17 03 06 2E 01 01 06 7F 10 01 01 01 01 01 0D F8 5B 1B A2 A0 E4 46 55 4C 4C 41 1E 00

RESET_REBOOT_REQUIRED response
before CRC: 00 03 11 00 0A 30 09 00 06 2E 00 00 00 06 03 00 00
CRC: AC44 (field 44 AC)
decoded: 00 03 11 00 0A 30 09 00 06 2E 00 00 00 06 03 00 00 44 AC
COBS + delimiter: 01 03 03 11 04 0A 30 09 03 06 2E 01 01 03 06 03 01 03 44 AC 00
```

### 26.10 Invalid lifecycle and protocol error

```text
APPLY INVALID_LIFECYCLE_STATE response, request 0x300B
before CRC: 00 03 11 00 0B 30 09 00 06 2D 00 00 00 06 11 00 00
CRC: 0C51 (field 51 0C)
decoded: 00 03 11 00 0B 30 09 00 06 2D 00 00 00 06 11 00 00 51 0C
COBS + delimiter: 01 03 03 11 04 0B 30 09 03 06 2D 01 01 03 06 11 01 03 51 0C 00

PROTOCOL_ERROR/UNSUPPORTED_MESSAGE_TYPE for offending type 0x55, request 0x30FF
before CRC: 00 03 7F 00 FF 30 04 00 03 55 00 00
CRC: 90B2 (field B2 90)
decoded: 00 03 7F 00 FF 30 04 00 03 55 00 00 B2 90
COBS + delimiter: 01 03 03 7F 04 FF 30 04 03 03 55 01 03 B2 90 00
```

### 26.11 Frozen minor-2 control vector

This is copied byte-for-byte from Host Protocol 0.2 and independently
reproduced. A 0.3 implementation MUST retain it exactly.

```text
HELLO_REQUEST 0.2, range 1..2, request 0x1234
before CRC: 00 02 01 00 34 12 02 00 01 02
CRC: F762 (field 62 F7)
decoded: 00 02 01 00 34 12 02 00 01 02 62 F7
COBS + delimiter: 01 03 02 01 04 34 12 02 05 01 02 62 F7 00
```

## 27. Complete numeric registry

| Namespace | Assignment |
|---|---|
| Protocol | major `0`, minor `3` |
| Messages | unchanged: `01`, `02`, `10`, `11`, `7F` |
| Category | PROVISIONING `0x06`, bitmap `0x0020`, minor-3 reserved mask `0xFFC0` |
| Feature | controlled provisioning bit 3 / `0x0008`, minor-3 reserved mask `0xFFF0` |
| Operations | status `0x2C`, apply `0x2D`, full reset `0x2E`, cancel `0x2F` |
| Result class | PROVISIONING_RESULT `0x06` |
| Result codes | `00..06`, `10..1D` exactly as Section 19 |
| Host-local target | target device `0x00`, target ID `0x0000`, provisioning/minor-3 only |
| HELLO role extension | NO_ACTIVE_ROLE `0x00`, minor-3 only |
| Device IDs | valid `0x01..0xFE`; `0x00`, `0xFF` reserved |
| Value types | unchanged; operation-specific STRUCTURE `0x7F` |
| Status structure | schema 1, 72 bytes |
| APPLY structure | schema 1, 40 bytes |
| Reset structure | schema 1, 16 bytes |
| APPLY flags | ACK_EVENT_ERASURE bit 0 / `0x01`; mask `0xFE` reserved |
| Reset flags | ACK_EVENT_ERASURE bit 0 / `0x01`; mask `0xFE` reserved |
| Factory token | ASCII `FULL` / `46 55 4C 4C` |
| Expected-generation sentinels | unprovisioned `0`; invalid reset only `0xFFFFFFFF` |
| Provisioning generations | `1..0xFFFFFFFE`, no wrap |
| Hardware profile | HELTEC_V4 `0x01` |
| Capability profile | HELTEC_V4_BASE `0x01` |
| Lifecycle | `00..07` exactly as Section 12 |
| Identity storage state | `00..06` exactly as Section 12 |
| Authorization/pending kind | NONE `0`, APPLY `1`, FULL_RESET `2` |
| Event-domain state | UNKNOWN `0`, EMPTY `1`, PRESENT `2`, INVALID `3`, UNAVAILABLE `4` |
| Status flags | bits `0..9` as Section 13; mask `0xFC00` reserved |
| Maximum Host payload | `128` |
| Maximum outstanding operations | `1` |

Numeric values overlap only across explicitly separate namespaces (for example
result codes under different result classes). Category `0x06`, operations
`0x2C..0x2F`, result class `0x06`, feature bit `0x0008`, category bit `0x0020`,
and role `0x00` were unassigned in their respective minor-1/2 namespaces.

## 28. Implementation invariants

Implementation and tests MUST prove:

1. Every minor-1 vector and behavior remains exact 0.1.
2. Every minor-2 vector and behavior remains exact 0.2.
3. Minor-3 frame authority is retained through immediate, deferred, and
   replayed responses.
4. Provisioning advertisement is role/service truthful and absent from lower
   selected minors.
5. Sentinel `0x00` never reaches a Wire builder or non-provisioning dispatcher.
6. Status is fixed, read-only, non-secret, and non-mutating.
7. No provisioning mutation occurs before complete validation, Host lifecycle
   admission, physical scope, and durable PENDING ordering.
8. No successful response precedes commit/readback/byte verification.
9. Exact durable retries are recognized by complete content plus generation,
   never request ID alone.
10. A divergent/stale request cannot overwrite current or pending authority.
11. Event-domain acknowledgement and clear apply to both Node and Hub state.
12. Reprovision/reset failures remain PENDING/radio-off after destructive work
    begins.
13. Unprovisioned/invalid/pending/reboot-required devices emit no ordinary RF.
14. Parser/value/frame bounds in Section 25 are compile-time asserted.
15. All Section 26 CRC and COBS vectors reproduce byte-for-byte.
16. No credential, authenticated identity, or security success is inferred.

## 29. Explicit non-goals

Host Protocol 0.3 does not introduce:

- RF provisioning, RF reset, or Wire Protocol changes;
- credentials, keys, certificates, authenticated Host users, authenticated RF
  peers, encryption, replay protection, rotation, or revocation;
- F-02, F-03, or F-04 remediation;
- more than one peer, multi-Node registry/scheduling, routing, repeaters,
  relays, or mesh;
- arbitrary persistent fields, general strings/blobs, raw NVS access, or a
  management shell;
- dynamic role switching without reboot;
- persistent Host request history or general cross-reset exactly-once RPC;
- multiple outstanding Host operations or unsolicited frames;
- changes to Configuration Schema 1 settings semantics;
- changes to Event identity, custody, retry, admission, POLL, or CONSUME; or
- a stable multi-client Host API.
