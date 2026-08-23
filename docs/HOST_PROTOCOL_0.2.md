# ARGUS REDLINE Host Protocol 0.2

**Status:** Normative developmental specification for v0.7.0; not a stable API  
**Version:** major `0`, minor `2`  
**Predecessor:** Host Protocol `0.1`, frozen for minor-1 frames  
**Architecture authority:** `V0.7.0_ARCHITECTURE_BASELINE.md`  
**Event design authority:** `V0.7.0_EVENT_RELIABILITY_DESIGN.md`

## 1. Purpose and Scope

Host Protocol 0.2 adds bounded local Hub Event retrieval and explicit
consumption to the developmental computer-to-device protocol. The Host remains
the initiator. Event service uses synchronous `POLL_EVENTS` and
`CONSUME_EVENT`; no unsolicited frame is introduced.

This specification encodes the accepted v0.7.0 Event custody model. It does not
change Wire Event admission, Node delivery, Hub ledger persistence, Event
families, retry, expiry, or radio arbitration.

Normative `MUST`, `MUST NOT`, `SHOULD`, and `MAY` statements are requirements.
Multi-byte integers are unsigned little-endian unless stated otherwise.

## 2. Compatibility with Host Protocol 0.1

Host Protocol 0.1 is frozen. A 0.2-capable device supports both:

```text
frame major 0 / minor 1 -> exact Host Protocol 0.1 semantics
frame major 0 / minor 2 -> Host Protocol 0.2 semantics in this document
```

The minor byte in every frame is authoritative. HELLO negotiation does not
create hidden connection state and never changes how a later frame is decoded.
A minor-1 frame cannot carry category EVENT, Event feature advertisement,
POLL_EVENTS, CONSUME_EVENT, request STRUCTURE values, or 0.2 result classes.

For minor-1 frames a 0.2 implementation MUST preserve the exact 0.1 accepted
versions, HELLO range rules and response bytes, categories, feature/reserved
bits, message/operation registry, value constraints, errors, lifecycle,
diagnostics, and parser recovery. Shared codec knowledge is not permission to
extend minor 1.

Existing Host Protocol 0.1 operations and numeric assignments are unchanged in
minor 2. A Host may use minor 1 indefinitely for v0.6 behavior.

## 3. Framing and Bounds

Framing remains:

```text
COBS(decoded frame) || 0x00
```

Decoded geometry is unchanged:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | protocol major |
| 1 | 1 | protocol minor |
| 2 | 1 | message type |
| 3 | 1 | flags |
| 4 | 2 | request ID |
| 6 | 2 | payload length `N` |
| 8 | `N` | payload, 0..128 bytes |
| 8+N | 2 | CRC-16/CCITT-FALSE |

Decoded length is exactly `10+N`, 10..138 bytes. The maximum non-delimiter COBS
candidate is 139 bytes and maximum encoded frame including delimiter is 140.
Buffers MUST be compile-time bounded.

CRC remains CRC-16/CCITT-FALSE: polynomial `0x1021`, initial `0xFFFF`, no
reflection, final XOR `0x0000`, covering decoded bytes 0 through the final
payload byte. CRC is stored low byte then high byte. It detects corruption; it
does not authenticate the Host or device.

Standard COBS and delimiter recovery are unchanged. USB CDC packet boundaries,
text encodings, line endings, and connection timing have no framing meaning.

## 4. Message Types, Flags, and Request IDs

No new message type is added:

| Value | Name | Direction |
|---:|---|---|
| `0x01` | `HELLO_REQUEST` | Host -> device |
| `0x02` | `HELLO_RESPONSE` | device -> Host |
| `0x10` | `OPERATION_REQUEST` | Host -> device |
| `0x11` | `OPERATION_RESPONSE` | device -> Host |
| `0x7F` | `PROTOCOL_ERROR` | device -> Host |

Flags MUST be `0x00`. Request ID zero remains reserved for responses that
cannot correlate to a parsed request. Host requests use `0x0001..0xFFFF` and
responses copy the request ID.

Request IDs are volatile Host-link correlation values. They are not Event
identity, authentication, or cross-reset idempotency.

