#include "bt_timing.h"
#include "log.h"

#include <string.h>

/* Arrival means a complete L2CAP report observed by this host. Summaries
 * avoid per-report logging disturbing the input path. */
void bt_timing_input(bt_input_timing *timing, int64_t received_at, int64_t now, int input_pending)
{
    int64_t at = received_at ? received_at : now;

    if (!timing->timing_log_at)
        timing->timing_log_at = now;

    if (timing->last_input_at && at >= timing->last_input_at) {
        unsigned long delta = (unsigned long)(at - timing->last_input_at);
        unsigned bucket = delta <= 4 ? 0 : delta <= 8 ? 1 : delta <= 16 ? 2 : delta <= 32 ? 3 : 4;
        ++timing->interval_count;
        timing->interval_sum += delta;
        if (delta > timing->interval_max)
            timing->interval_max = delta;
        ++timing->interval_bins[bucket];
    }

    timing->last_input_at = timing->latest_input_at = at;
    if (input_pending)
        ++timing->coalesced;
}

void bt_timing_delivered(bt_input_timing *timing, const char *transport, unsigned handle,
                         int64_t now)
{
    /* Registration can submit neutral input before any live report. */
    if (!timing->latest_input_at)
        return;

    unsigned long delay =
        now >= timing->latest_input_at ? (unsigned long)(now - timing->latest_input_at) : 0;
    ++timing->delay_count;
    timing->delay_sum += delay;
    if (delay > timing->delay_max)
        timing->delay_max = delay;
    if (now - timing->timing_log_at < 5000)
        return;

    log_line("BT timing: %s handle %u window %ld ms; "
             "arrival n=%lu avg=%lu.%lu max=%lu ms "
             "bins[0-4,5-8,9-16,17-32,>32]=%lu,%lu,%lu,%lu,%lu; "
             "host-to-submit n=%lu avg=%lu.%lu max=%lu ms; coalesced=%lu",
             transport, handle, (long)(now - timing->timing_log_at), timing->interval_count,
             timing->interval_count ? timing->interval_sum / timing->interval_count : 0,
             timing->interval_count
                 ? (timing->interval_sum % timing->interval_count) * 10 / timing->interval_count
                 : 0,
             timing->interval_max, timing->interval_bins[0], timing->interval_bins[1],
             timing->interval_bins[2], timing->interval_bins[3], timing->interval_bins[4],
             timing->delay_count, timing->delay_sum / timing->delay_count,
             (timing->delay_sum % timing->delay_count) * 10 / timing->delay_count,
             timing->delay_max, timing->coalesced);

    timing->timing_log_at = now;
    timing->interval_count = timing->interval_sum = timing->interval_max = 0;
    memset(timing->interval_bins, 0, sizeof timing->interval_bins);
    timing->delay_count = timing->delay_sum = timing->delay_max = timing->coalesced = 0;
}
