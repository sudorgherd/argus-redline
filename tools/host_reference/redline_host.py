#!/usr/bin/env python3
"""Developmental Host Protocol 0.1/0.2 reference/qualification utility.

This is deliberately a small protocol tool, not an SDK or application API.
"""

from __future__ import annotations

import argparse
import json
import secrets
import struct
import sys
import time
from dataclasses import dataclass
from typing import Callable, Iterable, Optional

MAJOR, MINOR = 0, 1
SUPPORTED_MINORS = (1, 2)
MAX_PAYLOAD = 128
MAX_DECODED = 138
MAX_CANDIDATE = 139
MAX_ENCODED = 140

MESSAGE_TYPES = {1: "HELLO_REQUEST", 2: "HELLO_RESPONSE", 0x10: "OPERATION_REQUEST", 0x11: "OPERATION_RESPONSE", 0x7F: "PROTOCOL_ERROR"}
CATEGORIES = {1: "DEVICE", 2: "CAPABILITY", 3: "PROCEDURE", 4: "DIAGNOSTIC", 5: "EVENT"}
OPERATIONS = {0x20: "PING", 0x21: "GET_DEVICE_INFO", 0x22: "GET_STATUS", 0x23: "GET_CAPABILITIES", 0x24: "DESCRIBE_CAPABILITY", 0x25: "READ_CAPABILITY", 0x26: "SET_INDICATOR", 0x27: "RUN_PROCEDURE", 0x28: "GET_DIAGNOSTICS", 0x29: "POLL_EVENTS", 0x2A: "CONSUME_EVENT"}
VALUE_TYPES = {0: "NONE", 1: "BOOLEAN", 2: "UNSIGNED_32", 3: "SIGNED_32", 4: "NORMALIZED_U16", 5: "FIXED_Q16_16", 6: "ENUM_U16", 0x7F: "STRUCTURE"}
RESULT_CLASSES = {0: "SUCCESS", 1: "REQUEST_REJECTED", 2: "OPERATION_RESULT", 3: "RADIO_RESULT", 4: "LOCAL_RUNTIME_RESULT", 5: "EVENT_RESULT"}
RESULT_CODES = {
    0: {0: "OK"},
    1: {1: "MALFORMED_REQUEST", 2: "UNSUPPORTED_OPERATION", 3: "BAD_TARGET", 4: "BUSY", 8: "MISMATCH"},
    2: {0: "OK", 1: "CAPABILITY_NOT_FOUND", 2: "UNSUPPORTED_OPERATION", 3: "INVALID_VALUE_TYPE", 4: "VALUE_OUT_OF_RANGE", 5: "UNAUTHORIZED", 6: "INTERLOCK_ACTIVE", 7: "HARDWARE_UNAVAILABLE", 8: "OPERATION_FAILED", 9: "BUSY", 10: "INVALID_DESCRIPTOR"},
    3: {6: "TIMEOUT", 7: "REMOTE_REJECTED", 8: "MISMATCH"},
    4: {4: "BUSY", 10: "OPERATION_FAILED"},
    5: {1: "NOT_FOUND", 2: "STORAGE_FAILURE"},
}
PROTOCOL_ERRORS = {1: "UNSUPPORTED_MAJOR", 2: "UNSUPPORTED_MINOR", 3: "UNSUPPORTED_MESSAGE_TYPE", 4: "UNSUPPORTED_FLAGS", 5: "MALFORMED_PAYLOAD", 6: "INVALID_REQUEST_ID"}
METRICS = {1: "transmissionsCompleted", 2: "decodedPacketsReceived", 3: "successfulTransactions", 4: "acceptedCommands", 5: "retransmissions", 6: "acknowledgmentTimeouts", 7: "duplicates", 8: "malformedPackets", 9: "ignoredPackets", 10: "radioErrors"}
STATUS_FLAGS = {1: "READY", 2: "RADIO_OPERATIONAL", 4: "TRANSACTION_ACTIVE", 8: "DEGRADED", 16: "ERROR"}
EVENT_FAMILIES = {0x40: "BUTTON", 0x41: "SENSOR_THRESHOLD", 0x44: "MANUAL_CHECK_IN"}
BUTTON_EVENTS = {1: "PRESS", 2: "RELEASE", 3: "SHORT_PRESS", 4: "LONG_PRESS", 5: "VERY_LONG_PRESS"}
THRESHOLD_VALUE_TYPES = {2: "UNSIGNED_32", 3: "SIGNED_32", 4: "NORMALIZED_U16", 5: "FIXED_Q16_16", 6: "ENUM_U16"}
THRESHOLD_RELATIONS = {1: "CROSSED_BELOW", 2: "CROSSED_ABOVE"}
EVENT_MIN_LIFETIME, EVENT_MAX_LIFETIME = 60, 86400

