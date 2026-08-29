import struct
import unittest
from unittest import mock

from tools.host_reference import redline_host as h


def response_frame(request_id, category=1, operation=0x20, target_device=1,
                   target_id=0, result_class=2, result_code=0,
                   value_type=2, value=struct.pack("<I", 42), minor=1):
    payload = struct.pack("<BBBHBBBB", category, operation, target_device,
                          target_id, result_class, result_code, value_type,
                          len(value)) + value
    return h.build_frame(0x11, request_id, payload, minor=minor)[1]


class CodecTests(unittest.TestCase):
    def test_crc_and_normative_hello_vectors(self):
        self.assertEqual(0x29B1, h.crc16(b"123456789"))
        vectors = h.named_vectors()
        self.assertEqual(bytes.fromhex("00 01 01 00 34 12 02 00 01 01 45 EA"), vectors["hello_decoded"])
        self.assertEqual(bytes.fromhex("01 03 01 01 04 34 12 02 05 01 01 45 EA 00"), vectors["hello"])
        frame = h.decode_frame(vectors["hello"])
        self.assertEqual((0x1234, b"\x01\x01"), (frame.request_id, frame.payload))

    def test_cobs_round_trip_and_bounds(self):
        for raw in (b"", b"abc", b"\0", bytes(range(139))):
            if len(raw) <= h.MAX_DECODED:
                self.assertEqual(raw, h.cobs_decode(h.cobs_encode(raw)))
        with self.assertRaises(h.ProtocolFailure): h.cobs_decode(b"\x05\x01")
        with self.assertRaises(h.ProtocolFailure): h.cobs_decode(b"\x01" * 140)

    def test_exact_firmware_ping_payload_and_frame_determinism(self):
        payload = h.operation_payload(1, 0x20, 1, 0)
        self.assertEqual(bytes.fromhex("01 20 01 00 00 00 00"), payload)
        a = h.build_frame(0x10, 0x1202, payload)
        b = h.build_frame(0x10, 0x1202, payload)
        self.assertEqual(a, b)
        self.assertNotEqual(a[1], h.build_frame(0x10, 0x1203, payload)[1])
        changed = h.operation_payload(1, 0x22, 1, 0)
        self.assertNotEqual(a[1], h.build_frame(0x10, 0x1202, changed)[1])

    def test_exact_firmware_ping_response_payload(self):
        encoded=response_frame(0x1234,value=bytes.fromhex("12 34 56 78"))
        payload=h.decode_frame(encoded).payload
        self.assertEqual(bytes.fromhex("01 20 01 00 00 02 00 02 04 12 34 56 78"),payload)

    def test_frame_rejections(self):
        for name in ("bad_cobs", "bad_crc", "declared_length_mismatch"):
            with self.subTest(name=name), self.assertRaises(h.ProtocolFailure):
                h.decode_frame(h.named_vectors()[name])
        for name in ("unsupported_major", "unsupported_minor", "unknown_message_type", "nonzero_flags", "invalid_request_id"):
            with self.subTest(name=name), self.assertRaises(h.ProtocolFailure):
                h.decode_frame(h.named_vectors()[name])


