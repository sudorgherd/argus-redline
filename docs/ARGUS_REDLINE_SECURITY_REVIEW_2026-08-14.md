# ARGUS REDLINE — Security Review Findings Report

**Report date:** August 14, 2026  
**Project:** ARGUS REDLINE  
**Repository:** `sudorgherd/argus-redline`  
**Reviewed revision:** `61b7a218aa11456d4d7b07e510e27f73a0bd947b`  
**Review type:** Static repository security review  
**Source:** Codex Security scan  
**Current firmware context:** v0.5.0 development line  
**Status:** Findings documented; remediation not yet implemented

---

## 1. Executive Summary

A static security review of the ARGUS REDLINE repository identified **four reportable findings**:

- **2 medium severity**
- **2 low severity**
- **0 high severity**
- **0 critical severity**

The review did **not** establish:

- a memory-corruption path in the reviewed packet codec, serial parser, fixed buffers, loops, or display formatting;
- a capability-authorization bypass;
- a hardware-interlock bypass;
- a persistent-settings integrity failure;
- a current production RF path into capability execution.

The findings are concentrated in the current pre-security radio transport:

1. malformed commands can poison the duplicate-result cache;
2. unauthenticated radio metadata is treated as peer identity;
3. repeated RF commands can force repeated acknowledgment work;
4. captured ACKs can become valid again after sequence reuse.

Three of the four findings are manifestations of security controls that are already expected to exist before operational deployment: authenticated packet integrity, cryptographic peer identity, persistent freshness, and replay rejection.

The duplicate-cache poisoning issue is different. It is a **transaction-engine correctness flaw that should be fixed independently and earlier**, because malformed requests should not be able to establish the cached semantic result for a later valid request.

This review therefore does not indicate a fundamental architectural failure. It identifies one near-term transaction-integrity fix and confirms the necessity of the planned authenticated transport and replay-protection layer.

---

## 2. Review Scope

The security review examined the repository at:

```text
61b7a218aa11456d4d7b07e510e27f73a0bd947b
```

Primary reviewed surfaces included:

- Wire Protocol codec;
- transaction engine;
- Hub firmware entry point and radio state machine;
- Node firmware entry point and radio state machine;
- capability registry;
- capability authorization;
- safety interlocks;
- concrete Heltec capability handler;
- settings codec;
- dual-slot settings manager;
- ESP32 NVS adapter;
- settings UI/editor;
- build configuration;
- board and variant definitions;
- relevant tests;
- security and deployment documentation.

### Review limitations

The scan was a **static source review only**.

It did not execute firmware, perform packet injection against physical radios, reproduce RF abuse conditions, or measure energy consumption.

Coverage was **partial**:

```text
42 of 74 tracked files fully reviewed
```

Remaining support-corpus files were searched or sampled but not all read line-by-line.

The repository also did not provide an authoritative fully resolved dependency lock suitable for offline version-specific vulnerability analysis.

---

## 3. Threat Model Confirmed by the Review

The current REDLINE v0.5.0 development line is a single-Hub/single-Node 915 MHz LoRa transport prototype.

The primary external trust boundary is the unauthenticated RF interface.

### Protected assets

The review treated the following as security-relevant assets:

- authoritative Hub transaction outcomes;
- peer-liveness state;
- Node command admission;
- Node peer-seen state;
- radio availability;
- device energy budget;
- capability authorization;
- hardware interlocks;
- persistent ordinary settings and recovery state.

### Attacker capabilities

A nearby attacker is assumed able to:

- observe compatible LoRa traffic;
- transmit compatible LoRa frames;
- replay captured frames;
- choose unauthenticated packet fields;
- time transmissions around known or observable transport state.

Physical access is a separate boundary.

### Current production limitation

The present RF production behavior remains limited to the transport TEST/ACK path.

The security review did **not** establish a production path from unauthenticated radio traffic into the capability execution layer.

This materially limits current impact but must not be treated as a substitute for authentication before future operational commands are introduced.

---

# 4. Findings Overview

| ID | Finding | Severity | Confidence | Primary Risk |
|---|---|---:|---:|---|
| F-01 | Malformed commands can poison the duplicate-result cache | Medium | High | Transaction integrity / availability |
| F-02 | Forgeable radio metadata is treated as peer identity | Medium | High | Authenticity / transaction integrity |
| F-03 | Repeated RF commands can force unbounded ACK work | Low | Medium | Availability / energy |
| F-04 | Captured ACKs become valid again after sequence reuse | Low | High | Replay / false transaction completion |