EXIT_OK, EXIT_DEVICE_RESULT, EXIT_TIMEOUT, EXIT_MALFORMED, EXIT_LOCAL = 0, 2, 3, 4, 5

class ProtocolFailure(ValueError): pass
class HostTimeout(TimeoutError): pass

@dataclass(frozen=True)
class Frame:
    major: int
    minor: int
    message_type: int
    flags: int
    request_id: int
    payload: bytes
    decoded: bytes = b""
    encoded: bytes = b""

def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for value in data:
        crc ^= value << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc

def cobs_encode(data: bytes) -> bytes:
    if len(data) > MAX_DECODED: raise ProtocolFailure("decoded frame too large")
    out = bytearray([0]); code_index = 0; code = 1
    for value in data:
        if value == 0:
            out[code_index] = code; code_index = len(out); out.append(0); code = 1
        else:
            out.append(value); code += 1
            if code == 0xFF:
                out[code_index] = code; code_index = len(out); out.append(0); code = 1
    out[code_index] = code
    return bytes(out)

def cobs_decode(data: bytes) -> bytes:
    if not data or len(data) > MAX_CANDIDATE: raise ProtocolFailure("malformed/oversized COBS candidate")
    out = bytearray(); index = 0
    while index < len(data):
        code = data[index]; index += 1
        if code == 0 or index + code - 1 > len(data): raise ProtocolFailure("malformed COBS")
        out.extend(data[index:index + code - 1]); index += code - 1
        if code != 0xFF and index < len(data): out.append(0)
    return bytes(out)

def build_frame(message_type: int, request_id: int, payload: bytes, *, major: int = MAJOR, minor: int = MINOR, flags: int = 0, validate=True) -> tuple[bytes, bytes]:
    if len(payload) > MAX_PAYLOAD: raise ProtocolFailure("payload exceeds 128 bytes")
    if validate and request_id == 0: raise ProtocolFailure("request ID zero is reserved")
    head = struct.pack("<BBBBHH", major, minor, message_type, flags, request_id, len(payload)) + payload
    decoded = head + struct.pack("<H", crc16(head))
    encoded = cobs_encode(decoded) + b"\0"
    if len(decoded) > MAX_DECODED or len(encoded) > MAX_ENCODED: raise ProtocolFailure("frame bound exceeded")
    return decoded, encoded

def decode_frame(encoded: bytes, *, require_supported=True) -> Frame:
    if not encoded or encoded[-1:] != b"\0" or len(encoded) > MAX_ENCODED: raise ProtocolFailure("missing delimiter or encoded frame bound exceeded")
    decoded = cobs_decode(encoded[:-1])
    if not 10 <= len(decoded) <= MAX_DECODED: raise ProtocolFailure("decoded length invalid")
    major, minor, msg, flags, request_id, length = struct.unpack_from("<BBBBHH", decoded)
    if length > MAX_PAYLOAD or len(decoded) != 10 + length: raise ProtocolFailure("declared payload length mismatch")
    if crc16(decoded[:-2]) != struct.unpack_from("<H", decoded, len(decoded)-2)[0]: raise ProtocolFailure("CRC mismatch")
    if require_supported:
        if major != MAJOR or minor not in SUPPORTED_MINORS: raise ProtocolFailure("unsupported Host Protocol version")
        if msg not in MESSAGE_TYPES: raise ProtocolFailure("unsupported message type")
        if flags: raise ProtocolFailure("unsupported flags")
        if request_id == 0 and msg != 0x7F: raise ProtocolFailure("invalid request ID")
    return Frame(major, minor, msg, flags, request_id, decoded[8:-2], decoded, encoded)

