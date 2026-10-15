# Archived implementation notes

Historical README retained during the 2026-10-15 restructure. For current setup
and support status, use the [project README](../../../README.md). Statements below
reflect the earlier investigation and may have been superseded.

# PS5 XInput: USB payload and Bluetooth Controller Manager

For the all-in-one solution, load **`dist/ps5-xinput-manager.elf`**. It opens
the console web interface and starts one combined background service for
up to four Bluetooth connections and up to four supported USB controllers.
The user has confirmed three Xbox wireless controllers working together.
The manager now also includes an experimental Switch / 8BitDo classic Bluetooth
path for the Ultimate 2C Bluetooth for Switch. The user confirmed its sticks,
face buttons and Home control the console after live debugging. Mixed operation
with three Xbox controllers still needs its own physical check.
USB controllers can be plugged in later or unplugged/reconnected; the service
keeps running when the page closes. Stop older standalone controller payloads
before starting the combined manager. Build/package it with `make manager`.

The new **Controller Manager** provides a console interface for Bluetooth
search, pairing, saved-controller connection, disconnection, forgetting and
automatic reconnect. Import `dist/ps5-xinput-manager.elf` into Payload Manager;
it starts the embedded service through the ELF loader on port 9021 and opens
the console browser. The controller service continues when the interface
closes. It manages up to four supported Bluetooth controllers (Xbox LE or experimental Switch Pro protocol) and stores up to
eight bonds. See [Controller Manager instructions](../../../docs/controller-manager.md).
The manager bundle is `dist/ps5-xinput-manager.zip`; compilation and host
integration tests are separate from live console validation.

Target setup: PS5 firmware **4.03**, UMTX jailbreak, Xbox Series X Starfield
controller connected directly to the console with a **USB data cable**.

The latest build supports **up to four supported USB controllers** in
one payload. Connect them before launching the standalone USB payload. Each USB device gets its own
virtual pad, Guide state and input/recovery counters. Logs label them `[C1]`
through `[C4]`; these labels follow USB discovery order, not player numbers.
Select a different PS5 user for each controller to test local multiplayer.
Previous live firmware 4.03 testing detected two USB devices and accepted input for
both virtual pads. The user confirmed independent two-player controls and
the Xbox button on both controllers work. Host integration tests cover
independent routing and cleanup, including four-controller USB service hotplug.
Four simultaneous USB controllers still require live console validation. Xbox One/Series GIP and the wired-format
Xbox 360 XInput protocol are supported, including USB receivers that expose
that format. Xbox 360 wireless-receiver framing remains unsupported.

Official Nintendo Switch Pro USB support (`057E:2009`) is now implemented in
the same payload, with factory calibration, physical-position face buttons
and Home mapping. Initial console testing revealed raw stick instability;
validation is still in progress. See [Switch Pro USB details](../switch-pro-usb.md).

The 8BitDo Ultimate C Bluetooth USB path now completes the Nintendo handshake
and delivers input to the PS5 virtual pad in live testing. It uses the same
physical-position button mapping and Home mapping as the Pro Controller.
Start the payload, then connect the controller by USB while it waits for up
to 60 seconds. After an unsuccessful earlier USB test, unplug the controller
for five seconds before reconnecting.

A separate experimental native Bluetooth build, `dist/ps5-xinput-bt.elf`,
now forwards the tested Xbox Series controller's wireless input to a PS5
virtual pad. The user confirmed its inputs work. It uses a saved Bluetooth
bond and logs on UDP **9024**. It currently supports one explicitly selected
Xbox and its verified report layout; it is not part of the quiet USB release.
See [Bluetooth build and testing instructions](../bluetooth-403.md).

