# Separate-service stability investigation, 2026-10-16

Console: 192.168.1.136, firmware 4.03. USB and Bluetooth ran in separate
processes from console log time 00:36:42. No runtime settings were changed
during this investigation. The earlier experimental reconnect patch was
rolled back before these services were built.

## Observations

In the captured Bluetooth log through approximately 00:42, there were eight
missed command-reply attempts, two missing delivery-credit failures, eleven
Switch setup failures, and four manual pairing requests for F46AD7BCB280-0.
That Xbox eventually completed pairing and registered a virtual pad at
00:40:59. The failed attempts reached different stages, including encryption
and HID discovery. This is not a consistent bond rejection.

Eight Switch failures reported connection status 4; three had no connection
completion status (-1). Status 4 is a page timeout. These failures preceded
authentication. They do not establish a report decoder or virtual-pad failure.

Three status samples five seconds apart showed one Xbox at zero received
reports / one delivered submission and another at 23 / 24, without increases.
Controller movement was not prescribed during these samples, so unchanged
counters do not prove input loss. Registration and a neutral submission do not
establish working physical controls.

USB logged 11,770 received/delivered reports for the Switch before disconnection
at 00:39:02. A later wired Xbox attachment logged repeated rebuilds and removal
at 00:39:35. Its totals reuse the slot's earlier counters; do not attribute the
11,770 reports to that Xbox. Cable changes and controller mode were not
controlled, so that failure needs a separate USB test.

## Leading explanations and limits

The strongest Bluetooth lead is competing consumption of native HCI events
and ACL data. `bt_hci.c` opens shared USB reads on function 0; the application
lock excludes other copies of this payload, not the PS5 native stack.
The current capture cannot identify which consumer received a missing packet.
Earlier single-reader trials already disrupted native reconnects without
sending HCI commands; see `native-bluetooth-routing.md`.

The session stops sending when outstanding delivery credit is missing for one
second. A lost or delayed completion can therefore abort otherwise successful
pairing. Bypassing this guard would risk exceeding controller buffers.

Switch failures are retried after five seconds. With approximately fifteen
seconds spent paging an unavailable peer, much of the observed interval is
occupied by reconnect setup. Previous input timing measurements also correlated
unavailable-peer initiation with longer report gaps, without proving causation.

The host integration tests replace the HCI transport and synthesize completion
events. They validate protocol/routing logic, not competing native reads,
physical radio scheduling or recovery after native state diverges.

## Next controlled comparison

### Single-Xbox baseline

The user disabled other reconnect preferences and confirmed Xbox
0C35265AE1DE-0 controls worked properly. The live API showed only that Xbox
connected, 1,896 received reports and 1,897 accepted submissions (including the
initial neutral submission), and no current error. Console log times 00:45:25
through 00:47:33 showed continuing input and timing summaries without new setup
failures in that interval. Host-to-submit window averages were 0.0-0.2 ms,
with maxima up to 4 ms. Report gaps include possible idle periods and do not
measure physical latency. This establishes a usable single-controller baseline,
not the cause of the earlier mixed/retry failures.

### Xbox during unavailable Switch reconnect attempts

The user reported smooth Xbox controls with Switch reconnect enabled. The log
confirms Switch page timeouts at 00:50:07, 00:50:27 and 00:50:47 while Xbox
input continued. Active five-second Xbox windows contained 93-94 reports,
approximately 54 ms average arrival intervals and 0.0-0.1 ms average
host-to-submit time. Longer gaps also occurred without prescribed continuous
movement, so they cannot be attributed to input loss. This comparison did not
reproduce Xbox instability merely by retrying the unavailable Switch.

The log also records fresh Switch pairing at 00:49:17, successful full setup at
00:49:19, manual disconnect at 00:49:37 and Connect at 00:49:52. Consequently,
the entire interval is not a pure single-variable retry experiment. The later
timeout windows still establish peer input during failed Switch paging.

### Normal Ultimate 2C power-on with Xbox connected

The user reported missed Xbox inputs and failed Ultimate 2C connection.
At 00:51:33 the Switch established a classic link and completed authentication,
but setup ended at 00:51:59 with encryption still disabled and no verified HID
profile. Xbox summaries during setup contained only four or five reports over
approximately eight to ten seconds, with gaps up to 4,861 ms. Input activity
was user-driven rather than instrumented, but the reported missed inputs now
correlate with real Switch setup, unlike the previous off-controller paging
comparison. Host-to-submit maxima in these sparse windows were 1-3 ms; most
of the observed gap preceded decoded input delivery.

