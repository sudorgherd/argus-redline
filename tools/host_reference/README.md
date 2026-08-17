# REDLINE Host Protocol 0.1 reference utility

This is a small developmental qualification tool for the binary REDLINE Host
Protocol. It is not a stable SDK, daemon, application client, or API contract.
The normative authority is [`docs/HOST_PROTOCOL_0.1.md`](../../docs/HOST_PROTOCOL_0.1.md).

Python 3 is required. Encoding, decoding, vectors, and tests use only the
standard library. Live serial use optionally requires `pyserial`; install it
explicitly if needed (`python -m pip install -r tools/host_reference/requirements.txt`).
Nothing is installed automatically.

## Examples

Dry run (no serial dependency):

```powershell
python tools/host_reference/redline_host.py ping --target-device 0x01 --request-id 0x1202 --dry-run
```

Decode a captured, delimiter-terminated frame:

```powershell
python tools/host_reference/redline_host.py decode "01 03 01 01 04 34 12 02 05 01 01 45 EA 00"
```

Live qualification examples (IDs `0x01` and `0x10` are examples for the
current two-device setup, not protocol constants):

The live utility constructs the serial object closed, disables software and
hardware flow control, sets DTR and RTS inactive, and only then opens the
requested port. It does not intentionally pulse either control line on close.
This avoids using reset/bootloader control behavior, but physical qualification
is still required for each host, driver, and board combination.

```powershell
python tools/host_reference/redline_host.py hello --port COM4 --request-id 0x1201 --show-hex
python tools/host_reference/redline_host.py ping --port COM4 --request-id 0x1202 --target-device 0x01 --show-hex
python tools/host_reference/redline_host.py status --port COM4 --request-id 0x1203 --target-device 0x10 --show-hex
python tools/host_reference/redline_host.py capabilities --port COM4 --request-id 0x1204 --target-device 0x10 --cursor 0
python tools/host_reference/redline_host.py describe --port COM4 --request-id 0x1205 --target-device 0x10 --capability-id 0x0101
python tools/host_reference/redline_host.py read --port COM4 --request-id 0x1206 --target-device 0x10 --capability-id 0x0201
python tools/host_reference/redline_host.py set-indicator --port COM4 --request-id 0x1207 --target-device 0x01 --capability-id 0x0101 --value true
```

The default host-observed response timeout is 10 seconds and can be changed
with `--timeout`. It is distinct from a device-authored
`RADIO_RESULT/TIMEOUT`. The tool never automatically retries. To qualify the
retained-result lifecycle, repeat the exact invocation with the same explicit
request ID. Never blindly retry a side effect after device reset: volatile
device retry evidence is then gone and completion may be ambiguous.

Named malformed vectors are listed with `vectors`; one vector can be printed
or explicitly transmitted:

```powershell
python tools/host_reference/redline_host.py vectors
python tools/host_reference/redline_host.py vectors bad_crc
python tools/host_reference/redline_host.py vectors bad_crc --port COM4
```

Malformed traffic is never transmitted unless both a vector name and port are
explicitly supplied. Output includes request IDs and raw payload hex; use
`--show-hex` for exact TX/RX evidence. `--json` is deterministic qualification
output, not a stable machine API.

Exit codes are developmental: `0` valid success, `2` valid device non-success,
`3` host-side correlated-response timeout, `4` malformed protocol data, and
`5` local usage/dependency/I/O failure.

Run pure tests without hardware or pyserial:

```powershell
python -m unittest tools.host_reference.test_redline_host
python -m py_compile tools/host_reference/redline_host.py
```
