# ARGUS REDLINE Host Protocol 0.1

**Status:** Normative developmental specification for v0.6.0; not a stable API

**Version:** major `0`, minor `1`

## 1. Purpose and scope

Host Protocol 0.1 is the bounded machine protocol between a local computer and
REDLINE firmware over a byte stream. It carries negotiation and structured
REDLINE operations. It does not carry human commands, arbitrary application
payloads, or application meaning, and it does not define the future stable
host service/API.

The initial physical transport is ESP32-S3 USB CDC/serial as a reliable,
ordered byte stream. The codec MUST NOT depend on USB packet boundaries, line
endings, baud timing, text encoding, or a permanently connected host. The
interface provides physical locality, not cryptographic user identity.

Normative words MUST, MUST NOT, SHOULD, and MAY are used as requirements.
Multi-byte integers are unsigned little-endian unless stated otherwise.

## 2. Framing and bounds

Each frame is:

```text
COBS(decoded frame) || 0x00
```

`0x00` is solely the frame delimiter and never occurs in valid COBS-encoded
data. An empty delimiter is ignored. Exactly one decoded message is carried by
each non-empty candidate. Standard COBS is used without a variant or implicit
length. A sender MUST append one delimiter; a receiver MUST accept consecutive
delimiters and recover independently at each delimiter.

Decoded geometry:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | protocol major |
| 1 | 1 | protocol minor |
| 2 | 1 | message type |
| 3 | 1 | flags |
| 4 | 2 | request ID |
| 6 | 2 | payload length `N` |
| 8 | `N` | payload, 0..128 bytes |
| 8+`N` | 2 | CRC-16/CCITT-FALSE |

Decoded length MUST equal `10 + N`. The minimum decoded frame is 10 bytes and
the maximum is 138 bytes. The maximum COBS candidate is 139 bytes and the
maximum stored/transmitted encoded frame including delimiter is 140 bytes.
Implementations MUST use compile-time bounded buffers. They MUST reject a
payload length above 128 before payload interpretation.

CRC parameters are CRC-16/CCITT-FALSE: polynomial `0x1021`, initial value
`0xFFFF`, no reflection, final XOR `0x0000`. It covers decoded bytes from major
through the final payload byte. The CRC field is stored low byte then high
byte. This CRC is corruption detection, not authentication.

## 3. Envelope assignments

### 3.1 Message types

| Value | Name | Direction |
|---:|---|---|
| `0x01` | `HELLO_REQUEST` | host → device |
| `0x02` | `HELLO_RESPONSE` | device → host |
| `0x10` | `OPERATION_REQUEST` | host → device |
| `0x11` | `OPERATION_RESPONSE` | device → host |
| `0x7F` | `PROTOCOL_ERROR` | device → host |

All other message-type values are unassigned and rejected.

### 3.2 Flags and request IDs

Flags MUST be `0x00` in 0.1. A nonzero flag field is unsupported. Request ID
`0x0000` is reserved for messages that cannot be correlated to a parsed
request. A host MUST use `0x0001..0xFFFF` for requests. A response MUST copy
the request ID. Request IDs are scoped to volatile device state; they provide
neither authentication nor cross-reset idempotency.

## 4. Payload vocabulary

### 4.1 Common numeric assignments

Operation categories:

| Value | Category |
|---:|---|
| `0x01` | `DEVICE` |
| `0x02` | `CAPABILITY` |
| `0x03` | `PROCEDURE` |
| `0x04` | `DIAGNOSTIC` |

Host result classes:

| Value | Result class |
|---:|---|
| `0x00` | `SUCCESS` |
| `0x01` | `REQUEST_REJECTED` |
| `0x02` | `OPERATION_RESULT` |
| `0x03` | `RADIO_RESULT` |
| `0x04` | `LOCAL_RUNTIME_RESULT` |

