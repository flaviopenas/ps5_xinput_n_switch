/* Standalone 4.03 native listener trial. Setup and 16-byte read size captured
 * from SceShellUI. Optional trace logs candidate fields derived offline;
 * their meanings remain unverified on 4.03. No USB calls.
 * Discovery and connection requests exist only in separate trial builds. */
#include "log.h"
#include <ps5/kernel.h>
#include <sys/types.h>
#include <sys/event.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

_Static_assert(sizeof(struct kevent) == 32, "captured native kevent size");
_Static_assert(offsetof(struct kevent, filter) == 8, "captured native filter offset");
_Static_assert(offsetof(struct kevent, flags) == 10, "captured native flags offset");

#if defined(BT_NATIVE_CONNECT_TRIAL) && (!defined(BT_NATIVE_EVENT_DISCOVERY) || !defined(BT_NATIVE_EVENT_TRACE))
#error Connection trial requires native discovery and event tracing
#endif
#if defined(BT_NATIVE_PAIR_TRIAL) && !defined(BT_NATIVE_CONNECT_TRIAL)
#error Pairing trial requires native connection trial
#endif

#ifdef BT_NATIVE_EVENT_DISCOVERY
static void query_devices(int fd)
{
    unsigned char records[8 * 256] = {0};
    struct {
        uint64_t filter, records;
        uint32_t capacity, reserved;
    } query = {0, (uint64_t)(uintptr_t)records, 8, 0};
    _Static_assert(sizeof query == 24, "captured native query size");
    errno = 0;
    int result = ioctl(fd, 0x80186201UL, &query);
    log_line("BT native events: device query result %d errno %d", result, errno);
    if (result < 0 || result > 8) return;
    for (int i = 0; i < result; ++i) {
        uint64_t identifier;
        memcpy(&identifier, records + i * 256, sizeof identifier);
        log_line("BT native events: record %d identifier 0x%016llX", i,
                 (unsigned long long)identifier);
    }
}
#endif

