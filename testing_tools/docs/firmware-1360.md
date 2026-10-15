# Firmware 13.60 build

Use `ps5-xinput-manager-1360.elf` for the console reported to run 13.60.
The default `ps5-xinput-manager.elf` continues to use the verified 4.03 layout.
On 2026-10-15 the user reported that their friend tested this variant and a USB
Xbox controller worked. This confirms that test, not general Bluetooth or
multi-controller compatibility on 13.60. Firmware 4.03 remains the default.

The supplied, decrypted `libScePad.sprx` is 227448 bytes with SHA-256:

```
081e645f0ac29406bd91d97a392d7f2b7f00d67d26618ce414dcf6f930b4d9d8
```

The firmware attribution comes from the supplied console/file context.
The file itself is not independently authenticated as a Sony 13.60 release.
Local disassembly is in `build/pad-1360/`; the module is not bundled.

Reproduce the inspection on Linux/WSL with:

```sh
python3 scripts/inspect_pad_module.py /path/to/libScePad.sprx --output build/pad-1360
```

`scePadVirtualDeviceInsertData` (relative address 0xDA20, 355 bytes) uses:

| Field | Supplied module | Existing 4.03 implementation |
| --- | --- | --- |
| Input ioctl | 0x8018482C | 0x8010482C |
| Request length | 24 bytes | 16 bytes |
| Handle | uint32 at offset 0 | Same |
| Report pointer | offset 8 | Same |
| Extra flag | byte at offset 16, set to zero | Absent |
| Report buffer initialized by wrapper | 168 bytes | 168 bytes |
| Buttons, sticks, triggers, active | 0x0C, 0x10, 0x14/0x15, 0x17 | Same |
| Eight-float motion block | 0x78 | 0x80 |

At 0xDB2C the wrapper loads the new ioctl; at 0xDB31/0xDB37/0xDB40 it
stores the handle, report pointer, and zero flag. The report base is rbp-0xC0;
the motion store at rbp-0x48 (0xDB58) gives offset 0x78. Its initialization
clears five 32-byte blocks plus eight bytes. AddDevice (0xD8A0, 283 bytes)
retains a 24-byte request and descriptor fields 0, 0x1C and 0x20. DeleteDevice
(0xD9C0, 95 bytes) retains the 16-byte command-5 request.

The failed live log detected/read the USB Xbox but rejected every submission
with raw return 0x803B0004. A request-format mismatch is consistent with that
failure. The separate variant was subsequently confirmed working in the USB
Xbox test reported above.

Build with `make manager-1360`. Objects, embedded service, ELF names, checksums
and ZIP are separate from the default firmware build. The firmware change is
limited to the virtual-pad request and motion layout. Bluetooth transport on
13.60 remains unverified; start with one USB Xbox controller.

Validation passed: both firmware builds, the full existing host suite, exact
request/flag and motion-layout checks for both profiles, and the four-controller
USB integration simulation compiled with the 13.60 profile. Both checksum files
verify their corresponding manager and service ELF. These checks do not verify
the actual 13.60 kernel or console UI.

Before testing, stop an older manager service cleanly: the launcher reuses a
service already listening on port 9030 instead of replacing it. If the service
is running, use:

```sh
python scripts/stop_payload.py FRIEND_PS5_IP --port 9024
```

Then capture logs and send the variant:

```sh
python scripts/watch_logs.py --ps5 FRIEND_PS5_IP --remote-port 9024 --port 19024 --output friend-1360-test.log --deduplicate
python scripts/send_payload.py FRIEND_PS5_IP --elf dist/ps5-xinput-manager-1360.elf
```

Look for `Virtual pad ABI: 13.60 test; InsertData 8018482C, request 24 bytes,
motion offset 78`. A correct label confirms the selected build, not working
controls. Confirm accepted input counts, buttons, sticks, triggers and Home.
Logs remain at `/data/ps5-xinput/manager.log` and UDP port 9024.
