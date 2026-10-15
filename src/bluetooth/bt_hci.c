/* Shared function-0 HCI event transport, never HCI Reset.
 * Shared-endpoint approach investigated using FGG-PlayPods hci.c, commit
 * 4b4ddb50e68a70af1cb914cff28bc4807a74e4bd (GPL-3.0). Pairing also opens ACL.
 * This probe must not be run alongside another payload using function 0. */
#include "log.h"
#include "bt_hci.h"

#define BT_HCI_FUNCTION 0
#include <sys/types.h>
#include <sys/ioctl.h>
#include <dev/usb/usb.h>
#include <dev/usb/usb_ioctl.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define HCI_EVENT_IN 0x82
#define HCI_ACL_IN 0x81
#define HCI_ACL_OUT 0x01

#ifndef BT_HCI_EVENT_READS
#define BT_HCI_EVENT_READS 96
#endif
#define READS BT_HCI_EVENT_READS
#define EVENT_SIZE 260
#ifndef BT_HCI_ACL_READS
#define BT_HCI_ACL_READS 24
#endif
#define ACL_READS BT_HCI_ACL_READS
#define OUT_INDEX (READS + ACL_READS)
#define COMMAND_INDEX (OUT_INDEX + 1)
#define TRANSFERS (OUT_INDEX + 1)
#define BUFFER_SIZE 1100
static int fd = -1, initialized, opened;
static int cleaning_up;
static int acl_opened, out_pending, out_opened;
static int command_opened, command_pending;
static struct usb_fs_endpoint endpoints[TRANSFERS];
static unsigned char buffers[TRANSFERS][BUFFER_SIZE];
static void *pointers[TRANSFERS][1];
static uint32_t lengths[TRANSFERS][1];
static bt_hci_event_fn other_event, acl_handler;
static void *handler_context;
static void (*control_tick)(void *);
static int (*control_cancelled)(void *);
static void *control_context;
static int lock_fd = -1;
void bt_hci_set_control(void (*tick)(void *), int (*cancelled)(void *), void *context)
{
    control_tick = tick;
    control_cancelled = cancelled;
    control_context = context;
}
static int stopping(void)
{
    return log_stop_requested() || (control_cancelled && control_cancelled(control_context));
}

void bt_hci_set_handlers(bt_hci_event_fn event, bt_hci_event_fn acl, void *context)
{
    other_event = event;
    acl_handler = acl;
    handler_context = context;
}