def typed_value(kind: int, value=None) -> bytes:
    if kind == 0: raw = b""
    elif kind == 1:
        if value not in (False, True, 0, 1): raise ProtocolFailure("BOOLEAN must be 0 or 1")
        raw = bytes([int(value)])
    elif kind in (4, 6): raw = struct.pack("<H", int(value))
    elif kind == 2: raw = struct.pack("<I", int(value))
    elif kind in (3, 5): raw = struct.pack("<i", int(value))
    else: raise ProtocolFailure("unknown/request-prohibited value type")
    return bytes([kind, len(raw)]) + raw

def encode_event_identity(source_device_id: int, event_epoch: int, event_id: int) -> bytes:
    if not 1 <= source_device_id <= 0xFF or not 1 <= event_epoch <= 0xFFFFFFFF or not 1 <= event_id <= 0xFFFFFFFF:
        raise ProtocolFailure("Event identity fields must be nonzero and in range")
    return struct.pack("<BII", source_device_id, event_epoch, event_id)

def decode_event_identity(raw: bytes) -> dict:
    if len(raw) != 9: raise ProtocolFailure("invalid Host Event identity length")
    source, epoch, event_id = struct.unpack("<BII", raw)
    encode_event_identity(source, epoch, event_id)
    return {"source_device_id":source,"event_epoch":epoch,"event_id":event_id}

def operation_payload(category: int, operation: int, target_device: int, target_id: int, value_type=0, value=None, *, minor=MINOR) -> bytes:
    pairs = {1:{0x20,0x21,0x22}, 2:{0x23,0x24,0x25,0x26}, 3:{0x27}, 4:{0x28}}
    if minor == 2: pairs[5] = {0x29, 0x2A}
    if operation not in pairs.get(category, set()): raise ProtocolFailure("invalid category/operation pair")
    if (category in (1,4,5) or operation == 0x23) and target_id != 0: raise ProtocolFailure("operation requires target ID zero")
    if operation in (0x24,0x25,0x26,0x27) and target_id == 0: raise ProtocolFailure("operation requires nonzero target ID")
    allowed = {0x20:{0},0x21:{0},0x22:{0},0x23:{0,2},0x24:{0},0x25:{0},0x26:{1},0x27:{0,1,2,3,4,5,6},0x28:{0,2},0x29:{0},0x2A:{0x7F}}
    if value_type not in allowed[operation]: raise ProtocolFailure("value type invalid for operation")
    if operation == 0x2A:
        if not isinstance(value, bytes) or len(value) != 9: raise ProtocolFailure("CONSUME_EVENT requires exact Event identity")
        decode_event_identity(value)
        typed = bytes([0x7F, 9]) + value
    else: typed = typed_value(value_type, value)
    return struct.pack("<BBBH", category, operation, target_device, target_id) + typed

def decode_typed(kind: int, raw: bytes):
    widths = {0:0,1:1,2:4,3:4,4:2,5:4,6:2}
    if kind in widths:
        if len(raw) != widths[kind] or (kind == 1 and raw[0] not in (0,1)): raise ProtocolFailure("invalid scalar geometry")
        if kind == 0: return None
        if kind == 1: return bool(raw[0])
        if kind in (2,): return struct.unpack("<I", raw)[0]
        if kind in (3,5): return struct.unpack("<i", raw)[0]
        return struct.unpack("<H", raw)[0]
    if kind == 0x7F: return raw
    raise ProtocolFailure("unknown value type")