Result class selects the authority for `result code`; a code MUST NOT be
interpreted without its class. `SUCCESS` uses only code `OK=0x00` for a
successful Host-layer action that is not target-operation completion.
`REQUEST_REJECTED` is rejection before an operation is accepted and uses:

| Value | Code |
|---:|---|
| `0x01` | `MALFORMED_REQUEST` |
| `0x02` | `UNSUPPORTED_OPERATION` |
| `0x03` | `BAD_TARGET` |
| `0x04` | `BUSY` |
| `0x08` | `MISMATCH` |

`RADIO_RESULT` describes Host-visible radio transaction/transport failure,
not a Wire operation result, and uses:

| Value | Code |
|---:|---|
| `0x06` | `TIMEOUT` |
| `0x07` | `REMOTE_REJECTED` |
| `0x08` | `MISMATCH` |

`LOCAL_RUNTIME_RESULT` describes a local Host bridge/runtime failure outside
the target operation and uses `BUSY=0x04` or `OPERATION_FAILED=0x0A`.

`OPERATION_RESULT` describes completion of an accepted device, capability,
procedure, or diagnostic operation. Its sole result-code authority is the
existing `OperationStatus` registry inherited unchanged from v0.5:

| Value | Status |
|---:|---|
| `0x00` | `OK` |
| `0x01` | `CAPABILITY_NOT_FOUND` |
| `0x02` | `UNSUPPORTED_OPERATION` |
| `0x03` | `INVALID_VALUE_TYPE` |
| `0x04` | `VALUE_OUT_OF_RANGE` |
| `0x05` | `UNAUTHORIZED` |
| `0x06` | `INTERLOCK_ACTIVE` |
| `0x07` | `HARDWARE_UNAVAILABLE` |
| `0x08` | `OPERATION_FAILED` |
| `0x09` | `BUSY` |
| `0x0A` | `INVALID_DESCRIPTOR` |

Each operation may constrain which `OperationStatus` values it can return.
Undefined codes, and defined codes prohibited by that operation's schema, are
malformed. No second Host operation-result enum is created. Equal numeric bytes
in different result classes do not share meaning; the class is mandatory.

Protocol-error codes:

| Value | Error |
|---:|---|
| `0x01` | `UNSUPPORTED_MAJOR` |
| `0x02` | `UNSUPPORTED_MINOR` |
| `0x03` | `UNSUPPORTED_MESSAGE_TYPE` |
| `0x04` | `UNSUPPORTED_FLAGS` |
| `0x05` | `MALFORMED_PAYLOAD` |
| `0x06` | `INVALID_REQUEST_ID` |

COBS, overflow, decoded-length, and CRC failures are not trustworthy enough
to answer and therefore produce no `PROTOCOL_ERROR`.

### 4.2 HELLO

`HELLO_REQUEST` payload is exactly two bytes:

```text
minimum minor      uint8_t
maximum minor      uint8_t
```

For 0.1 both MUST be `1`. `HELLO_RESPONSE` payload is exactly 16 bytes:

```text
selected minor             uint8_t   (=1)
firmware major             uint8_t
firmware minor             uint8_t
firmware patch             uint8_t
Wire Protocol              uint8_t
Configuration Schema       uint8_t
hardware profile           uint8_t   (HELTEC_V4=0x01)
role                       uint8_t   (HUB=0x01, NODE=0x02)
device ID                  uint8_t
maximum host payload       uint8_t   (=128)
operation-category bitmap  uint16_t  bit (category - 1)
feature bitmap             uint16_t
maximum outstanding ops    uint8_t   (=1)
reserved                   uint8_t   (=0)
```

Feature bit 0 means local operations are supported; bit 1 means the radio
structured-operation bridge is supported. All other bits are zero in 0.1.
HELLO describes implemented transport behavior, not application features.

