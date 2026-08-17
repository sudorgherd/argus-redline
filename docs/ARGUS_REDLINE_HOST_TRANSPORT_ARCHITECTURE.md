# ARGUS REDLINE — Host Transport Architecture

**Status:** Stable architectural direction; not a framing specification
**Current implementation baseline:** published firmware `v0.5.1`, Wire Protocol `1`, Configuration Schema `1`, hardware profile `HELTEC_V4`
**Next planned layer:** v0.6.0 documentation/design active; the v0.5.x required hardware qualification gate is satisfied, so implementation is hardware-unblocked but **NOT STARTED**
**Developmental framing authority:** [Host Protocol 0.1](HOST_PROTOCOL_0.1.md)
**General opaque application transport:** Planned for `v1.1`, not implemented

## Purpose

ARGUS REDLINE is being developed as an application-neutral communications and capability substrate. LoRa is its current physical transport, but LoRa packet geometry is not intended to become a permanent application-facing contract.

The governing boundary is:

> **Applications own meaning. REDLINE owns transport.**

REDLINE carries and delivers information. Host applications decide what application data means, how it is displayed or stored, and what business or operator action follows.

## Layering

```text
host application
(ARGUS / BLACKSHEEP / NIGHTWATCH / third-party software)
        │
        ▼
application-neutral REDLINE host service / driver
        │
        ▼
Host Protocol
        │
        ▼
REDLINE Hub firmware
        │
        ▼
Wire Protocol and REDLINE transport behavior
        │
        ▼
LoRa or a future physical transport
```

The Host Protocol is the computer-to-Hub interface. The Wire Protocol is the REDLINE device-to-device interface used across the radio or another network transport. They have independent compatibility and version lifecycles. A firmware release does not automatically change either protocol, and a Host Protocol payload limit must not be inferred from the current LoRa frame size.

## Responsibility boundaries

### Host applications

Host applications own:

- application payload schemas and meaning;
- operator interfaces, workflows, automation, and business policy;
- application databases and long-lived domain records;
- decisions about how to handle an ambiguous application operation;
- whether delivered data was accepted, displayed, persisted, or acted upon.

Application schemas for ARGUS, BLACKSHEEP, NIGHTWATCH, or third-party software do not belong in REDLINE firmware.

### Host service / driver

An application-neutral host service owns the computer-side REDLINE integration boundary. Its responsibilities may include:

- device discovery, connection, disconnection, and reopen behavior;
- Host Protocol encoding, decoding, validation, and capability negotiation;
- bounded host transaction bookkeeping and reconciliation;
- safe handling of duplicate submissions;
- mapping authoritative Hub transport state into an application-neutral local API;
- client multiplexing, local diagnostics, and appropriate host-side records.

The service does not interpret application payloads or duplicate radio scheduling, retry, or physical-fragment behavior owned by the REDLINE substrate. Its exact process name, implementation language, and local API technology are implementation choices.

### REDLINE Hub and device firmware

Firmware owns transport and constrained-device behavior, including:

- radio framing, scheduling, retries, duplicate suppression, and delivery state;
- bounded queues, caches, parsers, and active transaction state;
- physical-transport fragmentation and reassembly when required by packet geometry;
- peer and network identity mapping, transport security, and link diagnostics;
- local capability validation, authorization hooks, safety/interlocks, and approved hardware execution;
- truthful reporting of the transport state the device actually knows.

All embedded state must have explicit limits and failure behavior. Firmware is not a general application computer.

## Firmware non-goals

REDLINE firmware does not own:

- application databases, workflow engines, or user-facing records;
- ARGUS-, BLACKSHEEP-, NIGHTWATCH-, or third-party-specific schemas;
- application business rules, operator roles, or presentation policy;
- arbitrary uploaded application code or unrestricted hardware access;
- unbounded message history, transaction history, queues, or reassembly buffers.

Capability operations and other REDLINE-defined management operations may have structured semantics because firmware must validate and safely execute them. That does not make firmware the owner of general application meaning.

## Delivery and application outcomes

Transport delivery and application outcome are separate facts:

```text
transport delivery status != application outcome
```

REDLINE may report that a payload reached a peer according to the negotiated transport contract. It does not thereby know whether a receiving application accepted, displayed, persisted, rejected, or acted on that payload. Applications and host services must not reinterpret an application timeout as an authoritative radio failure, and firmware must not claim an application outcome it cannot observe.

## Transaction ownership and host-link loss

Conceptually, once the Hub accepts transport work, that work belongs to the Hub until it reaches a documented terminal state or is explicitly cancelled where cancellation is supported. Loss of the USB or other host link must not implicitly cancel accepted transport work.

The Hub is authoritative for outcomes it can still establish. The host service may retain enough history to explain what it submitted and observed, but it must not manufacture certainty. After reconnect or device reset, unresolved work must be reconciled where possible. If available evidence cannot prove success or failure, the outcome remains ambiguous.

Host submission must eventually be idempotent within a documented transaction scope: retrying the same accepted submission must not create a second physical action. The exact transaction identifiers, epoch representation, retention window, and status model are defined by the applicable Host Protocol specification rather than by this architecture document.

## General application transport

General opaque application payload transport is a planned `v1.1` capability. Earlier Host Protocol and host-service milestones establish extensible, application-neutral boundaries but do not claim that the general opaque transport, its fragmentation system, or a third-party application interface is already complete.

When a logical payload must be divided because of a physical transport MTU or packet geometry, fragmentation and reassembly belong to the REDLINE transport substrate. Applications may independently chunk or stream application objects for application-level reasons, but they must not need to understand LoRa fragment geometry to submit a REDLINE payload.

Host and application contracts must remain neutral to the physical transport. Adding a future physical transport should not require redesigning application payload schemas or application-facing lifecycle semantics merely because its MTU, framing, or retry behavior differs from LoRa.

## Specification boundary

For v0.6 development, [Host Protocol 0.1](HOST_PROTOCOL_0.1.md) freezes the
computer-to-device framing, CRC, numeric assignments, byte ordering, bounds,
and parser behavior. [v0.6.0 Wire Operation and Response Design](V0.6.0_WIRE_OPERATION_RESPONSE_DESIGN.md)
owns radio operation and RESPONSE semantics. Those developmental specifications
do not turn this architectural direction into a stable host API or service.

This architecture document does **not** freeze:

- transaction-, session-, boot-, or peer-ID widths;
- host-service implementation language or process name;
- Unix sockets, named pipes, localhost APIs, or other local IPC technology;
- fragmentation packet format or recovery algorithm;
- future stable Host Protocol/API versions, queue depths, or reassembly limits.

Those decisions require explicit design, resource analysis, compatibility review, and validation in the milestones that implement them.

## Evolution rule

New REDLINE features should preserve this layering. Earlier milestones may add structured REDLINE operations and an initial Host Protocol, but they must not bind applications permanently to LoRa frame geometry, move application semantics into firmware, or prevent the later application-neutral transport planned for `v1.1`.
