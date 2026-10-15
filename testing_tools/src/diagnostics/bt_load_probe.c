/* Isolate module loading from export inspection and radio access. */
#include "log.h"
#include <dlfcn.h>
#include <unistd.h>

int main(void)
{
    if (!log_open_port("/data/ps5-xinput", "/data/ps5-xinput/bt-load-probe.log", 9024))
        return 1;
    for (unsigned i = 0; i < 15; ++i) { log_poll(); usleep(100000); }
    log_line("BT load: starting load-only test; no export inspection or radio access");
    void *module = dlopen("libSceBluetoothHid.sprx", RTLD_NOW);
    if (!module) {
        log_line("BT load: module load failed: %s", dlerror());
        log_close(); return 1;
    }
    log_line("BT load: module loaded; holding for 120 seconds");
    notify("PS5 XInput: HID library load-only test ready");
    for (unsigned i = 0; i < 1200 && !log_stop_requested(); ++i) {
        log_poll(); usleep(100000);
    }
    /* Match inspection probes: module remains loaded until process exit. */
    log_line("BT load: test complete; payload exiting");
    log_close();
    return 0;
}
