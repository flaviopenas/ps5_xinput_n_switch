# Controller Manager for FW 4.03

This is the all-in-one USB and Bluetooth entry point. Load only
`dist/ps5-xinput-manager.elf`: it starts the embedded combined service and
opens the web interface. No separate USB payload is needed. The service
has capacity for up to four supported USB controllers plus four Bluetooth
controllers. The user confirmed three Xbox wireless controllers working together.
Four physical Xbox controllers, four USB controllers and mixed Xbox / Switch
operation still need live console validation. The user confirmed direct Ultimate
2C Bluetooth console control after the debugging described below. The USB section shows initialization, connection and whether
inputs have been accepted.

USB detection continues with no controllers attached and after unplugging
the last controller. Plug in controllers whenever needed; select a PS5 user
for each new virtual controller. Closing the page leaves both USB and
Bluetooth working. Stop older standalone USB and Bluetooth payloads before
starting this version. Holding View+Menu or Minus+Plus on a USB controller for two seconds stops
the combined service. On a Bluetooth controller the shortcut disconnects only
that controller and disables its reconnect preference.

The previous one-Xbox combined FW 4.03 service was loaded on the test console. Its API reported
USB support enabled; the 8BitDo Ultimate C USB virtual pad delivered 4,598
reports while the saved Xbox Bluetooth connection reached Connected and
delivered 71 reports. The physical navigation/page-close check is separate
from those accepted-report measurements. Host tests cover USB empty startup,
mixed hotplug, removal/replug, cleanup, and USB polling during Bluetooth
sessions through the manager control callback.

`ps5-xinput-manager.elf` starts the separately loaded controller service if it
is not already listening, then opens `http://127.0.0.1:9030` in the console's
browser. The launcher embeds the service, so importing this one ELF into
Payload Manager is enough. The local ELF loader must be running on TCP 9021
when the service needs to start. The service ELF is an internal build artifact; only the manager ELF is
published as the main payload.

Each launch uses a fresh page URL, and embedded CSS/JavaScript URLs contain
their content hashes. This prevents the console browser from keeping an older
interface open after a payload update.

The service hosts all interface assets on the PS5. No PC server, CDN or
internet connection is needed. Closing the browser leaves the service and
its virtual controller running. After a console reboot, jailbreak and start
the service again. This bundle is an ELF homebrew, not an FPKG.

Build with `make manager`. Copy `dist/ps5-xinput-manager.elf` to your USB drive
and import it with Payload Manager, or send it with:

```powershell
python testing_tools/scripts/send_payload.py 192.168.1.136 --elf dist/ps5-xinput-manager.elf
```

Stop any older Bluetooth bridge before launching this bundle. The new builds
use a shared radio lock and the service's listening port prevents duplicate
service instances. Older builds did not use that lock.

## Controller workflow

1. Use the DualSense to navigate the manager. Hold Pair on an Xbox until its
   light flashes rapidly, then choose **Search Xbox**.
2. Select the discovered controller and choose **Pair**. The service completes
   Secure Connections pairing and checks its HID input layout before saving
   the bond and registering a virtual pad.
3. Select a PS5 user if prompted. The Xbox now controls the console.
4. Repeat Search Xbox and Pair for the second, third and fourth supported Xbox. Pairing and saved
   reconnect setup are serialized; the first Xbox keeps receiving inputs while
   the second is being set up. Each Xbox has a separate virtual pad and needs
   its own PS5 user for local multiplayer.
5. Pair and Connect enable automatic reconnect for the selected slot. The four
   **Reconnect automatically** selectors save independent preferences. A card
   **Disconnect** stops only that Xbox and disables its reconnect preference.
   **Disconnect all Bluetooth** stops all four and cancels a search or pending setup.
6. Choose the controller in **Bluetooth input check** to view its inputs.
7. **Forget** removes the selected console-side bond; pairing again requires
   the controller's physical Pair button. Other stored bonds are preserved.

The selected Bluetooth input-check panel shows both sticks, triggers and pressed buttons. Keyboard
arrows/Enter are supported; DualSense input uses the console browser's native
controls without a second Gamepad API activation path. On the test FW 4.03 console,
the user confirmed fresh pairing, Forget and pairing again through the interface, DualSense navigation
in the manager, Xbox input alongside
the wireless DualSense, and automatic reconnect after closing the interface
and power-cycling the Xbox normally.

The verified trial bond is imported once automatically into
`/data/ps5-xinput/bonds` without deleting the original file. Up to eight bonds
can be stored. This version has **four simultaneous Bluetooth slots**, shared by Xbox LE and experimental Switch classic HID.
Search and pair a second Xbox while the first is connected. A fifth Connect
request is rejected until a slot is free. The combined service also has capacity
for four USB controllers. Games and the console may impose additional player
limits; eight simultaneous virtual pads have not been validated on hardware.

The native radio and HID profile remain experimental. Xbox forwarding requires the verified
Series input-descriptor prefix. Switch classic forwarding requires an encrypted
HID link, a Pro-type device-info reply, acknowledged full-report mode setup and
a complete full-mode input report. Once ready, both full `0x30` and simple
12-byte `0x3F` reports are decoded. Discovery of a HID
device does not establish that its report layout is supported. There is no
Bluetooth rumble, motion or headset implementation yet.