The operation-category bitmap mapping is `bit 0=DEVICE`,
`bit 1=CAPABILITY`, `bit 2=PROCEDURE`, and `bit 3=DIAGNOSTIC`. A set bit means
the category is implemented; bits 4..15 MUST be zero in Host Protocol 0.1.

### 4.3 OPERATION_REQUEST

The payload is:

```text
category          uint8_t
operation         uint8_t
target device ID  uint8_t
target ID         uint16_t
value type        uint8_t
value length      uint8_t
value             0..121 bytes
```

Payload length is `7 + value length`. Target ID is zero for device operations;
it is a `CapabilityId` or registered procedure ID where applicable. Value type
assignments reuse the capability model: `NONE=0x00`, `BOOLEAN=0x01`,
`UNSIGNED_32=0x02`, `SIGNED_32=0x03`, `NORMALIZED_U16=0x04`,
`FIXED_Q16_16=0x05`, `ENUM_U16=0x06`. `STRUCTURE=0x7F` is permitted only for
the fixed operation-specific response records defined by the Wire operation
design; it is not a generic buffer capability type. `NONE` requires length zero; BOOLEAN
requires one byte (`0` or `1`); 16-bit types require two bytes; 32-bit types
require four bytes. No string or arbitrary buffer value is defined in 0.1.

Category-specific operation numbers are `PING=0x20`,
`GET_DEVICE_INFO=0x21`, `GET_STATUS=0x22`, `GET_CAPABILITIES=0x23`,
`DESCRIBE_CAPABILITY=0x24`, `READ_CAPABILITY=0x25`,
`SET_INDICATOR=0x26`, `RUN_PROCEDURE=0x27`, and
`GET_DIAGNOSTICS=0x28`, matching the Wire design registry. A local
target uses the connected device ID. A remote target is submitted only through
the Hub and remains subject to the remote authorization policy.

`GET_DIAGNOSTICS` target ID is zero. Its value is either `NONE`, which starts
at diagnostic metric index zero, or `UNSIGNED_32`, which is a zero-based index
into the fixed diagnostic metric registry. Cursor equal to the metric count
returns an empty terminal page; cursor greater than the count returns
`OPERATION_RESULT/VALUE_OUT_OF_RANGE`. No other cursor value type is valid.

The v0.6 diagnostic metric registry is fixed in `RuntimeState` counter
declaration order: `0x01 transmissionsCompleted`,
`0x02 decodedPacketsReceived`, `0x03 successfulTransactions`,
`0x04 acceptedCommands`, `0x05 retransmissions`,
`0x06 acknowledgmentTimeouts`, `0x07 duplicates`,
`0x08 malformedPackets`, `0x09 ignoredPackets`, and `0x0A radioErrors`.
`DIAGNOSTIC_PAGE` contains at most three entries. Its next cursor is the
zero-based index of the next metric or `0xFF` when complete.

For `STATUS`, `READY` is set when `RuntimeState::State::isReady()` is true;
`RADIO_OPERATIONAL` is also set from readiness because the current runtime
reaches ready only after radio initialization. `TRANSACTION_ACTIVE` is set
in `TRANSMITTING`, `WAITING_FOR_ACK`, `TRANSMITTING_ACK`,
`WAITING_FOR_RESPONSE`, or `TRANSMITTING_RESPONSE`, not in `IDLE` or
`LISTENING`. `DEGRADED` and `ERROR` reflect their matching health
states independently, so ready may coexist with degraded. Retry and timeout
are the `retransmissions` and `acknowledgmentTimeouts` counters saturated to
`uint16_t`; uptime is a caller-supplied `uint32_t` seconds snapshot.

The only valid category/operation pairs are:

| Category | Valid operations |
|---|---|
| `DEVICE` | `PING`, `GET_DEVICE_INFO`, `GET_STATUS` |
| `CAPABILITY` | `GET_CAPABILITIES`, `DESCRIBE_CAPABILITY`, `READ_CAPABILITY`, `SET_INDICATOR` |
| `PROCEDURE` | `RUN_PROCEDURE` |
| `DIAGNOSTIC` | `GET_DIAGNOSTICS` |

