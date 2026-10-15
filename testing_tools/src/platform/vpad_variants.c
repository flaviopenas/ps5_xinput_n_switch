#include "vpad.h"
#include "gip.h"
#include "log.h"
#include "assignment.h"

#include <sys/ioctl.h>
#include <sys/types.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The three ioctls libScePad issues on /dev/hid, with the command word each
 * expects in the first field of its request. */
#define HID_ADD_DEVICE 0xC018482AUL /* _IOWR('H', 0x2A, 24) */
#ifdef VPAD_FW_1360
#define HID_INSERT_DATA 0x8018482CUL /* Supplied 13.60 module: 24-byte request. */
#define VPAD_ABI_NAME "13.60 test"
#else
#define HID_INSERT_DATA 0x8010482CUL /* FW 4.03: _IOW('H', 0x2C, 16) */
#define VPAD_ABI_NAME "4.03"
#endif
#define HID_DEL_DEVICE 0x80104850UL /* _IOW ('H', 0x50, 16) */

#define HID_CMD_ADD 3
#define HID_CMD_DEL 5

/* Device descriptor passed to AddDevice. The protocol and class bytes are the
 * pair the library writes for a standard pad; the user id says who owns it. */
#define VDEV_DESC_SIZE 168
#define VDEV_PROTO_OFF 0x00
#define VDEV_USER_OFF 0x1C
#define VDEV_CLASS_OFF 0x20
#define VDEV_PROTO 3
#define VDEV_CLASS 1

/* Field offsets within the report the kernel reads. Every one of these is a
 * position libScePad copies a ScePadData field to, not a ScePadData offset. */
#define VP_BUTTONS 0x0C /* uint32 */
#define VP_STICKS 0x10  /* lx, ly, rx, ry */
#define VP_L2 0x14
#define VP_R2 0x15
#define VP_ACTIVE 0x17 /* the library hardcodes 1 here */
#ifdef VPAD_FW_1360
#define VP_MOTION 0x78 /* Supplied 13.60 translation: eight floats. */
#else
#define VP_MOTION 0x80 /* FW 4.03: eight floats */
#endif

/* PlayStation button bits. */
#define SCE_L3 0x000002
#define SCE_R3 0x000004
#define SCE_OPTIONS 0x000008
#define SCE_UP 0x000010
#define SCE_RIGHT 0x000020
#define SCE_DOWN 0x000040
#define SCE_LEFT 0x000080
#define SCE_L2 0x000100
#define SCE_R2 0x000200
#define SCE_L1 0x000400
#define SCE_R1 0x000800
#define SCE_TRIANGLE 0x001000
#define SCE_CIRCLE 0x002000
#define SCE_CROSS 0x004000
#define SCE_SQUARE 0x008000
#define SCE_PS 0x010000
#define SCE_TOUCHPAD 0x100000

/* Trigger travel past which the digital bit is also reported. */
#define TRIGGER_DIGITAL 128

/* The 24-byte AddDevice request. */
typedef struct {
    uint32_t cmd;
    uint32_t reserved;
    void *data;
    void *out;
} hid_req;

typedef struct {
    uint32_t handle;
    uint32_t reserved;
    const void *data;
#ifdef VPAD_FW_1360
    uint8_t flags;
    uint8_t padding[7];
#endif
} hid_insert;

_Static_assert(sizeof(hid_req) == 24, "AddDevice ABI");
#ifdef VPAD_FW_1360
_Static_assert(sizeof(hid_insert) == 24, "Supplied 13.60 InsertData ABI");
#else
_Static_assert(sizeof(hid_insert) == 16, "FW 4.03 InsertData ABI");
#endif
_Static_assert(VPAD_REPORT_SIZE == 168, "Translated report buffer ABI");

static int hid_ioctl(int fd, unsigned long operation, void *data)
{
    int result;
    errno = 0;
    result = ioctl(fd, operation, data);
    /* These operations return zero. Accommodate raw positive errno returns
     * without confusing an unrelated USB errno for this ioctl's result. */
    if (result > 0)
        errno = result;
    else if (result < 0 && errno == 0)
        errno = EIO;
    return result;
}

/* The 16-byte request DeleteDevice takes instead. */
typedef struct {
    uint8_t cmd;
    uint8_t pad[7];
    uint32_t handle;
    uint32_t pad2;
} hid_del;

struct vpad {
    int fd;
    int32_t handle;
};