def decode_record(operation: int, raw: bytes):
    if operation == 0x29: return decode_event_record(raw)
    if operation == 0x21:
        if len(raw) != 8 or raw[5] != 1 or raw[6] not in (1,2): raise ProtocolFailure("invalid DEVICE_INFO")
        return dict(zip(("firmware_major","firmware_minor","firmware_patch","wire_protocol","configuration_schema","hardware_profile","role","device_id"), raw))
    if operation == 0x22:
        if len(raw) != 10: raise ProtocolFailure("invalid STATUS")
        flags, uptime, retry, timeout = struct.unpack("<HIHH", raw)
        if flags & 0xFFE0: raise ProtocolFailure("reserved STATUS flags")
        return {"status_flags":flags,"status_labels":[name for bit,name in STATUS_FLAGS.items() if flags&bit],"uptime_seconds":uptime,"retry_count":retry,"timeout_count":timeout}
    if operation == 0x23:
        if len(raw) < 3: raise ProtocolFailure("invalid CAPABILITY_PAGE")
        cursor, count = struct.unpack_from("<HB", raw)
        if count > 9 or len(raw) != 3 + 2*count: raise ProtocolFailure("invalid CAPABILITY_PAGE geometry")
        ids=list(struct.unpack_from("<"+"H"*count,raw,3))
        if any(value==0 for value in ids): raise ProtocolFailure("invalid capability ID")
        return {"next_cursor":cursor,"count":count,"capability_ids":ids}
    if operation == 0x24:
        if len(raw) != 6 or raw[0] > 0x0A or raw[1] not in range(7) or raw[2]&0xE0 or raw[3] > 5 or raw[4] not in (0,1) or raw[5]: raise ProtocolFailure("invalid CAPABILITY_DESCRIPTION")
        return dict(zip(("class","value_type","operation_flags","unit","availability","reserved"),raw))
    if operation == 0x28:
        if len(raw)<2: raise ProtocolFailure("invalid DIAGNOSTIC_PAGE")
        cursor,count=raw[:2]
        if count>3 or len(raw)!=2+5*count: raise ProtocolFailure("invalid DIAGNOSTIC_PAGE geometry")
        entries=[]
        for i in range(count):
            metric=raw[2+5*i]; value=struct.unpack_from("<I",raw,3+5*i)[0]
            if metric not in METRICS: raise ProtocolFailure("unknown diagnostic metric")
            entries.append({"metric_id":metric,"metric":METRICS[metric],"value":value})
        return {"next_cursor":cursor,"count":count,"entries":entries}
    raise ProtocolFailure("STRUCTURE not valid for this operation")

def decode_event_body(family: int, body: bytes) -> dict:
    if family == 0x40:
        if len(body) != 1 or body[0] not in BUTTON_EVENTS: raise ProtocolFailure("invalid BUTTON body")
        return {"button_event":BUTTON_EVENTS[body[0]],"button_event_raw":body[0]}
    if family == 0x41:
        if len(body) != 8: raise ProtocolFailure("invalid SENSOR_THRESHOLD body length")
        capability, value_type, bits, relation = struct.unpack("<HBIB", body)
        if capability == 0 or value_type not in THRESHOLD_VALUE_TYPES or relation not in THRESHOLD_RELATIONS:
            raise ProtocolFailure("invalid SENSOR_THRESHOLD body")
        if value_type in (4, 6) and bits >> 16: raise ProtocolFailure("noncanonical 16-bit threshold value")
        decoded = bits
        if value_type in (3, 5): decoded = struct.unpack("<i", struct.pack("<I", bits))[0]
        return {"capability_id":capability,"value_type":THRESHOLD_VALUE_TYPES[value_type],"value_type_raw":value_type,"observed_value_bits":bits,"observed_value":decoded,"relation":THRESHOLD_RELATIONS[relation],"relation_raw":relation}
    if family == 0x44:
        if body != b"\x01": raise ProtocolFailure("invalid MANUAL_CHECK_IN body")
        return {"reason":"USER_REQUEST","reason_raw":1}
    raise ProtocolFailure("unknown Event family")

def decode_event_record(raw: bytes) -> dict:
    if len(raw) != 29: raise ProtocolFailure("invalid Host Event record length")
    available = raw[0]
    if available == 0:
        if any(raw[1:]): raise ProtocolFailure("noncanonical empty Host Event record")
        return {"available":False}
    if available != 1: raise ProtocolFailure("invalid Host Event availability")
    source, family, flags = raw[1:4]
    epoch, event_id, lifetime = struct.unpack_from("<III", raw, 4)
    length = raw[16]; body, tail = raw[17:17+length], raw[17+length:]
    if source == 0 or epoch == 0 or event_id == 0: raise ProtocolFailure("zero Host Event identity")
    if family not in EVENT_FAMILIES or flags & 0xFE: raise ProtocolFailure("invalid Host Event family or flags")
    if not EVENT_MIN_LIFETIME <= lifetime <= EVENT_MAX_LIFETIME or length > 12 or any(tail):
        raise ProtocolFailure("noncanonical Host Event record")
    decoded_body = decode_event_body(family, body)
    return {"available":True,"source_device_id":source,"family":EVENT_FAMILIES[family],"family_raw":family,"flags":flags,"event_epoch":epoch,"event_id":event_id,"lifetime_budget_seconds":lifetime,"body_length":length,"body":decoded_body,"body_hex":body.hex(" ")}