Host Protocol 0.2 retains one active Host operation and one retained completion.
It does not add multiple outstanding operations.

Every accepted request MUST retain the protocol minor from its decoded frame.
Every correlated response--whether immediate, produced after deferred operation
completion, or replayed from retained completion--MUST use that retained request
minor. A minor-1 request therefore always receives a minor-1 correlated response;
raising the firmware's highest supported minor to 2 MUST NOT change its response
minor to 2. A response with request ID zero may use the decoded inbound minor only
when that minor is trustworthy under the existing parser validation rules.

## 5. Version Handling and HELLO

### 5.1 HELLO request

HELLO_REQUEST remains exactly two bytes:

```text
minimum minor   uint8
maximum minor   uint8
```

For a minor-1 frame, both bytes MUST equal 1 as Host Protocol 0.1 requires.

For a minor-2 frame:

- minimum and maximum must be nonzero and minimum must not exceed maximum;
- the device selects the highest supported minor in the requested inclusive
  range from `{1,2}`;
- no overlap returns `PROTOCOL_ERROR/UNSUPPORTED_MINOR`; and
- the HELLO_RESPONSE frame minor remains 2 because it answers a minor-2
  request, even when `selected minor` is 1. The Host uses its selected minor in
  each later frame; the device retains no negotiated-version state.

HELLO is discovery only. It MUST NOT create connection-version state, and it
MUST NOT alter the interpretation or response minor of any later frame.

### 5.2 HELLO response

HELLO_RESPONSE remains exactly 16 bytes:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | selected minor |
| 1 | 1 | firmware major |
| 2 | 1 | firmware minor |
| 3 | 1 | firmware patch |
| 4 | 1 | Wire Protocol |
| 5 | 1 | Configuration Schema |
| 6 | 1 | hardware profile (`HELTEC_V4=0x01`) |
| 7 | 1 | role (`HUB=0x01`, `NODE=0x02`) |
| 8 | 1 | device ID |
| 9 | 1 | maximum Host payload (`128`) |
| 10 | 2 | operation-category bitmap |
| 12 | 2 | feature bitmap |
| 14 | 1 | maximum outstanding operations (`1`) |
| 15 | 1 | reserved zero |

The reported selected minor determines which bitmap vocabulary applies. A
minor-2 response selecting 1 MUST report category/feature bits exactly as a
Host Protocol 0.1 implementation for that role; Event bits remain zero.

For selected minor 2, category bit 4 and Event-service feature bit 2 are set
only by a Hub that implements the complete local persistent Event service. A
Node or incomplete Hub MUST clear both. Codec support alone is insufficient.
Reserved bits and the final reserved byte MUST be zero.

## 6. Operation Categories

| Value | Category | Category bitmap bit |
|---:|---|---:|
| `0x01` | `DEVICE` | 0 |
| `0x02` | `CAPABILITY` | 1 |
| `0x03` | `PROCEDURE` | 2 |
| `0x04` | `DIAGNOSTIC` | 3 |
| `0x05` | `EVENT` | 4 |

For selected minor 2, category bitmap bits 5..15 are reserved zero. EVENT is a
Host-local Hub service category. It is not a Wire Event family and is never
bridged as a radio COMMAND.

## 7. Feature Bitmap

| Bit/value | Feature |
|---:|---|
| bit 0 / `0x0001` | local operations |
| bit 1 / `0x0002` | structured radio-operation bridge |
| bit 2 / `0x0004` | persistent local Hub Event service |

For selected minor 2, bits 3..15 are reserved zero. Event-service bit 2 MUST
equal category bit 4: both set for a complete Hub service, both clear
otherwise. Existing feature meaning is unchanged.

For minor-1 HELLO, only bits 0 and 1 exist and bits 2..15 MUST be zero.

## 8. Operation Registry

Existing operation values remain:

```text
PING                 0x20
GET_DEVICE_INFO      0x21
GET_STATUS           0x22
GET_CAPABILITIES     0x23
DESCRIBE_CAPABILITY  0x24
READ_CAPABILITY      0x25
SET_INDICATOR        0x26
RUN_PROCEDURE        0x27
GET_DIAGNOSTICS      0x28
```

