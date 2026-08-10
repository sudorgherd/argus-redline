# ARGUS REDLINE v0.5.0 Development Handoff

## Release

- ARGUS REDLINE `v0.5.0`
- Capability Registry and Safe Hardware Abstraction
- Wire Protocol `1`
- Configuration Schema `1`
- Hardware profile `HELTEC_V4`

## Major additions

- Fixed pointer-free capability vocabulary and exact 20-byte descriptor.
- Allocation-free immutable registry with capacity 16, bounded enumeration,
  copied lookup, validation, and duplicate rejection.
- Eight-byte typed values and 12-byte deterministic operation results.
- Compile-time LOCAL_ONLY authorization and mutating-interlock policies.
- Synchronous handler dispatch with strict gate ordering and sanitized results.
- Simulated vertical slice and production Heltec V4 profile.
- Role-safe production integration, DeviceInput stable snapshots, and existing
  LED-arbitration integration.
- Capability-owned diagnostics copied into RuntimeState and a minimal DEVICE
  CAPS presentation.
- Retained `tx_capability_characterization` and
  `rx_capability_characterization` development environments. These are test
  tooling, not production behavior, Host Protocol, a supported serial API, or
  a radio capability interface.

## Production registered capabilities

| ID | Class | Operations | Production disposition |
|---:|---|---|---|
| `0x0101` | `INDICATOR_OUTPUT` | DESCRIBE, READ, SET | Available through existing LED arbitration |
| `0x0201` | `DIGITAL_INPUT` | DESCRIBE, READ | Available from DeviceInput-owned stable snapshot |
| `0x0301` | `ANALOG_INPUT` | DESCRIBE, READ | Registered but `HARDWARE_UNAVAILABLE` |

Ordinary v0.5.0 does not initialize or sample GPIO4. The analog capability
remains unavailable pending controlled GPIO4/ADC1_CH3 electrical
characterization in a v0.5.x patch.

## Physical validation completed

- COM4 Hub `0x01` and COM5 Node `0x10` continued Wire Protocol 1 TEST/ACK.
- Registry enumeration returned the exact three logical descriptors without
  exposing GPIO numbers.
- Digital input physically returned released `0`, pressed `1`, released `0`;
  short, long, and very-long UI gestures retained their established behavior.
- Indicator on/off physically followed the capability request while the master
  LED setting remained authoritative and restored the retained request without
  another SET.
- FUTURE_REMOTE and active-interlock operations denied without side effects.
- The Node OLED displayed `CAPS  3 INTERLOCK` through the copied diagnostic
  presentation chain.
- Repeated matching TEST/ACK exchanges continued around capability actions with
  no observed associated retry, timeout, malformed packet, mismatch, reset,
  stuck receive state, or ACK-ownership failure.

## Deferred, non-blocking follow-up

- GPIO4/ADC1_CH3 electrical characterization and voltage/raw/normalized
  correlation.
- Formal isolated 30/30 post-action exchange count.
- Physical startup-held reproduction (covered by native tests).
- Extended endurance and fault testing.

These are not reported as passed. The analog source remains fail-closed until
the physical work is completed and deliberately enabled in a v0.5.x patch.

## Compatibility and retained non-goals

Wire Protocol remains 1: no capability radio opcode, packet type, RESPONSE
semantics, TEST/ACK encoding change, Host Protocol, or serial production
console exists. Configuration Schema remains 1 and settings persistence is
unchanged.

v0.5.0 retains no arbitrary GPIO API, runtime registration, transferred code,
remote code execution, hazardous actuator, real relay/external-power control,
multi-Node behavior, routing, repeater, store-and-forward, or mesh behavior.

## Next milestone

`v0.6.0` — Structured Operations, Responses, and Host Protocol.