vpad *vpad_open(uint32_t user, char *why, int whylen)
{
    unsigned char desc[VDEV_DESC_SIZE];
    struct vpad *pad = calloc(1, sizeof *pad);
    hid_req req;
#if XINPUT_VERBOSE && defined(ENABLE_EXPERIMENTAL_SHELL_BIND)
    int event_fd;
#endif

    if (!pad) {
        snprintf(why, (size_t)whylen, "no memory for virtual controller");
        return NULL;
    }
    log_line("Virtual pad ABI: %s; InsertData %08lX, request %lu bytes, motion offset %02X",
             VPAD_ABI_NAME, HID_INSERT_DATA, (unsigned long)sizeof(hid_insert), VP_MOTION);
    pad->fd = open("/dev/hid", O_RDWR);
    if (pad->fd < 0) {
        snprintf(why, (size_t)whylen, "/dev/hid: %s", strerror(errno));
        free(pad);
        return NULL;
    }

    memset(desc, 0, sizeof desc);
    desc[VDEV_PROTO_OFF] = VDEV_PROTO;
    memcpy(desc + VDEV_USER_OFF, &user, sizeof user);
    desc[VDEV_CLASS_OFF] = VDEV_CLASS;

    pad->handle = -1;

    memset(&req, 0, sizeof req);
    req.cmd = HID_CMD_ADD;
    req.data = desc;
    req.out = &pad->handle;

#if XINPUT_VERBOSE && defined(ENABLE_EXPERIMENTAL_SHELL_BIND)
    /* The legacy observer pauses USB polling for two seconds. Keep it
     * exclusive to the explicitly enabled shell-binding experiment. */
    event_fd = assignment_begin();
#endif
    if (hid_ioctl(pad->fd, HID_ADD_DEVICE, &req) != 0) {
#if XINPUT_VERBOSE && defined(ENABLE_EXPERIMENTAL_SHELL_BIND)
        if (event_fd >= 0)
            close(event_fd);
#endif
        snprintf(why, (size_t)whylen, "AddDevice: %s", strerror(errno));
        close(pad->fd);
        pad->fd = -1;
        free(pad);
        return NULL;
    }

#if XINPUT_VERBOSE && defined(ENABLE_EXPERIMENTAL_SHELL_BIND)
    assignment_finish(event_fd, (uint32_t)pad->handle, user);
#endif
    snprintf(why, (size_t)whylen, "handle %d", pad->handle);
    return pad;
}

int vpad_submit(vpad *pad, const unsigned char *report)
{
    hid_insert req;
    int result;

    memset(&req, 0, sizeof req);
    req.handle = (uint32_t)pad->handle;
    req.data = report;

    result = hid_ioctl(pad->fd, HID_INSERT_DATA, &req);
    if (result == 0)
        return 1;
    if (errno == EBUSY)
        return -1;
    log_line("InsertData handle %d: rc %d, errno %d (%s)", pad->handle, result, errno,
             strerror(errno));
    return 0;
}

void vpad_close(vpad *pad)
{
    hid_del del;

    if (!pad || pad->fd < 0)
        return;

    memset(&del, 0, sizeof del);
    del.cmd = HID_CMD_DEL;
    del.handle = (uint32_t)pad->handle;
    if (hid_ioctl(pad->fd, HID_DEL_DEVICE, &del) != 0)
        log_line("DeleteDevice handle %d: errno %d (%s)", pad->handle, errno, strerror(errno));

    close(pad->fd);
    free(pad);
}

int vpad_handle(const vpad *pad)
{
    return pad->handle;
}

void vpad_input_init(vpad_input *input)
{
    const unsigned char neutral[18] = {GIP_LL_INPUT_REPORT, 0, 0, 14};
    input->guide = 0;
    vpad_translate(neutral, input->report);
}

int vpad_normalize_usb360(const unsigned char *msg, int length, unsigned char normalized[18],
                          int *guide)
{
    unsigned buttons = 0, lt, rt;
    if (length < 20 || msg[0] != 0 || msg[1] != 20)
        return 0;
    if (msg[2] & 0x01)
        buttons |= GIP_UP;
    if (msg[2] & 0x02)
        buttons |= GIP_DOWN;
    if (msg[2] & 0x04)
        buttons |= GIP_LEFT;
    if (msg[2] & 0x08)
        buttons |= GIP_RIGHT;
    if (msg[2] & 0x10)
        buttons |= GIP_MENU;
    if (msg[2] & 0x20)
        buttons |= GIP_VIEW;
    if (msg[2] & 0x40)
        buttons |= GIP_LS;
    if (msg[2] & 0x80)
        buttons |= GIP_RS;
    if (msg[3] & 0x01)
        buttons |= GIP_LB;
    if (msg[3] & 0x02)
        buttons |= GIP_RB;
    if (msg[3] & 0x10)
        buttons |= GIP_A;
    if (msg[3] & 0x20)
        buttons |= GIP_B;
    if (msg[3] & 0x40)
        buttons |= GIP_X;
    if (msg[3] & 0x80)
        buttons |= GIP_Y;
    *guide = (msg[3] & 0x04) != 0;
    memset(normalized, 0, 18);
    normalized[0] = GIP_LL_INPUT_REPORT;
    normalized[3] = 14;
    normalized[4] = (unsigned char)buttons;
    normalized[5] = (unsigned char)(buttons >> 8);
    lt = (unsigned)msg[4] << 2;
    rt = (unsigned)msg[5] << 2;
    normalized[6] = (unsigned char)lt;
    normalized[7] = (unsigned char)(lt >> 8);
    normalized[8] = (unsigned char)rt;
    normalized[9] = (unsigned char)(rt >> 8);
    memcpy(normalized + 10, msg + 6, 8);
    return 1;
}