Host Protocol 0.2 adds:

| Category | Value | Operation | Dispatch |
|---|---:|---|---|
| EVENT | `0x29` | `POLL_EVENTS` | local Hub only |
| EVENT | `0x2A` | `CONSUME_EVENT` | local Hub only |

Only category EVENT may pair with 0x29 or 0x2A. Existing category/operation
pairs are unchanged. A valid EVENT request sent to a role without advertised
Event service receives `REQUEST_REJECTED/UNSUPPORTED_OPERATION`.

Host operation values and Wire EVENT family opcodes are separate namespaces.
For example, Host `POLL_EVENTS=0x29` is unrelated to Wire family
`BUTTON=0x40`.

## 9. OPERATION_REQUEST and OPERATION_RESPONSE Envelopes

### 9.1 OPERATION_REQUEST

The 0.1 envelope is unchanged:

```text
category          uint8
operation         uint8
target device ID  uint8
target ID         uint16
value type        uint8
value length      uint8
value             0..121 bytes
```

Payload length is `7+value length`. EVENT operations are local to the
connected Hub: target device ID MUST equal the Hub's local device ID and target
ID MUST be zero. They MUST NOT start or join a Wire transaction.

### 9.2 OPERATION_RESPONSE

The 0.1 envelope is unchanged:

```text
category          uint8
operation         uint8
target device ID  uint8
target ID         uint16
result class      uint8
result code       uint8
value type        uint8
value length      uint8
value             0..119 bytes
```

Payload length is `9+value length`. Category, operation, target device ID, and
target ID echo the accepted request. Values appear only where the exact result
schema permits them. All irrelevant fields are zero.

## 10. Value Vocabulary

Host Protocol 0.2 retains all Host Protocol 0.1 scalar types:

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

`STRUCTURE` remains operation-specific, never a generic byte array. Minor 2
adds exactly these Event uses:

- POLL_EVENTS success response: 29-byte Host Event record;
- CONSUME_EVENT request: 9-byte Host Event identity.

POLL_EVENTS request and successful CONSUME_EVENT response use NONE/length 0.
Error responses use NONE/length 0. No string, arbitrary blob, or generic
application payload is created.

For minor-1 frames, STRUCTURE remains response-only and constrained exactly by
Host Protocol 0.1.

## 11. Host Event Identity

Host Event identity is exactly nine bytes:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | Source_Device_ID |
| 1 | 4 | Event_Epoch |
| 5 | 4 | Event_ID |

Epoch and ID are little-endian and nonzero. This tuple is persistent across
Node retry/reboot and Hub reboot. It contains no Runtime_Session_ID or boot
nonce.

Identity is reliability/correlation state, not authenticated device identity,
cryptographic freshness, a credential, or security anti-replay state.

## 12. Host Event Record

POLL_EVENTS returns one fixed 29-byte STRUCTURE:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | available (`0` or `1`) |
| 1 | 1 | source device ID |
| 2 | 1 | Event family/Wire opcode |
| 3 | 1 | Event flags |
| 4 | 4 | Event epoch |
| 8 | 4 | Event ID |
| 12 | 4 | original lifetime budget seconds |
| 16 | 1 | Event body length `N` |
| 17 | 12 | Event body; unused tail zero |

When available is 1, every field must satisfy the Event reliability design:
known v0.7 family, nonzero identity, only known flags, lifetime 60..86,400,
exact family body length/schema, and zero unused body tail.

When available is 0, bytes 1..28 MUST all be zero. Absence is therefore an
explicit successful value, not NONE and not an error.

The complete record fits comfortably in the 128-byte Host payload: the
OPERATION_RESPONSE payload is `9+29=38` bytes and decoded frame is 48 bytes.
Exactly one Event is returned; batching is prohibited.

Hub `admission_ordinal` is deliberately not Host-visible. It is internal
persistent ledger ordering state. POLL's contract already guarantees oldest
ACTIVE selection, while exposing the ordinal would create an unnecessary
storage-generation token that applications could mistake for Event identity.

