/* Captured 4.03 HID registration ABI only. No radio reads or pairing.
 * A rejection checks the registration path, not the exact pairing branch. */
#include "log.h"
#include <ps5/kernel.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>

int main(void)
{
    if (!log_open_port("/data/ps5-xinput", "/data/ps5-xinput/bt-registration-probe.log", 9024)) return 1;
    for (unsigned i = 0; i < 15; ++i) { log_poll(); usleep(100000); }
    uint32_t firmware = kernel_get_fw_version();
    log_line("BT registration: FW 0x%08X; one HID registration request; no scan/connect", firmware);
    int fd = -1;
    if (firmware != 0x04030000 || log_stop_requested()) goto cleanup;
    fd = open("/dev/bt", O_RDWR | O_NONBLOCK);
    log_line("BT registration: native control open fd %d errno %d", fd, fd < 0 ? errno : 0);
    if (fd < 0) goto cleanup;
    /* This pair is the user's observed Series USB identity. It tests vendor
     * acceptance, without claiming it is the controller's BLE PnP identity.
     * Layout and requests are from the captured Register/Unregister wrappers.
     * No callback/event subscription is established; a missing client-context
     * result is inconclusive, and must not be classified as vendor rejection. */
    struct { uint16_t vendor, product; } identity = {0x045e, 0x0b12};
    _Static_assert(sizeof identity == 4, "captured HID registration size");
    errno = 0;
    int result = ioctl(fd, 0x8004622bUL, &identity);
    log_line("BT registration: Register 045E:0B12 result 0x%08X errno %d", (unsigned)result, errno);
    if (result == 0) {
        errno = 0;
        result = ioctl(fd, 0x8004622cUL, &identity);
        log_line("BT registration: Unregister result 0x%08X errno %d", (unsigned)result, errno);
    }
cleanup:
    if (fd >= 0) {
        errno = 0;
        int result = close(fd);
        log_line("BT registration: native control close result %d errno %d", result, errno);
    }
    log_line("BT registration: complete; no native pairing or input session created");
    notify("PS5 XInput: HID registration check complete");
    for (unsigned i = 0; i < 15; ++i) { log_poll(); usleep(100000); }
    log_line("Cleanup complete; payload exiting");
    log_close(); return 0;
}