class PayloadTests(unittest.TestCase):
    def test_all_request_operations_and_scalar_widths(self):
        cases = [(1,0x20,0,0,None),(1,0x21,0,0,None),(1,0x22,0,0,None),
                 (2,0x23,0,2,3),(2,0x24,0x101,0,None),(2,0x25,0x201,0,None),
                 (2,0x26,0x101,1,True),(3,0x27,7,6,2),(4,0x28,0,2,9)]
        for category,op,target,kind,value in cases:
            raw=h.operation_payload(category,op,0x10,target,kind,value)
            self.assertEqual(7+raw[6],len(raw))
        for kind,value,length in ((0,None,0),(1,True,1),(2,0xffffffff,4),(3,-1,4),(4,0xffff,2),(5,-32768,4),(6,0xffff,2)):
            raw=h.typed_value(kind,value); self.assertEqual(length,raw[1]); self.assertEqual(value,h.decode_typed(kind,raw[2:]))
        with self.assertRaises(h.ProtocolFailure): h.typed_value(1,2)
        with self.assertRaises(h.ProtocolFailure): h.operation_payload(1,0x28,1,0)

    def test_hello_response_exact(self):
        payload=struct.pack("<10BHH2B",1,0,5,1,1,1,1,1,1,128,0x0b,3,1,0)
        frame=h.decode_frame(h.build_frame(2,7,payload)[1])
        data=h.decode_payload(frame)
        self.assertEqual("0.5.1",data["firmware"])
        self.assertEqual(["DEVICE","CAPABILITY","DIAGNOSTIC"],data["categories"])
        self.assertEqual(["local_operations","radio_bridge"],data["features"])
        for corrupt in (payload[:-1], payload[:-1]+b"\x01", payload[:10]+b"\x10\x00"+payload[12:]):
            with self.assertRaises(h.ProtocolFailure): h.decode_payload(h.Frame(0,1,2,0,7,corrupt))

    def test_fixed_records(self):
        device=bytes([0,5,1,1,1,1,2,0x10])
        self.assertEqual(0x10,h.decode_record(0x21,device)["device_id"])
        status=struct.pack("<HIHH",0x1f,0xffffffff,0xffff,0xffff)
        self.assertEqual(0xffffffff,h.decode_record(0x22,status)["uptime_seconds"])
        caps=struct.pack("<HBHH",0xffff,2,0x101,0x201)
        self.assertEqual([0x101,0x201],h.decode_record(0x23,caps)["capability_ids"])
        desc=bytes([4,1,7,1,1,0]); self.assertEqual(1,h.decode_record(0x24,desc)["availability"])
        diag=bytes([0xff,1,10])+struct.pack("<I",0xffffffff)
        self.assertEqual("radioErrors",h.decode_record(0x28,diag)["entries"][0]["metric"])
        bad=(device+b"\0",status+b"\0",caps+b"\0",desc[:-1],diag+b"\0")
        for op,raw in zip((0x21,0x22,0x23,0x24,0x28),bad):
            with self.subTest(op=op),self.assertRaises(h.ProtocolFailure): h.decode_record(op,raw)
        with self.assertRaises(h.ProtocolFailure): h.decode_record(0x22,struct.pack("<HIHH",0x20,0,0,0))
        with self.assertRaises(h.ProtocolFailure): h.decode_record(0x24,bytes([4,1,0xe0,1,1,0]))
        with self.assertRaises(h.ProtocolFailure): h.decode_record(0x24,bytes([0xff,1,1,1,1,0]))
        with self.assertRaises(h.ProtocolFailure): h.decode_record(0x23,struct.pack("<HBH",0xffff,1,0))

    def test_result_classes_are_not_flattened(self):
        cases=((1,4,"REQUEST_REJECTED","BUSY"),(1,8,"REQUEST_REJECTED","MISMATCH"),
               (2,0,"OPERATION_RESULT","OK"),(2,5,"OPERATION_RESULT","UNAUTHORIZED"),
               (2,9,"OPERATION_RESULT","BUSY"),(3,6,"RADIO_RESULT","TIMEOUT"),
               (3,7,"RADIO_RESULT","REMOTE_REJECTED"))
        for result_class,code,class_name,code_name in cases:
            kind,value=(2,struct.pack("<I",42)) if (result_class,code)==(2,0) else (0,b"")
            frame=h.decode_frame(response_frame(9,result_class=result_class,result_code=code,value_type=kind,value=value))
            data=h.decode_payload(frame)
            self.assertEqual((class_name,code_name),(data["result_class"],data["result_code"]))
        with self.assertRaises(h.ProtocolFailure):
            h.decode_payload(h.decode_frame(response_frame(9,result_class=1,result_code=4,value_type=1,value=b"\1")))
        with self.assertRaises(h.ProtocolFailure):
            h.decode_payload(h.decode_frame(response_frame(9,operation=0x20,value_type=0,value=b"")))

    def test_structure_operation_response_and_protocol_error(self):
        info=bytes([0,5,1,1,1,1,1,1])
        frame=h.decode_frame(response_frame(7,operation=0x21,value_type=0x7f,value=info))
        self.assertEqual("0.5.1",f"{h.decode_payload(frame)['value']['firmware_major']}.{h.decode_payload(frame)['value']['firmware_minor']}.{h.decode_payload(frame)['value']['firmware_patch']}")
        payload=bytes([3,0xa5,0,0]); error=h.decode_payload(h.decode_frame(h.build_frame(0x7f,7,payload)[1]))
        self.assertEqual("UNSUPPORTED_MESSAGE_TYPE",error["error"])
        with self.assertRaises(h.ProtocolFailure): h.decode_payload(h.Frame(0,1,0x7f,0,7,b"\0\0\0\0"))