No native C/C++ structure may be serialized. All fields are encoded and
validated explicitly.

## 13. POLL_EVENTS

### 13.1 Request

```text
category          EVENT (0x05)
operation         POLL_EVENTS (0x29)
target device ID  connected Hub ID
target ID         0
value type        NONE (0x00)
value length      0
```

Any other target ID, value type, length, or trailing value is malformed and
cannot inspect or mutate the ledger.

### 13.2 Response

If Event service is implemented and storage is healthy, POLL returns
`SUCCESS/OK`, value type STRUCTURE, length 29.

- If ACTIVE Events exist, it returns the Event with the lowest/oldest
  persistent Hub admission ordinal and `available=1`.
- If none exist, it returns the all-zero absence record with `available=0`.

Both are successful queries. POLL is non-destructive, never changes ACTIVE or
CONSUMED state, does not transfer custody, and does not change admission order.
Repeated polls may return the same Event. Replacing the volatile Host retained
completion cannot consume an Event.

Storage corruption/failure returns `EVENT_RESULT/STORAGE_FAILURE` with NONE.
No Event is silently skipped to manufacture a successful result.

## 14. CONSUME_EVENT

### 14.1 Request

```text
category          EVENT (0x05)
operation         CONSUME_EVENT (0x2A)
target device ID  connected Hub ID
target ID         0
value type        STRUCTURE (0x7F)
value length      9
value             Host Event identity from Section 11
```

Complete envelope, exact length, nonzero identity, target, and reserved rules
MUST validate before ledger lookup or mutation. Malformed requests return
`REQUEST_REJECTED/MALFORMED_REQUEST` with NONE.

### 14.2 Behavior and response

| Ledger result | Mutation | Host result |
|---|---|---|
| matching ACTIVE | durably commit ACTIVE -> CONSUMED, retaining identity/content/admission ordinal | `SUCCESS/OK`, NONE |
| matching retained CONSUMED | none | `SUCCESS/OK`, NONE |
| identity absent | none | `EVENT_RESULT/NOT_FOUND`, NONE |
| corrupt/write/read-back failure | no reported success | `EVENT_RESULT/STORAGE_FAILURE`, NONE |

Host success is emitted only after durable transition commit/read-back or
existing durable CONSUMED proof. A lost Host response after commit is safely
retried: the ledger proves idempotent consumption even if the volatile Host
retry cache was lost. Consumption does not alter Event identity, canonical
content, or admission ordinal.

Host disconnect never consumes an Event. Hub reboot preserves ACTIVE and
CONSUMED state as defined by the Event reliability design.

## 15. Result Classes and Codes

Host Protocol 0.2 retains all Host Protocol 0.1 classes/codes and adds one
class for persistent Event-ledger outcomes:

```text
EVENT_RESULT = 0x05
```

EVENT_RESULT codes are:

| Value | Code | Meaning |
|---:|---|---|
| `0x01` | `NOT_FOUND` | requested Event identity is absent from ACTIVE and retained CONSUMED state |
| `0x02` | `STORAGE_FAILURE` | authoritative Hub Event state cannot be safely read or committed |

This class is required because absence/custody-storage failure is neither a
capability `OPERATION_RESULT`, a radio result, nor a transient generic runtime
failure. Equal numeric codes in other classes retain their own meanings.

Required mappings:

| Condition | Result |
|---|---|
| malformed POLL or consume | `REQUEST_REJECTED/MALFORMED_REQUEST` |
| known Event operation on unsupported role/service | `REQUEST_REJECTED/UNSUPPORTED_OPERATION` |
| wrong local target | `REQUEST_REJECTED/BAD_TARGET` |
| other request active | `REQUEST_REJECTED/BUSY` |
| same active/retained request ID, different payload | `REQUEST_REJECTED/MISMATCH` |
| POLL, no Event | `SUCCESS/OK` + absence STRUCTURE |
| POLL, Event found | `SUCCESS/OK` + Event STRUCTURE |
| consume ACTIVE | `SUCCESS/OK` after durable commit |
| consume retained CONSUMED | `SUCCESS/OK` idempotently |
| consume identity absent | `EVENT_RESULT/NOT_FOUND` |
| Event-ledger read/write/recovery failure | `EVENT_RESULT/STORAGE_FAILURE` |

