# Native Bluetooth routing investigation (FW 4.03)

The objective is to preserve native DualSense connection, authentication and
input handling while forwarding only Xbox traffic to the bridge. No native
packet-forwarding backend has been implemented or validated yet.

## Evidence

The function-0 transport competes with system reads. Filtering a packet after
USB completion does not return it to the native consumer. Console traces
captured classic connection requests and completions while the Xbox BLE
session was active. DualSense reconnect failed; releasing the complete service
eventually restored it without restarting the console in one test.

Automatic 30-second radio handoffs repeatedly interrupted both controllers.
Persistent pausing restored DualSense, but explicit Xbox reconnect triggered
another handoff. This is recovery behavior, not traffic isolation.

SceSysCore loads libSceBluetoothHid.sprx on 4.03. Existing export captures
include HID initialization, device registration and report/callback functions.
Export names alone establish neither a raw-HCI injection API nor BLE support.
Do not call these using guessed signatures.

The second USB HCI interface accepted endpoint setup but did not return the
feature-query responses with the tested interface-recipient request format.
This does not prove that every command-routing format is unavailable.

## Passive ownership diagnostic

Build `make bt-native-probe`, then load
`dist/ps5-xinput-bt-native-probe.elf` with the manager stopped. The diagnostic
uses process/module inventory and KERN_PROC_FILEDESC metadata. It does not
open the USB transport, start transfers, send HCI commands, invoke native HID
initialization or change another process's memory. Logs use UDP 9024.

Firmware record layout and syscall availability must be checked before
interpreting a missing file-descriptor match as evidence of no native owner.
Fresh execution captures on FW 4.03 parsed variable-sized descriptor records
successfully. SceSysCore had two `/dev/hid` descriptors and the BluetoothHid
module loaded, but no Bluetooth-specific or radio USB descriptor matched in
the captured process tables. This suggests kernel ownership of the native
transport; it does not identify its callbacks or prove ownership.