class HostProtocol02Tests(unittest.TestCase):
    def event_response(self, operation, value_type, value, result_class=0,
                       result_code=0, request_id=0x1001):
        return h.decode_frame(response_frame(
            request_id, category=5, operation=operation, result_class=result_class,
            result_code=result_code, value_type=value_type, value=value, minor=2))

    def present_record(self, family=0x40, flags=1, epoch=0x11223344,
                       event_id=0x01020304, lifetime=3600, body=b"\x02"):
        return struct.pack("<BBBBIII", 1, 2, family, flags, epoch, event_id,
                           lifetime) + bytes([len(body)]) + body.ljust(12, b"\0")

    def test_normative_02_vectors_and_minor_1_are_exact(self):
        vectors=h.named_vectors()
        self.assertEqual(bytes.fromhex("01 03 01 01 04 34 12 02 05 01 01 45 EA 00"),vectors["hello"])
        expected={
            "hello_0_2":"00 02 01 00 34 12 02 00 01 02 62 F7",
            "poll_events":"00 02 10 00 01 10 07 00 05 29 01 00 00 00 00 DD FD",
            "consume_event":"00 02 10 00 02 10 10 00 05 2A 01 00 00 7F 09 02 44 33 22 11 04 03 02 01 DA CB",
        }
        for name,decoded in expected.items():
            with self.subTest(name=name): self.assertEqual(bytes.fromhex(decoded),h.decode_frame(vectors[name]).decoded)
        for name in ("hello_response_0_2","poll_events_empty","poll_events_present",
                     "consume_event_success","consume_event_not_found"):
            with self.subTest(name=name): h.decode_frame(vectors[name])

    def test_minor_two_hello_selection_fallback_and_advertisement(self):
        hub=bytes([2,0,7,0,1,1,1,1,1,128])+struct.pack("<HH",0x1f,7)+b"\x01\0"
        data=h.decode_payload(h.decode_frame(h.build_frame(2,7,hub,minor=2)[1]))
        self.assertEqual((2,True),(data["next_request_minor"],data["event_service_available"]))
        self.assertIn("EVENT",data["categories"])
        fallback=bytes([1,0,7,0,1,1,1,1,1,128])+struct.pack("<HH",0x0f,3)+b"\x01\0"
        data=h.decode_payload(h.decode_frame(h.build_frame(2,8,fallback,minor=2)[1]))
        self.assertEqual((2,1,False),(data["frame_minor"],data["next_request_minor"],data["event_service_available"]))
        node=bytes([2,0,7,0,1,1,1,2,2,128])+struct.pack("<HH",0x0b,1)+b"\x01\0"
        self.assertFalse(h.decode_payload(h.decode_frame(h.build_frame(2,9,node,minor=2)[1]))["event_service_available"])
        for corrupt in (hub[:7]+b"\x02"+hub[8:], hub[:10]+struct.pack("<HH",0x1f,3)+hub[14:]):
            with self.assertRaises(h.ProtocolFailure): h.decode_payload(h.decode_frame(h.build_frame(2,10,corrupt,minor=2)[1]))

    def test_frame_minor_is_authoritative_for_event_vocabulary(self):
        event_payload=struct.pack("<BBBHBBBB",5,0x2A,1,0,5,1,0,0)
        with self.assertRaises(h.ProtocolFailure):
            h.decode_payload(h.decode_frame(h.build_frame(0x11,1,event_payload,minor=1)[1]))
        with self.assertRaises(h.ProtocolFailure): h.operation_payload(5,0x29,1,0,minor=1)
        self.assertEqual(bytes.fromhex("05 29 01 00 00 00 00"),h.operation_payload(5,0x29,1,0,minor=2))

    def test_poll_normative_present_and_absent_records(self):
        present=h.decode_payload(h.decode_frame(h.named_vectors()["poll_events_present"]))
        event=present["value"]
        self.assertEqual((True,"BUTTON",0x40,"RELEASE"),(event["available"],event["family"],event["family_raw"],event["body"]["button_event"]))
        self.assertEqual("02",event["body_hex"])
        absent=h.decode_payload(h.decode_frame(h.named_vectors()["poll_events_empty"]))
        self.assertEqual({"available":False},absent["value"])
        self.assertEqual(absent,h.decode_payload(h.decode_frame(h.named_vectors()["poll_events_empty"])))

    def test_poll_record_canonical_rejections(self):
        valid=bytearray(self.present_record())
        mutations=[]
        for index,value in ((0,2),(1,0),(2,0x42),(3,2),(16,2),(18,1)):
            changed=bytearray(valid); changed[index]=value; mutations.append(changed)
        for lifetime in (59,86401):
            changed=bytearray(valid); struct.pack_into("<I",changed,12,lifetime); mutations.append(changed)
        empty=bytearray(29); empty[1]=1; mutations.append(empty)
        for raw in mutations:
            with self.subTest(raw=raw.hex()),self.assertRaises(h.ProtocolFailure): h.decode_event_record(bytes(raw))

    def test_all_event_family_decoders(self):
        sensor=struct.pack("<HBIB",0x0301,4,0xC000,2)
        decoded=h.decode_event_record(self.present_record(family=0x41,body=sensor))
        self.assertEqual((0x0301,"NORMALIZED_U16",0xC000,"CROSSED_ABOVE"),(decoded["body"]["capability_id"],decoded["body"]["value_type"],decoded["body"]["observed_value_bits"],decoded["body"]["relation"]))
        checkin=h.decode_event_record(self.present_record(family=0x44,flags=0,body=b"\x01"))
        self.assertEqual("USER_REQUEST",checkin["body"]["reason"])
        bad_sensor=struct.pack("<HBIB",0x0301,4,0x10000,2)
        for family,body in ((0x40,b"\0"),(0x41,bad_sensor),(0x44,b"\x02")):
            with self.assertRaises(h.ProtocolFailure): h.decode_event_record(self.present_record(family=family,body=body))

    def test_identity_and_consume_request_geometry(self):
        identity=h.encode_event_identity(2,0x11223344,0x01020304)
        self.assertEqual(bytes.fromhex("02 44 33 22 11 04 03 02 01"),identity)
        self.assertEqual({"source_device_id":2,"event_epoch":0x11223344,"event_id":0x01020304},h.decode_event_identity(identity))
        payload=h.operation_payload(5,0x2A,1,0,0x7F,identity,minor=2)
        self.assertEqual(bytes.fromhex("05 2A 01 00 00 7F 09 02 44 33 22 11 04 03 02 01"),payload)
        for values in ((0,1,1),(1,0,1),(1,1,0)):
            with self.assertRaises(h.ProtocolFailure): h.encode_event_identity(*values)

    def test_event_success_and_class_scoped_results(self):
        success=h.decode_payload(self.event_response(0x2A,0,b"",request_id=0x1002))
        self.assertEqual(("SUCCESS","OK"),(success["result_class"],success["result_code"]))
        for code,name in ((1,"NOT_FOUND"),(2,"STORAGE_FAILURE")):
            operation=0x2A if code==1 else 0x29
            data=h.decode_payload(self.event_response(operation,0,b"",5,code))
            self.assertEqual(("EVENT_RESULT",5,name,code),(data["result_class"],data["result_class_raw"],data["result_code"],data["result_code_raw"]))
        with self.assertRaises(h.ProtocolFailure): h.decode_payload(self.event_response(0x2A,0,b"",5,3))
        with self.assertRaises(h.ProtocolFailure): h.decode_payload(self.event_response(0x29,0,b"",5,1))
        with self.assertRaises(h.ProtocolFailure): h.decode_payload(self.event_response(0x2A,1,b"\1"))
        with self.assertRaises(h.ProtocolFailure): h.decode_payload(self.event_response(0x2A,0,b"",2,0))

    def test_event_cli_dry_runs_are_exact_and_do_not_chain_consume(self):
        poll=["poll-events","--minor","2","--target-device","1","--request-id","0x1001","--dry-run"]
        consume=["consume-event","--minor","2","--target-device","1","--request-id","0x1002","--source-device-id","2","--event-epoch","0x11223344","--event-id","0x01020304","--dry-run"]
        with mock.patch.object(h,"live_exchange") as live:
            self.assertEqual(h.EXIT_OK,h.main(poll)); self.assertEqual(h.EXIT_OK,h.main(consume)); live.assert_not_called()
        args=h.make_parser().parse_args(poll); self.assertEqual(h.named_vectors()["poll_events"],h.request_for_args(args)[2])
        args=h.make_parser().parse_args(consume); self.assertEqual(h.named_vectors()["consume_event"],h.request_for_args(args)[2])

    def test_event_json_is_deterministic(self):
        data=h.decode_payload(h.decode_frame(h.named_vectors()["poll_events_present"]))
        with mock.patch("builtins.print") as output:
            h.print_result(data,True)
        rendered=output.call_args.args[0]
        self.assertIn('"available":true',rendered); self.assertIn('"family_raw":64',rendered)
        empty=h.decode_payload(h.decode_frame(h.named_vectors()["poll_events_empty"]))
        with mock.patch("builtins.print") as output: h.print_result(empty,True)
        self.assertIn('"available":false',output.call_args.args[0])


