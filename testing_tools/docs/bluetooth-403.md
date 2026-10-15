# Native Bluetooth investigation on FW 4.03

For the new search/pair/connect console interface and automatic reconnect,
use [Controller Manager](../../docs/controller-manager.md), built with `make manager`.
The standalone bridge below remains available for diagnostics.

The quiet USB release does not contain a Bluetooth driver. Separate
experimental executables investigate the console's built-in radio:

| Target | ELF | Behavior |
| --- | --- | --- |
| `make bt-probe` | `dist/ps5-xinput-bt-probe.elf` | Lists devices/modules, reads USB descriptors and inspects its own BluetoothHid exports. No HCI traffic. |
| `make bt-radio-probe` | `dist/ps5-xinput-bt-radio-probe.elf` | Queries version, supported commands, classic features and LE features on function 0. No reset or scanning. |
| `make bt-scan` | `dist/ps5-xinput-bt-scan.elf` | Runs a 30-second active LE discovery scan on function 0, disables its scan, then exits. No connections or bonding. |
| `make bt-pair` | `dist/ps5-xinput-bt-pair.elf` | Pairs the selected Xbox using LE Secure Connections, saves its bond, disconnects and exits. Subsequent runs try encrypted reconnect using the saved bond. No virtual pad or game input. |
| `make bt-pair-trial` | `dist/ps5-xinput-bt-pair-trial.elf` | Uses a separate trial bond file for fresh pairing and reconnect tests. |
| `make bt-capture` | `dist/ps5-xinput-bt-capture.elf` | Reads the HID report map, enables input notifications and logs reports for two minutes. No virtual pad. |
| `make bt-bridge` | `dist/ps5-xinput-bt.elf` | Loads the tested trial bond, enables HID notifications and forwards the verified Xbox Series input layout to a virtual pad until stopped or disconnected. |

All use UDP **9024**, separate from the working USB payload on 9023.
Run only one Bluetooth executable at a time. Do not run them alongside another
payload using the first Bluetooth function, such as PlayPods. Sharing event
reads with the system can lose command replies. Commands retry with bounded
waits; a retry can return Command Disallowed if an earlier enable succeeded.
Scan Disable is attempted even if the enable response was lost or the PC
requested STOP. A failed disable confirmation is logged explicitly.

Start the viewer before sending the executable:

```powershell
python scripts/watch_logs.py --ps5 192.168.1.136 --port 19024 --remote-port 9024 --output build/bluetooth-probe.log --deduplicate
python scripts/send_payload.py 192.168.1.136 --elf dist/ps5-xinput-bt-scan.elf
```

For discovery, disconnect the Xbox USB cable, power on the controller and hold
its Pair button until the Xbox light flashes rapidly. The scanner does not
create a PS5 user-assignment prompt because it does not create a virtual pad.

## Observations from the console

The metadata probe reported firmware `0x04030000`, `/dev/bluetooth_hid` and
`/system/common/lib/libSceBluetoothHid.sprx`. SceSysCore has the HID library
loaded. The library exposes initialization, callback/device registration,
device information, report descriptor and input-report functions. Their names
do not establish an API for discovery or bonding; no guessed prototypes were
called. Code captures are in `build/bluetooth-probe.log` and the console's
`/data/ps5-xinput/bt-probe.log`.

The internal USB radio is `/dev/ugen0.2`, `1286:2059`. Four Bluetooth-class
interfaces represent two HCI/SCO pairs. HCI interface 0 has event IN `82`, ACL
IN `81`, ACL OUT `01`; HCI interface 2 has event IN `85`, ACL IN `84`, ACL OUT
`04`. The initial metadata probe counted all four class interfaces as HCI
functions; the corrected probe counts the interrupt-IN interfaces, yielding
two. The transport validates the observed product and function-0 event endpoint
before opening USB_FS. It never detaches a system driver, claims an interface,
selects a USB configuration, resets HCI, or reads function-2 events.

The feature check succeeded on function 0:

| HCI command | Return parameters, including status |
| --- | --- |
| Read Local Version `1001` | `000a00830a48001c13` |
| Read Local Supported Features `1003` | `00bff88bfedbff7b83` |
| LE Read Local Supported Features `2003` | `006ffb460800000000` |

