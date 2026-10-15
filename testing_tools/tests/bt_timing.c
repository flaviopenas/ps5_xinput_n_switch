#include "bt_timing.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static unsigned logs;
static char last_log[1100];

void log_line(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(last_log, sizeof last_log, format, args);
    va_end(args);
    ++logs;
}

int main(void)
{
    bt_input_timing timing = {0}, peer = {0};
    bt_timing_delivered(&timing, "Switch", 2, 110);
    assert(!timing.delay_count && !logs); /* Registration-only neutral input. */

    bt_timing_input(&timing, 100, 110, 0);
    bt_timing_input(&timing, 116, 120, 1);
    assert(timing.interval_count == 1 && timing.interval_sum == 16);
    assert(timing.interval_bins[2] == 1 && timing.coalesced == 1);
    bt_timing_delivered(&timing, "Switch", 2, 120);
    assert(timing.delay_count == 1 && timing.delay_sum == 4 && timing.delay_max == 4);

    bt_timing_input(&timing, 124, 125, 0);
    assert(timing.interval_bins[1] == 1 && timing.interval_max == 16);
    bt_timing_delivered(&timing, "Switch", 2, 5111);
    assert(logs == 1 && strstr(last_log, "Switch handle 2 window 5001 ms"));
    assert(strstr(last_log, "host-to-submit n=2 avg=2495.5 max=4987 ms"));
    assert(!timing.interval_count && !timing.delay_count && !timing.coalesced);
    assert(timing.timing_log_at == 5111 && timing.last_input_at == 124);

    /* Bucket boundaries, missing receipt timestamp, independent peer state. */
    bt_timing_input(&peer, 0, 1000, 0);
    const unsigned deltas[] = {4, 5, 8, 9, 16, 17, 32, 33};
    int64_t now = 1000;
    for (unsigned i = 0; i < sizeof deltas / sizeof deltas[0]; ++i) {
        now += deltas[i];
        bt_timing_input(&peer, now, now + 2, 0);
    }
    const unsigned long expected[] = {1, 2, 2, 2, 1};
    assert(!memcmp(peer.interval_bins, expected, sizeof expected));
    assert(peer.interval_count == 8 && peer.interval_max == 33);
    assert(!timing.interval_count && timing.last_input_at == 124);
    puts("Bluetooth timing passed: arrival/submit separation, neutral exclusion, "
         "coalescing, window reset, bucket boundaries and independent links.");
}
