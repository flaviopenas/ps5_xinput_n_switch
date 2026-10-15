#include "gip.h"
#include "log.h"
#include "switch_usb.h"

#include <sys/ioctl.h>
#include <sys/types.h>

#ifdef GIP_HOST_TEST
#include "usb_test_types.h"
#else
#include <dev/usb/usb.h>
#include <dev/usb/usb_ioctl.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#define XFER_BUF 256
#define EP_IN 0
#define EP_OUT 1
#define EP_MAX 2

/* Guide LED message payload: which LED, the pattern, and a brightness. */
#define GIP_LED_GUIDE 0x00
#define GIP_LED_GUIDE_ON 0x01
#define GIP_LED_LEVEL 0x14

struct gip_device {
    int fd;
    char node[64];
    unsigned vendor, product;

    int iface, ep_in, ep_out, mps_in;
    int seq, protocol, raw_logged, transfer_errors;
    unsigned raw_buttons;

    struct usb_fs_endpoint eps[EP_MAX];
    void *bufptr[EP_MAX][1];
    uint32_t buflen[EP_MAX][1];
    unsigned char data[EP_MAX][XFER_BUF];
    int pending[EP_MAX];
    int init_step;
    switch_usb nintendo;
    int switch_failed_logged;
};

static int interface_protocol(const gip_device *dev, const unsigned char *d)
{
    if (d[3] != 0)
        return 0;
    if (dev->vendor == 0x057e && dev->product == 0x2009 && d[2] == 0 && d[5] == 3 && d[6] == 0 &&
        d[7] == 0)
        return USB_SWITCH_PRO;
    if (d[5] != 0xff)
        return 0;
    if (d[6] == 0x47 && d[7] == 0xd0)
        return XINPUT_GIP;
    if (d[6] == 0x5d && d[7] == 0x01)
        return XINPUT_USB360;
    return 0;
}