def decode_payload(frame: Frame) -> dict:
    base={"frame_minor":frame.minor,"request_id":frame.request_id,"message_type":MESSAGE_TYPES.get(frame.message_type,f"UNKNOWN_0x{frame.message_type:02X}"),"message_type_raw":frame.message_type,"raw_payload_hex":frame.payload.hex(" ")}
    p=frame.payload
    if frame.message_type==2:
        if len(p)!=16: raise ProtocolFailure("invalid HELLO_RESPONSE length")
        selected,fmaj,fmin,fpatch,wire,schema,profile,role,device,maxpayload,cat,feat,maxops,res=struct.unpack("<10BHH2B",p)
        reserved_categories = 0xFFF0 if selected == 1 else 0xFFE0
        reserved_features = 0xFFFC if selected == 1 else 0xFFF8
        event_advertised = bool(cat & 0x10); event_feature = bool(feat & 0x04)
        if selected not in SUPPORTED_MINORS or (frame.minor == 1 and selected != 1) or maxpayload!=128 or maxops!=1 or res or cat&reserved_categories or feat&reserved_features or profile!=1 or role not in (1,2): raise ProtocolFailure("invalid HELLO_RESPONSE fields")
        if event_advertised != event_feature or (event_advertised and (selected != 2 or role != 1)): raise ProtocolFailure("invalid Event-service advertisement")
        features=((1,"local_operations"),(2,"radio_bridge"),(4,"event_service"))
        base.update({"selected_minor":selected,"next_request_minor":selected,"firmware":f"{fmaj}.{fmin}.{fpatch}","wire_protocol":wire,"configuration_schema":schema,"hardware_profile":profile,"role":role,"device_id":device,"maximum_host_payload":maxpayload,"category_bitmap":cat,"categories":[CATEGORIES[x] for x in CATEGORIES if cat&(1<<(x-1))],"feature_bitmap":feat,"features":[name for bit,name in features if feat&bit],"event_service_available":event_advertised,"maximum_outstanding_operations":maxops})
    elif frame.message_type==0x11:
        if len(p)<9: raise ProtocolFailure("invalid OPERATION_RESPONSE length")
        category,operation,target_device,target_id,result_class,result_code,kind,length=struct.unpack_from("<BBBHBBBB",p)
        raw=p[9:]
        event_operation = category == 5 and operation in (0x29, 0x2A)
        if len(raw)!=length or category not in CATEGORIES or operation not in OPERATIONS or result_class not in RESULT_CLASSES or result_code not in RESULT_CODES[result_class]: raise ProtocolFailure("invalid OPERATION_RESPONSE fields")
        if frame.minor == 1 and (category == 5 or operation in (0x29,0x2A) or result_class == 5): raise ProtocolFailure("Host Protocol 0.2 vocabulary in minor-1 frame")
        if frame.minor == 2 and ((category == 5) != (operation in (0x29,0x2A))): raise ProtocolFailure("invalid category/operation pair")
        if event_operation and target_id != 0: raise ProtocolFailure("Event operation requires target ID zero")
        if event_operation and result_class not in (0,1,5): raise ProtocolFailure("invalid Event operation result class")
        if result_class == 5 and (not event_operation or (result_code == 1 and operation != 0x2A)): raise ProtocolFailure("invalid EVENT_RESULT scope")
        success = (result_class == 0 and result_code == 0) if event_operation else (result_class == 2 and result_code == 0)
        if not success and (kind!=0 or raw): raise ProtocolFailure("non-success result must carry NONE")
        value=decode_record(operation,raw) if kind==0x7F else decode_typed(kind,raw)
        if success:
            valid = ((operation==0x29 and kind==0x7F and length==29) or
                     (operation==0x2A and kind==0 and length==0) or
                     (operation==0x20 and kind==2) or
                     (operation in (0x21,0x22,0x23,0x24,0x28) and kind==0x7F) or
                     (operation==0x25 and kind in range(1,7)) or
                     (operation==0x26 and kind==0) or
                     (operation==0x27 and kind in range(7)))
            if not valid: raise ProtocolFailure("invalid successful response value schema")
        base.update({"category":CATEGORIES[category],"category_raw":category,"operation":OPERATIONS[operation],"operation_raw":operation,"target_device":target_device,"target_id":target_id,"result_class":RESULT_CLASSES[result_class],"result_class_raw":result_class,"result_code":RESULT_CODES[result_class][result_code],"result_code_raw":result_code,"value_type":VALUE_TYPES[kind],"value_type_raw":kind,"value_length":length,"value":value})
    elif frame.message_type==0x7F:
        if len(p)!=4: raise ProtocolFailure("invalid PROTOCOL_ERROR length")
        code,offending,detail=struct.unpack("<BBH",p)
        if code not in PROTOCOL_ERRORS or detail: raise ProtocolFailure("invalid PROTOCOL_ERROR fields")
        base.update({"error":PROTOCOL_ERRORS[code],"error_raw":code,"offending_type":offending,"detail":detail})
    else: base["payload_hex"]=p.hex(" ")
    return base