---

# 5. F-01 — Malformed Commands Can Poison the Duplicate-Result Cache

**Severity:** Medium  
**Confidence:** High  
**Category:** Transaction identity  
**CWE:** CWE-697

## Summary

The Node currently defines duplicate identity using:

```text
source + sequence + opcode
```

Payload length and payload content are not part of the duplicate key.

The transaction evaluator also performs duplicate lookup **before** validating the incoming command payload.

This creates a collision between semantically different requests.

A forged malformed request can therefore cause the Node to cache a malformed result under a tuple that a later legitimate request will reuse.

The legitimate request can then be classified as a duplicate and inherit the attacker's cached failure result without being evaluated on its own valid payload.

## Root cause

Two behaviors combine to create the flaw:

1. duplicate identity does not represent the complete canonical request;
2. cached duplicate state is consulted before the new request's payload contract is validated.

Conceptually:

```text
forged malformed request
        ↓
same source / sequence / opcode
        ↓
MALFORMED_PACKET cached
        ↓
legitimate valid request
        ↓
duplicate match occurs first
        ↓
cached malformed result reused
```

## Current impact

For the current TEST opcode, a correctly timed malformed frame can cause a legitimate TEST transaction to complete as a remote error.

Current impact is primarily:

- transport availability;
- false transaction failure;
- false remote-error completion;
- incorrect liveness/diagnostic interpretation.

The current TEST operation has no actuator or application side effect.

Future state-changing commands would increase the importance of correct transaction identity.

## Recommended remediation

This issue should be fixed **before waiting for the full security milestone**.

The transaction engine should enforce all of the following:

1. validate opcode and payload structure before allowing a request to mutate duplicate state;
2. define duplicate identity using the complete canonical request identity;
3. ensure semantically distinct payloads cannot collide;
4. preserve exact retransmission behavior for a legitimate repeated request;
5. ensure malformed or unauthenticated requests cannot poison cached results.

A practical near-term design may use an exact canonical request comparison within the existing bounded packet size.

A later authenticated transport should bind duplicate identity to the authenticated request representation.

## Required tests

Add permanent tests proving:

- malformed TEST followed by valid TEST using the same source, sequence, and opcode does not reuse the malformed result;
- payload-distinct commands do not collide in duplicate tracking;
- malformed requests do not mutate valid duplicate state;
- exact legitimate retransmissions still regenerate the original result;
- ignored requests still do not mutate duplicate state.

## Acceptance criteria

This finding is resolved when:

```text
a malformed or payload-distinct request cannot determine
the cached result returned for a later valid request
```

while preserving legitimate retransmission idempotency.

---

# 6. F-02 — Forgeable Radio Metadata Is Treated as Peer Identity

**Severity:** Medium  
**Confidence:** High  
**Category:** Data authenticity  
**CWE:** CWE-345

## Summary

Current REDLINE radio packets contain routing and transaction metadata but no cryptographic authenticator.

Current fields include:

```text
packet type
source
destination
sequence
opcode
payload length
payload
```

These values are transmitted in clear and are structurally validated, but structural correctness does not establish sender identity.

A nearby compatible transmitter can therefore impersonate the configured Hub or Node by constructing a frame with the expected public metadata.

## Hub-side consequence

A forged ACK can match the current outstanding transaction when it contains the expected:

- source;
- destination;
- sequence;
- opcode;
- one-byte status payload.

The Hub can then accept the forged ACK as the Node's authoritative response.

## Node-side consequence

A forged COMMAND containing the expected addressing fields can reach current Node command admission.

Current production behavior remains limited to TEST/ACK, so the review did not establish remote actuator or capability execution.

## Current impact

A nearby transmitter can potentially falsify:

- transaction success;
- transaction error;
- peer liveness;
- accepted-command state;
- health state;
- diagnostics.

This is a known pre-security limitation of the current development transport.

## Required architectural remediation

Introduce an explicitly versioned secure wire format using an established authenticated-encryption construction.

The secure transport must authenticate, at minimum:

- protocol/security version;
- sender identity;
- receiver identity or routing context;
- direction;
- persistent freshness value;
- transaction correlation fields;
- payload.

Authentication must occur **before**:

- duplicate tracking;
- command evaluation;
- ACK evaluation;
- peer-seen mutation;
- health mutation;
- diagnostic mutation caused by semantic packet acceptance;
- response generation.

Device IDs must remain routing identifiers rather than credentials.

## Required properties

The future secure transport should provide:

- unique device credentials;
- pairwise or otherwise appropriately scoped operational keys;
- authenticated integrity;
- confidentiality where required;
- authenticated persistent counters or nonces;
- replay rejection;
- explicit protocol/security versioning;
- downgrade rejection;
- rotation and revocation support.

## Required tests

Add tests proving:

- correct public metadata with an invalid authentication tag is rejected before semantic state mutation;
- a forged SUCCESS ACK cannot complete a Hub transaction;
- a forged error ACK cannot complete a Hub transaction;
- an unauthenticated COMMAND cannot set peer-seen or accepted-command state;
- secure and legacy packet forms cannot be confused;
- downgrade attempts fail closed.

## Acceptance criteria

This finding is resolved when:

```text
knowledge of public routing and transaction metadata is insufficient
to impersonate an authorized REDLINE peer
```

---

# 7. F-03 — Repeated RF Commands Can Force Unbounded ACK Work

**Severity:** Low  
**Confidence:** Medium  
**Category:** Resource consumption  
**CWE:** CWE-400

## Summary

Every admitted command classification currently enters acknowledgment work.

This includes:

- valid commands;
- duplicate commands;
- unsupported commands;
- malformed addressed commands.

The Node performs:

```text
radio standby
→ 100 ms delay
→ ACK transmission
→ receive restart
```

There is currently no:

- authentication-first silent discard;
- minimum response interval;
- per-peer response budget;
- global response budget;
- energy budget;
- duty-cycle guard.

## Risk

A nearby transmitter can repeatedly cause active Node transmit work and receive-off intervals.

This differs from ordinary carrier jamming because the attacker is inducing protocol-level response behavior rather than merely occupying the channel.

Possible effects include:

- increased power consumption;
- reduced receive availability;
- repeated radio state transitions;
- airtime consumption.

The review did not physically quantify battery drain, receive loss, or sustained ACK rate.

## Recommended remediation

The first and most important mitigation is the future authenticated transport:

```text
unauthenticated / stale input
→ silent discard
```

No radio response should be generated for unauthenticated or stale frames.

After authentication exists, add bounded response governance:

- global ACK budget;
- per-peer ACK budget;
- minimum response spacing;
- duty-cycle accounting where appropriate;
- legitimate retransmission allowance;
- burst suppression.

The implementation should remain deterministic and bounded.

## Required tests

Add tests proving:

- unauthenticated requests produce no ACK;
- stale requests produce no ACK;
- duplicate bursts cannot exceed the configured response budget;
- malformed authenticated traffic cannot create unlimited response work;
- legitimate retry traffic remains serviceable.

## Physical validation required

This finding should also receive a later hardware test measuring:

- ACK rate;
- Node current draw;
- airtime;
- receive gaps;
- recovery after hostile traffic stops.

## Acceptance criteria

This finding is resolved when hostile traffic cannot force unbounded protocol-generated response work beyond documented, enforced limits.

---

# 8. F-04 — Captured ACKs Become Valid Again After Sequence Reuse

**Severity:** Low  
**Confidence:** High  
**Category:** Replay protection  
**CWE:** CWE-294

## Summary

Current transaction freshness is represented only by an eight-bit sequence value.

The Hub sequence:

- starts predictably;
- increments after terminal completion;
- wraps after 256 values;
- resets after Hub restart;
- is not paired with a persistent boot/session epoch;
- is not authenticated.

A previously captured ACK can therefore become structurally valid again when the same sequence value recurs.

## Replay condition

Conceptually:

```text
capture valid ACK for sequence N
        ↓
Hub eventually wraps or restarts
        ↓
sequence N becomes active again
        ↓
captured ACK replayed during ACK window
        ↓
metadata matches
        ↓
old ACK accepted
```

## Current impact

The current TEST transaction can have its liveness or transaction outcome falsely completed.

This is lower severity than the missing-authentication finding because an attacker who can forge arbitrary frames does not need to wait for replay conditions.

However, replay resistance remains a required independent property of the future secure transport.

## Recommended remediation

Introduce direction-specific authenticated freshness.

The future design should include:

- persistent message counters or equivalent nonces;
- authenticated counter values;
- replay windows;
- defined reboot behavior;
- defined storage-loss behavior;
- rollback detection or safe failure behavior;
- synchronization/recovery rules.

The existing eight-bit sequence may remain as a short reliability/correlation field, but it must not serve as the security freshness mechanism.

## Required tests

Add tests proving:

- a previously accepted ACK is rejected after sequence wrap;
- a previously accepted ACK is rejected after Hub restart;
- counter rollback is detected or fails safely;
- storage loss does not silently restore old replay validity;
- legitimate traffic within the defined replay window behaves as documented.

## Acceptance criteria

This finding is resolved when:

```text
a packet that was previously valid cannot become valid again
merely because a short transaction sequence value repeats
```

---

# 9. Cross-Cutting Security Conclusion

The four findings divide cleanly into two classes.

## Class A — Immediate transaction-integrity defect

```text
F-01 duplicate-cache poisoning
```

This should be corrected in the transaction engine independently of cryptographic work.

It is a correctness issue in how requests are identified and cached.

## Class B — Expected pre-security transport deficiencies

```text
F-02 missing authenticated peer identity
F-03 unauthenticated response work
F-04 replay after sequence reuse
```

These should be addressed together by the future secure transport architecture.

The preferred processing order should eventually become:

```text
raw RF frame
    ↓
bounded framing / basic decode
    ↓
security-version validation
    ↓
cryptographic authentication
    ↓
freshness / replay validation
    ↓
canonical request identity
    ↓
duplicate handling
    ↓
semantic command / ACK evaluation
    ↓
runtime state mutation
    ↓
optional response generation
```

This order prevents unauthenticated or stale traffic from reaching semantic transport state.

---

# 10. Positive Security Results

The review also produced important non-findings.

## Memory and parser safety

No memory-corruption path was established in the reviewed:

- RF packet codec;
- length handling;
- serial parser;
- display formatting;
- fixed buffers;
- bounded loops.

The reviewed parsing surfaces appeared bounded.

## Capability safety

The review found no issue in the reviewed:

- capability registry;
- capability authorization;
- safety interlocks;
- concrete hardware handler.

Current capability dispatch fails closed.

Most importantly, the review did not establish a production RF path into capability execution.

## Persistent settings

No issue was established in the reviewed:

- settings codec;
- dual-slot manager;
- NVS adapter;
- recovery behavior;
- reset path;
- settings editor scheduling.

These are meaningful results and should be preserved as security invariants.

---

# 11. Remediation Priority

## Priority 1 — Fix F-01

Implement and test the duplicate-cache correctness fix first.

This is bounded, independently useful, and does not require waiting for the security wire format.

## Priority 2 — Preserve findings as requirements for the secure transport milestone

The future security milestone must explicitly close:

```text
F-02 peer authentication
F-04 replay protection
```

and should make authentication-first silent discard the primary mitigation for:

```text
F-03 response amplification
```

Do not bolt independent ad hoc authentication checks onto the current wire format if doing so would compromise the planned versioned security architecture.

## Priority 3 — Add bounded response governance

After authentication/freshness exists, add explicit response limits and document them.

## Priority 4 — Perform physical RF abuse validation

Measure the real power and availability effects of repeated hostile traffic.

## Priority 5 — Resolve dependency versions

Produce an authoritative resolved PlatformIO dependency manifest and audit the exact versions used for release builds.

## Priority 6 — Complete remaining source-review coverage

The initial scan fully reviewed 42 of 74 tracked files.

A later pass should close the remaining support-corpus coverage gap.

---

# 12. Dependency and Coverage Follow-Up

The scan identified two process-level follow-ups.

## Resolved dependency audit

The repository currently lacks an authoritative dependency lock sufficient for exact offline vulnerability analysis.

Follow-up should determine the exact resolved versions of:

- Espressif platform packages;
- RadioLib;
- other build dependencies included in release artifacts.

The resulting manifest should be reproducible from the release environment.

## Supporting corpus review

A later full review should cover the remaining documentation, tests, assets, and repository metadata not fully read line-by-line in the initial scan.

This is primarily a coverage-completeness task rather than evidence of a known vulnerability.

---

# 13. Security Invariants to Preserve During Fixes

Any remediation work must preserve current verified behavior unless an intentional versioned change is approved.

## Transport invariants

Preserve:

- bounded packet sizes;
- explicit source/destination addressing;
- existing transaction timeout/retry ownership;
- exact retransmission semantics unless deliberately changed;
- duplicate retransmission idempotency;
- flag-only radio ISRs;
- bounded runtime state;
- current radio orchestration ordering.

## Capability invariants

Preserve:

- local capability authorization;
- safety-interlock enforcement;
- current denial of unauthorized callers;
- no implicit RF-to-capability execution path.

## Architecture invariants

Preserve:

- application semantics outside the radio substrate;
- bounded embedded behavior;
- no unbounded allocation;
- no general-purpose remote execution in firmware;
- independent protocol/version lifecycles where already established.

