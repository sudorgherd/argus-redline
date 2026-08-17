# ARGUS REDLINE v0.5.1 Development Handoff

## Release

- ARGUS REDLINE `v0.5.1`
- Focused F-01 Duplicate-Cache Security/Correctness Patch
- Wire Protocol `1`
- Configuration Schema `1`
- Hardware profile `HELTEC_V4`
- Qualified firmware commit `d8c92fc9e88c833113102c6b1060c43bf2a3edad`

This patch is not a new capability milestone. It retains the v0.5.0 bounded
capability registry and hardware abstraction while correcting transaction
identity and completing the associated release evidence.

## Starting state

The implementation began from the committed
[`V0.5.1_IMPLEMENTATION_AND_QUALIFICATION_BRIEF.md`](V0.5.1_IMPLEMENTATION_AND_QUALIFICATION_BRIEF.md)
at `f8c10922c3188093001d73211dafa038290d83e6`. The brief translated F-01 from
the [August 14 security review](ARGUS_REDLINE_SECURITY_REVIEW_2026-08-14.md)
into bounded implementation, regression, build, physical, dependency, and
source-review gates.

## F-01 root cause

The Node previously identified a duplicate using only source, sequence, and
opcode, and performed that lookup before complete opcode-specific payload
validation. A malformed command could establish an error result under an
identity later reused by a valid command. Payload-distinct requests could also
collide on the incomplete tuple.

The security consequence was bounded transaction-result confusion and
availability/liveness misreporting for the current TEST surface. It did not
establish authentication, spoofing, persistent replay, RF denial-of-service,
or capability-execution compromise.

## Implemented correction

Commit `d8c92fc9e88c833113102c6b1060c43bf2a3edad` changed the Node transaction
path and its focused tests. Semantic duplicate state is now unavailable to a
request until that request is a fully validated, admissible command.

The cache remains:

- fixed-size;
- single-entry;
- volatile across reset;
- allocation-free;
- deterministic and bounded.

No packet field, packet size, opcode, addressing rule, retry interval, timeout,
capability behavior, public API, or dependency changed.

## Canonical duplicate identity

Exact duplicate identity is:

```text
source
+ sequence
+ opcode
+ payload length
+ exact meaningful payload bytes
```

Payload storage is fixed at the existing maximum bound and deterministically
initialized. Equality compares the declared validated length and exactly those
meaningful bytes; unused capacity and structure padding are not semantic
identity. No hash or checksum substitutes for exact equality.

Version, packet type, destination, and configured sender are prevalidated
admission invariants. They do not need duplicate-entry storage under the
single-Hub/single-Node Wire Protocol 1 model.

## Validation and cache-mutation order

The effective Node order is:

1. decode and validate packet bounds;
2. validate Wire Protocol version and packet type;
3. validate destination and configured sender;
4. validate opcode;
5. validate payload length and opcode-specific structure;
6. reject, ignore, or error inadmissible input without consulting or mutating
   semantic duplicate state;
7. construct the canonical admissible request identity;
8. compare it exactly with the valid cache entry;
9. regenerate the prior ACK only for an exact retransmission; or
10. execute a new admissible request and cache its canonical identity and
    result under the existing single-entry mutation rules.

Execution success and admissible execution error remain cacheable results.
Malformed, unsupported, ignored, wrongly addressed, wrong-sender, wrong-version,
wrong-type, and decode-failed inputs do not consult, populate, overwrite, clear,
or otherwise influence semantic duplicate state. Generating a required error
ACK does not grant cache-mutation authority.

## Regression coverage

The focused transaction-engine suite passed `59/59` and covers:

- malformed TEST followed by valid same-tuple TEST;
- payload-length and equal-length byte-distinct canonical mismatch;
- identical meaningful-byte equality;
- unused payload-capacity independence;
- oversized-length rejection without out-of-bounds comparison;
- malformed input unable to overwrite a valid entry;
- exact retransmission returning the original result without re-execution;
- ignored and wrongly addressed input leaving cache state unchanged;
- deterministic sequence rollover; and
- diagnostic behavior around accepted, malformed, duplicate, retransmission,
  successful, timeout, radio-error, and completed transactions.