static unsigned word(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

/* ---- descriptors ------------------------------------------------------- */

/* Walks a configuration descriptor for a supported interface and the interrupt
 * endpoints belonging to it. Endpoints seen outside that interface are
 * ignored, so a composite device's other functions cannot be mistaken for the
 * gamepad. */
static int scan_layout(gip_device *dev, const unsigned char *buf, int len)
{
    int i = 0, in_gip = 0;

    dev->iface = dev->ep_in = dev->ep_out = -1;
    dev->mps_in = 0;
    dev->protocol = 0;

    while (i + 2 <= len) {
        unsigned blen = buf[i], btype = buf[i + 1];
        if (blen < 2 || i + (int)blen > len)
            break;

        if (btype == UDESC_INTERFACE && blen >= 9) {
            int protocol = interface_protocol(dev, buf + i);
            in_gip = protocol != 0;
            if (in_gip && dev->iface < 0) {
                dev->iface = (int)buf[i + 2];
                dev->protocol = protocol;
            } else
                in_gip = in_gip && dev->iface == (int)buf[i + 2] && dev->protocol == protocol;
        } else if (btype == UDESC_ENDPOINT && blen >= 7 && in_gip) {
            unsigned addr = buf[i + 2], attr = buf[i + 3];
            unsigned mps = (unsigned)buf[i + 4] | ((unsigned)buf[i + 5] << 8);

            if ((attr & UE_XFERTYPE) == UE_INTERRUPT) {
                if ((addr & UE_DIR_IN) && dev->ep_in < 0) {
                    dev->ep_in = (int)addr;
                    dev->mps_in = (int)mps;
                } else if (!(addr & UE_DIR_IN) && dev->ep_out < 0) {
                    dev->ep_out = (int)addr;
                }
            }
        }
        i += (int)blen;
    }

    return dev->iface >= 0 && dev->ep_in >= 0 && dev->mps_in > 0 && dev->mps_in <= XFER_BUF &&
           (dev->protocol != USB_SWITCH_PRO || dev->ep_out >= 0);
}

/* ---- transfers --------------------------------------------------------- */

static int fs_setup(gip_device *dev)
{
    struct usb_fs_init init;
    struct usb_fs_open op;
    int i;

    memset(dev->eps, 0, sizeof dev->eps);
    memset(dev->pending, 0, sizeof dev->pending);

    for (i = 0; i < EP_MAX; i++) {
        dev->bufptr[i][0] = dev->data[i];
        dev->buflen[i][0] = XFER_BUF;
        dev->eps[i].ppBuffer = dev->bufptr[i];
        dev->eps[i].pLength = dev->buflen[i];
        dev->eps[i].nFrames = 1;
        dev->eps[i].flags = USB_FS_FLAG_SINGLE_SHORT_OK;
        dev->eps[i].timeout = 1000;
    }

    memset(&init, 0, sizeof init);
    init.pEndpoints = dev->eps;
    init.ep_index_max = EP_MAX;
    int rc = ioctl(dev->fd, USB_FS_INIT, &init);
#if XINPUT_VERBOSE
    if (dev->protocol == USB_SWITCH_PRO)
        log_debug("Switch USB FS_INIT rc %d errno %d request 0x%lx size %u slots %u", rc,
                  rc == 0 ? 0 : errno, (unsigned long)USB_FS_INIT, (unsigned)sizeof init,
                  (unsigned)init.ep_index_max);
#endif
    if (rc != 0)
        return 0;

    memset(&op, 0, sizeof op);
    op.max_bufsize = XFER_BUF;
    op.max_frames = 1;
    op.ep_index = EP_IN;
    op.ep_no = (uint8_t)dev->ep_in;
    rc = ioctl(dev->fd, USB_FS_OPEN, &op);
#if XINPUT_VERBOSE
    if (dev->protocol == USB_SWITCH_PRO)
        log_debug(
            "Switch USB FS_OPEN IN rc %d errno %d request 0x%lx size %u ep_index %u ep_no %02X", rc,
            rc == 0 ? 0 : errno, (unsigned long)USB_FS_OPEN, (unsigned)sizeof op,
            (unsigned)op.ep_index, (unsigned)op.ep_no);
#endif
    if (rc != 0)
        return 0;

#if defined(USB_SWITCH_IN_ONLY_TEST)
    if (dev->protocol == USB_SWITCH_PRO) {
        log_line("Switch USB probe: OUT endpoint deliberately left unopened");
    } else
#endif
        if (dev->ep_out >= 0) {
        memset(&op, 0, sizeof op);
        op.max_bufsize = XFER_BUF;
        op.max_frames = 1;
        op.ep_index = EP_OUT;
        op.ep_no = (uint8_t)dev->ep_out;
        rc = ioctl(dev->fd, USB_FS_OPEN, &op);
#if XINPUT_VERBOSE
        if (dev->protocol == USB_SWITCH_PRO)
            log_debug("Switch USB FS_OPEN OUT rc %d errno %d request 0x%lx size %u ep_index %u "
                      "ep_no %02X",
                      rc, rc == 0 ? 0 : errno, (unsigned long)USB_FS_OPEN, (unsigned)sizeof op,
                      (unsigned)op.ep_index, (unsigned)op.ep_no);
#endif
        if (rc != 0 && dev->protocol == USB_SWITCH_PRO)
            return 0;
    }
    return 1;
}

/* Queues a transfer unless one is already outstanding on that endpoint. */
static int fs_start(gip_device *dev, int index, int length)
{
    struct usb_fs_start start;

    if (dev->pending[index])
        return 1;

    dev->buflen[index][0] = (uint32_t)length;
    dev->eps[index].nFrames = 1;
    dev->eps[index].aFrames = 0;
    dev->eps[index].status = 0;

    memset(&start, 0, sizeof start);
    start.ep_index = (uint8_t)index;
    int result = ioctl(dev->fd, USB_FS_START, &start);
    if (result != 0) {
        if (dev->transfer_errors++ < 5)
            log_debug("USB transfer: %s USB_FS_START 0x%lx size %u ep_index %d rc %d errno %d (%s)",
                      dev->node, (unsigned long)USB_FS_START, (unsigned)sizeof start, index, result,
                      errno, strerror(errno));
        return 0;
    }

    dev->pending[index] = 1;
    return 1;
}

/* Collects one completion, for whichever endpoint finished.
 *
 * USB_FS_COMPLETE reports a completed endpoint rather than the one the caller
 * is interested in, so the index it returns has to be honoured -- reading the
 * IN endpoint's state after an OUT completion silently produces nonsense.
 */
static int fs_poll(gip_device *dev)
{
    struct usb_fs_complete comp;
    int index;

    memset(&comp, 0, sizeof comp);
    if (ioctl(dev->fd, USB_FS_COMPLETE, &comp) != 0)
        return 0;

    index = (int)comp.ep_index;
    if (index < 0 || index >= EP_MAX)
        return 0;

    dev->pending[index] = 0;

    if (dev->eps[index].status != 0) {
        if (dev->transfer_errors++ < 5)
            log_debug("USB transfer: %s %s completion status %u frames %u", dev->node,
                      index == EP_IN ? "IN" : "OUT", (unsigned)dev->eps[index].status,
                      (unsigned)dev->eps[index].aFrames);
        return -1;
    }
    if (index != EP_IN)
        return 0;
    if (dev->eps[EP_IN].aFrames == 0)
        return 0;

    return (int)dev->buflen[EP_IN][0];
}

/* ---- messages ---------------------------------------------------------- */

static int gip_send(gip_device *dev, unsigned char cmd, unsigned char seq,
                    const unsigned char *payload, int plen)
{
    unsigned char *p = dev->data[EP_OUT];

    if (dev->ep_out < 0)
        return 0;

    /* Dropping a message whose predecessor is still in flight is safe: every
     * message sent from here is either idempotent or an acknowledgement that
     * will be asked for again. */
    if (dev->pending[EP_OUT])
        return 0;

    p[0] = cmd;
    p[1] = GIP_OPT_SYSTEM;
    p[2] = seq;
    p[3] = (unsigned char)plen;
    if (plen > 0)
        memcpy(p + 4, payload, (size_t)plen);

    return fs_start(dev, EP_OUT, 4 + plen);
}

void gip_init(gip_device *dev)
{
    if (dev->protocol == USB_SWITCH_PRO) {
        if (!dev->nintendo.stage || dev->nintendo.failed)
            switch_usb_init(&dev->nintendo);
        return;
    }
    /* Do not restart a sequence whose OUT transfer is still pending. */
    if (!dev->init_step)
        dev->init_step = 1;
}

static void init_progress(gip_device *dev)
{
    static const unsigned char power_on[] = {0x00};
    static const unsigned char auth[] = {0x01, 0x00};
    static const unsigned char led_on[] = {GIP_LED_GUIDE, GIP_LED_GUIDE_ON, GIP_LED_LEVEL};

    unsigned char command;
    const unsigned char *payload;
    int length;
    if (dev->protocol == USB_SWITCH_PRO) {
#if defined(USB_SWITCH_IN_ONLY_TEST)
        return;
#else
        struct timespec tick;
        clock_gettime(CLOCK_MONOTONIC, &tick);
        int64_t now = (int64_t)tick.tv_sec * 1000 + tick.tv_nsec / 1000000;
        if (dev->pending[EP_OUT])
            return;
        length = switch_usb_output(&dev->nintendo, now, dev->data[EP_OUT]);
        if (length && fs_start(dev, EP_OUT, length))
            switch_usb_sent(&dev->nintendo, now);
        if (dev->nintendo.failed && !dev->switch_failed_logged++)
            log_line("Switch Pro: USB initialization timed out on %s (stage %d)", dev->node,
                     dev->nintendo.stage);
        return;
#endif
    }
    if (!dev->init_step || dev->ep_out < 0)
        return;
    if (dev->protocol == XINPUT_USB360) {
        /* Standard Xbox 360 LED command, not a GIP header. Some receivers
         * need a host output transfer before starting their input stream. */
        if (dev->pending[EP_OUT])
            return;
        dev->data[EP_OUT][0] = 0x01;
        dev->data[EP_OUT][1] = 0x03;
        dev->data[EP_OUT][2] = 0x02;
        if (fs_start(dev, EP_OUT, 3))
            dev->init_step = 0;
        return;
    }
    switch (dev->init_step) {
    case 1:
        command = GIP_CMD_SET_DEVICE_STATE;
        payload = power_on;
        length = sizeof power_on;
        break;
    case 2:
        command = GIP_CMD_AUTHENTICATE;
        payload = auth;
        length = sizeof auth;
        break;
    default:
        command = GIP_CMD_LED;
        payload = led_on;
        length = sizeof led_on;
        break;
    }
    if (gip_send(dev, command, (unsigned char)dev->seq, payload, length)) {
        ++dev->seq;
        if (++dev->init_step > 3)
            dev->init_step = 0;
    }
}

void gip_acknowledge(gip_device *dev, const unsigned char *msg)
{
    unsigned char body[9];
    if (dev->protocol != XINPUT_GIP)
        return;

    /* The payload carries the command, options and length of the message being
     * answered; the header echoes its sequence number. Mirrors the packet
     * Linux xpad sends in xpadone_ack_mode_report. */
    memset(body, 0, sizeof body);
    body[1] = msg[0];
    body[2] = msg[1];
    body[3] = msg[3];

    gip_send(dev, GIP_CMD_ACK, msg[2], body, (int)sizeof body);
}

int gip_read(gip_device *dev, const unsigned char **msg)
{
    int n;

    fs_start(dev, EP_IN, dev->mps_in ? dev->mps_in : 64);
    n = fs_poll(dev);
    if (n > 0 && dev->protocol == USB_SWITCH_PRO)
        switch_usb_receive(&dev->nintendo, dev->data[EP_IN], n);
    if (n > 0)
        *msg = dev->data[EP_IN];
#if XINPUT_VERBOSE
    if (n >= 13 && dev->protocol == USB_SWITCH_PRO &&
        (dev->data[EP_IN][0] == 0x30 || dev->data[EP_IN][0] == 0x21)) {
        unsigned buttons = (unsigned)dev->data[EP_IN][3] | ((unsigned)dev->data[EP_IN][4] << 8) |
                           ((unsigned)dev->data[EP_IN][5] << 16);
        if (buttons != dev->raw_buttons) {
            log_debug("Switch native buttons: %s right %02X shared %02X left %02X, Home %u",
                      dev->node, dev->data[EP_IN][3], dev->data[EP_IN][4], dev->data[EP_IN][5],
                      !!(dev->data[EP_IN][4] & 0x10));
            dev->raw_buttons = buttons;
        }
    }
    if (n > 0 && dev->protocol != XINPUT_GIP && dev->raw_logged++ < 40) {
        char hex[XFER_BUF * 2 + 1];
        int length = n < XFER_BUF ? n : XFER_BUF;
        for (int i = 0; i < length; ++i)
            snprintf(hex + i * 2, 3, "%02x", dev->data[EP_IN][i]);
        log_debug("USB native raw: %s protocol %d length %d %s", dev->node, dev->protocol, n, hex);
    }
#endif
    init_progress(dev);
    if (dev->protocol == USB_SWITCH_PRO && dev->nintendo.failed)
        return -2;
    return n;
}

int gip_normalize_switch(const gip_device *dev, const unsigned char *msg, int length,
                         unsigned char normalized[18], int *home)
{
    return switch_usb_normalize(&dev->nintendo, msg, length, normalized, home);
}

int gip_prepare(gip_device *dev)
{
    const unsigned char *msg;
    struct timespec tick;
    if (dev->protocol != USB_SWITCH_PRO)
        return 1;
    clock_gettime(CLOCK_MONOTONIC, &tick);
    int64_t deadline = (int64_t)tick.tv_sec * 1000 + tick.tv_nsec / 1000000 + 15000;
    while (dev->nintendo.stage < 10 && !dev->nintendo.failed) {
        if (log_stop_requested())
            return 0;
        (void)gip_read(dev, &msg);
        log_poll();
        clock_gettime(CLOCK_MONOTONIC, &tick);
        if ((int64_t)tick.tv_sec * 1000 + tick.tv_nsec / 1000000 >= deadline)
            return 0;
        usleep(1000);
    }
    return !dev->nintendo.failed && dev->nintendo.stage >= 10;
}

int gip_ready(const gip_device *dev)
{
    return dev->protocol != USB_SWITCH_PRO || (!dev->nintendo.failed && dev->nintendo.stage >= 10);
}

/* ---- lifecycle --------------------------------------------------------- */

/* Configures, claims and initialises an already-open device node. */
static int bring_up(gip_device *dev)
{
    int cfg = 0;

    int current = -1;
    int already_configured = dev->protocol == USB_SWITCH_PRO &&
                             ioctl(dev->fd, USB_GET_CONFIG, &current) == 0 && current == cfg;
    int config_rc = already_configured ? 0 : ioctl(dev->fd, USB_SET_CONFIG, &cfg);
    int config_errno = config_rc == 0 ? 0 : errno;
    int claim_rc = ioctl(dev->fd, USB_CLAIM_INTERFACE, &dev->iface);
    (void)config_rc;
    (void)config_errno;
    (void)claim_rc;
    if (dev->protocol == USB_SWITCH_PRO)
        log_debug("Switch USB setup: config rc %d errno %d; claim rc %d errno %d", config_rc,
                  config_errno, claim_rc, claim_rc == 0 ? 0 : errno);
    if (dev->protocol == USB_SWITCH_PRO)
        log_debug("Switch USB: current configuration %d; configuration reset %s", current,
                  already_configured ? "skipped" : "requested");

    if (!fs_setup(dev))
        return 0;

    gip_init(dev);
    return 1;
}

static int read_descriptors(int fd, unsigned char *cfg, int cfglen, int *actlen)
{
    struct usb_gen_descriptor gd;

    memset(&gd, 0, sizeof gd);
    gd.ugd_data = cfg;
    gd.ugd_maxlen = (uint16_t)cfglen;
    gd.ugd_config_index = 0;

    if (ioctl(fd, USB_GET_FULL_DESC, &gd) != 0 || gd.ugd_actlen == 0)
        return 0;
    *actlen = (int)gd.ugd_actlen;
    return 1;
}

void gip_log_inventory(void)
{
#if XINPUT_VERBOSE
    unsigned char cfg[1024];
    char node[64];
    for (int bus = 0; bus < 8; ++bus) {
        for (int unit = 1; unit < 16; ++unit) {
            struct usb_device_descriptor dd;
            int length = 0, fd;
            snprintf(node, sizeof node, "/dev/ugen%d.%d", bus, unit);
            if (access(node, F_OK) != 0)
                continue;
            fd = open(node, O_RDWR);
            if (fd < 0)
                continue;
            memset(&dd, 0, sizeof dd);
            if (ioctl(fd, USB_GET_DEVICE_DESC, &dd) == 0) {
                log_debug("USB inventory: %s %04X:%04X", node,
                          word((const unsigned char *)&dd.idVendor),
                          word((const unsigned char *)&dd.idProduct));
                if (read_descriptors(fd, cfg, sizeof cfg, &length)) {
                    for (int offset = 0; offset + 2 <= length;) {
                        unsigned size = cfg[offset];
                        if (size < 2 || offset + (int)size > length)
                            break;
                        if (cfg[offset + 1] == UDESC_INTERFACE && size >= 9)
                            log_debug("USB inventory: %s interface %u alt %u class %02X subclass "
                                      "%02X protocol %02X",
                                      node, cfg[offset + 2], cfg[offset + 3], cfg[offset + 5],
                                      cfg[offset + 6], cfg[offset + 7]);
                        if (cfg[offset + 1] == UDESC_ENDPOINT && size >= 7)
                            log_debug(
                                "USB inventory: %s endpoint %02X attributes %02X max packet %u",
                                node, cfg[offset + 2], cfg[offset + 3], word(cfg + offset + 4));
                        offset += (int)size;
                    }
                }
            }
            close(fd);
        }
    }
#endif
}

gip_device *gip_open(char *why, int whylen)
{
    return gip_open_next(NULL, 0, why, whylen);
}

gip_device *gip_open_next(gip_device *const *claimed, int count, char *why, int whylen)
{
    gip_device *dev = calloc(1, sizeof *dev);
    unsigned char cfg[1024];
    int bus, unit, found = 0, cfg_len = 0;

    if (!dev) {
        snprintf(why, (size_t)whylen, "no memory for USB controller");
        return NULL;
    }
    dev->fd = -1;

    for (bus = 0; bus < 8 && !found; bus++) {
        for (unit = 1; unit < 16 && !found; unit++) {
            struct usb_device_descriptor dd;
            int fd, i, owned = 0;

            /* The PS5 HCI transport owns this node (see bt_hci.c). Never
             * open/close its shared USB FIFO during controller hotplug scans. */
            if (bus == 0 && unit == 2)
                continue;

            snprintf(dev->node, sizeof dev->node, "/dev/ugen%d.%d", bus, unit);
            for (i = 0; i < count; ++i)
                if (claimed[i] && strcmp(dev->node, claimed[i]->node) == 0)
                    owned = 1;
            if (owned)
                continue;
            if (access(dev->node, F_OK) != 0)
                continue;

            fd = open(dev->node, O_RDWR);
            if (fd < 0)
                continue;

            memset(&dd, 0, sizeof dd);
            if (ioctl(fd, USB_GET_DEVICE_DESC, &dd) != 0) {
                close(fd);
                continue;
            }
            dev->vendor = word((const unsigned char *)&dd.idVendor);
            dev->product = word((const unsigned char *)&dd.idProduct);
            if (!read_descriptors(fd, cfg, (int)sizeof cfg, &cfg_len) ||
                !scan_layout(dev, cfg, cfg_len)) {
                close(fd);
                continue;
            }

            dev->fd = fd;
            found = 1;
        }
    }

    if (!found) {
        snprintf(why, (size_t)whylen, "no supported Xbox or Switch Pro USB controller connected");
        free(dev);
        return NULL;
    }

    /* The descriptor's own length, not the buffer's: walking past the end
     * reads uninitialised stack and can invent endpoints that do not exist. */
    if (!scan_layout(dev, cfg, cfg_len)) {
        snprintf(why, (size_t)whylen, "%s has no usable GIP endpoints", dev->node);
        close(dev->fd);
        dev->fd = -1;
        free(dev);
        return NULL;
    }

    if (!bring_up(dev)) {
        snprintf(why, (size_t)whylen, "transfer setup failed: %s", strerror(errno));
        close(dev->fd);
        dev->fd = -1;
        free(dev);
        return NULL;
    }

    snprintf(why, (size_t)whylen, "%s interface %d, IN %02X, OUT %02X",
             dev->protocol == USB_SWITCH_PRO  ? "Nintendo Switch Pro USB"
             : dev->protocol == XINPUT_USB360 ? "Xbox 360 XInput"
                                              : "Xbox One/Series GIP",
             dev->iface, dev->ep_in, dev->ep_out);
    return dev;
}

int gip_rebuild(gip_device *dev)
{
    struct usb_fs_uninit un;

    memset(&un, 0, sizeof un);
    ioctl(dev->fd, USB_FS_UNINIT, &un);
    close(dev->fd);

    memset(dev->pending, 0, sizeof dev->pending);
    dev->init_step = 0;
    memset(&dev->nintendo, 0, sizeof dev->nintendo);
    dev->switch_failed_logged = 0;

    dev->fd = open(dev->node, O_RDWR);
    if (dev->fd < 0)
        return 0;

    return bring_up(dev);
}

void gip_close(gip_device *dev)
{
    struct usb_fs_uninit un;

    if (!dev)
        return;

    if (dev->fd >= 0) {
        memset(&un, 0, sizeof un);
        ioctl(dev->fd, USB_FS_UNINIT, &un);
        close(dev->fd);
    }
    free(dev);
}

const char *gip_node(const gip_device *dev)
{
    return dev->node;
}
int gip_protocol(const gip_device *dev)
{
    return dev->protocol;
}
int gip_present(const gip_device *dev)
{
    struct usb_device_descriptor dd;
    memset(&dd, 0, sizeof dd);
    return dev->fd >= 0 && ioctl(dev->fd, USB_GET_DEVICE_DESC, &dd) == 0 &&
           word((const unsigned char *)&dd.idVendor) == dev->vendor &&
           word((const unsigned char *)&dd.idProduct) == dev->product;
}
unsigned gip_vendor(const gip_device *dev)
{
    return dev->vendor;
}
unsigned gip_product(const gip_device *dev)
{
    return dev->product;
}