The classic feature response advertises LE support. All four capability
queries returned success. The user confirmed that the wireless DualSense still
navigated normally after both the metadata and feature probes.

The first discovery run received successful Set Scan Parameters (`200B`) and
Set Scan Enable (`200C`) responses, followed by an advertising report and scan
response identifying **Xbox Wireless Controller**, with Microsoft company ID
`0006` and HID service UUID `1812`. This establishes native BLE discovery on
this console; it does not establish Bluetooth controller input support.
The scan completed with 8 devices, 675 advertising reports, 367 LE events and
zero malformed events. Scan Disable returned success (`0`), its own event
transfers closed, and the user confirmed the wireless DualSense still worked.

## Pairing result and usage

The Xbox completed Secure Connections Just Works pairing on the console.
The payload received and validated the peer public key, confirm value and
DHKey check; the radio reported successful encryption. The Xbox then sent
Identity Information and Identity Address Information. Only after these
steps did the payload save `/data/ps5-xinput/bt-bond.bin`, disconnect its own
connection and exit. No secret key bytes are included in the logs.

Build and run:

```powershell
wsl.exe -d Ubuntu --cd /home/fmlp/ps5_xinput -- make bt-pair
python scripts/send_payload.py 192.168.1.136 --elf dist/ps5-xinput-bt-pair.elf
```

This experimental build is pinned to the discovered controller
`F4:6A:D7:BC:B2:80`. It does not choose an arbitrary Xbox in range. A first
run requires the Xbox Pair button; a subsequent run loads the saved bond and
tries encryption directly, without exchanging new pairing keys. Turn the
Xbox off and on normally for this reconnect test. The test intentionally
disconnects after verification, so the controller light can start flashing
again. It does not register a virtual controller or deliver menu/game input.
Saved-bond reconnect has been verified with the separate trial build described
below; the original diagnostic bond failed its reconnect tests.

The bond is a versioned 64-byte record containing the radio address, controller
identity, LTK, optional IRK and advertising address. It is written to a unique
temporary file with mode `0600`, flushed with `fsync`, and atomically renamed.
Existing malformed/inaccessible bonds and radio-address mismatches stop the
test; they are not silently overwritten. A future pairing UI will provide a
Forget action. For development, removing exactly
`/data/ps5-xinput/bt-bond.bin` via the console's file manager allows a fresh
pairing, which requires the controller's Pair button again.

To stop this experimental payload from the PC:

```powershell
python scripts/stop_payload.py 192.168.1.136 --port 9024
```

Pairing has a 30-second discovery deadline, a 15-second initial connection
deadline (45 seconds for reconnect), and a 45-second security deadline.
Successful encryption is followed by primary GATT service discovery and a
15-second observation before disconnection. Cleanup disables its own scan, cancels a pending
connection attempt and requests disconnection of its own connected handle.
It keeps the system driver attached and does not reset the chip. Lost shared
event/ACL reads can still interrupt an exchange; the build does not exceed
the radio's ACL buffer credits to work around a missing completion event.

Host verification includes Bluetooth f4/f5/f6 reference vectors and Secure
Connections state tests: successful exchange, wrong confirm/DHKey values,
invalid public points, malformed messages, reduced key size and rejection of
legacy pairing. See [dependency provenance](../../third_party/README.md).

The separate `make bt-pair-trial` build uses
`/data/ps5-xinput/bt-bond-trial.bin` instead of the original bond file. Its first
run performs fresh pairing; subsequent runs test that trial bond. It preserves
the original console-side record, although fresh pairing changes the bond on
the Xbox itself. This is a development diagnostic, not an automatic recovery
or user-facing Forget workflow.

Hardware observations: fresh Secure Connections pairing and encrypted GATT
service discovery succeeded. The Xbox returned HID service `1812` at handles
`0016` through `0023`, and remained encrypted for the entire 15-second
observation. Earlier saved-bond attempts connected only in pairing mode and
timed out during encryption with HCI status `08`. After a fresh trial bond,
normal-power-on reconnect succeeded without a new SMP pairing exchange,
encryption enabled, service discovery completed, and notifications arrived.
Two consecutive controller power-cycle reconnect tests passed, each remaining
connected and encrypted for the full 15-second observation. The user confirmed
wireless DualSense navigation still worked between tests.
The root cause of the original reconnect failure has not been established.
These results do not establish Bluetooth input support.