Subsequent Switch attempts immediately failed with status 11 (0x0B). This is
consistent with an existing classic link not being recovered, but the trace
does not establish which consumer owns it. At 00:52:21 an unrecognized classic
request triggered native handoff and closed all managed Bluetooth links. The
rolled-back build lacks request-address logging, so this final request cannot
be attributed conclusively to the Switch. API state afterward confirmed
`Bluetooth paused for DualSense` and no connected Bluetooth controllers.

This narrows the reproducer to live classic connection/authentication and
recovery while LE input is active. Do not deploy another handoff-only fix:
it would not explain or repair the earlier missing encryption and input gaps.

### Sync-assisted mixed connection

The user reported eventual success after retries, with the Xbox going off once.
At 00:59:31 the Switch authenticated, encrypted and opened both HID channels,
then failed at 00:59:32 with an L2CAP queue/buffer overflow and a missing
delivery-credit event. Unlike the power-on attempt, this failure was after
encryption. The overflow message combines oversized messages and full queues;
the current log does not distinguish these conditions or identify the peer.

At 00:59:48 the next Switch attempt completed its profile/setup and registered
a pad. At approximately 01:00:13 both saved peers were connected: Switch had
2,536 received/accepted reports; Xbox had 129 received and 130 accepted
submissions. Switch timing windows contained about 503 reports per five seconds,
9.9 ms average arrival intervals, and maxima of 18-23 ms. This demonstrates
simultaneous established links and received input from both, not yet sustained
simultaneous physical controls or reliable startup. No payload was replaced
during this comparison.

### Power cycle after both controllers worked

The user confirmed both controllers responded, then powered the 2C off/on and
reported Xbox went off. At 01:02:31 the log records native handoff, followed by
closure of all managed Bluetooth links. The API confirmed paused state and an
empty Bluetooth list. Switch auto-reconnect was disabled at that point.

A narrow fix now identifies saved classic addresses using the manager's bond
list before deciding to yield to a native controller. A request from a saved
Switch between attempts preserves the existing managed links; an unknown
address still triggers native handoff. It does not initiate or accept the
inactive Switch link itself, change encryption, cancel Xbox setup or modify
radio settings. The host regression checks saved-peer preservation, subsequent
Xbox input and native handoff for an unknown peer. Console validation remains
necessary; connection reliability is still unresolved.

Keep the USB service as the independent control path. Record existing reconnect
preferences, temporarily disable only the unavailable Switch's automatic
reconnect, and exercise one connected Xbox continuously for a bounded capture.
Compare report progress and failures against the mixed/retry capture, then
restore the preference. Repeat with only the Switch if the first baseline is
stable. These settings changes have not been performed yet.

Do not infer that increasing reader counts, increasing timeouts, removing credit
checks or another restart fixes the underlying event-ownership problem. A
durable transport fix needs verified routing that preserves native events.

Local captures are under `testing_tools/artifacts/reconnect-2026-10-16/`:
`ultimate-2c.log`, `usb-only.log`, and `status-samples.jsonl`. Logs append and
include replay; use console timestamps to select this separate-service run.

## Narrow handoff fix: live result

The fix was loaded in the Bluetooth-only service. At 01:04:12 the log identified
E4:17:D8:AF:AB:93 as saved, not active, and preserved managed links rather than
requesting native handoff. The user then confirmed Xbox plus Sync-assisted
Switch connection worked. At approximately 01:05:55, the API showed both
connected, no current error, 1,788 Switch reports and 300 Xbox reports. This
validates this mixed connection attempt with the fix; normal power-on reconnect
without Sync is not yet validated.

The subsequent normal power-on preserved Xbox but did not reconnect Switch.
Automatic reconnect was enabled; the log repeatedly reported status 0x0B.
The next revision retains incoming saved-peer events, schedules saved-session
setup outside HCI callbacks, and replays an already-established link before
deciding whether to issue Create Connection. Retained event storage is bounded
and wiped after replay or transport cleanup. A host regression injects a
connection completion before slot setup and verifies no additional Create
Connection, successful saved-key setup and preservation of Xbox input. Main
build, Bluetooth-only build and full host suite passed. Bluetooth service was
replaced after confirmed clean shutdown; console power-on validation is pending.

That deferred-event revision failed console validation and was removed. A
Switch request at 01:09:06 waited behind Xbox setup until 01:09:34; Accept
Connection was then rejected (0x0C), Create Connection Cancel reported status
2, and cleanup closed the shared transport at 01:09:36, dropping Xbox.
Retaining an incoming request across serialized setup without a freshness bound
is invalid; the host regression did not cover that delay. The earlier narrow
saved-address classification fix was rebuilt and restored after clean shutdown
at 01:10:37. USB was not replaced. Reliable power-on reconnect remains unsolved.