Protocol error codes and untrustworthy-frame silent discard behavior remain
unchanged from Host Protocol 0.1.

## 16. Host Operation Lifecycle and Retained Completion

The Host Protocol 0.1 single-active/single-retained lifecycle applies to Event
operations. Retained identity remains request ID plus byte-exact accepted
OPERATION_REQUEST payload.

The active-operation entry and retained-completion entry additionally store the
accepted request's frame minor as response-encoding context. Immediate correlated
responses copy the inbound request minor; deferred completions copy the active
entry's stored minor; retained replays copy the retained entry's stored minor.
Replacing a retained completion replaces this stored context together with the
response. Firmware-wide `CURRENT_MINOR` or `MAX_SUPPORTED_MINOR` constants MUST
NOT be substituted for it.

The volatile Host retry cache and persistent Hub Event ledger are independent:

- replacing a retained Host completion never consumes, deletes, reorders, or
  otherwise mutates an ACTIVE Event;
- an exact retained POLL retry may replay its original response, even if a
  later independent Host operation changed the ledger;
- an exact retained CONSUME retry replays its result without a second mutation;
- after reset, a retried CONSUME can still succeed idempotently from persistent
  CONSUMED proof even though the Host retry cache is gone; and
- request-ID match alone never proves an Event identity or duplicate request.

While active, exact duplicate, mismatched same-ID, and different-ID BUSY rules
are unchanged. After completion, exact replay, mismatch protection, and
replacement by newly accepted work are unchanged.

## 17. Disconnect and Reset Behavior

- Disconnect clears partial Host input/overflow state but does not cancel
  accepted work or consume an Event.
- Reconnect may POLL again; the same ACTIVE Event may reappear.
- Parser, active Host request, and retained Host completion remain volatile as
  in 0.1.
- Hub Event ledger and consumption proof are persistent across Hub reboot.
- Host request IDs do not persist and are not cross-reset Event identities.
- Event identity and admission order are independent of Host connection state.
- Host Protocol does not provide general cross-reset Host request history.

A Host uncertain whether CONSUME completed may safely resubmit the same Event
identity after reconnect/reset. SUCCESS proves retained CONSUMED state; NOT_FOUND
means neither ACTIVE nor retained CONSUMED proof exists and must not be
reinterpreted as proven prior consumption.

## 18. Validation Order

Common order remains:

```text
COBS
-> decoded bounds/minimum
-> declared length/exact geometry
-> CRC
-> major/minor
-> type/direction
-> flags
-> request ID
-> payload envelope
-> category/operation pair
-> target
-> operation-specific value schema
-> Host lifecycle classification
-> Event ledger lookup/mutation
```

No POLL ledger read or CONSUME mutation occurs before applicable validation and
lifecycle admission. Unknown major/minor/type, flags, invalid IDs, malformed
payloads, encoded overflow, CRC failure, and recovery behavior follow 0.1.

A minor-1 frame is validated solely against 0.1 vocabulary. A syntactically
valid minor-2 Event operation on a non-Hub/unsupported service is unsupported,
not malformed. Invalid STRUCTURE length/content is malformed and cannot mutate
the ledger.

## 19. Diagnostics Boundary

Host Protocol 0.1 diagnostic ownership/increment semantics remain unchanged.
New valid POLL/CONSUME work increments `requestsDispatched` once when accepted.
Retained replay and active duplicate do not. REQUEST_REJECTED increments
`busyOrRejectedRequests`; EVENT_RESULT does not because it is an accepted
Event-service outcome.

Logical response handoff increments `responsesEmitted` under existing rules.
Event-service counters such as Host poll/consumption and storage failure remain
owned by the Event runtime design; Host diagnostics MUST NOT double-count them
as radio/capability diagnostics. No diagnostic counter becomes persistent.

