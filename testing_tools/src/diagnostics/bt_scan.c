/* Discovery only, no bonding or connection. Event masks are deliberately not
 * changed: the system's current masks cannot be queried/restored with HCI. */
#include "log.h"
#include "bt_hci.h"
#include "bt_advert.h"
#include <stdint.h>
#include <string.h>
#include <time.h>

static bt_advert seen[64];
static unsigned count, reports, malformed, le_events;
static int64_t milliseconds(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static void advertisement(const bt_advert *report, void *unused)
{
    (void)unused;
    unsigned i;
    ++reports;
    for (i = 0; i < count; ++i)
        if (seen[i].address_type == report->address_type &&
            !memcmp(seen[i].address, report->address, 6)) break;
    if (i == count) {
        if (count == sizeof seen / sizeof seen[0]) return;
        seen[count++] = *report;
    } else {
        if ((!report->name[0] || !strcmp(seen[i].name, report->name)) &&
            (!report->hid_service || seen[i].hid_service) &&
            (!report->microsoft || seen[i].microsoft)) return;
        if (report->name[0]) memcpy(seen[i].name, report->name, sizeof report->name);
        seen[i].hid_service |= report->hid_service;
        seen[i].microsoft |= report->microsoft;
    }
    const bt_advert *r = &seen[i];
    log_line("BT scan: %02X:%02X:%02X:%02X:%02X:%02X type %u RSSI %d name [%s] HID %s Microsoft %s",
        r->address[5], r->address[4], r->address[3], r->address[2], r->address[1], r->address[0],
        r->address_type, report->rssi, r->name[0] ? r->name : "unknown",
        r->hid_service ? "yes" : "no", r->microsoft ? "yes" : "no");
}

static void event(const unsigned char *data, size_t size, void *context)
{
    if (data[0] == 0x3e) ++le_events;
    if (bt_advert_parse(data, size, advertisement, context) < 0) ++malformed;
}

static int command(unsigned opcode, const unsigned char *params, size_t size)
{
    unsigned char result[80];
    int n = bt_hci_command(opcode, params, size, result, sizeof result);
    int status = n > 0 ? result[0] : -1;
    log_line("BT scan: command %04X status %d", opcode, status);
    return status;
}

int main(void)
{
    /* Active scan, interval/window 100/50ms, public identity, no filter. */
    const unsigned char params[] = {1, 0xa0, 0, 0x50, 0, 0, 0};
    const unsigned char enable[] = {1, 0}, disable[] = {0, 0};
    int attempted = 0;
    if (!log_open_port("/data/ps5-xinput", "/data/ps5-xinput/bt-scan.log", 9024)) return 1;
    if (bt_hci_open()) {
        if (command(0x200b, params, sizeof params) == 0) {
            attempted = 1;
            int status = command(0x200c, enable, sizeof enable);
            /* A repeated enable can report Command Disallowed after its first
             * successful reply was consumed by the shared system reader. */
            if (status == 0 || status == 0x0c || status == -1) {
                log_line("BT scan: discovery window 30 seconds; no connection or pairing");
                notify("PS5 XInput: Bluetooth scan, 30 seconds");
                int64_t deadline = milliseconds() + 30000;
                while (milliseconds() < deadline && !log_stop_requested()) {
                    if (bt_hci_poll(20, event, NULL) < 0) break;
                }
            }
        }
        /* Cleanup even if an enable reply was lost. Only reached after our
         * scan-parameter command succeeded; never disable an existing scan
         * when Set Scan Parameters returns Command Disallowed. */
        if (attempted) {
            unsigned char result[80];
            int n = bt_hci_cleanup_command(0x200c, disable, sizeof disable, result, sizeof result);
            int status = n > 0 ? result[0] : -1;
            log_line("BT scan: scan-disable status %d", status);
            if (status != 0) log_line("BT scan: scan-disable not confirmed; status %d", status);
        }
    }
    bt_hci_close();
    log_line("BT scan: finished %u devices, %u reports, %u LE events, %u malformed",
             count, reports, le_events, malformed);
    notify("PS5 XInput: Bluetooth scan complete");
    log_close();
    return 0;
}