A known category combined with an operation not listed for it is rejected as
`REQUEST_REJECTED/UNSUPPORTED_OPERATION`. It MUST NOT dispatch. A malformed or
unknown category/operation encoding is `REQUEST_REJECTED/MALFORMED_REQUEST`.

### 4.4 OPERATION_RESPONSE

The payload is:

```text
category          uint8_t
operation         uint8_t
target device ID  uint8_t
target ID         uint16_t
result class      uint8_t
result code       uint8_t
value type        uint8_t
value length      uint8_t
value             0..119 bytes
```

Payload length is `9 + value length`. Category, operation, target device, and
target ID echo the request. The result class determines interpretation of
result code. `REQUEST_REJECTED` applies before acceptance;
`OPERATION_RESULT` applies to accepted target-operation completion and uses
`OperationStatus`; `RADIO_RESULT` applies to radio transaction failure; and
`LOCAL_RUNTIME_RESULT` applies to bridge/runtime failure outside the target
operation. Values are present only when the class, operation, and result permit
one. Reserved/irrelevant fields MUST be zero.

### 4.5 PROTOCOL_ERROR

The payload is exactly four bytes:

```text
error code          uint8_t
offending type      uint8_t
detail               uint16_t
```

Detail is zero unless a message definition assigns it. A protocol error is a
terminal response for its request ID but is not an operation outcome.

## 5. Lifecycle and backpressure

A device executes at most one operation at a time and retains at most one
completed terminal result. The retained entry contains the request ID, a
byte-exact copy of the accepted `OPERATION_REQUEST` payload, and its terminal
response. It is fixed-size, single-entry, volatile, and allocation-free. It is
a retry cache, not a queue.

While an operation is active:

- the same request ID with a byte-identical accepted request is an active
  duplicate and MUST NOT execute again or generate a second terminal result;
  the original active work remains responsible for the eventual response;
- the same request ID with different payload is
  `REQUEST_REJECTED/MISMATCH` and MUST NOT execute; and
- a different request ID receives `REQUEST_REJECTED/BUSY` and MUST NOT execute.

After completion, the retained result does not make the device BUSY:

- the same request ID with a byte-identical accepted request re-emits the
  retained terminal response without execution;
- the same request ID with different payload receives
  `REQUEST_REJECTED/MISMATCH`, does not execute, and does not evict the retained
  result; and
- a different request ID may be accepted as new work and replaces the retained
  completed entry when the new request is accepted.

Thus retained Host request identity is request ID plus the byte-exact accepted
`OPERATION_REQUEST` payload. Request ID alone never establishes a duplicate.
No older history is retained. HELLO may be answered only without disturbing
radio ownership.

Host-link loss does not cancel accepted work. Partial input is discarded. A
completed result remains recoverable after reconnect only while its retained
entry has not been replaced. Reset clears parser, active-request, and retained
request/response state. After reset, the device cannot establish whether a
previously accepted side-effecting operation completed. That ambiguity is a
host interpretation/state condition, not an on-wire device result. The host
MUST NOT blindly retry the operation. A reference utility or future stable
host API may surface the uncertainty without inventing persistent v0.6 state.

Responses MUST match the request ID. A host ignores responses for other IDs.
The device emits at most one newly generated terminal response per accepted
request, except byte-identical re-emission from the retained cache after
reconnect/retry.

### 5.1 Diagnostic observation mapping

Host Protocol diagnostics are nine independently owned, volatile, saturating
`uint32_t` counters. A non-empty candidate increments `framesReceived` exactly
once when it produces `FRAME_READY`, `FRAME_REJECTED`, or
`OVERSIZED_CANDIDATE`; empty delimiters, partial candidates, discarded bytes
after an oversize decision, and transport-discarded partial candidates do not.
`framesAccepted` increments only after envelope, version, inbound direction,
flags, request ID, and semantic payload validation all succeed.

