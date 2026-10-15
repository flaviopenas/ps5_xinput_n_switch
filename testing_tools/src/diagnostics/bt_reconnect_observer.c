/* Controlled shared-event competition test; BT_OBSERVER_ACL also opens ACL
 * queues for cleanup testing. No HCI commands, kernel inspection,
 * controller registration, or bond changes. Reading an
 * event consumes it; this is deliberately not a transparent packet tap. */
#include "bt_hci.h"
#include "log.h"
#include <stdint.h>
#include <time.h>

static unsigned counts[256];
static void observe(const unsigned char *packet, size_t length, void *context)
{
    (void)context;
    if (length < 2) return;
    ++counts[packet[0]];
    if (packet[0] == 4 && length == 12)
        log_line("Reconnect observer: classic connection request, link type %u", packet[11]);
    else if (packet[0] == 3 && length == 13)
        log_line("Reconnect observer: classic connection complete, status %02X", packet[2]);
    else if (packet[0] == 5 && length == 6)
        log_line("Reconnect observer: disconnect status %02X reason %02X", packet[2], packet[5]);
    else if (packet[0] == 0x0f && length == 6)
        log_line("Reconnect observer: native command status opcode %04X status %02X credits %u",
                 packet[4] | ((unsigned)packet[5] << 8), packet[2], packet[3]);
    else if (packet[0] == 0x0e && length >= 6)
        log_line("Reconnect observer: native command complete opcode %04X status %02X credits %u",
                 packet[3] | ((unsigned)packet[4] << 8), packet[5], packet[2]);
}

int main(void)
{
    if (!log_open_port("/data/ps5-xinput", "/data/ps5-xinput/reconnect-observer.log", 9025)) return 1;
#ifdef BT_OBSERVER_ACL
    log_line("Reconnect observer: event and ACL queues, no HCI commands; 120-second cleanup test");
#else
    log_line("Reconnect observer: single event read, no commands or ACL; 120-second test");
#endif
    if (!bt_hci_open()) { log_close(); return 1; }
#ifdef BT_OBSERVER_ACL
    if (!bt_hci_open_acl()) { bt_hci_close(); log_close(); return 1; }
#endif
    notify("PS5 XInput: reconnect observer ready (2 minutes)");
    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);
    do {
        if (bt_hci_poll(20, observe, NULL) < 0) break;
        clock_gettime(CLOCK_MONOTONIC, &now);
    } while (!log_stop_requested() && now.tv_sec - start.tv_sec < 120);
    bt_hci_close();
    for (unsigned i = 0; i < 256; ++i)
        if (counts[i]) log_line("Reconnect observer: event %02X count %u", i, counts[i]);
    log_line("Reconnect observer: complete; own transport closed");
    notify("PS5 XInput: reconnect observer stopped");
    log_close();
    return 0;
}