static unsigned word(const unsigned char *p)
{
    return p[0] | ((unsigned)p[1] << 8);
}
static int64_t milliseconds(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

void bt_hci_close(void)
{
    if (fd < 0) {
        if (lock_fd >= 0) {
            close(lock_fd);
            lock_fd = -1;
        }
        return;
    }
    if (initialized) {
        struct usb_fs_uninit uninit;
        log_line("BT radio: releasing shared transfers");
        /* Cancel the whole queue before unsetting any transfer. FreeBSD's
         * USB_FS_UNINIT drains transfers; leaving later shared reads queued
         * while an earlier read is drained can stall radio teardown.
         * Only indices opened by this transport are touched. */
        for (unsigned i = 0; i < TRANSFERS; ++i) {
            if (!((i < (unsigned)opened) || (i >= READS && i < READS + (unsigned)acl_opened) ||
                  (i == OUT_INDEX && out_opened) || (i == COMMAND_INDEX && command_opened)))
                continue;
            struct usb_fs_stop stop;
            memset(&stop, 0, sizeof stop);
            stop.ep_index = (uint8_t)i;
            if (ioctl(fd, USB_FS_STOP, &stop) != 0)
                log_line("BT radio: stop transfer %u errno %d", i, errno);
        }
        log_line("BT radio: own transfer queue canceled");
        for (int i = TRANSFERS - 1; i >= 0; --i) {
            if (!((i < opened) || (i >= READS && i < READS + acl_opened) ||
                  (i == OUT_INDEX && out_opened) || (i == COMMAND_INDEX && command_opened)))
                continue;
            struct usb_fs_close closed;
            if (i == COMMAND_INDEX || i == OUT_INDEX || i == READS + acl_opened - 1 ||
                i == opened - 1)
                log_line("BT radio: closing own endpoint group at index %d", i);
            memset(&closed, 0, sizeof closed);
            closed.ep_index = (uint8_t)i;
            if (ioctl(fd, USB_FS_CLOSE, &closed) != 0)
                log_line("BT radio: close transfer %d errno %d", i, errno);
        }
        log_line("BT radio: own endpoints closed");
        memset(&uninit, 0, sizeof uninit);
        if (ioctl(fd, USB_FS_UNINIT, &uninit) != 0)
            log_line("BT radio: transfer cleanup errno %d", errno);
    }
    close(fd);
    fd = -1;
    initialized = opened = acl_opened = out_pending = out_opened = 0;
    command_opened = command_pending = 0;
    if (lock_fd >= 0) {
        close(lock_fd);
        lock_fd = -1;
    }
    log_line("BT radio: shared transport closed");
}

static int validate_layout(void)
{
    struct usb_device_descriptor device;
    struct usb_gen_descriptor descriptor;
    unsigned char config[4096];
    int first = 0, endpoint = 0;
    memset(&descriptor, 0, sizeof descriptor);
    descriptor.ugd_data = config;
    descriptor.ugd_maxlen = sizeof config;
    if (ioctl(fd, USB_GET_DEVICE_DESC, &device) != 0 ||
        word((const unsigned char *)&device.idVendor) != 0x1286 ||
        word((const unsigned char *)&device.idProduct) != 0x2059 ||
        ioctl(fd, USB_GET_FULL_DESC, &descriptor) != 0 || descriptor.ugd_actlen > sizeof config)
        return 0;
    for (unsigned at = 0; at + 2 <= descriptor.ugd_actlen;) {
        unsigned size = config[at];
        if (size < 2 || size > descriptor.ugd_actlen - at)
            return 0;
        if (config[at + 1] == UDESC_INTERFACE && size >= 9)
            first = config[at + 2] == BT_HCI_FUNCTION && config[at + 3] == 0 &&
                    config[at + 5] == 0xe0 && config[at + 6] == 1 && config[at + 7] == 1;
        if (first && config[at + 1] == UDESC_ENDPOINT && size >= 7 &&
            config[at + 2] == HCI_EVENT_IN && (config[at + 3] & 3) == 3)
            endpoint = 1;
        at += size;
    }
    return endpoint;
}

static int start_read(unsigned index)
{
    struct usb_fs_start start;
    memset(&start, 0, sizeof start);
    lengths[index][0] = index < READS ? EVENT_SIZE : BUFFER_SIZE;
    endpoints[index].nFrames = 1;
    endpoints[index].aFrames = endpoints[index].status = 0;
    start.ep_index = (uint8_t)index;
    if (ioctl(fd, USB_FS_START, &start) != 0)
        return 0;
    return 1;
}

int bt_hci_open(void)
{
    struct usb_fs_init init;
    lock_fd = open("/data/ps5-xinput/bt-radio.lock", O_CREAT | O_RDWR | O_NOFOLLOW, 0600);
    if (lock_fd < 0 || flock(lock_fd, LOCK_EX | LOCK_NB) != 0) {
        log_line("BT radio: another Bluetooth payload owns the radio, or lock unavailable");
        bt_hci_close();
        return 0;
    }
    fd = open("/dev/ugen0.2", O_RDWR);
    if (fd < 0 || !validate_layout()) {
        log_line("BT radio: known function-0 layout unavailable, errno %d", errno);
        bt_hci_close();
        return 0;
    }
    for (unsigned i = 0; i < TRANSFERS; ++i) {
        pointers[i][0] = buffers[i];
        lengths[i][0] = EVENT_SIZE;
        endpoints[i].ppBuffer = pointers[i];
        endpoints[i].pLength = lengths[i];
        endpoints[i].nFrames = 1;
        endpoints[i].flags = USB_FS_FLAG_SINGLE_SHORT_OK;
    }
    endpoints[OUT_INDEX].flags = USB_FS_FLAG_FORCE_SHORT;
    memset(&init, 0, sizeof init);
    init.pEndpoints = endpoints;
    init.ep_index_max = TRANSFERS;
    if (ioctl(fd, USB_FS_INIT, &init) != 0) {
        log_line("BT radio: USB_FS_INIT errno %d", errno);
        bt_hci_close();
        return 0;
    }
    initialized = 1;
    for (unsigned i = 0; i < READS; ++i) {
        struct usb_fs_open op;
        memset(&op, 0, sizeof op);
        op.max_bufsize = EVENT_SIZE;
        op.max_frames = 1;
        op.ep_index = (uint8_t)i;
        op.ep_no = HCI_EVENT_IN;
        if (ioctl(fd, USB_FS_OPEN, &op) != 0)
            break;
        ++opened;
        if (!start_read(i)) {
            log_line("BT radio: event read start errno %d", errno);
            bt_hci_close();
            return 0;
        }
    }
    if (!opened) {
        log_line("BT radio: event endpoint unavailable errno %d", errno);
        bt_hci_close();
        return 0;
    }
    log_line("BT radio: function %d open with %d shared event reads", BT_HCI_FUNCTION, opened);
    return 1;
}

int bt_hci_send(unsigned opcode, const void *params, size_t length)
{
    unsigned char packet[258];
    struct usb_ctl_request request;
    if (length > 255 || (length && !params) || fd < 0)
        return 0;
    packet[0] = (unsigned char)opcode;
    packet[1] = (unsigned char)(opcode >> 8);
    packet[2] = (unsigned char)length;
    if (length)
        memcpy(packet + 3, params, length);
    memset(&request, 0, sizeof request);
    request.ucr_data = packet;
    request.ucr_request.bmRequestType = UT_WRITE_CLASS_DEVICE;
    USETW(request.ucr_request.wLength, 3 + length);
    if (ioctl(fd, USB_DO_REQUEST, &request) != 0) {
        log_line("BT radio: command %04X transport errno %d", opcode, errno);
        return 0;
    }
    return 1;
}

int bt_hci_poll(unsigned timeout_ms, bt_hci_event_fn handler, void *context)
{
    int64_t deadline = milliseconds() + timeout_ms;
    do {
        if (control_tick)
            control_tick(control_context);
        struct usb_fs_complete complete;
        memset(&complete, 0, sizeof complete);
        if (ioctl(fd, USB_FS_COMPLETE, &complete) != 0) {
            if (errno != EBUSY && errno != EAGAIN) {
                log_line("BT radio: completion errno %d", errno);
                return -1;
            }
            log_poll();
            if (stopping() && !cleaning_up)
                return 0;
            usleep(1000);
            continue;
        }
        unsigned index = complete.ep_index;
        if (index == OUT_INDEX && out_opened) {
            out_pending = 0;
            if (endpoints[index].status) {
                log_line("BT radio: ACL OUT status %u", endpoints[index].status);
                return -1;
            }
            return 1;
        }
        if (index >= TRANSFERS || (index < READS && index >= (unsigned)opened) ||
            (index >= READS && index >= READS + (unsigned)acl_opened))
            return -1;
        size_t received = lengths[index][0];
        size_t capacity = index < READS ? EVENT_SIZE : BUFFER_SIZE;
        size_t header = index < READS ? 2 : 4;
        if (endpoints[index].status == 0 && endpoints[index].aFrames && received <= capacity) {
            for (size_t at = 0; at + header <= received;) {
                const unsigned char *event = buffers[index] + at;
                size_t size = index < READS ? 2 + event[1] : 4 + word(event + 2);
                if (size > received - at)
                    break;
                if (index < READS) {
                    if (handler)
                        handler(event, size, context);
                    else if (other_event)
                        other_event(event, size, handler_context);
                } else if (acl_handler)
                    acl_handler(event, size, handler_context);
                at += size;
            }
        }
        if (!start_read(index)) {
            log_line("BT radio: restart event read errno %d", errno);
            return -1;
        }
        return 1;
    } while (milliseconds() < deadline);
    return 0;
}

struct command_reply {
    unsigned opcode;
    unsigned char *result;
    size_t capacity;
    int count;
};

static void command_event(const unsigned char *event, size_t size, void *context)
{
    struct command_reply *reply = context;
    if (event[0] != 0x0e || size < 6 || word(event + 3) != reply->opcode) {
        if (other_event)
            other_event(event, size, handler_context);
        return;
    }
    size_t count = size - 5;
    if (count > reply->capacity)
        return;
    memcpy(reply->result, event + 5, count);
    reply->count = (int)count;
}

int bt_hci_command(unsigned opcode, const void *params, size_t length, unsigned char *result,
                   size_t capacity)
{
    struct command_reply reply = {opcode, result, capacity, -1};
    for (unsigned attempt = 0; attempt < 4 && (!stopping() || cleaning_up); ++attempt) {
        if (!bt_hci_send(opcode, params, length))
            return -1;
        int64_t deadline = milliseconds() + 700;
        while (milliseconds() < deadline && (!stopping() || cleaning_up)) {
            if (bt_hci_poll(10, command_event, &reply) < 0)
                return -1;
            if (reply.count >= 0)
                return reply.count;
        }
        log_line("BT radio: command %04X response missed, attempt %u/4", opcode, attempt + 1);
    }
    return -1;
}

int bt_hci_open_acl(void)
{
    if (fd < 0 || acl_opened || out_opened)
        return 0;
    struct usb_fs_open op;
    memset(&op, 0, sizeof op);
    op.max_bufsize = BUFFER_SIZE;
    op.max_frames = 1;
    op.ep_index = OUT_INDEX;
    op.ep_no = HCI_ACL_OUT;
    if (ioctl(fd, USB_FS_OPEN, &op) != 0)
        return 0;
    out_opened = 1;
    for (unsigned i = 0; i < ACL_READS; ++i) {
        memset(&op, 0, sizeof op);
        op.max_bufsize = BUFFER_SIZE;
        op.max_frames = 1;
        op.ep_index = (uint8_t)(READS + i);
        op.ep_no = HCI_ACL_IN;
        if (ioctl(fd, USB_FS_OPEN, &op) != 0)
            break;
        ++acl_opened;
        if (!start_read(READS + i))
            return 0;
    }
    log_line("BT radio: ACL endpoints open, %d shared reads", acl_opened);
    return acl_opened > 0;
}

int bt_hci_acl_send(const unsigned char *packet, size_t length)
{
    int64_t deadline = milliseconds() + 1000;
    if (!out_opened || length < 4 || length > BUFFER_SIZE)
        return 0;
    while (out_pending) {
        if (milliseconds() >= deadline || bt_hci_poll(10, NULL, NULL) < 0)
            return 0;
    }
    memcpy(buffers[OUT_INDEX], packet, length);
    lengths[OUT_INDEX][0] = (uint32_t)length;
    endpoints[OUT_INDEX].nFrames = 1;
    endpoints[OUT_INDEX].aFrames = endpoints[OUT_INDEX].status = 0;
    struct usb_fs_start start;
    memset(&start, 0, sizeof start);
    start.ep_index = OUT_INDEX;
    if (ioctl(fd, USB_FS_START, &start) != 0)
        return 0;
    out_pending = 1;
    return 1;
}

int bt_hci_cleanup_command(unsigned opcode, const void *params, size_t length,
                           unsigned char *result, size_t capacity)
{
    /* A PC STOP request ends scanning but must not skip Scan Disable. */
    cleaning_up = 1;
    int count = bt_hci_command(opcode, params, length, result, capacity);
    cleaning_up = 0;
    return count;
}

int bt_hci_cleanup_poll(unsigned timeout_ms)
{
    cleaning_up = 1;
    int result = bt_hci_poll(timeout_ms, NULL, NULL);
    cleaning_up = 0;
    return result;
}