class StreamTests(unittest.TestCase):
    def test_fragmentation_all_splits_back_to_back_and_empty(self):
        a=response_frame(1); b=response_frame(2)
        for split in range(len(a)):
            parser=h.StreamAccumulator(); self.assertEqual([],parser.feed(a[:split])); self.assertEqual([a],parser.feed(a[split:]))
        parser=h.StreamAccumulator(); self.assertEqual([a,b],parser.feed(b"\0"+a+b"\0"+b))

    def test_malformed_and_oversize_recovery(self):
        good=response_frame(1); parser=h.StreamAccumulator()
        self.assertEqual([b"\x05\x01\0",good],parser.feed(b"\x05\x01\0"+good))
        parser=h.StreamAccumulator(); self.assertEqual([good],parser.feed(b"\x01"*200+b"\0"+good))

    def test_correlation_ignores_unrelated_then_matches(self):
        chunks=[response_frame(2)+response_frame(1)]
        frame,elapsed=h.wait_for_response(lambda n:chunks.pop(0) if chunks else b"",1,1.0)
        self.assertEqual(1,frame.request_id); self.assertGreaterEqual(elapsed,0)

    def test_deadline(self):
        times=iter((0.0,0.0,1.0))
        with mock.patch.object(h.time,"sleep"):
            with self.assertRaises(h.HostTimeout): h.wait_for_response(lambda n:b"",1,0.5,lambda:next(times))

    def test_malformed_response_is_distinct_from_silence(self):
        chunks=[h.named_vectors()["bad_crc"]]
        times=iter((0.0,0.0,1.0))
        with mock.patch.object(h.time,"sleep"):
            with self.assertRaises(h.ProtocolFailure): h.wait_for_response(lambda n:chunks.pop(0) if chunks else b"",1,0.5,lambda:next(times))