int vpad_input_update(vpad_input *input, const unsigned char *msg, int length)
{
    uint32_t buttons;
    if (length < 4)
        return 0;
    if (msg[0] == GIP_LL_INPUT_REPORT && length >= 18 && msg[3] >= 14) {
        vpad_translate(msg, input->report);
    } else if (msg[0] == GIP_CMD_GUIDE_BUTTON && length >= 5 && msg[3] >= 1) {
        input->guide = (msg[4] & 3) != 0;
    } else {
        return 0;
    }
    memcpy(&buttons, input->report + VP_BUTTONS, sizeof buttons);
    buttons = (buttons & ~SCE_PS) | (input->guide ? SCE_PS : 0);
    memcpy(input->report + VP_BUTTONS, &buttons, sizeof buttons);
    return 1;
}

/* ---- translation ------------------------------------------------------- */

static int16_t le16(const unsigned char *p)
{
    return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

/* GIP sticks are signed 16-bit with Y increasing upwards; PlayStation sticks
 * are unsigned 8-bit with Y increasing downwards, so Y is inverted here rather
 * than left for every game to be confused by. */
static uint8_t axis(int16_t v)
{
    return (uint8_t)(((int)v + 32768) >> 8);
}
static uint8_t axis_inv(int16_t v)
{
    return (uint8_t)((32767 - (int)v) >> 8);
}

void vpad_translate(const unsigned char *gip_report, unsigned char *report)
{
    const unsigned char *d = gip_report + 4; /* past the message header */
    unsigned m = (unsigned)d[0] | ((unsigned)d[1] << 8);
    unsigned lt = (unsigned)d[2] | ((unsigned)d[3] << 8);
    unsigned rt = (unsigned)d[4] | ((unsigned)d[5] << 8);
    uint32_t b = 0;
    float ident = 1.0f;

    memset(report, 0, VPAD_REPORT_SIZE);

    if (m & GIP_A)
        b |= SCE_CROSS;
    if (m & GIP_B)
        b |= SCE_CIRCLE;
    if (m & GIP_X)
        b |= SCE_SQUARE;
    if (m & GIP_Y)
        b |= SCE_TRIANGLE;
    if (m & GIP_UP)
        b |= SCE_UP;
    if (m & GIP_DOWN)
        b |= SCE_DOWN;
    if (m & GIP_LEFT)
        b |= SCE_LEFT;
    if (m & GIP_RIGHT)
        b |= SCE_RIGHT;
    if (m & GIP_LB)
        b |= SCE_L1;
    if (m & GIP_RB)
        b |= SCE_R1;
    if (m & GIP_LS)
        b |= SCE_L3;
    if (m & GIP_RS)
        b |= SCE_R3;
    if (m & GIP_MENU)
        b |= SCE_OPTIONS;
    if (m & GIP_VIEW)
        b |= SCE_TOUCHPAD;

    /* The triggers are analogue on both sides, but PlayStation also expects a
     * digital bit once they are meaningfully pressed. */
    if (lt > TRIGGER_DIGITAL)
        b |= SCE_L2;
    if (rt > TRIGGER_DIGITAL)
        b |= SCE_R2;

    memcpy(report + VP_BUTTONS, &b, sizeof b);

    report[VP_STICKS + 0] = axis(le16(d + 6));
    report[VP_STICKS + 1] = axis_inv(le16(d + 8));
    report[VP_STICKS + 2] = axis(le16(d + 10));
    report[VP_STICKS + 3] = axis_inv(le16(d + 12));

    report[VP_L2] = (uint8_t)(lt >> 2);
    report[VP_R2] = (uint8_t)(rt >> 2);
    report[VP_ACTIVE] = 1;

    /* Identity quaternion, so the motion block is at least well formed. */
    memcpy(report + VP_MOTION + 12, &ident, sizeof ident);
}