An 8BitDo Ultimate 2.4 GHz receiver in XInput mode was identified as
`2DC8:3106`, interface `FF/5D/01`, with IN `81` and OUT `02`. The decoder for
its standard 20-byte reports has been added, and the user confirmed PS5 input
works through the receiver. The quiet release was also confirmed for sticks,
triggers, Home and responsiveness after about 30 seconds idle.
A diagnostic sample recorded 2,634/2,634 accepted
reports. The receiver stays registered when silent while its USB descriptor
remains available; it is not repeatedly reset or removed for idle radio input.
Use the receiver's XInput mode. DInput/Switch modes require different drivers.

`dist/ps5-xinput.elf` is the quiet release build. It logs startup, the first
accepted input for each controller, errors/recovery and cleanup. Routine
buttons, sticks, Guide events, periodic statistics and ABI dumps are disabled.
The release also omits the diagnostic kernel-log assignment observation and
remote shell code. Four-controller capacity and PS-button mapping are retained.
`dist/ps5-xinput-debug.elf` is the separate verbose diagnostic build; remote
shell binding remains disabled there as well.
Console testing on **firmware 4.03** confirmed USB input, virtual-device
registration, user selection and Xbox navigation on the home screen after
correcting the native ioctl ABI. One live sample recorded **98 read, 98
delivered**. The user also confirmed gameplay works; the game title and PS4/PS5
version have not yet been recorded. The USB driver reads Xbox
GIP reports and submits translated input through a virtual PlayStation
controller. The original upstream targets 11.60; the current submission layout
was checked against the console's own 4.03 `libScePad` code. A successful ioctl
return alone does not prove that the shell or games receive input.

## First console test

1. Sign into your normal console user. Keep a DualSense available.
2. Run your UMTX setup and start a compatible **TCP ELF loader**. UMTX is the
   exploit; an SDK-compatible ELF loader must also be running. This build uses
   the PS5 Payload SDK runtime and is not a raw `.bin` payload.
3. Connect up to four supported controllers/receivers by USB before launch. Do not start another controller
   bridge payload alongside this one.
4. From this folder, send the payload to the loader (replace the example IP):

   ```sh
   python3 scripts/send_payload.py 192.168.1.50 --port 9021
   ```

   On Windows, use `python` instead of `python3`. The computer is only used to
   load the ELF; it is not in the controller input path afterward.
5. Use each Xbox controller's D-pad and A to select its user on the console's
   assignment screen. For multiplayer, assign separate users. Keep the DualSense
   available if selection fails. `InsertData`
   can return `EBUSY` while assignment is pending. The payload retries this
   condition at up to 10 Hz for 120 seconds rather than counting it as a fatal
   report failure. This is a diagnostic retry, not a verified firmware 4.03
   assignment fix. Look for `Virtual input accepted` in the logs.
   Check navigation with the D-pad and A/B, then test both sticks, triggers,
   shoulders and stick clicks in a game. Test PS4 and native PS5 games
   separately; support for either is unverified on this firmware.
6. Hold **View + Menu** on any Xbox controller for two seconds to stop the
   entire payload and remove all virtual pads. A USB disconnect or unrecoverable
   error removes only the affected controller; the others keep running. To add
   or reconnect a removed controller, stop the payload and launch it again with
   all desired controllers connected. New devices are discovered at launch rather than hot-plugged.
   The latest build also accepts a stop request from the logging PC:

   ```sh
   python scripts/stop_payload.py 192.168.1.136
   ```

   An acknowledgment confirms receipt; wait for `Cleanup complete` in the logs
   before sending another ELF. Older running builds may not support this command.

The log is `/data/ps5-xinput/xsense.log`. Retrieve it with your existing console
FTP service if inputs do not work. Useful evidence: UserService result, detected
USB VID/PID, AddDevice error or handle, InsertData errors, and the delivered
report count. A nonzero delivered count means the ioctl accepted reports, not
that games used them. If the loader itself fails, its own logs are also needed.
Live testing was performed by sending the ELF and capturing the console's UDP logs.

## Live runtime logs on your PC

Start the viewer **before** sending or launching the updated payload:

```sh
python scripts/watch_logs.py --ps5 192.168.1.50
```

