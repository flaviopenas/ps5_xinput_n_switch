/* Shared function-0 HCI event transport, never HCI Reset.
 * Shared-endpoint approach investigated using FGG-PlayPods hci.c, commit
 * 4b4ddb50e68a70af1cb914cff28bc4807a74e4bd (GPL-3.0). Pairing also opens ACL.
 * This probe must not be run alongside another payload using function 0. */
#include "log.h"
#include "bt_hci.h"
#ifdef BT_HCI_DRAIN_CANCELLED
#include "bt_cancel_drain.h"
#endif
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

#ifndef BT_HCI_FUNCTION
#define BT_HCI_FUNCTION 0
#endif
#if defined(BT_HCI_BULK_COMMAND_PROBE) && !defined(BT_HCI_BULK_COMMAND)
#define BT_HCI_BULK_COMMAND 1
#endif
#if BT_HCI_FUNCTION == 2
#define HCI_EVENT_IN 0x85
#define HCI_ACL_IN 0x84
#define HCI_ACL_OUT 0x04
#else
#define HCI_EVENT_IN 0x82
#define HCI_ACL_IN 0x81
#define HCI_ACL_OUT 0x01
#endif

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
#ifdef BT_HCI_BULK_COMMAND
#define TRANSFERS (COMMAND_INDEX + 1)
#else
#define TRANSFERS (OUT_INDEX + 1)
#endif
#define BUFFER_SIZE 1100
static int fd = -1, initialized, opened;
static int cleaning_up;
static int acl_opened, out_pending, out_opened;
static int command_opened, command_pending;
#ifdef BT_HCI_DRAIN_CANCELLED
static unsigned char pending_slots[TRANSFERS];
#endif
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

#ifdef BT_HCI_DRAIN_CANCELLED
static int cancel_next(void *unused, unsigned *index)
{
    (void)unused;
    struct usb_fs_complete complete = {0};
    if (ioctl(fd, USB_FS_COMPLETE, &complete) == 0) {
        *index = complete.ep_index;
        return 1;
    }
    return errno == EBUSY || errno == EAGAIN ? 0 : -1;
}
static int64_t cancel_now(void *unused)
{
    (void)unused;
    return milliseconds();
}
static void cancel_idle(void *unused)
{
    (void)unused;
    log_poll();
    usleep(1000);
}
#endif

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
#ifdef BT_HCI_DRAIN_CANCELLED
        unsigned remaining;
        const bt_cancel_ops drain = {cancel_next, cancel_now, cancel_idle, NULL};
        int drained = bt_cancel_drain(pending_slots, TRANSFERS, 1500, &drain, &remaining);
        log_line("BT radio: cancellation drain result %d, pending slots %u", drained, remaining);
#endif
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
#ifdef BT_HCI_DRAIN_CANCELLED
    memset(pending_slots, 0, sizeof pending_slots);
#endif
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
#ifdef BT_HCI_DRAIN_CANCELLED
    pending_slots[index] = 1;
#endif
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
#ifdef BT_HCI_BULK_COMMAND
    endpoints[COMMAND_INDEX].flags = USB_FS_FLAG_FORCE_SHORT;
#endif
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
#ifndef BT_HCI_BULK_COMMAND
    struct usb_ctl_request request;
#endif
    if (length > 255 || (length && !params) || fd < 0)
        return 0;
    packet[0] = (unsigned char)opcode;
    packet[1] = (unsigned char)(opcode >> 8);
    packet[2] = (unsigned char)length;
    if (length)
        memcpy(packet + 3, params, length);
#ifdef BT_HCI_BULK_COMMAND
    /* Keep commands separate from ACL OUT. The native btts driver uses
     * dedicated command bulk OUT endpoints 02/05 on this Marvell chip. */
    if (!command_opened) {
        struct usb_fs_open opened_out;
        memset(&opened_out, 0, sizeof opened_out);
        opened_out.max_bufsize = BUFFER_SIZE;
        opened_out.max_frames = 1;
        opened_out.ep_index = COMMAND_INDEX;
        opened_out.ep_no = BT_HCI_FUNCTION == 2 ? 0x05 : 0x02;
        if (ioctl(fd, USB_FS_OPEN, &opened_out) != 0) {
            log_line("BT radio: dedicated command endpoint unavailable errno %d", errno);
            return 0;
        }
        command_opened = 1;
        log_line("BT radio: dedicated bulk command endpoint %02X open", opened_out.ep_no);
    }
    int64_t deadline = milliseconds() + 1000;
    while (command_pending) {
        if (milliseconds() >= deadline || bt_hci_poll(10, NULL, NULL) < 0)
            return 0;
    }
    memcpy(buffers[COMMAND_INDEX], packet, length + 3);
    lengths[COMMAND_INDEX][0] = (uint32_t)(length + 3);
    endpoints[COMMAND_INDEX].nFrames = 1;
    endpoints[COMMAND_INDEX].aFrames = endpoints[COMMAND_INDEX].status = 0;
    struct usb_fs_start start;
    memset(&start, 0, sizeof start);
    start.ep_index = COMMAND_INDEX;
    if (ioctl(fd, USB_FS_START, &start) != 0)
        return 0;
    command_pending = 1;
#ifdef BT_HCI_DRAIN_CANCELLED
    pending_slots[COMMAND_INDEX] = 1;
#endif
    return 1;
#else
    memset(&request, 0, sizeof request);
    request.ucr_data = packet;
    request.ucr_request.bmRequestType = UT_WRITE_CLASS_DEVICE;
#if BT_HCI_FUNCTION == 2
    request.ucr_request.bmRequestType = UT_WRITE_CLASS_INTERFACE;
    USETW(request.ucr_request.wIndex, BT_HCI_FUNCTION);
#endif
    USETW(request.ucr_request.wLength, 3 + length);
    if (ioctl(fd, USB_DO_REQUEST, &request) != 0) {
        log_line("BT radio: command %04X transport errno %d", opcode, errno);
        return 0;
    }
    return 1;
#endif
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
#ifdef BT_HCI_DRAIN_CANCELLED
        if (index < TRANSFERS)
            pending_slots[index] = 0;
#endif
#ifdef BT_HCI_BULK_COMMAND
        if (index == COMMAND_INDEX && command_opened) {
            command_pending = 0;
            if (endpoints[index].status) {
                log_line("BT radio: command OUT status %u", endpoints[index].status);
                return -1;
            }
            return 1;
        }
#endif
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
#ifdef BT_HCI_BULK_COMMAND_PROBE
    /* Its OUT slot is a command pipe, not an ACL pipe. */
    return 0;
#else
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
#endif
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
#ifdef BT_HCI_DRAIN_CANCELLED
    pending_slots[OUT_INDEX] = 1;
#endif
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
