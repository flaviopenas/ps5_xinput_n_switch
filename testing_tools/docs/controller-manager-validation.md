# Controller Manager validation history

Preserved during main-payload isolation. Paths and commands below describe
the earlier tests; use testing_tools/README.md for current development commands.

## Four-Xbox validation

The new build has not yet been validated with four physical Xbox Bluetooth
controllers on FW 4.03. Stop the old service before loading the new manager;
launching the manager while the old service listens on 9030 reopens that old
service instead of replacing it.

1. Load the manager, pair/connect the first Xbox, and assign its PS5 user.
2. Keep connected Xbox controllers active while using Search Xbox and Pair
   for the second, third and fourth. Assign each to a different PS5 user.
3. Select each Xbox in Bluetooth input check. Check separate buttons, sticks,
   triggers and Home, then verify independent navigation/gameplay.
4. Disconnect one Xbox through its card; verify the other continues working.
   Reconnect it, then power-cycle each Xbox to check independent reconnect.
5. Close the browser and repeat input tests. Capture UDP 9024 logs and record
   whether all four controllers work with the wireless DualSense present.

Host tests exercise the real link setup/HID decoder and virtual-pad submission
against a simulated HCI radio: peer input during setup, interleaved ACL fragments,
Guide isolation, busy retries, rejected initiation, unsupported profiles,
independent disconnection, native-radio yield and full cleanup. Manager/API and
browser tests cover four reconnect preferences, capacity rejection, selected
input views and per-controller Disconnect. These checks cannot establish the
radio's multi-link capacity, native Bluetooth coexistence or game player limits.


## Ultimate 2C Bluetooth for Switch: experimental direct Bluetooth

This is the **Ultimate 2C Bluetooth for Switch**, not the Windows/Android
Ultimate 2C Wireless model or a USB receiver. Its Switch Pro protocol uses
classic BR/EDR HID rather than the Xbox LE/GATT path. The manager uses the same
HCI reader and four connection slots for both transports. Existing Xbox bonds
and reconnect preferences remain compatible.

Stop the old service before sending the updated manager ELF. A manager launched
while an old service listens on 9030 opens that service without replacing it.

```powershell
python testing_tools/scripts/stop_payload.py 192.168.1.136 --port 9024
# Wait for service cleanup, then send the new launcher.
python testing_tools/scripts/send_payload.py 192.168.1.136 --port 9021 --elf dist/ps5-xinput-manager.elf
```

Replace the example IP with your PS5 address. In the controller manager:

1. Turn on the Ultimate 2C and put it in Bluetooth pairing mode using its Pair
   button. See the [official controller manual](https://download.8bitdo.com/Manual/Controller/Ultimate/8BitDo-Ultimate-2C-Bluetooth-Controller-8.pdf).
2. Choose **Search Switch / 8BitDo**. Classic inquiry runs for about 12 seconds;
   names are resolved where available. The controller may advertise as
   **Pro Controller**, an 8BitDo name, or an unnamed Bluetooth device.
3. Choose its **Pair** button and select a PS5 user if prompted. Three Xbox
   Bluetooth controllers can occupy the other slots. Verify that their input
   continues while this controller connects.
4. Select the new controller in **Bluetooth input check**. Check both sticks,
   D-pad, shoulders, ZL/ZR, face buttons, Plus, Minus, Capture and Home. Nintendo
   B (south) maps to Cross and A (east) to Circle. ZL/ZR are digital triggers;
   Home maps to PS and Capture/Minus to the touchpad shortcut.
5. Disconnect only this controller and verify all three Xbox controllers still
   work. Reconnect, then power-cycle it to test saved-key automatic reconnect.
   Close the browser and repeat the input checks in a game with four-player support.

Classic IDs end in `-2`; existing LE IDs retain `-0`/`-1`. Classic link keys
are stored as private, atomically written `XBCL0001` bond records. Keys are
never exposed by the HTTP API or logged. Pairing replies are restricted to the
selected address, and ACL packets are routed by connection handle. This build
requires the radio's existing Secure Simple Pairing mode to be enabled; it does
not reset the radio or change global event masks, scan-enable, class or name.

The protocol implementation is independent, using
[Linux hid-nintendo](https://github.com/torvalds/linux/blob/master/drivers/hid/hid-nintendo.c)
and [Bluepad32's Switch parser](https://github.com/ricardoquesada/bluepad32/blob/main/src/components/bluepad32/parser/uni_hid_parser_switch.c)
as references. It verifies the controller's Pro-type device-info response,
reads optional factory stick calibration, enables IMU sampling (samples are ignored),
disables rumble and requests full
input mode and sets a player LED. Gyro, rumble, audio, rear-button remapping and
Switch 2-specific features are not implemented.

Host tests cover classic discovery bounds, selected-peer SSP, malformed HID
configuration, fresh pairing, saved-key reconnect, profile rejection and setup
timeout with three Xbox peers preserved, mixed input routing, Home edges,
independent disconnect and cleanup. Browser/API tests cover the new search
command and a classic controller in slot 4. These tests simulate the radio and
cannot confirm Ultimate 2C firmware behavior or native PS5 radio coexistence.

For a failed live attempt, capture UDP 9024 logs with
`python testing_tools/scripts/watch_logs.py --ps5 PS5_IP --remote-port 9024 --port 19024` and report the `BT Switch:`
stages. The known native DualSense reconnect limitation described above still
applies to the shared radio.


### Live Ultimate 2C debugging

A live direct-Bluetooth attempt discovered `Pro Controller` but rejected Create
Connection with HCI status `0x12`. The ACL packet-type field had been byte-swapped
(`0x18CC`); it is now `0xCC18`, encoded as `18 CC`. The integration mock checks
those actual command bytes. After the correction the console completed classic
authentication, encryption and both HID channels and verified firmware 3.72.

The controller sends full `0x30` reports of 49 bytes and acknowledged all setup
commands. Its initial full report can arrive before the LED acknowledgment. The
bridge now retains that report until setup completes, without requiring idle
controllers to send a duplicate. Host integration reproduces this ordering.
A live traced attempt registered the Switch virtual pad; physical controls,
saved-key reconnect and simultaneous operation with three Xbox controllers still
need separate live checks.


The first connected build accepted only its initialization reports; the user
reported that controls did not work. Live tracing then showed 12-byte `0x3F`
reports during normal operation. The decoder now handles that verified Pro
profile's simple reports (buttons, hat, four 16-bit axes and Home) as well as
full reports. Tests cover all hat directions, malformed lengths, axes, triggers,
Home press/release and mixed Xbox input isolation. A selected controller can
also connect during read-only radio queries; its incoming link is retained and
no duplicate Create Connection is sent. An integration test reproduces this
ordering. Existing native links remain untouched; an already-established link
whose completion was consumed elsewhere may require controller power cycling.

After controller power cycling, the final decoder build reconnected using the
saved bond and delivered a sustained stream: API samples rose from 2,688
received / 2,685 delivered to 3,372 received / 3,369 delivered, with a changing
D-pad bit. This establishes live input decoding and kernel submission; user
confirmation of console navigation and mixed four-player operation is separate.

The user then confirmed that the Ultimate 2C sticks, face buttons and Home work
on the console with the final build. The running PS5 service and packaged ELF
contain the corrected ACL packet mask, early-input/incoming-link handling and
the simple-report decoder. This confirms this controller's direct Bluetooth
controls, not simultaneous operation with three physical Xbox controllers.