Replace the example with your console's IP. Use `python3` on Linux. On Windows,
run the viewer in Windows PowerShell; WSL's default NAT network may not receive
LAN broadcast packets. Allow Python on your private network if Windows Firewall
prompts. PC and PS5 must be on the same local broadcast network; Wi-Fi client
isolation or separate VLANs can prevent delivery.

With `--ps5`, the viewer sends a connection request to console UDP port **9023**.
The payload then sends logs directly back to that viewer, replaying up to 32
recent lines. Without `--ps5`, the viewer listens for LAN broadcasts. The payload
keeps
writing `/data/ps5-xinput/xsense.log`. The viewer displays the source IP and saves
an append-only copy as `ps5-xinput-live.log` in its current directory. Use
`--output path/to/session.log` to choose another file. Press Ctrl+C to stop the
viewer; the payload continues running.

UDP is best effort: packets can be lost. Replay is limited to a running payload's
recent lines; older lines and logs from a payload that already exited remain in
the console file. Streaming is
nonblocking so network delivery cannot stall controller input. These are this
payload's application logs, not kernel/panic logs. The release is quiet during
normal input; the diagnostic build logs input changes, Guide events and a
progress line every 60 seconds. Check the console file for
`LAN log stream UDP 9023: unavailable` if no
packets arrive despite correct network/firewall settings.

## Controls and limitations

| Xbox | PlayStation |
| --- | --- |
| A / B / X / Y | Cross / Circle / Square / Triangle |
| LB / RB | L1 / R1 |
| LT / RT | Analog L2 / R2 plus digital trigger flags |
| Stick clicks | L3 / R3 |
| Menu | Options |
| View | Touchpad click |
| Xbox / Guide | PS button: tap for Control Center, hold for home |
| D-pad and sticks | D-pad and sticks |