int main(void)
{
    if (!log_open_port("/data/ps5-xinput", "/data/ps5-xinput/bt-native-events.log", 9024)) return 1;
    for (unsigned i = 0; i < 15; ++i) { log_poll(); usleep(100000); }
    int fd = -1, queue = -1, registered = 0;
#ifdef BT_NATIVE_EVENT_DISCOVERY
    int discovery_started = 0;
#endif
#ifdef BT_NATIVE_CONNECT_TRIAL
    uint64_t requested_peer = 0;
    int connection_accepted = 0, connection_sent = 0;
#ifdef BT_NATIVE_PAIR_TRIAL
    int confirmation_sent = 0, confirmation_accepted = 0;
#endif
#endif
    unsigned events = 0;
    log_line("BT native events: start FW 0x%08X", kernel_get_fw_version());
    if (kernel_get_fw_version() != 0x04030000) goto cleanup;
    fd = open("/dev/bt", O_RDWR | O_NONBLOCK);
    log_line("BT native events: open fd %d errno %d", fd, fd < 0 ? errno : 0);
    if (fd < 0) goto cleanup;
    const uint64_t registration = 0xfe;
    errno = 0;
    ssize_t written = write(fd, &registration, sizeof registration);
    log_line("BT native events: registration write %lld errno %d", (long long)written, errno);
    if (written != sizeof registration) goto cleanup;
    queue = kqueue();
    if (queue < 0) { log_line("BT native events: kqueue failed errno %d", errno); goto cleanup; }
    struct kevent change;
    EV_SET(&change, fd, EVFILT_READ, EV_ADD, 0, 0, NULL);
    errno = 0;
    int result = kevent(queue, &change, 1, NULL, 0, NULL);
    log_line("BT native events: queue registration result %d errno %d", result, errno);
    if (result < 0) goto cleanup;
    registered = 1;
#ifdef BT_NATIVE_EVENT_DISCOVERY
    query_devices(fd);
    errno = 0;
    result = ioctl(fd, 0x20046204UL, NULL);
    log_line("BT native events: discovery control start %d errno %d", result, errno);
    if (result != 0) goto cleanup;
    discovery_started = 1;
#ifdef BT_NATIVE_CONNECT_TRIAL
    notify("PS5 XInput: native Xbox connection trial (2 minutes)");
    log_line("BT native events: ready; waiting for fresh Xbox advertisement; 120-second limit");
    time_t deadline = time(NULL) + 120;
#else
    notify("PS5 XInput: native discovery listener ready (30 seconds)");
    log_line("BT native events: ready; discovery control; no pairing; 30-second limit");
    time_t deadline = time(NULL) + 30;
#endif
#else
    notify("PS5 XInput: native listener ready (2 minutes)");
    log_line("BT native events: ready; no discovery or pairing; 120-second limit");
    time_t deadline = time(NULL) + 120;
#endif
    const struct timespec immediate = {0, 0};
    time_t next_status = time(NULL) + 10;
    while (time(NULL) < deadline && !log_stop_requested() && events < 256) {
        log_poll();
        struct kevent event;
        errno = 0;
        result = kevent(queue, NULL, 0, &event, 1, &immediate);
        if (result < 0) { log_line("BT native events: queue poll failed errno %d", errno); break; }
        if (result > 0) {
            if (event.flags & (EV_ERROR | EV_EOF)) {
                log_line("BT native events: queue flags 0x%X; stopping", event.flags); break;
            }
            unsigned char notification[16];
            errno = 0;
            ssize_t count = read(fd, notification, sizeof notification);
            if (count < 0 && (errno == EAGAIN || errno == EINTR)) continue;
            log_line("BT native events: notification %u read %lld bytes errno %d",
                     events + 1, (long long)count, count < 0 ? errno : 0);
            if (count != sizeof notification) break;
#ifdef BT_NATIVE_EVENT_TRACE
            /* Offline native read code copies byte fields at 0/1, a u32 at
             * 4, and a u64 at 8. Log unlabeled values, not assumed ABI names.
             * The notification buffer contains no full device/bond record. */
            uint32_t field4;
            uint64_t field8;
            memcpy(&field4, notification + 4, sizeof field4);
            memcpy(&field8, notification + 8, sizeof field8);
            log_line("BT native fields: event %u byte0 0x%02X byte1 0x%02X reserved 0x%02X%02X field4 0x%08X field8 0x%016llX",
                     events + 1, notification[0], notification[1], notification[3], notification[2],
                     field4, (unsigned long long)field8);
#ifdef BT_NATIVE_CONNECT_TRIAL
            /* This diagnostic is restricted to the controller identified in
             * the previous live trace. Preserve native transport/address bits
             * exactly as received; never connect a merely similar device. */
            if (!requested_peer && notification[0] == 1 &&
                (field8 & UINT64_C(0x0000ffffffffffff)) == UINT64_C(0xf46ad7bcb280) &&
                (field8 & UINT64_C(0x0001000000000000))) {
                requested_peer = field8;
                /* The previous live request returned 0x802F0204. Offline
                 * dispatch associates that with a native state precondition.
                 * Finish discovery before one connection attempt; this delay
                 * is a bounded trial, not proof the driver is ready. */
                errno = 0;
                int stopped = ioctl(fd, 0x20046205UL, NULL);
                log_line("BT native connect: stop discovery before request result 0x%08X errno %d",
                         (unsigned)stopped, errno);
                if (stopped != 0) break;
                discovery_started = 0;
                log_line("BT native connect: allowing two seconds for native discovery completion");
                for (unsigned settle = 0; settle < 20; ++settle) {
                    log_poll();
                    if (log_stop_requested()) break;
                    usleep(100000);
                }
                if (log_stop_requested()) break;
                errno = 0;
                connection_sent = 1;
                int rc = ioctl(fd, 0x8008620aUL, &requested_peer);
                int error = errno;
                connection_accepted = rc == 0;
                log_line("BT native connect: request peer 0x%016llX result 0x%08X errno %d",
                         (unsigned long long)requested_peer, (unsigned)rc, error);
                log_line("BT native connect: request acceptance does not confirm pairing or inputs");
                if (rc != 0) break;
            }
#ifdef BT_NATIVE_PAIR_TRIAL
            /* Captured 4.03 shell confirmation wrapper packs peer + u32 in
             * 16 bytes. Offline LE reply handler treats nonzero as accept.
             * Respond only to the exact requested peer's observed 4/4 event. */
            if (connection_accepted && !confirmation_sent && notification[0] == 4 &&
                notification[1] == 4 && field8 == requested_peer) {
                struct {
                    uint64_t peer;
                    uint32_t accept, reserved;
                } reply = {requested_peer, 1, 0};
                _Static_assert(sizeof reply == 16, "captured confirmation request size");
                confirmation_sent = 1;
                errno = 0;
                int rc = ioctl(fd, 0x80106208UL, &reply);
                confirmation_accepted = rc == 0;
                log_line("BT native pair: confirmation reply result 0x%08X errno %d", (unsigned)rc, errno);
                log_line("BT native pair: reply acceptance does not confirm a saved bond or inputs");
                if (rc != 0) break;
            }
#endif
#endif
#endif
            ++events;
        }
        if (time(NULL) >= next_status) {
            log_line("BT native events: listening; %u notifications", events);
#ifdef BT_NATIVE_EVENT_DISCOVERY
            query_devices(fd);
#endif
            next_status = time(NULL) + 10;
        }
        usleep(100000);
    }
cleanup:
#ifdef BT_NATIVE_CONNECT_TRIAL
    if (connection_accepted) {
        /* The captured 4.03 wrapper takes the same 64-bit peer. Offline
         * dispatch identifies this as record cleanup/pending cancellation.
         * Restrict cleanup to the peer requested by this diagnostic. */
        errno = 0;
        int rc = ioctl(fd, 0x80086202UL, &requested_peer);
        log_line("BT native connect: peer cleanup result 0x%08X errno %d", (unsigned)rc, errno);
    }
#endif
#ifdef BT_NATIVE_EVENT_DISCOVERY
    if (discovery_started) {
        errno = 0;
        int result = ioctl(fd, 0x20046205UL, NULL);
        log_line("BT native events: discovery control stop %d errno %d", result, errno);
        query_devices(fd);
    }
#endif
    if (registered) {
        struct kevent change;
        EV_SET(&change, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
        errno = 0;
        int result = kevent(queue, &change, 1, NULL, 0, NULL);
        log_line("BT native events: queue removal result %d errno %d", result, errno);
    }
    if (queue >= 0) {
        int result = close(queue);
        log_line("BT native events: queue close %d", result);
    }
    if (fd >= 0) {
        int result = close(fd);
        log_line("BT native events: device close %d", result);
    }
#ifdef BT_NATIVE_CONNECT_TRIAL
#ifdef BT_NATIVE_PAIR_TRIAL
    log_line("BT native pair: confirmation sent %d accepted %d", confirmation_sent, confirmation_accepted);
#endif
    log_line("BT native connect: complete; %u notifications; request sent %d accepted %d; no USB/input injection",
             events, connection_sent, connection_accepted);
#else
    log_line("BT native events: complete; %u notifications; no USB/pairing", events);
#endif
    log_line("Cleanup complete; payload exiting");
    notify("PS5 XInput: native listener test complete");
    for (unsigned i = 0; i < 15; ++i) { log_poll(); usleep(100000); }
    log_close();
    return 0;
}