---

# 14. Recommended Codex Implementation Boundaries

This report may be added to the repository immediately as documentation.

Suggested path:

```text
docs/ARGUS_REDLINE_SECURITY_REVIEW_2026-08-14.md
```

Adding the report should **not itself imply that any finding is fixed**.

A documentation-only integration should:

1. add this report;
2. verify the reviewed commit and current repository state;
3. preserve finding severity and confidence;
4. preserve the scan limitations;
5. avoid rewriting findings as resolved;
6. link the report from the appropriate security or project documentation if that is consistent with the current repository structure;
7. make no firmware behavior changes unless separately requested.

---

# 15. Suggested Future Fix Work

The recommended implementation order is intentionally separated from the documentation commit.

### Work item A — Duplicate-cache correctness

Scope:

```text
transaction engine
native tests
targeted firmware regression
documentation update
```

Do not mix this with cryptographic protocol work.

### Work item B — Secure transport design

Scope should be architecture-first and should define:

- identity;
- credential scope;
- AEAD selection;
- authenticated header fields;
- freshness/counter model;
- replay window;
- reboot behavior;
- key generation/rotation;
- revocation interaction;
- downgrade policy.

Only after the design is stable should the secure wire format be implemented.

### Work item C — Response governance

After authentication:

- response budgets;
- rate limits;
- duty-cycle accounting;
- abuse tests.

### Work item D — Release dependency manifest

Resolve and document exact dependency versions used by the release build.

---

# 16. Required Regression Gates After Security Fixes

Any implementation that closes these findings should pass all existing baseline tests plus new security-specific tests.

At minimum:

```text
native test suite: PASS
configuration fixtures: PASS
Hub build: PASS
Node build: PASS
git diff --check: PASS
physical two-board regression: PASS
```

For F-01 specifically, physical RF injection is not required to validate the core logic if the transaction-engine tests reproduce the poisoning path deterministically.

For F-02 through F-04, eventual physical radio validation should be performed once the secure transport is implemented.

---

# 17. Final Assessment

The August 14, 2026 Codex Security review identified real issues, but it did not reveal a fundamental collapse of the REDLINE architecture.

The strongest conclusions are:

1. **The duplicate cache currently has an incomplete request identity and should be fixed.**
2. **The current RF transport is not cryptographically authenticated and must not be treated as secure.**
3. **Replay protection must use authenticated persistent freshness rather than the current eight-bit reliability sequence.**
4. **Unauthenticated or stale traffic should eventually be discarded before response work or semantic state mutation.**
5. **The reviewed capability authorization, interlocks, parser bounds, and settings recovery did not produce reportable findings.**
6. **Current production RF does not presently reach capability execution.**
7. **The remaining dependency and source-coverage gaps should be closed in later review passes.**

The security review therefore reinforces the existing development direction:

> **Fix transaction identity now. Add cryptographic identity, authenticated freshness, and replay rejection before operational deployment. Keep unauthenticated traffic outside semantic state. Preserve the bounded capability and hardware-safety boundaries that already held up under review.**

---

## Appendix A — Original Finding Identifiers

```text
F-01
Finding ID: csf_986a284a0a3a38c82f81eeb3
Rule: transaction-cache.incomplete-request-identity

F-02
Finding ID: csf_c04a1c4dd34586682e8224f2
Rule: radio-transport.missing-authentication

F-03
Finding ID: csf_6ff4265b9a47962a9faa7cb9
Rule: radio-response.unbounded-ack-work
```

The scan also reported the captured-ACK replay finding associated with sequence reuse.

---

## Appendix B — Codex Repository Integration Prompt

Add this security review report to the ARGUS REDLINE repository as:

```text
docs/ARGUS_REDLINE_SECURITY_REVIEW_2026-08-14.md
```

Before editing:

1. switch to `main`;
2. pull with `--ff-only`;
3. verify the working tree is clean;
4. record current HEAD;
5. inspect the existing security/documentation structure and choose the smallest appropriate place to link this report.

Requirements:

- add the report without altering its finding severity, confidence, or remediation status;
- do not claim any finding is fixed;
- do not implement firmware or security changes in this task;
- if the repository already has a security index or architecture/security document, add a concise link to this report;
- avoid unrelated documentation rewrites;
- run `git diff --check`;
- report the final diff summary;
- commit the documentation change with a focused commit message;
- do not push, merge, tag, or publish a release unless explicitly instructed.

Suggested commit message:

```text
Document August 2026 security review findings
```