The latest build handles the separate Guide press/release packets and keeps the
PS button bit held across ordinary input reports. The user confirmed the
Control Center/home test works on firmware 4.03 with the Starfield controller.
Guide packets are decoded using the same two low state bits as the
[Linux Xbox driver](https://github.com/torvalds/linux/blob/master/drivers/input/joystick/xpad.c).
Touch coordinates, gyro, adaptive triggers, rumble,
audio/headset are not implemented. Native PS5 Bluetooth input uses the separate
experimental build described above. A supported USB
receiver handles its own wireless connection; the payload reads its USB reports.
Wired-format Xbox 360 reports are normalized to the existing controls, including
Home/Guide, Back as touchpad click and Start as Options. Generic HID/DInput,
Switch mode and Microsoft's Xbox 360 wireless-receiver protocol are unsupported.
USB tests used the Starfield controller above.

## Build and verify in WSL/Linux

The workspace already contains a local SDK under `.tools/`. For a fresh setup,
install the compiler prerequisites and fetch the pinned SDK:

```sh
sudo apt-get install clang-18 lld-18 llvm-18 make curl unzip
bash scripts/setup_sdk.sh
make -j2
make host-test
sha256sum -c dist/SHA256SUMS
```

LLVM 18 packages may require an LLVM package source on older Ubuntu versions.
The current WSL environment already has the necessary compiler tools; no system
packages were installed by this work. The SDK download stays in the project.
An existing SDK can be selected with `make PS5_PAYLOAD_SDK=/path/to/sdk`.

`make` or `make release` builds the quiet `dist/ps5-xinput.elf`, which the send
script uses by default. `make debug` builds `dist/ps5-xinput-debug.elf` with
verbose diagnostics. The profiles use separate object directories, so switching
between them does not require cleaning. Each has a checksum manifest:
`dist/SHA256SUMS` for release and `dist/SHA256SUMS.debug` for diagnostics.

To load the diagnostic version explicitly:

```sh
python scripts/send_payload.py 192.168.1.136 --port 9021 --elf dist/ps5-xinput-debug.elf
```

The host tests check all 14 mapped buttons, neutral sticks, axis endpoints/Y
inversion, trigger range, release, submission status and event parsing. They
exercise independent two-controller routing and recovery in both build
profiles, and verify routine input logs are absent in release builds. They
also simulate remote-call success and cleanup after denied operations,
unsupported vector-state capture, wrong-thread stops and timeouts. They cannot
check PS5 USB access, firmware-specific virtual-device ABI, user assignment,
game compatibility or latency.

## Firmware 4.03 assignment investigation

Read-only capture of the console's `libScePad` established the native layout:

| Operation/field | Firmware 4.03 value |
| --- | --- |
| AddDevice request | `0xC018482A`, 24-byte header |
| InsertData request | `0x8010482C`, 16-byte header: handle, padding, report pointer |
| Translated report | 168 bytes (`0xA8`); motion block starts at `0x80` |
| DeleteDevice request | `0x80104850`, 16-byte header, command 5 |

The previous submission opcode was `0x8018482C` with a 24-byte request and a
160-byte report. After correcting those values, PS5 accepted the Xbox reports
without any shell binding call. The user confirmed profile selection and Xbox
navigation on the PS5 home screen. Ioctl diagnostics now clear errno before calls
and normalize raw positive error numbers, so errors cannot inherit a stale USB
errno. The original `EBUSY` interpretation was therefore not reliable evidence
of blocked assignment.

`testing_tools/src/diagnostics/pad_abi.c` only reads the payload's own loaded `libScePad` exports. Captures
are logged as hex chunks; `python scripts/extract_pad_abi.py` reassembles them
under `build/pad-abi` for local disassembly. Basic buttons/axes are verified
against these captures; motion behavior remains untested.

Earlier investigation, before that ABI correction: registration opened the
user-assignment screen and Xbox reports arrived, but logs reported `EBUSY`. The initial
ten-error stop has been replaced with a bounded busy retry; waiting alone did
not resolve the block.

The console produced a matching virtual-pad `DEVICE_ADDED` event (subtype 22)
with device ID `0xf030d`. An experimental local call to
`sceMbusBindDeviceWithUserId` logged its entry but never logged a return; the
console remained responsive. This does not distinguish a blocked call from a
payload fault. **That call has been removed from the current build.**

The current build observes `/dev/klog` around device creation and reports the
matched identifier and requested user. It then attempts to invoke the existing
binding implementation inside **SceShellUI**, where MBus is already loaded.
This follows the [community shell-context approach](https://github.com/diegobarbosaa/OmniPad-PS5/blob/main/src/shellui_inject.c).
Testing on 4.03 reached the correct shell symbol and saved thread state. SDK
memory writes succeeded after ptrace returned error 14. Live debugging then
identified a different shell thread hitting the shared INT3 return breakpoint;
that scheme has been removed. The current build tries a thread-local hardware
breakpoint, but the first console test could not arm it, so assignment remains
unconfirmed. A subsequent test armed the hardware breakpoint, entered the
binding call and stopped with SIGSTOP without a confirmed return. The user then
reported a console error and force-closed the payload. **Remote shell binding
is now disabled in the default build. Restart the PS5 and rerun UMTX before
further testing.** No existing physical controller is selected for binding.

The retained experimental code (not enabled in the distributed/default build)
temporarily gives the payload debugger credentials, resolves the
binding export at runtime, attaches to SceShellUI, identifies the stopped thread,
saves its general/FP/vector/debug registers and temporarily arms a hardware
execution breakpoint at its saved RIP in the selected thread only. Existing
enabled debug breakpoints cause the call to be skipped. Debugger stop waits are
bounded (1 second for attach, 3 seconds
for the call, 1 second for timeout recovery). After a return or unexpected stop,
it attempts to restore the return-address slot and registers before detaching
and restoring payload credentials. It does not write executable code or change
mapping protections. Debug-register arming and restoration are checked by
reading them back. A successful return requires the expected thread, saved RIP,
post-return stack pointer and hardware breakpoint status bit, rather than just
SIGTRAP or a zero return register. Unsupported full vector-state capture skips
the call. No permanent shell hook is installed.

Memory-read diagnostics report the exact ptrace operation, requested/reported
length and errno. An instruction-space read failure is retried as a data-space
read, then through SDK `mdbg_copyout`. Stack reads also have the SDK read fallback.
Failed writes fall back to SDK `mdbg_copyin`; every return-address stack write
must match a read-back before execution. The raw ptrace error is normalized to
errno, so error 14 now reports `Bad address`. The attempt stops without writes
if no stack read path succeeds or hardware breakpoint arming cannot be verified.

**Material limitation:** debugger waits are bounded, but an interrupted library
call can leave service-side effects that restoring CPU state cannot undo. The
shell can pause or become unresponsive during this experiment. If logs report
`restoration/detach failed`, restart the console before continuing. Host cleanup
simulations cannot prove PS5 debugger behavior or service compatibility.

The default build reports `Shell bind: disabled after FW 4.03 console error`.
It submits reports using the corrected native ABI without executing remote
shell calls. The console accepts submissions and the user confirmed UI
navigation; game behavior requires separate testing. The
host tests compile the retained experimental branch to check cleanup behavior;
passing simulations do not establish that remote calls are safe on the console.

If a previous build stops logging at `attempting local MBus bind`, it is still
before the USB input loop and may not respond to View+Menu or unplug timeouts.
Restart the PS5 and rerun UMTX to clear that instance before loading any new
build. The 120-second timeout applies to busy input submissions in the controller
loop, not to the removed binding call.

The API and event format were investigated using the community
[OmniPad virtual-pad implementation](https://github.com/diegobarbosaa/OmniPad-PS5/blob/main/src/ps5_vpad.c),
which targets later firmware. The parser has host checks for valid virtual-pad
events, wrong subtypes, missing identifiers and malformed/overflowing IDs.

## Source and license

Native Bluetooth discovery, Secure Connections pairing and encrypted GATT
service discovery have been tested separately on FW 4.03. Saved-bond reconnect
passed two consecutive controller power cycles, and the separate Bluetooth
bridge now forwards the verified HID reports to a virtual pad. The user
confirmed its inputs work. Long-term reconnect reliability is still under
investigation. The USB release does not contain Bluetooth pairing or input. See
[Bluetooth investigation and diagnostic builds](../bluetooth-403.md) for
the verified radio capabilities, scan instructions and remaining work.

Based on [FGGstore/FGG-XSense](https://github.com/FGGstore/FGG-XSense), pinned to
commit `bfc5f9d324e2ca8b0a90fc498957699caa9fe5b0`. Its source and GPL-3.0
license are preserved; the original checkout is in `upstream/FGG-XSense`.

Local changes resolve the foreground user (falling back to the initial user),
use a separate state/log directory, log virtual-input failures, and stop after
ten consecutive non-busy rejected submissions. Busy submissions are retried
with an assignment timeout and cleanup diagnostics. They also add host mapping verification,
a pinned SDK setup, a build target and a TCP payload sender.

The [original linked DualSense repository](https://github.com/StonedModder/dualsense-controller)
is a browser visualization and was not imported into this native payload.
The additional [NRF52840-dualsense reference](https://github.com/StonedModder/NRF52840-dualsense)
documents DualSense USB/Bluetooth HID descriptors and includes a browser report
explorer. Its stick direction and trigger ranges agree with this payload's
translation. Those physical-controller HID reports use different layouts from
the 168-byte virtual-pad ioctl input used here; their offsets must not replace
the virtual-pad offsets. The repository does not provide the PS5 shell/device
assignment implementation. A local
reference checkout is in `.tools/NRF52840-dualsense`; no source was copied into
the payload. Its DualSense Bluetooth details also do not implement the Xbox
Bluetooth input driver that this project would need later.
The build uses [PS5 Payload SDK v0.44](https://github.com/ps5-payload-dev/sdk/releases/tag/v0.44).
Distributed source changes retain the GPL-3.0 license in `LICENSE`.
