/* Bounded controller transport test, independent of virtual-pad registration. */
#include "gip.h"
#include "log.h"
#include <time.h>
#include <unistd.h>

int main(void)
{
    char why[160];
    const unsigned char *report;
    gip_device *dev = NULL;
    unsigned received = 0;
    if (!log_open_port("/data/ps5-xinput", "/data/ps5-xinput/usb-transport.log", 9025)) return 1;
    log_line("USB transport probe v1: 30 seconds, no virtual pad; UDP 9025");
    notify("PS5 XInput: USB transport test (30 seconds)");
    time_t deadline = time(NULL) + 30;
    while (!dev && time(NULL) < deadline && !log_stop_requested()) {
        dev = gip_open(why, sizeof why);
        log_poll();
        if (!dev) usleep(500000);
    }
    if (dev) {
        log_line("USB transport: %s %04X:%04X %s", gip_node(dev), gip_vendor(dev), gip_product(dev), why);
        deadline = time(NULL) + 30;
        while (time(NULL) < deadline && !log_stop_requested()) {
            int n = gip_read(dev, &report);
            if (n > 0) {
                ++received;
                if (received <= 8)
                    log_line("USB transport report %u: length %d, id %02X", received, n, report[0]);
            }
            if (n == -2) break;
            log_poll();
            usleep(1000);
        }
        gip_close(dev);
    } else log_line("USB transport: %s", why);
    log_line("USB transport complete: %u reports; controller handle closed", received);
    log_close();
    return received ? 0 : 1;
}