class StreamAccumulator:
    def __init__(self): self.candidate=bytearray(); self.discarding=False
    def feed(self,data:bytes)->list[bytes]:
        frames=[]
        for b in data:
            if b==0:
                if self.discarding: self.discarding=False
                elif self.candidate: frames.append(bytes(self.candidate)+b"\0")
                self.candidate.clear()
            elif not self.discarding:
                if len(self.candidate)>=MAX_CANDIDATE: self.candidate.clear(); self.discarding=True
                else: self.candidate.append(b)
        return frames

def wait_for_response(read:Callable[[int],bytes], request_id:int, timeout:float, clock=time.monotonic)->tuple[Frame,float]:
    parser=StreamAccumulator(); start=clock(); deadline=start+timeout; malformed=False
    while clock()<deadline:
        chunk=read(MAX_ENCODED)
        for encoded in parser.feed(chunk):
            try: frame=decode_frame(encoded)
            except ProtocolFailure: malformed=True; continue
            if frame.request_id==request_id and frame.message_type in (2,0x11,0x7F): return frame,clock()-start
        if not chunk: time.sleep(0.005)
    if malformed: raise ProtocolFailure("malformed frame received before response deadline")
    raise HostTimeout("no correlated device response before host-side deadline")

def named_vectors()->dict[str,bytes]:
    hello_decoded=bytes.fromhex("00 01 01 00 34 12 02 00 01 01 45 EA")
    good=bytes.fromhex("01 03 01 01 04 34 12 02 05 01 01 45 EA 00")
    def raw(major=0,minor=1,msg=1,flags=0,rid=0x1234,payload=b"\x01\x01",declared=None):
        n=len(payload) if declared is None else declared
        body=struct.pack("<BBBBHH",major,minor,msg,flags,rid,n)+payload
        return cobs_encode(body+struct.pack("<H",crc16(body)))+b"\0"
    bad_crc=bytearray(good); bad_crc[-2]^=1
    vectors={"hello":good,"bad_cobs":b"\x05\x01\x00","bad_crc":bytes(bad_crc),"declared_length_mismatch":raw(declared=3),"unsupported_major":raw(major=1),"unsupported_minor":raw(minor=3),"unknown_message_type":raw(msg=0x55),"nonzero_flags":raw(flags=1),"invalid_request_id":raw(rid=0),"malformed_operation_request":raw(msg=0x10,payload=b"\x01\x20"),"oversized_candidate":b"\x01"*140+b"\0","hello_decoded":hello_decoded}
    normative={
        "hello_0_2": "01 03 02 01 04 34 12 02 05 01 02 62 F7 00",
        "hello_response_0_2": "01 03 02 02 04 34 12 10 02 02 02 07 08 01 01 01 01 01 80 1F 02 07 02 01 03 2C C5 00",
        "poll_events": "01 03 02 10 04 01 10 07 04 05 29 01 01 01 01 03 DD FD 00",
        "poll_events_empty": "01 03 02 11 04 01 10 26 04 05 29 01 01 01 01 03 7F 1D 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 03 02 36 00",
        "poll_events_present": "01 03 02 11 04 01 10 26 04 05 29 01 01 01 01 11 7F 1D 01 02 40 01 44 33 22 11 04 03 02 01 10 0E 01 03 01 02 01 01 01 01 01 01 01 01 01 01 03 0B 49 00",
        "consume_event": "01 03 02 10 04 02 10 10 04 05 2A 01 01 0E 7F 09 02 44 33 22 11 04 03 02 01 DA CB 00",
        "consume_event_success": "01 03 02 11 04 02 10 09 04 05 2A 01 01 01 01 01 01 03 1B 85 00",
        "consume_event_not_found": "01 03 02 11 04 02 10 09 04 05 2A 01 01 03 05 01 01 03 6E 0E 00",
    }
    vectors.update({name:bytes.fromhex(value) for name,value in normative.items()})
    storage_payload=struct.pack("<BBBHBBBB",5,0x29,1,0,5,2,0,0)
    vectors["poll_events_storage_failure"]=build_frame(0x11,0x1001,storage_payload,minor=2)[1]
    return vectors