Existing duplicate ACK regeneration, retry, timeout, malformed input, version,
packet-type, addressing, sender filtering, receive restoration, codec bounds,
and Wire Protocol 1 tests remain passing.

## Software qualification

The implementation pass recorded:

| Gate | Result |
|---|---|
| Focused transaction tests | PASS — `59/59` |
| Complete native suite | PASS — `469/469` across 13 test programs |
| Hub production build (`tx`) | PASS |
| Node production build (`rx`) | PASS |
| `config_hub_valid` | PASS |
| `config_node_valid` | PASS |
| `config_missing_local` | Expected failure: `ARGUS_LOCAL_DEVICE_ID must be defined by the build environment` |
| `config_missing_peer` | Expected failure: `ARGUS_PEER_DEVICE_ID must be defined by the build environment` |
| `config_equal_ids` | Expected failure: `Local and peer device IDs must be different` |

The documentation closeout reran the same complete software gate: `469/469`
native tests passed, both production builds passed at the recorded sizes, both
valid fixtures passed, and all three negative fixtures failed with the exact
intended diagnostics.

## Firmware size

Repository-supported v0.5.0 and v0.5.1 production values are:

| Role | v0.5.0 RAM | v0.5.1 RAM | Delta | v0.5.0 flash | v0.5.1 flash | Delta |
| ---- | ---------: | ---------: | ----: | -----------: | -----------: | ----: |
| Hub  |     20,428 |     20,428 |     0 |      354,193 |      354,193 |     0 |
| Node |     20,452 |     20,484 |   +32 |      351,809 |      351,829 |   +20 |

These are observed build values, not estimates. The additional Node storage is
consistent with retaining the bounded canonical identity; no heap allocation or
packet expansion was introduced.

## Physical qualification

The complete record is
[`V0.5.1_QUALIFICATION_RESULTS.md`](V0.5.1_QUALIFICATION_RESULTS.md).

- Basic two-board TEST/ACK: PASS.
- Formal isolated exchanges: PASS, `30/30`, sequences 90 through 119.
- Retry behavior: PASS.
- Terminal timeout: PASS.
- Receive restoration after Node return: PASS.
- Physical cached-ACK regeneration: PASS.
- Fresh transaction after cached regeneration: PASS.

Production artifact hashes:

```text
Hub:
22BF8EC88EFA9F0532D2CC2B8695738F38956C37441B2514CE62CD6B17079D50

Node:
D2648C60B48817BACC7E8A85AF68B356B763F71332A70465BD907EECBC1C45BB
```

## Cached-ACK regeneration evidence

With the Node continuously powered, a controlled Hub-only reset caused the
first post-reset TEST to repeat the cached canonical request:

| Field | Value |
|---|---:|
| Source | 1 |
| Destination | 16 |
| Sequence | 1 |
| Opcode | 100 |
| Payload length | 0 |
| Payload | empty |

The Node reported DUPLICATE, regenerated the cached SUCCESS ACK, and did not
execute/accept the command again. Counters proved accepted remained 1,
duplicate advanced 0 to 1, and ACK TX advanced 1 to 2. The Hub completed the
matching transaction, both roles restored receive operation, and fresh sequence
2 subsequently passed.

Selective first-ACK-loss retry was not physically isolated because no
documented deterministic production-only method could suppress only the first
ACK. Native tests separately prove exact live-transaction retransmission. The
Hub-reset method exercises the same Node comparison/cache/ACK path, but also
demonstrates F-04 sequence reuse; it is not replay protection.

## Dependency and advisory evidence

The readable
[`V0.5.1_DEPENDENCY_AND_SOURCE_AUDIT.md`](V0.5.1_DEPENDENCY_AND_SOURCE_AUDIT.md)
and machine-readable
[`platformio-resolved-dependencies.json`](evidence/v0.5.1/platformio-resolved-dependencies.json)
preserve 40 unique resolved components, including PlatformIO Core 6.1.19,
Espressif32 7.0.1, Arduino-ESP32 2.0.17, RadioLib 7.7.1, compiler/linker,
uploader, build tools, native compiler, libraries, and PlatformIO runtime
packages.

No advisory was found applicable to the compiled firmware or qualified build
execution. Affected-but-unreached support-tool/framework matches are recorded.
Embedded advisory coverage and missing package-manager content digests remain
limitations, so this is not a claim that the dependency set contains no
vulnerabilities. No dependency was upgraded during evidence collection.