## 20. Security Boundary

The USB/serial link provides physical locality, not cryptographic user
identity. Host CRC detects corruption only. POLL and CONSUME do not authenticate
the local user or original radio sender.

Wire Protocol 1 remains unauthenticated and unencrypted. Event identity and
persistent Hub deduplication are reliability mechanisms, not authenticated
identity, credentials, cryptographic freshness, or anti-replay protection.
CONSUME_EVENT changes local Hub custody state but grants no new remote
side-effect authority.

F-02 peer authentication, F-03 authentication-first discard/response
governance, and F-04 authenticated persistent freshness/replay protection remain
deferred. Host Protocol 0.2 does not claim to close them.

## 21. Representative Vectors

These normative vectors use request ID `0x1234` for HELLO, `0x1001` for POLL,
and `0x1002` for CONSUME. HELLO_RESPONSE illustrates a v0.7.0 Hub ID 1 with all
five categories and all three features. The sample Event is BUTTON family
`0x40`, source 2, flags 1, epoch `0x11223344`, ID `0x01020304`, lifetime 3600
seconds, and one-byte body `0x02`. All lines include CRC bytes; encoded lines
include the final delimiter.

For the frozen v0.7 Event registry, BUTTON body `0x02` means `RELEASE`.

```text
HELLO_REQUEST 0.2, supported range 1..2
decoded: 00 02 01 00 34 12 02 00 01 02 62 F7
CRC: F762 (field 62 F7)
COBS + delimiter: 01 03 02 01 04 34 12 02 05 01 02 62 F7 00

HELLO_RESPONSE 0.2, selected 2
decoded: 00 02 02 00 34 12 10 00 02 00 07 00 01 01 01 01 01 80 1F 00 07 00 01 00 2C C5
CRC: C52C (field 2C C5)
COBS + delimiter: 01 03 02 02 04 34 12 10 02 02 02 07 08 01 01 01 01 01 80 1F 02 07 02 01 03 2C C5 00

POLL_EVENTS request
decoded: 00 02 10 00 01 10 07 00 05 29 01 00 00 00 00 DD FD
CRC: FDDD (field DD FD)
COBS + delimiter: 01 03 02 10 04 01 10 07 04 05 29 01 01 01 01 03 DD FD 00

POLL_EVENTS response, no Event
decoded: 00 02 11 00 01 10 26 00 05 29 01 00 00 00 00 7F 1D 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 02 36
CRC: 3602 (field 02 36)
COBS + delimiter: 01 03 02 11 04 01 10 26 04 05 29 01 01 01 01 03 7F 1D 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 03 02 36 00

POLL_EVENTS response, one Event
decoded: 00 02 11 00 01 10 26 00 05 29 01 00 00 00 00 7F 1D 01 02 40 01 44 33 22 11 04 03 02 01 10 0E 00 00 01 02 00 00 00 00 00 00 00 00 00 00 00 0B 49
CRC: 490B (field 0B 49)
COBS + delimiter: 01 03 02 11 04 01 10 26 04 05 29 01 01 01 01 11 7F 1D 01 02 40 01 44 33 22 11 04 03 02 01 10 0E 01 03 01 02 01 01 01 01 01 01 01 01 01 01 03 0B 49 00

CONSUME_EVENT request
decoded: 00 02 10 00 02 10 10 00 05 2A 01 00 00 7F 09 02 44 33 22 11 04 03 02 01 DA CB
CRC: CBDA (field DA CB)
COBS + delimiter: 01 03 02 10 04 02 10 10 04 05 2A 01 01 0E 7F 09 02 44 33 22 11 04 03 02 01 DA CB 00

CONSUME_EVENT success response
decoded: 00 02 11 00 02 10 09 00 05 2A 01 00 00 00 00 00 00 1B 85
CRC: 851B (field 1B 85)
COBS + delimiter: 01 03 02 11 04 02 10 09 04 05 2A 01 01 01 01 01 01 03 1B 85 00

CONSUME_EVENT NOT_FOUND response
decoded: 00 02 11 00 02 10 09 00 05 2A 01 00 00 05 01 00 00 6E 0E
CRC: 0E6E (field 6E 0E)
COBS + delimiter: 01 03 02 11 04 02 10 09 04 05 2A 01 01 03 05 01 01 03 6E 0E 00
```