COMMANDS={"ping":(1,0x20),"device-info":(1,0x21),"status":(1,0x22),"capabilities":(2,0x23),"describe":(2,0x24),"read":(2,0x25),"set-indicator":(2,0x26),"run-procedure":(3,0x27),"diagnostics":(4,0x28),"poll-events":(5,0x29),"consume-event":(5,0x2A)}
def parse_int(text): return int(text,0)
def parse_bool(text):
    if text.lower() in ("true","1"): return True
    if text.lower() in ("false","0"): return False
    raise argparse.ArgumentTypeError("expected true or false")

def request_for_args(args)->tuple[int,bytes,bytes]:
    rid=args.request_id if args.request_id is not None else secrets.randbelow(0xFFFF)+1
    minor=args.minor
    if args.command=="hello":
        minimum=args.minimum_minor if args.minimum_minor is not None else 1
        maximum=args.maximum_minor if args.maximum_minor is not None else minor
        if minor==1 and (minimum,maximum)!=(1,1): raise ProtocolFailure("minor-1 HELLO requires range 1..1")
        if minor==2 and not (1<=minimum<=maximum<=2): raise ProtocolFailure("minor-2 HELLO range must be within 1..2")
        payload=bytes([minimum,maximum]); msg=1
    else:
        category,op=COMMANDS[args.command]; target_id=getattr(args,"capability_id",None) or getattr(args,"procedure_id",None) or 0
        kind=0; value=None
        if args.command in ("capabilities","diagnostics") and args.cursor is not None: kind,value=2,args.cursor
        elif args.command=="set-indicator": kind,value=1,args.value
        elif args.command=="run-procedure": kind,value=args.value_type,args.value
        elif args.command=="consume-event": kind,value=0x7F,encode_event_identity(args.source_device_id,args.event_epoch,args.event_id)
        payload=operation_payload(category,op,args.target_device,target_id,kind,value,minor=minor); msg=0x10
    decoded,encoded=build_frame(msg,rid,payload,minor=minor)
    return rid,decoded,encoded

def print_result(data:dict,as_json=False):
    if as_json: print(json.dumps(data,sort_keys=True,separators=(",",":")))
    else:
        for key,value in data.items(): print(f"{key}={value}")

def add_common(parser):
    parser.add_argument("--minor",type=int,choices=SUPPORTED_MINORS,default=MINOR)
    parser.add_argument("--request-id",type=parse_int)
    parser.add_argument("--port")
    parser.add_argument("--baud",type=int,default=115200)
    parser.add_argument("--timeout",type=float,default=10.0)
    parser.add_argument("--dry-run",action="store_true")
    parser.add_argument("--show-hex",action="store_true")
    parser.add_argument("--json",action="store_true")