## Remaining work

1. Extend saved-bond reconnect testing to console restarts and longer sessions.
2. Add a controller selection/Forget workflow and harden shared transport
   reliability under repeated connections and missing events.
3. Extend report-map support beyond the verified Xbox Series descriptor and
   broaden sustained gameplay and idle testing.
4. Add reconnection and independent state for two Bluetooth controllers; test
   simultaneous USB/Bluetooth operation and wireless DualSense coexistence.

Discovery leaves the system's existing HCI/LE event masks unchanged. It can
therefore depend on the system's radio initialization. Supported features and
a successful scan do not prove that pairing, encryption or sustained input
traffic will coexist reliably with the shared system reader.

## Experimental Bluetooth input bridge

The user confirmed input works with `dist/ps5-xinput-bt.elf` on FW 4.03.
The bridge uses the saved `/data/ps5-xinput/bt-bond-trial.bin` from the successful
pairing/reconnect tests. If that file is absent, it performs fresh pairing and
needs the Pair button. With an existing valid bond, turn the Xbox on normally
without pressing Pair. Disconnect its USB cable for native Bluetooth operation.

```powershell
wsl.exe -d Ubuntu --cd /home/fmlp/ps5_xinput -- make bt-bridge
python scripts/send_payload.py 192.168.1.136 --elf dist/ps5-xinput-bt.elf
```

After the controller-ready notification, select your PS5 user using the Xbox
controller. The bridge maps both sticks, LT/RT, ABXY, D-pad, LB/RB, stick clicks,
Menu to Options, View/Share to touchpad click and Xbox/Home to the PS button.
It uses the same FW 4.03 virtual-pad submission layout as the working USB
bridge. Remote shell injection stays disabled.

Hold **View + Menu for two seconds** to stop, or send:

```powershell
python scripts/stop_payload.py 192.168.1.136 --port 9024
```

For live logs, the Bluetooth executable uses remote UDP port **9024**:

```powershell
python scripts/watch_logs.py --ps5 192.168.1.136 --port 19024 --remote-port 9024 --output build/bluetooth-probe.log --deduplicate
```

Setup discovers attributes within HID service `1812`, reads the complete
283-byte report map, reads each Report Reference descriptor and enables
notifications only for input reports. On the tested Xbox, report ID 1 is the
16-byte input value at `001E`; report ID 3 at `0022` is output and is not
subscribed. The controller returns zero-expanded 128-bit `2A4A` through `2A4E`
UUIDs in Find Information responses. A compatibility rule accepts that
representation only within this discovered HID service.

The decoder validates the captured 191-byte input-descriptor prefix before
registering a pad. Unknown layouts remain unregistered. Tests cover captured
descriptor validation, stick direction/endpoints, trigger scaling, buttons,
eight hat directions and neutral, Guide press/release, Share and malformed
reports. GATT tests cover paged discovery, both UUID representations, long
map reads, reference/CCCD handling, notification routing and response timeouts.

This build remains experimental: one pinned controller, diagnostic logging,
no automatic reconnect after a live link drops, no Bluetooth multiplayer,
rumble, motion or headset support. After disconnection, load the payload again
and turn the controller on normally. The quiet USB release remains separate.
Only one Bluetooth payload may own function 0 at a time. Shared system reads
can still lose a reply or completion event and interrupt setup.

## References

- [FGG-PlayPods](https://github.com/FGGstore/FGG-PlayPods), local reference at
  commit `4b4ddb50e68a70af1cb914cff28bc4807a74e4bd`: dual-function USB layout and
  sharing event reads without detaching the system driver. Its audio pairing
  uses classic Bluetooth and is not an Xbox BLE pairing implementation.
- [PS5 3.20 BluetoothHid export stubs](https://github.com/DNNDHH/PS5-3.20_Libs/blob/main/libSceBluetoothHid.c):
  candidate symbol names, verified against the console's own 4.03 module.
- [Zephyr HCI definitions](https://github.com/zephyrproject-rtos/zephyr/blob/main/include/zephyr/bluetooth/hci_types.h):
  LE command and advertising report layouts.

The advertising decoder has host checks for truncated events, malformed AD
fields, excessive legacy data lengths, incomplete multi-report events, signed
RSSI, HID/manufacturer/name extraction and control-character sanitization.
