#ifndef PS5_XINPUT_BT_TIMING_H
#define PS5_XINPUT_BT_TIMING_H

#include <stdint.h>

/* One accumulator per link. Times are monotonic host milliseconds, not
 * physical-button timestamps. Zero-initialize on connection creation. */
typedef struct {
    int64_t last_input_at, latest_input_at, timing_log_at;
    unsigned long interval_count, interval_sum, interval_max;
    unsigned long interval_bins[5];
    unsigned long delay_count, delay_sum, delay_max, coalesced;
} bt_input_timing;

void bt_timing_input(bt_input_timing *timing, int64_t received_at, int64_t now, int input_pending);
void bt_timing_delivered(bt_input_timing *timing, const char *transport, unsigned handle,
                         int64_t now);

#endif