Implementations MUST reproduce these vectors exactly and retain the Host
Protocol 0.1 vectors for minor-1 compatibility.

## 22. Required Native Tests

Before integration, native tests MUST cover:

- byte-exact minor-1 framing, HELLO, operations, errors, values, bitmaps,
  lifecycle, diagnostics, and golden-vector compatibility;
- minor-2 HELLO ranges selecting 1 or 2, no-overlap error, frame-minor
  authority, and absence of hidden connection-version state;
- immediate, deferred, and retained completion responses preserving the exact
  accepted request minor, including minor-1 requests while firmware supports
  minor 2 and retained minor-1 replay after unrelated minor-2 traffic;
- role-dependent Event category/feature advertisement and equality of both
  bits; all reserved category/feature/HELLO bits and bytes;
- unchanged existing operation categories, values, pairings, responses, and
  remote bridge behavior;
- EVENT category and operation numeric values and rejection under minor 1;
- POLL exact request validation, bad target/type/length/trailing bytes,
  unsupported role, storage failure, no-Event record, one-Event record,
  canonical field validation, 12-byte maximum body, and 38-byte response;
- persistent admission-ordinal oldest selection without exposing ordinal;
- POLL non-destructive/repeated behavior and byte-exact retained retry;
- CONSUME exact nine-byte identity, endian behavior, zero identity, malformed
  length/type/target rejection before ledger mutation;
- ACTIVE->CONSUMED commit-before-success, retained content/ordinal, idempotent
  CONSUMED retry, NOT_FOUND, storage failure, and lost response retry;
- retained Host completion independence from the Event ledger, replacement not
  consuming ACTIVE, active duplicate, BUSY, and MISMATCH behavior;
- Host disconnect/reconnect with ACTIVE Event and no implicit consumption;
- Hub reboot with ACTIVE and CONSUMED Events, volatile Host cache loss, and
  persistent consume proof;
- request-ID mismatch and lack of cross-reset Host request identity;
- EVENT_RESULT class/code validation and prohibited values;
- COBS/CRC, split/concatenated/empty frames, overflow boundaries, malformed
  geometry, recovery at next delimiter, and bounded service; and
- every Section 21 decoded, CRC, COBS, and delimiter golden vector.

These are specification gates; no tests are run by this documentation task.

## 23. Explicit Non-Goals

Host Protocol 0.2 does not introduce:

- unsolicited asynchronous Event frames;
- multi-Event batching;
- multiple outstanding Host operations;
- general/opaque application transport;
- strings or arbitrary blob values;
- persistent Host request history;
- authenticated local-user or radio identity;
- cryptographic freshness or security anti-replay;
- a multi-client stable Host API;
- Host-visible Hub admission ordinals;
- Wire Event admission or radio scheduling; or
- application workflow or product-specific Event meaning.

## 24. Implementation Gate

Implementation may begin only when:

```text
[ ] Minor-1 behavior is frozen by byte-exact regression fixtures
[ ] Minor-2 HELLO selection and frame-minor authority are accepted
[ ] Immediate, deferred, and retained responses preserve their request minor
[ ] EVENT category 0x05 and feature bit 0x0004 are accepted
[ ] POLL_EVENTS 0x29 and CONSUME_EVENT 0x2A are accepted
[ ] 29-byte Host Event record and 9-byte consume identity are frozen
[ ] EVENT_RESULT/NOT_FOUND/STORAGE_FAILURE assignments are accepted
[ ] Role-dependent advertisement and reserved-bit rules are frozen
[ ] Host retained completion remains independent from persistent Event custody
[ ] All representative vectors are independently reproduced in native tests
[ ] Event reliability-design ledger/reboot/custody invariants remain unchanged
[ ] Host Protocol 0.1 remains available without Event behavior
[ ] Security findings F-02, F-03, and F-04 remain open
```
