# Bluetooth coexistence: offline findings

Current scope: inspect existing source, logs and saved captures only. No new
console deployment, protected-memory capture or firmware modification.

The goal remains Xbox Bluetooth input alongside normal DualSense off/on
reconnection. Xbox Bluetooth inputs already worked in the direct USB/HCI
build. The unresolved failure is native DualSense reconnection, not Xbox
button mapping or initial Xbox pairing.

## Two different failures

| Path | Established result | Remaining problem |
| --- | --- | --- |
| Direct USB/HCI reads | Xbox pairing, reconnect and inputs worked; both controllers worked when DualSense connected first | DualSense off/on reconnect failed while this transport was active |
| Native `/dev/bt` requests | Discovery sees Xbox; connection and confirmation requests returned 0; tested DualSense off/on reconnect passed after cleanup | Pairing ends with notification status `0x802F3403`; no Xbox inputs or native saved bond confirmed |

Native registration of the Microsoft USB identity `045E:0B12` on 4.03
returned `0x802F3701`. This is a separate registration result; it does not
identify the exact branch responsible for the pairing notification. The
controller's BLE PnP identity has not been established by this test.

## What the direct reader actually does

In `src/bluetooth/bt_hci.c`, `bt_hci_poll` consumes USB completions, dispatches event
or ACL bytes to payload callbacks, and rearms its read. `other_event` is
another callback inside the payload; it does not forward bytes to the native
PS5 driver.

In `src/bluetooth/bt_session.c`, the ACL callback immediately returns when a packet's
connection handle does not match the Xbox handle. Therefore a native
controller packet delivered to this USB reader is discarded by the payload
after USB consumption. Filtering it at this point cannot give it back to the
native consumer. The event callback similarly handles only selected events
and credits for the Xbox handle.

This source-level behavior explains how the direct transport can interfere
with native operation. It does not establish which particular missing event
or ACL packet caused each observed reconnect failure. Existing trials with
one event read still failed DualSense reconnect; reducing the queue alone
has not solved coexistence.

## What the saved native trace establishes

`build/bt-native-pair-trial.log` records:

1. Fresh Xbox advertisement, followed by discovery stop and connection
   request accepted with result 0.
2. Event 4/subtype 4 for the exact Xbox peer, followed by confirmation reply
   accepted with result 0.
3. Event 5/subtype FF with status `0x802F3403` immediately afterward.
4. Event 6/subtype 13 five seconds later. Device-list queries still show only
   the two original records.
5. Listener removal and both descriptor closes succeed; final exit is logged.
   The user confirms DualSense off/on wireless reconnection works afterward.

The supplied `x86_kernel_1000.elf` has device-type/vendor/model checks leading
to this status in its BLE completion code. That image is not verified as
4.03. Its checks are evidence for a policy hypothesis, not valid 4.03 patch
locations or proof of the live failure's exact cause.

## Consequences for the next implementation

A complete coexistence fix needs a path that delivers Xbox data without
consuming native controller traffic. Adding another button mapping, waiting
longer after confirmation, or suppressing logs does not address ownership.

Before implementing a native backend, establish its supported-device contract
and a report path using verified captures or documented interfaces. The
current saved captures do not establish a generic native Xbox report API.
Without that evidence, an internal-radio fix remains incomplete.

The unvalidated GPU experiment is parked, absent from all build targets, and
has an explicit compile-time stop in `testing_tools/src/diagnostics/bt_gpu_capture.cpp`. It was never
built or deployed. Production manager/controller code was not changed by
this investigation. The previous kernel-mapping metadata diagnostic exited
before the offline-only instruction; no kernel instruction bytes were read.
