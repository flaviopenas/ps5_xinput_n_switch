# Firmware 4.03 live debugging, 2026-10-11

## 8BitDo Ultimate 2.4 GHz USB receiver

The user's XInput-mode receiver exposed `2DC8:3106` on `/dev/ugen2.2`, interface
`FF/5D/01`, IN `81`, OUT `02`, max packet 32. The earlier GIP-only filter ignored
it. Support now covers standard wired-format Xbox 360 20-byte reports: buttons
in bytes 2/3, 8-bit triggers in 4/5, signed sticks in 6..13, and Guide in byte
3 bit 2. Controls are normalized through the existing PS5 mapping. It receives
the native `01 03 02` player-LED command, not GIP authentication or GIP ACKs.
The format was checked against the
[Linux xpad driver](https://github.com/torvalds/linux/blob/master/drivers/input/joystick/xpad.c).

A diagnostic retry received 20-byte input and logged 2,634/2,634 accepted
reports; the user confirmed input works on PS5. Logs also showed Guide edges.
The first attempt produced no input before its silence timeout, so the LED
command's necessity has not been isolated. Receiver silence now checks USB
presence instead of repeatedly resetting the radio or removing a healthy pad.
Host tests cover all mapped buttons/Guide, stick endpoints, trigger scaling,
malformed packets, mixed GIP/360 routing, native-only startup and idle/unplug
handling in release and diagnostic profiles. All passed. Both ELF variants
have been rebuilt. The quiet release registered handle `983821` and accepted
input 46 seconds after startup, confirming it survived the former 30-second
silence limit. The user then confirmed sticks, triggers, a short Home-button
tap and responsiveness after about 30 seconds idle all work in the quiet release.

## Two USB controllers

The payload now discovers up to two devices at startup. USB and virtual-pad
objects are separately allocated, and the event loop keeps each controller's
input, Guide state, assignment retry and recovery counters separate. Runtime
USB initialisation advances asynchronously through `gip_read()` so a keepalive
on one controller does not sleep or consume the other's input. A USB recovery
failure or assignment timeout removes only its affected virtual controller.
View+Menu on either controller and the remote stop command still stop the
whole payload. Newly attached devices require restarting the payload.

Host integration tests exercise the actual USB driver, pad driver and scheduler
with simulated USB/HID ioctls: two distinct nodes and handles, discovery without
reopening the first device, isolated sticks/buttons/Guide and acknowledgments,
nonblocking initialisation, failed registration, busy/recovery isolation, remote
stop and cleanup. The payload build and all host tests passed.

Live firmware 4.03 startup detected `045E:0B12` on both `/dev/ugen2.2` and
`/dev/ugen2.3`. Virtual handles were `721677` and `787212`, and both logged
accepted submissions. A live sample logged 1,236/1,236 reports for C1 and
58/58 for C2. The user confirmed both controllers work when asked to test
independent two-player controls and the Xbox button on each. The game title
and PS4/PS5 version were not recorded.

Console: 192.168.1.136, UMTX; wired Xbox Series X Starfield controller
045E:0B12, `/dev/ugen2.2`, interface 0, IN 82, OUT 02.
PC capture: `ps5-xinput-debug.log` (ignored by Git).

Observed on the console:

- UserService returned user `0x1EB70483`. Device-added subtype 22 matched
  each newly registered virtual controller's handle.
- Xbox reports reached the payload. D-pad Down twice followed by A did not
  move the profile picker; submissions returned EBUSY and delivered stayed zero.
- SceShellUI pid 55 resolved `sceMbusBindDeviceWithUserId` to `0x830EDBCF0`.
- Ptrace instruction reads/writes returned raw error 14 (EFAULT). SDK debugger
  reads/writes succeeded and the software breakpoint passed read-back checks.
- At 21:37:42, thread 100661 hit the shared breakpoint at RIP `0x80D98647D`,
  while the borrowed thread was 100588. RAX zero from that other thread was
  correctly rejected as a binding return. The old cleanup did not rewind the
  other thread's RIP after its INT3; a later console error cannot be attributed
  uniquely to this or to interrupted service calls. Shared code breakpoints
  have been removed from the implementation.
- At 21:42:15, a thread-local breakpoint armed successfully: DR0
  `0x80D98647C`, DR7 `0x401`, set/get return zero. The binding call started.
- At 21:42:17, the borrowed thread stopped with SIGSTOP (17), RIP
  `0x80D986A7C`, RSP `0x7EF9F2008`, DR6 `0xFFFF0FF0`. It had not reached the
  hardware return breakpoint. Cleanup reported restoration; this only verifies
  debugger state, not service-side effects of the interrupted call.
- The user subsequently reported a console "something went wrong" error and
  force-closed the payload with Payload Manager.

Current default ELF disables remote shell calls. It remains a USB/virtual-pad
diagnostic build; input acceptance alone does not prove UI/game operation. The experimental code is
retained for investigation and exercised by host simulations; those simulations
do not validate firmware ABI or service behavior. The additional-SIGSTOP retry
change was built but **was not sent to the console** after the error report.

Before another console test, restart the PS5 and rerun UMTX/ELF loader. Do not
resume remote calls solely because CPU registers can be restored. Establish a
call context that avoids borrowing a shell thread with live service state, and
verify the firmware's virtual-pad ABI separately. Keep physical DualSense HID
report formats separate from the virtual input ioctl format.

## Subsequent native ABI correction

Read-only local `libScePad` captures at 21:47:50 exposed a firmware mismatch:
4.03 InsertData uses opcode `0x8010482C`, a 16-byte request, and a 168-byte
translated report. The previous upstream-derived values were `0x8018482C`,
24 bytes and 160 bytes. The 4.03 motion block is at `0x80`, rather than `0x78`.
AddDevice and DeleteDevice request formats matched the existing implementation.

The corrected build clears errno before hid ioctls and normalizes raw positive
error results. Previous EBUSY logs alone cannot establish blocked assignment.
At 21:52:09, Xbox Down and A produced accepted submissions without a remote
binding call. At 21:53:49, stats showed **98 read, 98 delivered**. The user
confirmed profile selection and Xbox navigation on the home screen. Game
confirmation was requested separately. These acceptance results supersede the
earlier claim that no compatible input-submission path had been found.
# Xbox Guide / PS button follow-up

The ordinary GIP `0x20` packet does not contain the Xbox Guide button. The
event loop previously ignored its separate `0x07` packets. The updated build
tracks the two low state bits in byte 4, submits both press and release using
PS bit `0x10000`, and preserves that state across ordinary input packets.
Separate Guide packets retain the latest sticks, triggers and other buttons;
before the first ordinary packet, the cached report starts with neutral sticks.

Host mapping tests cover truncated packets, Guide press/release, held state
across ordinary reports and preservation of the other controls. All host
checks and the payload build passed. Live logs recorded Guide state changes
and accepted virtual submissions. The user confirmed the Control Center/home
test works on firmware 4.03. No remote shell injection was used.