## 2C-only isolation

Xbox reconnect preferences were disabled and no Xbox link was present. With
only E417D8AFAB93-2 automatic reconnect enabled, the user powered the 2C fully
off and on normally. At 01:14:21 it established handle 1, authenticated and
enabled encryption. At 01:14:41 setup failed with authenticated=1, encrypted=1,
profile=0 and stage=5; no HID channel-open message appeared. Later retries
again reported 0x0B. Thus Xbox coexistence is not required to reproduce the
failure, and this attempt failed after saved-key authentication, during HID
channel establishment. Native radio-reader competition is not excluded by
removing the Xbox. Additional diagnostics log HCI setup command status,
outbound HID channel requests, inbound L2CAP signalling and channel state on
failure; they do not change protocol behavior or log keys/input contents.

The diagnostic capture at 01:16:15 confirms successful authentication and
encryption, followed by a single outbound control-channel request (PSM 0x0011,
CID 0x0040, identifier 1). No inbound L2CAP signalling was logged before the
failure at 01:16:35. The channel was requested but had no remote CID. This
does not distinguish an unanswered request from a response consumed elsewhere.

A bounded connection-request RTX implementation now retransmits the identical
request/identifier after one and then two seconds, allows four more seconds
for the final response, and fails rather than waiting indefinitely. A Pending
response suppresses these RTX retries while the manager retains its overall
setup deadline. Accepted responses stop retransmission. No credit guard is
removed and no radio configuration changes are made. The protocol reference is
[Bluetooth Core 5.4, L2CAP sections 4 and 6.2](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-54/out/en/host/logical-link-control-and-adaptation-protocol-specification.html).
Host tests cover lost responses, duplicate identifiers, backoff, bounded failure
and Pending followed by success. Console validation is still required.

After deployment the user reported recovery on normal power-on. At 01:18:55,
authentication, encryption, both HID channels and Pro setup succeeded; by
approximately 01:19:30 the API showed 3,706 received/accepted Switch reports
and no current error. Both channel responses arrived on their first requests
in this successful attempt, so the trace does not establish that RTX caused
recovery. Repeated power-cycle trials are still needed. No Xbox link or Xbox
automatic reconnect was enabled in this capture.

The next power cycle failed: at 01:20:34 authentication succeeded and Set
Connection Encryption returned Command Status 0, but no encryption-change
event reached the session before timeout at 01:20:59. HID RTX was never reached.
Subsequent attempts again reported 0x0B. Automatic retries were disabled to stop
reopening the transport while investigating.

A checked readback was added after two seconds of authenticated setup without
an encryption event. HCI Read Encryption Key Size (0x1408) must return an error
for an unencrypted ACL link. The session recovers only on a successful reply
matching its still-live handle with a 16-byte key, and no recorded failure.
No encryption is inferred from Command Status alone. Negative or malformed
readback leaves setup blocked. Host integration covers a missing encryption
event plus affirmative readback, insufficient security, wrong handle and short
key, while checking preservation of three Xbox peers. Protocol reference:
[HCI section 7.5.7](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-60/out/en/host-controller-interface/host-controller-interface-functional-specification.html).

Live readback at 01:24:13 returned status 0 and a 16-byte key, confirming radio
encryption despite the missing event. The session resumed from that readback;
this directly validates the missed-notification failure mode for this attempt.
The eventual successful setup at 01:24:22 used a normal encryption event on a
later attempt. The user confirmed recovery. At approximately 01:25:57, the API
showed 9,836 received/accepted reports with no error and continuous timing
windows around 9.9 ms average arrival. This is one recovered power-cycle trial,
not yet evidence of repeatable reconnect reliability.

The next cycle failed. At 01:27:02 encryption succeeded and the manager sent
control-channel Connection Request identifier 1. It received a Configuration
Request before any Connection Response. After RTX at 01:27:03, Connection
Response reported result 4; the same ordering recurred at 01:27:33-34. This
suggests the original successful response was not delivered to this session,
while the peer had already allocated channel state and rejected the duplicate.
It is not evidence that another retry or the prior successful cycle fixed
receive reliability. Automatic retries were disabled again.

The existing successful session stopped at 01:26:56 without a recorded reason;
the prior logging did not distinguish a remote disconnect, transport error or
manager cleanup. New diagnostics count HCI/ACL packets, USB errors, truncated
packets/headers, key event classes, and log classic disconnect status/reason
and cleanup state. No packets or secrets are dumped, and interpretation is
unchanged. This is intended to distinguish local receive loss from messages
consumed before this transport sees them; native-reader competition remains
a lead, not a proven explanation for every missing response.
