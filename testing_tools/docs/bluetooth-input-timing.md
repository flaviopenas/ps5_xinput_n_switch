# Bluetooth input timing

The manager emits one `BT timing:` summary per active link approximately every
five seconds, on successful virtual-pad submission. Switch and Xbox use the
same instrumentation and unchanged input scheduling.

Capture a bounded test while moving sticks and pressing buttons:

```sh
python3 scripts/measure_bt_timing.py 192.168.1.136 --seconds 60
```

`arrival` measures milliseconds between accepted, decoded input reports using
the time the host received their complete L2CAP packets. Its buckets are
0-4, 5-8, 9-16, 17-32 and above 32 milliseconds. An idle controller that only
sends changes can have long intervals without indicating a delay.

`host-to-submit` measures time from receipt of the latest input to successful
virtual-pad submission, including decoding, queueing and the submit call.
`coalesced` counts reports that replaced an input still awaiting submission.
Busy user assignment can increase submission delay. Cached startup input has
no live receipt timestamp and uses decode time instead.

These are host measurements with millisecond precision. They cannot measure
physical button scanning, radio latency before host receipt, console/game
processing or display latency. Report cadence alone does not establish
end-to-end input latency. The capture can include historical log replay;
use console timestamps to select the current test. Averages are truncated
to one decimal place, and each summary resets its window counters.

## Live observations, 2026-10-12

The manager, standalone bridge and pairing builds and full host tests passed.
The integration test verifies receipt-to-submit accounting, interval buckets,
coalescing, window reset, and exclusion of registration-only neutral input.

The Ultimate 2C was measured on 192.168.1.136. The first reconnect timed out;
after the user powered it on again, it reconnected with the saved key and
produced live stick movement. `build/bt-timing-comparison-2.log` contains 35
distinct five-second summaries (including replayed earlier windows).

- Most early windows: arrival average 10.6-11.2 ms, maximum 63-73 ms,
  approximately 50 intervals above 32 ms per five seconds. Submission averages
  0.5-0.7 ms, maxima 4-5 ms, no coalescing in those windows.
- Later windows at console log times 23:13:07 through 23:13:42: arrival average
  9.9 ms, maxima 19-25 ms, no intervals above 32 ms. Submission averages round
  down to 0.0 ms at this clock precision, maxima 1-4 ms, no coalescing.

This supports uneven arrival timing as a lead, but does not establish its
cause or physical-button-to-screen latency. The LE initiation parameters use
a 100 ms scan interval and 50 ms window; radio scheduling during unavailable
Xbox reconnect attempts is a hypothesis, not a confirmed diagnosis.

Xbox registration occurred, but no accepted live Xbox input reports were
captured, so there is no valid Xbox latency comparison. Registration-only
summaries in the initial build are invalid and were subsequently excluded.
Temporarily disabling the three Xbox reconnect preferences to isolate radio
activity was inconclusive because the Switch link dropped and could not
reconnect during that capture. All four original reconnect preferences were
preserved/restored. No controller timing or deadzone tuning was applied.

The corrected diagnostic build was deployed and the Switch reconnected.
`build/bt-timing-final-live.log` contains nine additional valid summaries with
the same repeating long-gap pattern and 0.5-0.7 ms average receipt-to-submit
time. The user subsequently reported the PS5 stuck in the browser app. The
manager HTTP and UDP services remained responsive and reported Switch inputs
accepted; this does not prove the console UI was responsive. The diagnostic
payload was stopped cleanly to release virtual pads and shared radio transfers.
Console UI recovery and the Xbox comparison remain unverified.
