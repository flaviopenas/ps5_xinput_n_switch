#define _DEFAULT_SOURCE
#include "usb_service.h"
#include "log.h"
#include <unistd.h>

int main(void)
{
    if (!log_open_port("/data/ps5-xinput", "/data/ps5-xinput/usb-only.log", 9023))
        return 1;
    if (!usb_service_init()) {
        log_close();
        return 1;
    }
    log_line("USB-only service ready; Bluetooth is owned by a separate payload");
    while (!log_stop_requested()) {
        log_poll();
        if (usb_service_poll())
            break;
        usleep(1000);
    }
    usb_service_close();
    log_line("Cleanup complete; payload exiting");
    log_close();
    return 0;
}