## Source-corpus completion

The August 14 scan recorded 42/74 fully reviewed files but did not preserve its
path-level ledger. The follow-up reviewed all current tracked text files:

```text
textual coverage: 76/76
tracked inventory explicitly disposed: 77/77
```

The remaining item is a separately inspected PNG asset. No new vulnerability
was established.

## Compatibility

v0.5.1 preserves:

- Wire Protocol version 1 and its packet/payload bounds;
- Hub/Node addressing and configured-peer behavior;
- exact cached-ACK regeneration for legitimate retransmissions;
- retry, timeout, and receive-restoration semantics;
- fixed-memory, allocation-free operation;
- Configuration Schema 1 and existing settings;
- current public APIs and v0.5.0 capability registry behavior; and
- existing production and fixture configuration compatibility.

## Known limitations and deferred findings

- **F-02 remains open:** public radio metadata is not authenticated peer
  identity. It requires authenticated integrity and cryptographic identity.
- **F-03 remains open:** unauthenticated traffic can force ACK work. It requires
  authentication-first silent discard followed by response/rate governance.
- **F-04 remains open:** captured ACKs can become valid after sequence reuse. It
  requires authenticated persistent freshness, replay windows, and defined
  reboot/storage-loss behavior.
- The F-01 correction does not solve spoofing, authentication, RF denial of
  service, persistent replay, reboot freshness, or ACK capture.
- The project has not been independently audited.

An additional non-blocking correctness observation remains unresolved: a
matching ACK with an invalid status terminates the Hub transaction. It does not
add meaningful attacker capability beyond forged ACK acceptance already
captured by F-02, is outside F-01, and should be revisited with structured
response validation/authenticated transport rather than expanding v0.5.1.

## Remaining broader v0.5.x hardware work

The later [August 16 hardware qualification follow-up](V0.5.1_HARDWARE_QUALIFICATION_RESULTS.md)
completed GPIO4/ADC1_CH3 electrical characterization over the tested
0.51–2.31 V range and a separate formal 30/30 post-characterization TEST/ACK
regression. Ordinary production analog remains fail-closed as
`HARDWARE_UNAVAILABLE`; this evidence does not enable it.

Remaining broader work is:

- physical startup-held-button reproduction;
- extended endurance and broader fault testing; or
- selective first-ACK-loss physical isolation.

Selective first-ACK-loss isolation retains authoritative native coverage and
is not represented as a physical pass. These remaining items do not change the
completed F-01 acceptance result.

## Release contents and commit chain

The intended ordered fast-forward chain is:

1. `f8c10922c3188093001d73211dafa038290d83e6` — implementation and qualification brief;
2. `d8c92fc9e88c833113102c6b1060c43bf2a3edad` — F-01 implementation and regression tests;
3. `64c69655bd55eb2be0a852396256d21a9bd3a78a` — initial hardware qualification evidence;
4. `7a24f9ca5e63174f13b007f285cbef27983dd079` — physical cached-ACK regeneration evidence;
5. `1731cb4dec8f3d1e6a8f05b6f9e81fe9e2108bb6` — dependency and source-corpus audit; and
6. the final documentation-closeout commit containing this handoff.

## Release-readiness checklist

- [x] F-01 implementation committed without Wire Protocol change.
- [x] Canonical exact duplicate identity and admission ordering implemented.
- [x] Focused and complete native tests passed in the implementation pass.
- [x] Production and configuration-fixture build gates passed in the implementation pass.
- [x] Physical TEST/ACK, 30/30, retry, timeout, restoration, and cached-ACK evidence recorded.
- [x] Resolved dependency manifest and advisory applicability audit committed.
- [x] Current source corpus completely disposed with no new vulnerability established.
- [x] August 14 security review follow-up records F-01 remediation and qualification.
- [x] F-02, F-03, and F-04 remain open authenticated-transport requirements.
- [x] Known physical and advisory limitations remain explicit.
- [x] Documentation-closeout software gate rerun passed.
- [x] Documentation-closeout evidence is internally consistent and ready for its focused commit.
- [ ] Maintainer performs the separately authorized merge, push, annotated tag, and GitHub release actions.
