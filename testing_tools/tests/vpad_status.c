#include "vpad.h"
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

static int injected_error;
static int raw_error;
void log_line(const char *fmt, ...) { (void)fmt; }
int __wrap_open(const char *path, int flags, ...)
{
    (void)path; (void)flags;
    return 42;
}
int __wrap_close(int fd) { assert(fd == 42); return 0; }
int __wrap_ioctl(int fd, unsigned long request, ...)
{
    va_list args;
    void *data;
    assert(fd == 42);
    va_start(args, request);
    data = va_arg(args, void *);
    va_end(args);
    if (request == 0xC018482AUL) {
        struct req { uint32_t cmd, reserved; void *data, *out; };
        *(int32_t *)((struct req *)data)->out = 7;
        return 0;
    }
    if (request == 0x8010482CUL) {
        struct insert { uint32_t handle, reserved; const void *data; };
        struct insert *in = data;
        assert(sizeof *in == 16 && in->handle == 7 && !in->reserved && in->data);
        if (injected_error) {
            if (raw_error) return injected_error;
            errno = injected_error;
            return -1;
        }
        return 0;
    }
    assert(request == 0x80104850UL);
    return 0;
}
int main(void)
{
    char why[100];
    unsigned char report[VPAD_REPORT_SIZE] = {0};
    vpad *pad = vpad_open(1, why, sizeof why);
    assert(pad && vpad_handle(pad) == 7);
    injected_error = EBUSY;
    assert(vpad_submit(pad, report) == -1);
    injected_error = EINVAL;
    assert(vpad_submit(pad, report) == 0);
    raw_error = 1;
    injected_error = EBUSY;
    errno = EAGAIN;
    assert(vpad_submit(pad, report) == -1 && errno == EBUSY);
    injected_error = EINVAL;
    errno = EBUSY;
    assert(vpad_submit(pad, report) == 0 && errno == EINVAL);
    injected_error = 0;
    assert(vpad_submit(pad, report) == 1);
    vpad_close(pad);
    puts("Submission status passed: busy, fatal failure, then accepted input.");
    return 0;
}
