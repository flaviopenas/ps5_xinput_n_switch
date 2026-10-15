# Nintendo Switch Pro over USB

The USB payload recognizes the official Pro Controller (`057E:2009`) on its
HID interface 0 with interrupt IN and OUT endpoints. Unrelated HID devices
are not claimed. Xbox GIP and Xbox 360-format support remain available; the
two controller slots can contain a mix of supported protocols.

Connect the Pro Controller with a USB data cable before loading
`dist/ps5-xinput.elf`. This is part of the USB payload, not the Bluetooth
manager. Select a PS5 user using D-pad and Nintendo **B**.

| Switch Pro | PS5 |
| --- | --- |
| B / A / Y / X | Cross / Circle / Square / Triangle |
| L / R | L1 / R1 |
| ZL / ZR | L2 / R2, released or fully pressed |
| Plus | Options |
| Minus or Capture | Touchpad button |
| Home | PS button, including press and release |
| Left/right stick click | L3 / R3 |

Hold Minus + Plus for two seconds to stop the USB payload. Remote stop and
UDP logging use port 9023, as with the Xbox USB payload.

Initialization uses acknowledged USB handshake/baud commands, disables the
controller's USB timeout, reads factory stick calibration, and selects full
input reports. Calibration is read only; no SPI writes or pairing commands
are sent. Missing calibration falls back to bounded default values. Required
handshake/report setup has bounded retries and removes a failed virtual pad.

Rumble, motion forwarding, NFC and Switch Bluetooth are not implemented here.
The MCU's IMU/vibration setup is initialized, but only neutral rumble is sent
and IMU input is ignored. Factory stick calibration is supported; saved user
stick calibration is not yet read.

Host tests cover initialization acknowledgements and timeouts, calibration
validation, mapping, Home edges, mixed Xbox/Switch routing and cleanup.
Initial FW 4.03 testing accepted inputs but exposed raw stick instability;
the user reproduced stick drift on a PC and stopped testing that particular
Pro Controller. A stable official Pro Controller has not yet been validated.

For unknown USB models, `make usb-probe` builds a separate read-only inventory
ELF. It reads device/configuration descriptors without configuring, claiming
or resetting interfaces, and reports them on UDP 9025.

The 8BitDo Ultimate C Bluetooth also enumerates as `057E:2009` in wired mode,
with IN endpoint `81` and OUT endpoint `02`. Live FW 4.03 tests received 2,944
native reports in 30 seconds, then delivered 593 reports to a virtual PS5 pad
with zero transfer/submission failures before an accidental unplug. A later
connection also registered successfully and began delivering input.

That quiet session delivered 22,671 reports with zero failures before a normal
remote stop. A later diagnostic trace captured the native Home bit (`shared
buttons & 0x10`), its PS-button press/release, and accepted submission; inputs
continued afterward. The on-screen Home behavior is still awaiting confirmation.

USB handshake commands now use their two-byte wire length, matching
hid-nintendo. The driver preserves an already active USB configuration and
finishes negotiation before creating the virtual pad. Startup waits up to
60 seconds for a ready controller, including in the quiet release. If an
earlier failed test left this controller unresponsive, unplug it for five
seconds and reconnect while the payload is waiting. Button/navigation
confirmation remains a separate hardware check from accepted input reports.

The two-second kernel-log assignment observer is now restricted to the
explicit `ENABLE_EXPERIMENTAL_SHELL_BIND` experiment. Enabling verbose logs
alone no longer pauses USB polling during virtual-pad registration; that
pause caused this controller's diagnostic connection to fail after setup.

`make usb-transport-probe` builds a bounded test with no virtual pad; it closes
its USB handle after at most 30 seconds of capture. `make usb-controller-test`
builds the quiet controller payload using UDP 9025 for this test session,
independent of the usual UDP 9023 logger. Neither is needed for normal use.

Protocol references:

- [Linux hid-nintendo](https://github.com/torvalds/linux/blob/master/drivers/hid/hid-nintendo.c)
- [Nintendo protocol research](https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering)
- [Chromium Nintendo controller implementation](https://github.com/chromium/chromium/blob/main/device/gamepad/nintendo_controller.cc)