Previous one-Xbox fresh-console isolation verified the manager alone, USB 8BitDo plus DualSense,
and all three controllers together with Xbox Bluetooth. Shutdown with USB
cleanup first and explicit Bluetooth transfer cancellation/endpoint closure
reached the final service-stopped log, and the manager was reloaded afterward.
DualSense reconnection after power-off failed while Xbox Bluetooth was active:
DualSense only blinked while USB 8BitDo and Xbox still worked. Disconnecting
Xbox and closing our Bluetooth transport did not restore DualSense, and
connecting it by USB also did not restore input. This remains an unresolved
console compatibility issue; the combined build is experimental. Stopping the
whole service also did not restore DualSense input. Restart the console to
recover before further Bluetooth testing. UDP logs can arrive at the active viewer instead of the stop
script when both subscribe, causing a missing-confirmation timeout even if
the viewer records successful cleanup.

The reconnect trace captured classic connection requests on the bridge's
shared event reads. The recovery experiment closes our transport
immediately when an unowned classic ACL connection request arrives, removes all
managed Bluetooth virtual pads, and pauses automatic reconnection until an
explicit Connect command. Requests from an explicitly selected Switch peer are
handled by that peer rather than triggering native handoff. It sends cleanup
commands only for its own links, without consuming native handshake replies or changing existing native
connections. USB polling continues during the pause. An initial handoff test
restored DualSense and the logs subsequently recorded Xbox input registration;
the automatic 30-second retry experiment subsequently cycled between
controllers and left neither usable. Automatic retry after native handoff has
therefore been removed. The persistent-pause build needs fresh-console
validation. A persistent-pause run restored DualSense, but explicitly resuming
Xbox triggered another native request and paused the bridge again. This interrupts Xbox Bluetooth; reliable native reconnect and
uninterrupted radio coexistence remain unresolved.

`make bt-secondary-probe` builds a feature-only function-2 investigation
using event endpoint `85` and interface-recipient control requests with index
2. On the test console, the endpoint opened but queries `1001`, `1003`,
`1002` and `2003` returned no command responses. Cleanup completed. This
does not establish function 2 as a usable alternative radio transport; it
does not scan, connect, pair or reset the controller.

On FW 4.03, an initiation rejected with HCI status `0x0C` gets one recovery
attempt: cancel the pending LE initiation on function 0, then retry. This
does not reset the radio or disconnect an established foreign handle. If
initiation is still rejected, automatic retries stop. Function 2 remains
untouched. The console's native Bluetooth stack still shares event reads.

## Service interface

The service listens on HTTP 9030 and logs on UDP 9024. `/api/status` reports
public device metadata, connection state and input values; it never returns
pairing keys. Commands use `/api/command` with a per-process random token and
same-origin checks. The manager is intended for the trusted console/LAN; the
token prevents cross-origin command requests, not access by other LAN users.

Reconnect slot 1 retains the existing `auto.txt` preference; slot 2 uses
`auto2.txt`; slots 3 and 4 use `auto3.txt` and `auto4.txt`. Existing bond files remain compatible. `/api/status` adds
`bluetoothCapacity`, per-controller `bluetooth` state/input, and `autos`.
`disconnect DEVICE_ID` stops one controller; `disconnect` stops all Bluetooth controllers.

Bond records are private, versioned 64-byte files written atomically. Invalid
records are not imported. Device IDs are validated before use in file paths.
The one-time legacy-import marker prevents Forget from being undone on the
next service start. Release logs keep connection stages, failures and cleanup;
routine input packets and detailed GATT/UUID traces are omitted.
Closing the interface does not send a disconnect or stop request. To stop the
whole service from the PC:

```powershell
python testing_tools/scripts/stop_payload.py 192.168.1.136 --port 9024
```

View + Menu (Xbox) or Minus + Plus (Switch) held for two seconds disconnects only that Bluetooth controller
and disables its auto reconnect; the manager service stays available.

The launch API follows the local PS5 Payload SDK `samples/browser/main.c`.
Payload Manager's console browser shortcut is documented by its
[project](https://github.com/itsPLK/ps5-payload-manager). Runtime compatibility
and controller navigation are validated separately from compilation. Host
tests cover HTTP bounds and origin/token checks, commands during a connection,
scan cancellation, bond permissions, reconnect preferences and persistent
forgetting. `testing_tools/tests/manager_ui.py` additionally exercises the interface in a
desktop Chromium browser using a simulated service.

## Direct Ultimate 2C Bluetooth

For the Ultimate 2C Bluetooth for Switch, use Search Switch / 8BitDo while its
Pair indicator flashes rapidly. Pair the discovered Pro Controller, then
select a PS5 user if prompted. Sticks, face buttons and Home have been confirmed
working on 4.03. If reconnection stalls, fully power the controller off before
turning it on and entering pairing mode again.

Input timing and reconnection reliability remain under investigation. Previous
measurements and implementation details are preserved under testing_tools/docs.
Four physical Bluetooth controllers, mixed Xbox/Switch operation and eight
physical USB/Bluetooth controllers together still require console validation.
