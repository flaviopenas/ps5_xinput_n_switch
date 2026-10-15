#include "assignment.h"
#include "log.h"
#include "shell_bind.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Accept only explicitly identified virtual-pad DEVICE_ADDED events.
 * Subtype 22 follows the community MBus reference; it is not inferred from
 * arbitrary device IDs or from a successfully allocated input handle. */
uint64_t assignment_parse_event(const char *line)
{
    const char *sub, *id, *end;
    char *parsed;
    unsigned long subtype;
    unsigned long long device;
    if (!strstr(line, "DEVICE_ADDED") && !strstr(line, "device_added") &&
        !strstr(line, "DeviceAdded")) return 0;
    sub = strstr(line, "subType");
    if (!sub) sub = strstr(line, "subtype");
    if (!sub) sub = strstr(line, "SubType");
    if (!sub) return 0;
    sub += 7;
    while (*sub == ' ' || *sub == ':' || *sub == '=') ++sub;
    subtype = strtoul(sub, &parsed, 0);
    if (parsed == sub || subtype != 22 || isalnum((unsigned char)*parsed)) return 0;
    id = strstr(line, "DeviceId");
    if (!id) id = strstr(line, "deviceId");
    if (id) id += 8;
    else {
        id = strstr(line, "dev_id");
        if (id) id += 6;
    }
    if (!id) return 0;
    while (*id == ' ' || *id == ':' || *id == '=') ++id;
    end = id;
    /* Kernel reference prints IDs as hexadecimal, with or without 0x. */
    errno = 0;
    device = strtoull(end, &parsed, 16);
    if (parsed == end || errno || isalnum((unsigned char)*parsed)) return 0;
    return (uint64_t)device;
}

int assignment_begin(void)
{
    char discard[1024];
    int i, fd = open("/dev/klog", O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        log_line("Assignment: /dev/klog unavailable: errno %d (%s)", errno, strerror(errno));
        return -1;
    }
    /* Bound work and decline capture if the backlog cannot be drained. */
    for (i = 0; i < 128; ++i) {
        if (read(fd, discard, sizeof discard) <= 0) return fd;
    }
    log_line("Assignment: kernel log backlog too large; skipping automatic bind");
    close(fd);
    return -1;
}

void assignment_finish(int fd, uint32_t handle, uint32_t user)
{
    char buffer[512], line[1024];
    size_t used = 0;
    int overflow = 0, round, ambiguous = 0;
    uint64_t candidate = 0;
    if (fd < 0) return;
    /* Limit observation to two seconds. Only a unique matching device may
     * be bound; never enumerate or reassign existing physical controllers. */
    for (round = 0; round < 40; ++round) {
        ssize_t n = read(fd, buffer, sizeof buffer);
        ssize_t i;
        for (i = 0; i < n; ++i) {
            if (buffer[i] != '\n') {
                if (used < sizeof line - 1) line[used++] = buffer[i];
                else overflow = 1;
                continue;
            }
            line[used] = '\0';
            if (!overflow) {
                uint64_t device = assignment_parse_event(line);
                if (device) {
                    log_line("Assignment event: %s", line);
                    /* The ioctl returns a device identifier on the reference
                     * firmware. Require the observed event to agree with it. */
                    if ((uint32_t)device == handle) {
                        if (candidate && candidate != device) ambiguous = 1;
                        candidate = device;
                    }
                }
            }
            used = 0;
            overflow = 0;
        }
        log_poll();
        usleep(50000);
    }
    close(fd);
    if (!candidate || ambiguous) {
        log_line("Assignment: no unique subtype-22 DEVICE_ADDED matching handle 0x%08X; bind skipped", handle);
        return;
    }
    log_line("Assignment: matched device 0x%llX, requested user 0x%08X",
             (unsigned long long)candidate, user);
    if (shell_bind_device(candidate, user))
        log_line("Assignment: shell binding returned success; test input acceptance next");
    else
        log_line("Assignment: shell binding not confirmed; local MBus call remains disabled");
}