Malformed framing, geometry, CRC, flags, request ID, or semantic payload
increments `malformedFrames`. An otherwise trustworthy unsupported Host major,
minor, or HELLO minor range increments only `unsupportedVersions`; an unknown
or wrong-direction Host message type increments only
`unsupportedMessageTypes`. These three rejection classifications do not
overlap.

`requestsDispatched` increments once when new valid operation work is accepted
by a local service or remote bridge, not for HELLO, duplicates, retained replay,
pre-acceptance rejection, or Wire retry. `busyOrRejectedRequests` increments
once for an `OPERATION_RESPONSE` whose result class is `REQUEST_REJECTED`, not
for accepted operation or radio failures. `responsesEmitted` increments at one
logical complete-response handoff to Host output; creation while disconnected
does not count, while a later retained replay handoff does. Physical byte
completion does not increment it again. `transportResets` increments only for
an explicitly observed Host physical transport reset/disconnect event and does
not clear diagnostic history.

Host counters do not mutate or reuse radio diagnostics, capability diagnostics,
RuntimeState health, or the ten-metric `GET_DIAGNOSTICS` registry. Construction
or explicit device-state reset clears them; no counter is persistent.

## 6. Validation and recovery

Validation order is COBS → decoded bound/minimum → declared length/exact
geometry → CRC → major/minor → type/direction → flags → request ID → payload.
No operation dispatch occurs before every applicable check passes.

- Encoded overflow: once 139 non-delimiter bytes are exceeded, discard through
  the next delimiter, increment the bounded malformed/overflow diagnostic, and
  reset with no response.
- Bad COBS, length, or CRC: discard the candidate, emit no response, and resume
  after its delimiter.
- Unknown major: if envelope/CRC are valid, return `UNSUPPORTED_MAJOR`.
- Minor compatibility: major 0 accepts only minor 1. HELLO negotiates only an
  overlap containing minor 1; otherwise return `UNSUPPORTED_MINOR`. A future
  higher minor is not assumed compatible until its specification says so.
- Unknown type, nonzero flags, invalid request ID, or malformed payload: return
  the corresponding protocol error when the request is otherwise trustworthy.
- A valid envelope carrying a malformed/unknown category or operation encoding
  receives `REQUEST_REJECTED/MALFORMED_REQUEST`; a valid but unsupported
  category/operation pair receives `REQUEST_REJECTED/UNSUPPORTED_OPERATION`.
  Neither dispatches.
- Disconnect or explicit transport reset: clear the partial candidate and
  overflow-discard state; retain active work and the completed retry-cache entry.

Parsing and output MUST consume bounded bytes per firmware service call.
Recovery MUST NOT scan indefinitely, allocate memory, retain input pointers,
or block radio/UI/settings work.

## 7. Representative vectors

The following normative vectors include the final delimiter. CRC and COBS
values MUST be independently verified by implementation tests before firmware
integration:

```text
HELLO_REQUEST, request 0x1234, payload 01 01
decoded: 00 01 01 00 34 12 02 00 01 01 45 EA
encoded: 01 03 01 01 04 34 12 02 05 01 01 45 EA 00

OPERATION_REQUEST PING to Hub 0x01, request 0x0001
payload: 01 01 01 00 00 00 00
decoded: 00 01 10 00 01 00 07 00 01 01 01 00 00 00 00 64 1D
encoded: 01 03 01 10 02 01 02 07 04 01 01 01 01 01 01 03 64 1D 00
```

Required tests also cover zero-heavy and maximum payload frames, every split
point, concatenated frames, empty delimiters, one-byte truncation, corrupted
CRC, invalid COBS, length disagreement, 139/140-byte candidate boundary,
unknown version/type/flags, request-ID mismatch, disconnect/reset, and recovery
of the immediately following valid frame.