The diagnostic also checks the FreeBSD read-only `hw.bus.info` and
`hw.bus.devices` inventory, validating its version and record sizes before
interpretation. On this console `hw.bus.info` returned ENOENT (errno 2), so
that inventory cannot identify the native driver. The implementation follows
the generation/index query format in FreeBSD's
[libdevinfo](https://github.com/freebsd/freebsd-src/blob/releng/11.0/lib/libdevinfo/devinfo.c).
No kernel hooks or native report injection have been attempted.

## Kernel inventory experiment (disabled)

A separate bounded read experiment used the SDK's FW 4.03
`KERNEL_ADDRESS_BUS_DATA_DEVICES` mapping and the candidate FreeBSD 11
device prefix. One complete capture passed list-backlink/state checks for
126 records, including four attached devices `btts0` through `btts3`, all
with driver name `btts`. This identifies native Bluetooth driver instances,
but not their endpoint ownership or a forwarding API.

A subsequent capture intended to inspect static class/method metadata ended
at device index 99, before reaching those instances or the completion marker;
the ELF loader was then unreachable. The user confirmed the console froze.
The kernel experiment is excluded from every build target behind
`BT_NATIVE_KERNEL_EXPERIMENT`. Canonical address and list checks do not prove
that a memory read is safe. Do not enable it until the read mechanism and
record handling have been hardened. Normal `bt-native-probe` retains the
metadata/sysctl path that the user confirmed leaves DualSense functional.

## Offline kernel reference

The supplied `x86_kernel_1000.elf` is retained unchanged. SHA-256:
`1e2e55bbd67ae52311f742b01440ec180493286b98ab2910404b2d0fa717d17f`.
Its executable segment starts at `0xffffffff80210000`; its following read-only
segment starts at `0xffffffff80ed0000`, a difference of `0xCC0000`. This agrees
with the SDK text/data spacing for the 10.00/10.01/10.20/10.40/10.60 family,
whereas the SDK spacing for FW 4.03 is `0xC00000`. This is a family inference,
not confirmation of the exact firmware version.

`python3 scripts/inspect_kernel.py x86_kernel_1000.elf --output
build/kernel-offline-report.json` reconstructs relative pointers only in a
local bytearray. It never executes the image, alters the source ELF, or
contacts the console. The capture has no section headers, but its program
headers and dynamic relocation table are sufficient for this inspection.
The script located a candidate `btts` class at link address
`0xffffffff822d5db0` with three method entries. These are offline link
addresses for this image, not addresses to call or patch on FW 4.03.
The method entries are direct-jump trampolines. Offline inspection now follows
bounded direct jumps and exports the implementation bytes for disassembly
with `--export-methods build/kernel-methods`. Method index 1 resolves to
`0xffffffff8083f370` in this image. Its code checks USB VID `1286`, PID `2059`,
and branches on interface numbers 0 and 2. This validates its relevance to
the payload's radio device, but does not establish FW 4.03 compatibility or
a way to preserve native traffic. Disassembly artifacts remain local only.

The attach code selects a five-transfer config at `0xffffffff81087c60` for
Marvell interface 0 and a four-transfer config at `0xffffffff81087d00` for
interface 2. The candidate 40-byte USB config records encode command bulk
OUT endpoints `02`/`05`, event interrupt IN endpoints `82`/`85`, and ACL
bulk OUT/IN `01`/`81` and `04`/`84`. Earlier FW 4.03 USB descriptors contain
the same extra bulk OUT endpoints `02` and `05`. Command callbacks for both
interfaces feed a common transmit routine; it copies the queued bytes into
the outgoing USB frame. This differs from the control-endpoint routing used
in the earlier feature-only function-2 test.

`make bt-secondary-bulk-probe` builds a feature-only variant with one event
read on `85`, bulk commands on `05`, and no ACL endpoints. It queries local
version, supported features/commands, LE features, and controller address;
no reset, scanning, pairing or connection commands are sent. The bridge's
transport is unchanged: this bulk route is isolated behind a probe-only
compile option. Build success does not establish runtime responses or
native-controller coexistence. It must run without other HCI payloads.

The FW 4.03 console feature-only bulk test subsequently returned success
for all five queries. Local address was `80:60:B7:BB:7F:C5`, distinct from
function 0's previously observed `80:60:B7:BB:7F:C4`. LE support was present.
One event read was used; several replies required retries. Cleanup confirmed
transfer cancellation, command endpoint closure, event endpoint closure,
and transport closure. This establishes a working command route for function
2, not uninterrupted native coexistence or an Xbox bridge on that function.
`make bt-secondary-observer` builds the same no-command, single-event-read
120-second reconnect test on interface 2 (event endpoint `85`, log port 9025).
The user confirmed DualSense reconnects and works during the interface-2
single-reader trial. It then closed automatically after 120 seconds, with
cancel/close/completion markers captured by an older viewer in
`build/8bitdo-transport-only.log`; that competing viewer was stopped. This
is a positive contrast to function 0's failure with one reader, but it does
not test concurrent Xbox ACL traffic.

`make bt-secondary-bridge` builds the standalone Xbox trial
`dist/ps5-xinput-bt-secondary.elf`. It uses function 2's event/ACL endpoints
and dedicated bulk command endpoint `05` with its own transfer slot and
completion/cleanup tracking. Trial bonds use
`/data/ps5-xinput/bt-bond-secondary.bin`; the prior manager bonds are retained.
The feature probe was repeated after separating command and ACL transfer
slots and returned successful responses for the four capability queries.
The final address query and cleanup markers were not captured in that repeat;
the probe log port no longer responded while the ELF loader remained reachable.
Do not describe that repeat's cleanup as verified. The full bridge
is built but requires fresh pairing and concurrent/reconnect validation.
It is not yet integrated into the all-in-one manager. Standalone trial input
has a two-minute observation window after encrypted setup.

The first full function-2 bridge trial opened 62 event reads, 24 ACL reads,
and its dedicated command pipe; local address was confirmed as the `C5`
radio. Scanning discovered the selected Xbox and LE Connection Complete
reported success, handle 128. After the first SMP Pairing Request, Disconnect
Complete reported reason `3E` (Connection Failed to be Established), before
any pairing response or encryption. No new trial bond was saved. Cancellation
and closure of command, ACL OUT, ACL IN and event transfers all completed.
This does not validate Xbox input or identify the cause of the failed link.
A fresh pairing-mode retry and native-controller check are pending.
The user confirmed native DualSense still worked after the first trial.
The fresh retry progressed through the Secure Connections public-key,
confirm/random and DHKey exchanges and enabled encryption. During identity
distribution, the bridge received Identity Address Information (`09`) without
having received Identity Information (`08`), so the strict state machine
sent failure `0A` and saved no bond. This is consistent with a missed ACL
packet but does not prove the native reader took it. The trial transport now
requests up to 64 ACL readers instead of 24 to reduce competition; the default
bridge remains at 24. Actual opened count and pairing remain untested.
The retry logged cancellation and command/ACL OUT endpoint closure but no
final shutdown marker; a subsequent UDP stop check received no response.
Payload Manager verification/closure was requested before another trial.
The user's next response selected a combined "DualSense stopped working or
trial cannot close" option. That response does not distinguish controller
failure, an uncloseable trial, or a whole-console freeze. Further deployment
was stopped and a three-way clarification requested. Do not interpret the
single-reader function-2 success as validated full-bridge coexistence.

FreeBSD 11's
[USB generic implementation](https://github.com/freebsd/freebsd-src/blob/releng/11.0/sys/dev/usb/usb_generic.c)
shows that `USB_FS_STOP` can leave a cancellation callback pending, while
`USB_FS_CLOSE` calls `usbd_transfer_unsetup`. This motivates investigating
cancellation/drain behavior, but does not prove the PS5 teardown stall's
cause or justify claiming that draining userland completions fixes it.

## Requirements before integration

### Controller behavior baseline

`make native-baseline` builds `dist/ps5-xinput-native-baseline.elf`, a direct
service payload with automatic Bluetooth connection attempts paused at startup.
Saved bonds and the stored reconnect preference remain unchanged. It keeps
the manager and USB polling available, but opens no HCI transport unless an
explicit Search/Pair/Connect command requests it. It does not open a browser
automatically. Do not press those commands during the initial baseline.

After recovering from the freeze and confirming the native wireless DualSense
works, run this baseline with Xbox off and USB controllers unplugged. Turn
DualSense off via Accessories, then press PS and verify wireless reconnection
and navigation. This distinguishes service/USB polling from shared-radio reads.
The user confirmed DualSense reconnects and navigates wirelessly during this
baseline; execution logs confirmed the radio remained unopened.

`make bt-reconnect-observer` builds a separate 120-second event-only
competition test, `dist/ps5-xinput-reconnect-observer.elf`. It uses one pending
read on function 0, no ACL reads and no HCI commands. Its log/stop port is
9025. It closes its transport automatically and logs captured event counts.
This consumes shared events; it is not a transparent or passive packet tap.
It tests whether even a single reader can disrupt native reconnection without
an Xbox session. The baseline service can remain running because it leaves
the radio unopened. Never run it alongside an active Bluetooth bridge.

The first console trial confirmed function 0 opened with one event read.
The user reported DualSense could not reconnect. The observer captured one
Disconnect Complete (`05`, success, reason `13`), one Command Status (`0F`),
and one Number Of Completed Packets (`13`). It sent no HCI commands, so the
captured command reply belonged to other radio activity. This supports shared
event consumption as the issue independently of Xbox pairing/encryption or
the bridge's large read queue. It does not prove which missed event caused
the failure, nor exclude other effects of opening the transport.
An explicit stop captured transfer cancellation, endpoint closure, shared
transport closure and the observer completion marker. The stop script's old
marker list incorrectly reported missing confirmation; it now recognizes the
observer's completion marker as well. The baseline manager remained Ready.
The user then confirmed DualSense still only blinked after observer shutdown
and a retry delay. The baseline manager was subsequently stopped: logs
confirmed USB cleanup and `Manager: service stopped`. Recovery with all
payload activity stopped also failed: the user confirmed DualSense still
only blinked after the retry delay. Console restart is required to recover
from this particular trial. The lasting failure
means simply reducing the read queue or closing it after detecting a native
request cannot be considered a reliable coexistence strategy.

Identify the native transport consumer and a verified way to duplicate or
route packets before competing USB reads consume them. Native classic events,
native ACL fragments, command replies and shared completion-credit events must
continue reaching that consumer in order. Xbox routing must be based on its
established LE handle and outstanding commands, preserving mixed events.

### Secondary-interface cleanup test

The user clarified that DualSense stopped responding after the encrypted
interface-2 trial while the PS5 otherwise remained responsive.
`make bt-secondary-cleanup` builds `ps5-xinput-secondary-cleanup.elf`: an
interface-2 event/ACL queue observer that sends no HCI commands and makes no
Xbox connection. It tracks its outstanding transfers and drains cancellation
completions for up to 1.5 seconds before the existing endpoint-close sequence.
Timeout or error is logged; this does not establish that endpoint closure
cannot stall. The diagnostic isolates queue cleanup from encrypted pairing.

Local cancellation tests pass for delayed and duplicate completions, timeout,
callback error, invalid indices and an empty queue. The diagnostic compiles
with warnings treated as errors. It has not been deployed or console-validated;
the user reports the console is not ready yet.

Subsequent live test: the diagnostic opened 62 event reads and 24 ACL reads
on interface 2, with Xbox off and USB controllers unplugged. After switching
DualSense off and attempting reconnection, the user reported no response and
a blue light (rather than blinking). The observer received a native command
complete for opcode 080D with status 02. This confirms native command traffic
can reach our interface-2 queue; endpoint numbering alone does not establish
isolation. It does not prove which consumed packet caused the failure.

The two-minute timer completed. Cancellation draining returned 0 with zero
pending slots, all endpoint groups closed, and the final transport/completion
markers were logged. Thus this test reproduced native-controller disruption
without Xbox pairing and without a cleanup stall. Recovery after complete
shutdown failed: the user held PS to power off and retried both wireless and
USB, reporting neither worked and a steady blue light. A steady light is not
proof of an operational native input connection. Console restart is the next
recovery step; no further shared-reader trials should run in this state.
The earlier one-event-read interface-2
success cannot be generalized to a larger event/ACL queue footprint.

### Suspend recovery and native HID library inspection

After the failed queue-only test, the user reports Rest Mode followed by
waking with DualSense restores input, and the ELF loader resumes. This is a
recovery observation, not evidence that Bluetooth coexistence is fixed or that
the observer remains running (its completion marker was already captured).
Suspend/resume restoring driver or controller state is a hypothesis.

The existing firmware-4.03 `build/bluetooth-probe.log` contains seven 512-byte
Bluetooth HID export captures. Reassemble and disassemble offline with:

```
python3 scripts/extract_pad_abi.py build/bluetooth-probe.log --prefix bt --output build/bt-native-abi --disassemble
```

`sceBluetoothHidRegisterDevice` saves the first two integer arguments as two
16-bit fields and passes their four-byte buffer with request 0x8004622B to an
import stub. It checks library initialization and callback state first.
This does not resemble a direct Bluetooth-address/key pairing API, and its
argument meanings and import target remain unverified. Do not call it as a
replacement for LE pairing based on its name alone. Callback setup also calls
uncaptured import stubs; the available fragments do not establish a native
GATT path. Further offline import/caller identification is needed.

The library-only probe ran twice on 4.03 and completed both times. It opens
no USB transport and invokes no HID initialization, registration or pairing
exports. The user confirmed DualSense navigation/PS after the first run;
the second run's native input check remains pending. Strings identify
`/dev/bluetooth_hid` as the path loaded by HID initialization and `/dev/bt`
as the path loaded by GetDeviceInfo. These names do not establish BLE support.
The report worker capture contains request 0x8010622E with a buffer pointer
and byte-sized second field; its output layout and semantics remain unknown.
Import stubs use indirect GOT jumps, so capturing stubs alone has not resolved
the target identities. Artifacts: `build/bt-library-abi` and
`build/bt-library-probe.log`. Some repeated export chunks were lost over UDP;
the three extended ranges are contiguous and extracted using `--symbol`.

### Minimal-queue Xbox trial

`make bt-secondary-minimal` builds an isolated interface-2 trial using one
event read, one ACL read, dedicated bulk commands and cancellation draining.
It preserves the existing secondary trial bond path and release configuration.
Live log: `build/bt-secondary-minimal.log`.

The trial established Xbox LE handle 128, sent SMP Pairing Request, received
Pairing Response and sent its public key. Multiple HCI command replies required
retries. It then stopped on a missing delivery-credit event, before encryption
or bond save. Do not infer successful pairing from LE connection alone.
Cancellation drain returned zero pending slots; all four endpoint groups and
the transport closed, with the final success-0 marker captured. The user
confirmed DualSense still navigates and PS works after this trial. Native
off/reconnect while a successful Xbox session runs remains untested here.
This shows reducing the queue alone does not provide reliable command/ACL
completion delivery. Exact competing consumer or loss mechanism is unverified.

The user also confirmed DualSense works after the second library-only probe.

### Resolved native HID imports

The own-library import probe completed on 4.03 (`build/bt-import-probe.log`).
It reads captured PLT/GOT slots and uses dladdr, without calling the targets.
`scripts/identify_bt_imports.py` matches their encoded NIDs against local SDK
identifiers using the SDK's salted SHA1 encoding. All 17 captured imports
matched, including open, write, read, close, kqueue, kevent, _ioctl, mutex and
thread routines. The previously unknown request stub at Init+0x17B0 is _ioctl;
Init+0x1720 is open, Init+0x1740 write and Init+0x17C0 read.

Corrected path resolution: HID Init opens `/dev/bt` read/write and GetDeviceInfo
opens `/dev/bluetooth_hid` read-only. Init's RIP-relative path is
HidPathStrings+0xAB; GetDeviceInfo's is +0x68. The earlier analysis reversed
these two strings. Report worker uses _ioctl request 0x8010622E;
RegisterDevice uses _ioctl request 0x8004622B. Native callback setup writes an
eight-byte registration value and establishes a kqueue watcher/thread. None
of this proves a BLE/GATT connection interface or authorizes assuming the HID
driver accepts Xbox reports. Next investigate the native request contracts;
do not substitute guessed _ioctl payloads for packet routing.
The user reported DualSense stopped responding after the import probe, then
returned from Rest Mode. Recovery of navigation/PS after this wake remains
unconfirmed. This probe ran after the minimal-queue Xbox trial without an
intervening suspend/reboot. Although DualSense worked immediately after that
trial, delayed transport disruption is a confounder. Neither library loading
nor import inspection is established as the cause. The earlier claim that
USB queue competition alone explains all observed failures is too strong.
Keep live probes stopped; any future comparison needs a fresh recovered
baseline, a no-probe idle control, and separated load-only versus inspection
conditions. No shared radio reads should accompany that comparison.

### Clean-baseline load versus inspection comparison

The user clarified that the intervening outage was house power loss, not a
console crash. After reloading UMTX/receiver, a two-minute no-probe idle control
passed DualSense navigation and PS. A separate `make bt-load-probe` executable
loads the HID library with dlopen and holds it for 120 seconds, without symbol
inspection, mdbg reads, radio access or Bluetooth initialization calls. The user
confirmed input checks during and after the hold both worked; the exit marker
was captured in `build/bt-load-probe.log`.

The full library/import inspection probe was then repeated with no intervening
Xbox/radio trial. Its completion marker was captured in
`build/bt-inspection-control.log`; the user confirmed both immediate and
two-minute delayed navigation/PS checks worked. Neither library loading nor
inspection reproduced the failure in this clean-baseline comparison. A delayed
effect of the preceding shared-radio trial remains plausible for the earlier
failure; it is not proven. Keep competing USB reads stopped while investigating
native routing contracts. These checks do not validate native reconnection or
Xbox coexistence.

### Offline native request handler search

`scripts/find_kernel_requests.py` searches executable ELF segments for five
captured request constants and exports bounded disassembly windows. On the
provided kernel image, only 0x8010A40B matched literally. Manual disassembly
confirms its comparison in a request handler at 0xffffffff809B0E50, forwarding
the caller's integer ID and output pointer to 0xffffffff80DFC670. That routine
looks up four stored IDs, prepares a 16-byte record, and passes that record and
the output pointer to a copy routine. This supports a device-information query,
not a pairing operation. Exact field meanings remain unverified.

Artifacts are under `build/native-requests`. These are addresses in the supplied
kernel ELF, not verified firmware-4.03 addresses. No code was executed on the
PS5. Missing literal HID constants do not prove missing support: arithmetic or
table dispatch may encode them differently. Do not send guessed native ioctl
structures based on this search.

### Offline GATT label and suspend/resume references

The string inventory found 33 Bluetooth-related strings. RIP-relative LEA
candidate references were exported and manually reviewed. In driver setup,
`bt_gatt` is passed as an internal synchronization-object name alongside a
pointer into the driver context, not as a device pathname. Its presence does
not establish a user-accessible GATT interface.

Setup also registers named callbacks for process resume and system suspend/
resume. The `bt_system_resume_phase1` callback points through trampoline
0xffffffff804CD1B0 to implementation 0xffffffff80840AE0 in this image. It clears
bit 0x00800000 in a driver-context word and tail-calls 0xffffffff80A66CE0. The
meaning of that flag and callee remains unknown. This is evidence of a driver
resume path, not evidence that clearing the bit would restore 4.03 input.
Artifacts: `bluetooth-strings.json`, `string-references.json`, xref disassemblies
and `resume-phase1.disasm` under `build/native-requests`. No live kernel reads,
writes, callback invocation or PS5 payload deployment occurred in this step.

### Resume worker and native command queue

The next resume routine (0xffffffff80A66CE0 in this ELF) enters the driver's
`SceBttsSync` synchronization path, checks/clears flag 0x4, and conditionally
schedules callback 0xffffffff804CE4A8 -> 0xffffffff80A66AF0. The worker tests
multiple context flags/state values and sends vendor opcodes including FC5B
and FC6F via 0xffffffff80ADF340. This makes a one-bit resume repair unsupported.

The command helper is variadic and formats parameters before enqueueing a
length-prefixed command in context storage. It uses queue metadata at context
offsets 0x250/0x258/0x25C and backing pointer 0x260, writes an opcode/parameter
length header plus formatted payload, and calls 0xffffffff80ADF1F0 after the
queue update. It is an internal driver helper, not an identified public API.
No addresses/offsets here have been validated for firmware 4.03. Captures:
`resume-next.disasm`, `driver-locking.disasm`, `resume-worker.disasm`, and
`native-command-helper.disasm` under `build/native-requests`.

Next trace the queue consumer and any user-accessible request that feeds this
path. Native commands alone are insufficient: Xbox event/ACL routing must also
preserve native traffic and command credits. No live call or kernel write is
justified by the offline helper's existence.

### Native command queue consumer

Offline consumer 0xffffffff80ADF1F0 refuses submission while context words at
0x268/0x26C are nonzero, reads a length/tag prefix, sets an outstanding length
and busy flag, and submits through 0xffffffff8083F010 with a completion callback
trampoline 0xffffffff804CD9A0 -> 0xffffffff80AE0250. These state words are not
yet established as HCI command-credit counts; do not label them as such.

The transport submitter accepts either the primary context or the secondary
context at primary+0x20918 and selects separate transport queues. It queues
buffer, length, operation kind, callback and context, then kicks a serialized
submit path with its own busy flag. This supports two native transport contexts
but not independent ownership by our USB readers, nor a public invocation ABI.
Artifacts: `native-command-consumer.disasm` and
`native-transport-submit.disasm` under `build/native-requests`.
The native command-completion callback and native event routing remain to be
traced. No new console test was run.

### Receive routing and controller completion events

Offline common receive callback 0xffffffff80840030 copies a completed USB frame,
enters `SceBttsSync`, dispatches slot 1 to 0xffffffff80ADF550 (tail branch to
event parser 0xffffffff80BDBDB0), and slot 3 to 0xffffffff80ADFF30 (ACL path),
then rearms the USB transfer. Context comes from the native transport softc.
The parser validates event length and dispatches through a 255-entry table at
0xffffffff81AB701C. Manually reconstructed entries include Command Complete
0E -> 0xffffffff80BDCC42, Command Status 0F -> 0xffffffff80BDC1D1,
Number Of Completed Packets 13 -> 0xffffffff80BDCD32, and LE Meta 3E ->
0xffffffff80BDC7A9. Completed-packet handling iterates handle/count records and
calls 0xffffffff80ADF5B0, which updates context and per-handle outstanding
accounting. Command status/complete paths use 0xffffffff80ADF1D0 when the
reported command allowance is positive. This supports the importance of
preserving native completion events; it does not prove exactly which event
caused the live DualSense failure.

USB command-completion callback 0xffffffff80AE0250 advances the command ring by
its outstanding byte length; this is separate from controller HCI completion.
Artifacts: `native-command-completion.disasm`, `native-usb-receive.disasm`,
`native-event-dispatch.disasm` and the exported event jump table. Addresses and
layouts are still from the supplied unverified-firmware ELF. No live hooks,
kernel reads or callback calls were made.

### Firmware-4.03 live native inventory

`make bt-inventory` builds a directory and candidate-export survey. It opens
no device nodes, starts no USB transfers, performs no kernel device-structure
reads and calls no Bluetooth exports. It loads only the HID library whose
load-only control passed. Live result (`build/bt-native-inventory.log`):
`/dev/bt`, `/dev/bluetooth_hid`, and `/system/common/lib/libSceBluetoothHid.sprx`
were listed. No other matching library names were listed in the surveyed
common/private library directories.

Candidate report, callback, device registration and unregister exports resolve.
Candidates named sceBluetoothHidConnect, Disconnect, StartScan, Pair,
GetDeviceList and GetConnectionStatus do not resolve. This is not exhaustive
symbol enumeration and does not establish that native pairing is impossible.
The probe completion marker was captured; the user confirmed DualSense still
works afterward. No guessed ioctl or native initialization was called.

### Full cached HID export table on 4.03

`make bt-exports` reads the payload SDK's own cached SPRX metadata, using its
local rtld_sprx.c layout, bounded mdbg copies, module-name/size checks and an
exact known-anchor lookup before enumeration. It invokes no Bluetooth export
and opens no device. Live capture `build/bt-native-exports.log` validated 44
symbol records and completed with all 17 defined nonlocal symbols logged.
This reader depends on the local SDK loader layout, not a public dlfcn ABI.

Offline NID matching (`scripts/identify_bt_exports.py`) identifies 13 exports,
including `sceBluetoothHidDisconnectDevice` (previous candidate Disconnect
was absent), initialization/parameter setup, device/callback registration and
unregistration, input/feature reads, output/feature writes and device/descriptor
queries. Four remain unidentified: L5jHxZ-J630 at offset 0x12D0 (6 bytes),
eGVPAJ+M5A8 at 0xBF0 (333 bytes), ud-L5OvD5bA at 0x1130 (21 bytes), and
o+5KjZ3r4O4 at 0x2B0 (27 bytes). Unknown names prevent claiming that the library
has no pairing entry point. No candidate function was invoked. Native input
verification after enumeration is pending.

### Four unknown HID exports captured on 4.03

The user confirmed native input after full export enumeration. A separate
`make bt-unknown-exports` build copied only the four unidentified functions,
using their cached sizes, type checks and module-range bounds. It completed
without invoking them. Contiguous captures/disassembly are in
`build/bt-unknown-abi`; live log `build/bt-unknown-exports.log`.

Unknown0 returns constant 30. Unknown3 initializes a structure with an integer
at offset 0, pointer at offset 8 and zero at offset 16. Unknown2 forwards four
arguments to the existing report helper with operation kind 4. Unknown1 packs
its first argument and output pointer into a 16-byte buffer and calls ioctl
0xC0106206 on the HID descriptor. It checks initialization and maps driver
errors. These observations do not identify its device/handle semantics, and
do not establish a pairing operation. Native input verification after this
capture passed: the user confirmed navigation/PS both work.

Offline request search found literal 0xC0106206 in dispatcher
0xffffffff80A67220 (supplied ELF). Its branch forwards two 64-bit fields to
0xffffffff80A64080 with mode arguments 1 and 0. That routine locks the driver,
looks up an existing record, clears/formats a 121-byte staging buffer, then
copies it to the supplied output pointer. This supports a stored-device
information query, not an observed scan/pair command. Field names and its
4.03 output layout remain unverified. No request was sent to the console.
Artifacts: `native-hid-ioctl.disasm` and `unknown-hid-request-handler.disasm`.
The dispatcher contains additional arithmetic-indexed request tables; these
can be mapped offline to find possible connection/scanning branches. The
library's 17 exports do not enumerate all possible native ioctl operations.

### Offline ioctl dispatcher map and record/control distinction

`scripts/map_bt_ioctl.py` now maps 33 non-default entries from five relative
jump tables, plus six manually verified direct-comparison branches. It rejects
images whose SHA256 differs from the inspected ELF, because all locations are
image-specific. Output: `build/native-requests/ioctl-map.json`, accompanying
dispatcher disassembly and text listing. Branch snippets are not a complete
control-flow graph or verified request contracts. The supplied ELF remains
unchanged and is not a verified firmware 4.03 kernel.

Observed paths in this offline image:

| Request | Handler | Observation |
| --- | --- | --- |
| 0x80186201 | 0xffffffff80A63830 | Walks stored records and optionally copies 256-byte formatted entries; includes Sony model checks. |
| 0x80086202 | 0xffffffff80A63B70 | Looks up a record, then calls 0xffffffff804F2400 when appropriate. That helper clears record fields, republishes state, and conditionally sends LE Create Connection Cancel (0x200E). It is not an established connect operation. |
| 0x80086203 | 0xffffffff80A63BD0 | Checks driver/record state and Sony model fields; returns small status values. |
| 0x80086234 | 0xffffffff80A63CF0 | Sets a record flag and increments its change counter, then invokes the record notification/publishing path via 0xffffffff804F2280 and 0xffffffff804F0F70. |
| 0x20046204 | 0xffffffff80A63D90 | Sets driver flag 0x02000000, optionally issues vendor command 0xFC1E, and cancels a pending LE creation in state 0x27. Otherwise sets another state flag. |
| 0x20046205 | 0xffffffff80A63F40 | Clears that driver flag, optionally sends 0xFC1E, and dispatches state-dependent cancellation handling. |
| 0xC0106206 / 0xC0106237 | 0xffffffff80A64080 | Stored-record information helper with differing mode arguments. |

The publishing path traverses records and calls a notification routine outside
the driver lock. This matters: reaching that path is not evidence that a
request initiates a radio connection. The labels and field meanings above are
observations, not official API names.

The nearby function 0xffffffff80A63D50 emits Inquiry (0x0401), with LAP 0x9E8B33,
length 0x30 and zero response limit, through the already traced native command
queue. It is not a direct target of the mapped tables. Its callers and the
native state machine still need tracing. This classic inquiry is not evidence
of the BLE scanning/connection path needed by the Xbox Series controller.

Additional artifacts: `native-request-handlers.disasm`,
`connection-trampolines.disasm`, `native-record-create.disasm`,
`native-scheduler.disasm`, `native-record-publish.disasm`, and
`native-state-worker.disasm`. No native ioctl, kernel address or Bluetooth
command from this map was sent to the console. Before a console trial, verify
the relevant operation and argument layout on 4.03 and identify how it returns
BLE events/input without taking events away from the native driver.

### Native client request ABI captured on firmware 4.03

New `make bt-client-probe` reads process metadata and bounded executable
mappings through the SDK debugger copy API. It performs no USB/device I/O,
driver callback invocation, injection or discovery. Early passes corrected an
exact process-name mismatch (`SceSysCore.elf`), then handled VM records with
empty paths by using verified executable mappings. The observed VM stride is
144 bytes, with usable start/end/protection fields and no path strings.

SceSysCore: 23 executable mappings, 4,571,136 bytes scanned, one query constant
(0xC0106206 in HID code). ShellCore/ShellUI search: 16 MiB per process cap,
32 MiB total, nine candidate windows in SceShellUI's main executable. This is
a bounded constant search, not proof that other operations are absent.
All captures completed. The user confirmed native navigation and PS after
the SysCore and shell code searches.

4.03 SceShellUI wrappers observed in that session:

| Request | Packed third argument / wrapper |
| --- | --- |
| 0x80186201 | 24 bytes: 64-bit filter, 64-bit output pointer, 32-bit capacity (padding last). |
| 0x80086202 / 0x80086203 / 0x8008620A / 0x8008620B | Pointer to one 64-bit value. |
| 0x20046204 / 0x20046205 | Zero third argument; common descriptor load followed by request and syscall wrapper tail call. |
| 0xC0106206 | 16 bytes: identifier and output pointer. |
| 0x80106208 | 64-bit identifier plus 32-bit value in a 16-byte buffer. |

The native setup function opens **/dev/bt** with flag 2 (O_RDWR), stores the
descriptor, writes an 8-byte value 0xFE, and sets up a queue/event structure.
The role of that write and event ABI remain unverified. No registration write
was reproduced. The shell's descriptor slot held -1 outside its active control
session; finding wrappers alone does not make their functionality active.
Code/reference/path captures are in `build/bt-shell-abi`,
`build/bt-shell-references`, `build/bt-native-shell-references.log`, and
`build/bt-native-control-path.log`. Session addresses are not stable addresses
to embed in a payload. The current client probe traces only SceShellUI.

`make bt-native-query` uses the directly captured 24-byte device-list query on
a separately opened /dev/bt descriptor. The first console test succeeded:
result 2, errno 0, 512 changed output bytes, then clean close/completion. No
event reads, registration, scanning or pairing were performed. This confirms
a usable native query and 256-byte records on 4.03; full record semantics are
still unverified and records are not dumped because they could contain bond
information. Log: `build/bt-native-query.log`.

`make bt-native-discovery` is a separate experimental target. It uses the
captured zero-argument 0x20046204 request, queries the native device cache for
30 seconds only if that request succeeds, then sends 0x20046205 and closes.
It never opens USB, consumes HCI events, writes a listener registration or
requests pairing. Whether this operation discovers Xbox BLE and preserves
native reconnect requires a console test; no native backend is integrated in
the release manager yet.

The first native discovery/control trial completed on 4.03 with Xbox in rapid
pairing mode: /dev/bt opened, 0x20046204 returned 0/errno 0, all six device-list
polls returned the same two records, 0x20046205 returned 0/errno 0, descriptor
closed and final completion logged. No Xbox record was observed. This verifies
request acceptance and orderly completion, not active BLE scanning: the cache
may expose only registered records, and discovery may require the native event
registration/setup sequence. Do not infer that Xbox advertising was absent.
Post-trial native off/on reconnect verification passed: the user confirmed
DualSense reconnects and works after turning it off through Accessories and
pressing PS wirelessly. This verifies this bounded native-control trial only,
not coexistence with an Xbox BLE session. Capture:
`build/bt-native-discovery.log`. The original kernel ELF SHA256 remains intact.

### Native notification listener capture and isolated trial

The client probe now captures a bounded 4096-byte executable window around
the dynamically matched native descriptor writer. No session address is
embedded. `build/bt-native-event-code.log` completed and reconstructed all
4096 bytes into `build/bt-native-event-code/NativeClient11-49f7e970.disasm`.
The user confirmed DualSense navigation and PS still worked afterward.

The expanded 4.03 shell code shows listener setup writing the 64-bit value
0xFE, then a 32-byte kevent with EVFILT_READ (-1), EV_ADD (1), and zero
remaining fields. Its event consumer polls kevent with a zero timespec and
reads 16 bytes from the device when ready. Cleanup removes the read filter
with EV_DELETE (2), then closes the queue and device. Notification field
meanings and registration ownership semantics remain unverified.

`make bt-native-events` builds a separate 120-second listener trial following
that setup/read/cleanup contract. It adds O_NONBLOCK to prevent a raced
readiness indication from blocking the payload, accepts at most 256 complete
notifications, and supports the existing UDP stop request. It records only
notification sizes/counts, not payloads. No discovery, pairing, connection,
USB transport access or production manager change occurs in this target.

The first live setup on 4.03 succeeded: device opened, the registration write
returned 8 bytes, and kevent registration returned 0. It received 73 complete
16-byte notifications before a PC stop request. Queue removal, queue close,
and device close all returned 0, followed by the completion marker. The stop
script's generic cleanup matcher did not recognize this diagnostic's marker,
but the captured individual closes and final marker confirm completion. The
source now also emits the shared `Cleanup complete; payload exiting` marker.
Capture: `build/bt-native-events.log`.

While this listener was active, the user turned DualSense off through
Accessories and confirmed wireless reconnect, navigation and PS worked.
This verifies native notification reception with that tested reconnect;
it does not establish Xbox Bluetooth coexistence or pairing support.

`make bt-native-event-discovery` combines the captured listener setup with
the previously tested 0x20046204/05 control pair for a bounded 30-second
trial, periodically querying native records. It stops the control operation
before removing the filter and closing descriptors. Both targets compile
with -Wall -Wextra -Werror.

The first combined discovery trial ran with Xbox in rapid pairing mode.
Registration returned 8 bytes, kevent setup and discovery control start
returned 0, and 55 complete 16-byte notifications were received. Baseline,
periodic, and final queries all returned the same two identifiers
0x0000D0BCC1090F58 and 0x0000D0BCC196006D. Control stop, filter removal,
queue close and device close returned 0; the final cleanup marker was captured.
Log: `build/bt-native-event-discovery.log`. The notifications have not been
decoded and cannot be attributed to Xbox. This does not establish BLE
discovery or pairing; the cache may not expose discovered devices. Post-trial
DualSense navigation, PS, and wireless off/on reconnect verification passed:
the user confirmed DualSense reconnects and works. This result covers the
bounded native listener/control trial, not coexistence with an Xbox session.

### Notification field tracing

Offline inspection of the preserved kernel image identified a per-open
listener object and its 16-entry, 16-byte notification ring. The read path
at image address 0xffffffff80A67030 copies byte fields at offsets 0 and 1,
zeroes the two padding bytes, copies a 32-bit field at offset 4 and a 64-bit
field at offset 8. The write path at 0xffffffff80A67180 requires exactly
8 bytes and stores the value in the listener's mask slot. Notification
publication at 0xffffffff80A62900 selects listeners by that mask and copies
its four arguments into the same field positions. These are observations
from the supplied image, not verified 4.03 kernel locations or API names.
They suggest an event mask and event/subtype/status/identifier layout;
the live meanings remain to be checked.

Offline artifacts are in `build/native-events-offline`, including
`device-read-write.disasm`, `notification-publish.disasm`, and
`event-dispatch.disasm`. The kernel ELF is only read by these tools.

`make bt-native-event-trace` builds the same bounded discovery/listener trial
with logging of those candidate fields. It uses unlabeled field names to
avoid assuming their semantics. It reads only the already tested 16-byte
notifications and does not dump full device records or bond material.
Build passed with -Wall -Wextra -Werror.

The first live trace received 58 notifications and exited cleanly. Eleven
type-byte 0x01 notifications contained the known Xbox address in field8:
0x0001F46AD7BCB280, with field4 0x00000508 and byte1 values 0xB8-0xC2.
The other notifications referred to 0x00002028BC685023. The two queried
records remained unchanged. This demonstrates that the native notification
path sees the advertising Xbox even when the device-list query does not list
it. It does not demonstrate a connection or pairing. Byte1 is consistent
with signed RSSI, field4 with device class, and bit48 with a transport flag;
these meanings remain inferred pending the corresponding producer trace.
Control stop, filter removal and both closes returned 0. The user confirmed
DualSense navigation/PS and wireless off/on reconnect after the trial.
Capture: `build/bt-native-event-trace.log`.

`make bt-native-connect-trial` prepares a separate bounded diagnostic. It
uses the previously captured 4.03 0x8008620A wrapper's pointer-to-u64 contract
only after a fresh type-1 notification matches the exact Xbox address and
transport bit. It preserves the full native identifier, sends at most one
request, monitors native notifications for up to 120 seconds, and logs
acceptance separately from pairing success. Cleanup uses the captured
0x80086202 peer request only if the connection request returned 0, then
stops discovery and closes its listener. The offline 0x0A handler allocates
native connection context and branches on transport flags; 0x02 includes
pending connection cancellation/record cleanup. Full 4.03 operation semantics
are still being tested. No USB reads, input injection, credential/key import
or production-manager integration occurs. This diagnostic embeds the test
Xbox's address and is not a general controller manager. Build passed.

The first live connection trial matched a fresh Xbox notification and sent
0x8008620A with native identifier 0x0001F46AD7BCB280. It returned 0x802F0204,
errno 0, so acceptance was false and no peer cleanup request was issued.
After 24 notifications a PC stop request ended the trial. Discovery stop,
filter removal and both closes returned 0; cleanup/exit was confirmed.
The user confirmed DualSense navigation/PS and wireless off/on reconnect
afterward. Log: `build/bt-native-connect-trial.log`.

In the offline handler, 0x802F0204 is returned if the driver state is not
0x27, before lookup of the requested peer. This suggests a native state
precondition failure, not an Xbox compatibility result. The next prepared
trial stops discovery after the matching advertisement, allows two seconds
for native completion, then sends one connection request. The delay is an
experiment and does not establish the driver's state or readiness; no kernel
state is modified. A rejected request now terminates the trial automatically.
Revised build passed.

The stop-before-connect trial accepted 0x8008620A (result 0, errno 0)
two seconds after stopping discovery. Native events included 7/0 and 2/0
without a peer, followed by event 4/subtype 4/field4 0x0000F0F4 for the exact
Xbox identifier. About 30 seconds later, event 6/subtype 0x13 arrived for
Xbox. No confirmation response was sent; no bond or inputs were confirmed.
PC stop was acknowledged. The peer cleanup request returned 0x802F0102;
filter removal and both closes returned 0, followed by the final cleanup
marker. The stop script timed out before seeing that marker, but the viewer
captured it. Post-trial DualSense navigation/PS and wireless off/on
reconnection passed, as confirmed by the user. This does not yet establish
coexistence with a paired Xbox input session.
Capture: `build/bt-native-connect-after-discovery.log`.

Offline notification producer candidates were exported to
`build/native-event-producers`. The producer at 0xffffffff8086BA91 emits
event 4 with the connection's byte field and a number reduced modulo
1,000,000, then sets the pending reply bit. The LE reply helper at
0xffffffff80867E00, reached by 0x80106208, accepts a nonzero value with a
null PIN pointer in the corresponding pending branch. This supports the
interpretation of the observed 4/4 event as a pairing confirmation prompt;
its precise 4.03 semantics remain to be tested. No offline address is called.

`make bt-native-pair-trial` adds one 0x80106208 response using the captured
4.03 shell's 16-byte peer/u32-value contract. It responds with value 1 only
to event 4/subtype 4 for the exact peer whose connection request succeeded.
Reply acceptance is logged separately from saved-bond/input success. The
remaining bounds and cleanup follow the connection trial. Build passed with
-Wall -Wextra -Werror.

The first native pairing-response trial completed without a saved Xbox
record or input delivery. After refreshing Xbox pairing mode, discovery
matched it, stop-before-connect returned 0, and connection returned 0.
Event 4/subtype 4 arrived for the exact Xbox identifier; 0x80106208 with
accept=1 returned 0. The next event was 5/subtype 0xFF/status 0x802F3403,
followed five seconds later by event 6/subtype 0x13. Device-list queries
continued to return only the two prior records. PC stop was acknowledged;
peer cleanup returned 0x802F0102, filter removal and both closes returned 0,
and the final cleanup marker was captured. Total: 84 notifications.
The user confirmed post-trial DualSense navigation/PS and wireless off/on
reconnection work.
Capture: `build/bt-native-pair-trial.log`.

`scripts/find_kernel_requests.py --request` now supports bounded disassembly
of selected unsigned 32-bit constants. Searching the preserved offline ELF
for 0x802F3403 produced four byte candidates in `build/native-pair-error`.
One is a coincidental request-constant match in ioctl dispatch, not this
error. The other three include device-type/vendor/model checks. In the BLE
security completion path near 0xffffffff80867B74, unsupported record type or
vendor/model reaches notification of 0x802F3403 and disconnect. Other uses
also guard supported-device handling. This suggests device policy rather
than an unanswered confirmation, but the corresponding 4.03 branch has not
been captured and its exact cause must not be asserted as verified. No
kernel modification or native bond-record fabrication is implemented.
Next investigation: the native generic-device registration/metadata path.

Offline follow-up exported the registration handler to
`build/native-registration/register-device.disasm`. In this supplied image,
0x8004622B reaches 0xffffffff80a67950, which rejects vendor/model pairs other
than 0x044F/0x1000 and 0x06F8/0xE051-0xE052 with 0x802F3701 before allocating
its client registration. These match the vendor/model checks in the examined
BLE security-completion path. Registration is therefore not an unrestricted
generic-device enrollment API in this image. This does not prove that the
same restriction exists on firmware 4.03; no registration request was sent
to the console.

The previously captured 4.03 shell wrapper for 0x80186223 supplies selector
14, an output-buffer pointer, and size 512. The offline dispatch routes this
request to a selector-based query/copy function, not the client registration
handler. Its setter counterpart 0x80186224 remains unverified and must not be
used as an assumed device-policy override. Next resolve the corresponding
4.03 registration/policy behavior before another pairing experiment. No
additional console payload was loaded for that offline follow-up.

`make bt-registration-probe` builds a separate 4.03-only diagnostic. It sends
one captured 0x8004622B request with a four-byte pair 0x045E/0x0B12, the user's
observed Series USB identity, not a verified BLE PnP identity. There is no
scan, connection, callback, event subscription or radio read. If accepted,
the captured 0x8004622C unregister request is sent immediately; the descriptor
is closed in every path. A registration rejection cannot identify the exact
branch that produced the prior pairing error.

The initial probe mistakenly opened `/dev/bluetooth_hid` and returned
0x81548005. Offline disassembly of that node's handler returns this for
unrecognized requests. Reviewing the 4.03 library's RIP-relative string
references corrected the reversed path interpretation: Init opens `/dev/bt`
at HidPathStrings+0xAB; GetDeviceInfo opens `/dev/bluetooth_hid` at +0x68.
The revised live probe on `/dev/bt` returned 0x802F3701, errno 0, for
0x045E/0x0B12. This matches the offline registration handler's unsupported
vendor/model result and establishes that this Microsoft-ID registration
request is rejected on 4.03 too. It does not establish every allowed ID or
prove the pairing error originates from the same check. Both probes closed
their descriptor with result 0 and logged cleanup/exit. Build passed with
-Wall -Wextra -Werror. Capture: `build/bt-registration-probe.log`.
The user confirmed post-probe DualSense navigation/PS and wireless off/on
reconnection work.

No adapter reset, native pairing replacement, guessed native call or fabricated
successful reconnect should stand in for this transport work. Validate native
reconnect first, then Xbox reconnect, concurrent USB input, and clean shutdown.
