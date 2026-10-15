/* Native /dev/bt query ABI captured from SceShellUI on firmware 4.03.
 * The default target sends only a device-list query. The separate discovery
 * target also tests captured native control requests; neither reads events,
 * writes a listener registration or requests a connection. */
#include "log.h"
#include <ps5/kernel.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

struct device_query {
    uint64_t filter;
    uint64_t records;
    uint32_t capacity;
    uint32_t reserved;
};
_Static_assert(sizeof(struct device_query) == 24, "native query size");
_Static_assert(offsetof(struct device_query, capacity) == 16, "native query capacity");

static void query_devices(int fd)
{
    unsigned char records[16384];
    memset(records, 0xa5, sizeof records);
    struct device_query query = {0, (uint64_t)(uintptr_t)records, 8, 0};
    errno = 0;
    int result = ioctl(fd, 0x80186201UL, &query);
    int error = errno;
    unsigned changed = 0;
    for (unsigned i = 0; i < sizeof records; ++i) if (records[i] != 0xa5) ++changed;
    log_line("BT native query: device-list result 0x%08X (%d), errno %d, output changed %u bytes",
             (unsigned)result, result, error, changed);
#ifdef BT_NATIVE_DISCOVERY
    if (result >= 0 && result <= 8 && !error) {
        for (int i = 0; i < result; ++i) {
            uint64_t identifier;
            memcpy(&identifier, records + i * 256, sizeof identifier);
            log_line("BT native query: record %d identifier 0x%016llX", i,
                     (unsigned long long)identifier);
        }
    }
#endif
}

int main(void)
{
    if (!log_open_port("/data/ps5-xinput", "/data/ps5-xinput/bt-native-query.log", 9024)) return 1;
    for (unsigned i = 0; i < 15; ++i) { log_poll(); usleep(100000); }
    log_line("BT native query: start; firmware 0x%08X", kernel_get_fw_version());
    if (kernel_get_fw_version() == 0x04030000) {
        int fd = open("/dev/bt", O_RDWR);
        log_line("BT native query: open /dev/bt fd %d errno %d", fd, fd < 0 ? errno : 0);
        if (fd >= 0) {
            query_devices(fd);
#ifdef BT_NATIVE_DISCOVERY
            /* Zero third argument matches the captured 4.03 shell wrappers.
             * The driver owns command/event queues throughout this test. */
            errno = 0;
            int start = ioctl(fd, 0x20046204UL, NULL);
            log_line("BT native discovery: start result 0x%08X errno %d", (unsigned)start, errno);
            if (start == 0) {
                notify("PS5 XInput: native discovery trial (30 seconds)");
                for (unsigned second = 0; second < 30; ++second) {
                    for (unsigned i = 0; i < 10; ++i) { log_poll(); usleep(100000); }
                    if ((second + 1) % 5 == 0) query_devices(fd);
                }
                errno = 0;
                int stop = ioctl(fd, 0x20046205UL, NULL);
                log_line("BT native discovery: stop result 0x%08X errno %d", (unsigned)stop, errno);
            }
#endif
            close(fd);
            log_line("BT native query: descriptor closed");
        }
    }
#ifdef BT_NATIVE_DISCOVERY
    log_line("BT native discovery: complete; no USB reads or pairing request");
#else
    log_line("BT native query: complete; no discovery, pairing or event consumption");
#endif
    notify("PS5 XInput: native Bluetooth query complete");
    for (unsigned i = 0; i < 15; ++i) { log_poll(); usleep(100000); }
    log_close(); return 0;
}