def make_parser():
    p=argparse.ArgumentParser(description="Developmental REDLINE Host Protocol 0.1/0.2 qualification utility")
    sub=p.add_subparsers(dest="command",required=True)
    for name in ["hello",*COMMANDS]:
        q=sub.add_parser(name); add_common(q)
        if name=="hello":
            q.add_argument("--minimum-minor",type=int,choices=SUPPORTED_MINORS)
            q.add_argument("--maximum-minor",type=int,choices=SUPPORTED_MINORS)
        if name!="hello": q.add_argument("--target-device",type=parse_int,required=True)
        if name in ("describe","read","set-indicator"): q.add_argument("--capability-id",type=parse_int,required=True)
        if name=="set-indicator": q.add_argument("--value",type=parse_bool,required=True)
        if name=="run-procedure":
            q.add_argument("--procedure-id",type=parse_int,required=True); q.add_argument("--value-type",type=parse_int,default=0); q.add_argument("--value",type=parse_int)
        if name in ("capabilities","diagnostics"): q.add_argument("--cursor",type=parse_int)
        if name=="consume-event":
            q.add_argument("--source-device-id",type=parse_int,required=True)
            q.add_argument("--event-epoch",type=parse_int,required=True)
            q.add_argument("--event-id",type=parse_int,required=True)
    q=sub.add_parser("decode"); q.add_argument("hex"); q.add_argument("--json",action="store_true")
    q=sub.add_parser("vectors"); q.add_argument("name",nargs="?"); q.add_argument("--port"); q.add_argument("--baud",type=int,default=115200)
    return p

def open_serial_transport(port, baud, timeout, write_timeout, serial_factory=None):
    if serial_factory is None:
        try: import serial
        except ImportError as exc: raise RuntimeError("live mode requires pyserial; install with: python -m pip install pyserial") from exc
        serial_factory = serial.Serial
    stream = serial_factory()
    stream.baudrate = baud
    stream.timeout = timeout
    stream.write_timeout = write_timeout
    stream.xonxoff = False
    stream.rtscts = False
    stream.dsrdtr = False
    stream.dtr = False
    stream.rts = False
    stream.port = port
    stream.open()
    if stream.dtr or stream.rts:
        stream.close()
        raise RuntimeError("serial driver did not preserve inactive DTR/RTS state")
    return stream

def live_exchange(port,baud,encoded,rid,timeout):
    start=time.monotonic()
    stream=open_serial_transport(port,baud,0.05,timeout)
    try:
        stream.write(encoded); stream.flush()
        frame,elapsed=wait_for_response(lambda n:stream.read(n),rid,timeout)
    finally:
        stream.close()
    return frame,time.monotonic()-start if elapsed is None else elapsed

def main(argv=None):
    try:
        args=make_parser().parse_args(argv)
        if args.command=="decode":
            encoded=bytes.fromhex(args.hex); frame=decode_frame(encoded); data=decode_payload(frame); data.update({"encoded_hex":encoded.hex(" "),"decoded_hex":frame.decoded.hex(" ")}); print_result(data,args.json); return EXIT_OK
        if args.command=="vectors":
            vectors=named_vectors()
            if args.name:
                if args.name not in vectors: raise ProtocolFailure("unknown vector name")
                data=vectors[args.name]; print(f"{args.name}={data.hex(' ')}")
                if args.port:
                    stream=open_serial_transport(args.port,args.baud,0.1,1)
                    try: stream.write(data)
                    finally: stream.close()
            else:
                for name,data in vectors.items(): print(f"{name}={data.hex(' ')}")
            return EXIT_OK
        rid,decoded,encoded=request_for_args(args)
        if args.show_hex or args.dry_run or not args.port:
            print(f"request_id=0x{rid:04X}"); print(f"tx_decoded_hex={decoded.hex(' ')}"); print(f"tx_encoded_hex={encoded.hex(' ')}")
        if args.dry_run or not args.port: return EXIT_OK
        frame,elapsed=live_exchange(args.port,args.baud,encoded,rid,args.timeout)
        data=decode_payload(frame); data["elapsed_seconds"]=round(elapsed,6)
        if args.show_hex: data.update({"rx_encoded_hex":frame.encoded.hex(" "),"rx_decoded_hex":frame.decoded.hex(" ")})
        print_result(data,args.json)
        if frame.message_type==2: return EXIT_OK
        if frame.message_type==0x11 and ((data["result_class_raw"]==0 and data["result_code_raw"]==0) or (data["result_class_raw"]==2 and data["result_code_raw"]==0)): return EXIT_OK
        return EXIT_DEVICE_RESULT
    except HostTimeout as exc: print(f"host_timeout={exc}",file=sys.stderr); return EXIT_TIMEOUT
    except ProtocolFailure as exc: print(f"malformed_protocol={exc}",file=sys.stderr); return EXIT_MALFORMED
    except (RuntimeError,OSError,ValueError) as exc: print(f"local_error={exc}",file=sys.stderr); return EXIT_LOCAL

if __name__=="__main__": raise SystemExit(main())