class CliTests(unittest.TestCase):
    def test_dry_run_needs_no_serial_and_zero_id_rejected(self):
        self.assertEqual(h.EXIT_OK,h.main(["ping","--target-device","1","--request-id","0x1202","--dry-run"]))
        self.assertEqual(h.EXIT_MALFORMED,h.main(["ping","--target-device","1","--request-id","0","--dry-run"]))

    def test_decode_and_vectors_modes(self):
        self.assertEqual(h.EXIT_OK,h.main(["decode",h.named_vectors()["hello"].hex()]))
        self.assertEqual(h.EXIT_OK,h.main(["vectors","bad_crc"]))

    def test_live_missing_pyserial_is_actionable(self):
        with mock.patch.dict("sys.modules",{"serial":None}):
            with self.assertRaises(RuntimeError): h.live_exchange("COM_TEST",115200,b"x",1,0.1)

    def test_serial_open_establishes_inactive_control_lines_before_open(self):
        events=[]
        class FakeSerial:
            def __init__(self):
                object.__setattr__(self,"is_open",False); object.__setattr__(self,"events",events)
                events.append(("construct",False))
            def __setattr__(self,name,value):
                if name in ("dtr","rts","rtscts","dsrdtr","xonxoff","port"):
                    self.events.append((name,value,self.is_open))
                object.__setattr__(self,name,value)
            def open(self):
                self.events.append(("open",self.port,self.dtr,self.rts,self.rtscts,self.dsrdtr,self.xonxoff))
                self.is_open=True
            def close(self):
                self.events.append(("close",self.dtr,self.rts)); self.is_open=False

        for _ in range(2):
            stream=h.open_serial_transport("COM_TEST",115200,0.05,1.0,FakeSerial)
            self.assertTrue(stream.is_open); stream.close()
        opens=[event for event in events if event[0]=="open"]
        self.assertEqual(2,len(opens))
        self.assertTrue(all(event[1:]==("COM_TEST",False,False,False,False,False) for event in opens))
        for index,event in enumerate(events):
            if event[0]=="open":
                prior=events[:index]
                self.assertIn(("dtr",False,False),prior)
                self.assertIn(("rts",False,False),prior)
                self.assertIn(("port","COM_TEST",False),prior)
        self.assertFalse(any(event[0] in ("dtr","rts") and event[2] for event in events if len(event)>2))


if __name__ == "__main__": unittest.main()
