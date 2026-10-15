/* Read-only USB inventory; does not configure, claim or reset any interface. */
#include "gip.h"
#include "log.h"
#include <dev/usb/usb.h>
#include <dev/usb/usb_ioctl.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbhid.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static unsigned usb_word(const uint8_t value[2])
{
    return (unsigned)value[0] | ((unsigned)value[1] << 8);
}

/* Read the HID report descriptor through the generic USB control ioctl. This
 * is descriptor-only: it does not claim the interface or send controller
 * output reports. */
static void probe_switch_report_descriptor(int fd, const char *node)
{
    unsigned char report[512];
    struct usb_gen_descriptor gd;

    memset(&gd, 0, sizeof gd);
    gd.ugd_data = report;
    gd.ugd_maxlen = sizeof report;
    gd.ugd_iface_index = 0;
    gd.ugd_altif_index = 0;
    int rc = ioctl(fd, USB_GET_REPORT_DESC, &gd);
    int saved_errno = rc == 0 ? 0 : errno;
    log_line("USB HID descriptor: %s GET_REPORT_DESC rc %d errno %d (%s), length %u",
             node, rc, saved_errno, strerror(saved_errno), (unsigned)gd.ugd_actlen);
    unsigned length = rc == 0 ? gd.ugd_actlen : 0;
    if (length > sizeof report) length = sizeof report;
    if (length == 0) {
        struct usb_ctl_request request;
        memset(&request, 0, sizeof request);
        request.ucr_data = report;
        request.ucr_flags = USB_SHORT_XFER_OK;
        request.ucr_request.bmRequestType = UT_READ | UT_STANDARD | UT_INTERFACE;
        request.ucr_request.bRequest = 6; /* GET_DESCRIPTOR */
        USETW(request.ucr_request.wValue, (UDESC_REPORT << 8));
        USETW(request.ucr_request.wIndex, 0);
        USETW(request.ucr_request.wLength, sizeof report);
        rc = ioctl(fd, USB_DO_REQUEST, &request);
        saved_errno = rc == 0 ? 0 : errno;
        log_line("USB HID descriptor: %s control GET_DESCRIPTOR rc %d errno %d (%s), length %u",
                 node, rc, saved_errno, strerror(saved_errno), (unsigned)request.ucr_actlen);
        if (rc == 0) length = request.ucr_actlen;
        if (length > sizeof report) length = sizeof report;
    }
    if (length == 0) return;

    for (unsigned offset = 0; offset < length; offset += 32) {
        char hex[32 * 2 + 1];
        unsigned count = length - offset;
        if (count > 32) count = 32;
        for (unsigned i = 0; i < count; ++i)
            snprintf(hex + i * 2, 3, "%02x", report[offset + i]);
        hex[count * 2] = '\0';
        log_line("USB HID descriptor: %s +%03u %s", node, offset, hex);
    }
}

/* Check whether this firmware exposes FreeBSD-style endpoint nodes for the
 * Switch-compatible 8BitDo. Open and close only; do not claim or transfer. */
static int probe_switch_endpoint_nodes(void)
{
    char node[64], endpoint[80];

    for (int attempt = 0; attempt < 30; ++attempt) {
      for (int bus = 0; bus < 8; ++bus) {
        for (int unit = 1; unit < 16; ++unit) {
            struct usb_device_descriptor dd;
            int fd;

            snprintf(node, sizeof node, "/dev/ugen%d.%d", bus, unit);
            fd = open(node, O_RDWR | O_NONBLOCK);
            if (fd < 0) continue;
            memset(&dd, 0, sizeof dd);
            if (ioctl(fd, USB_GET_DEVICE_DESC, &dd) == 0 &&
                usb_word(dd.idVendor) == 0x057e && usb_word(dd.idProduct) == 0x2009) {
                log_line("USB node probe: found 057E:2009 at %s", node);
                probe_switch_report_descriptor(fd, node);

                snprintf(endpoint, sizeof endpoint, "%s.1", node);
                int in_fd = open(endpoint, O_RDONLY | O_NONBLOCK);
                int in_errno = in_fd >= 0 ? 0 : errno;
                log_line("USB node probe: IN endpoint node %s open rc %d errno %d (%s)",
                         endpoint, in_fd >= 0 ? 0 : -1, in_errno, strerror(in_errno));
                if (in_fd >= 0) close(in_fd);

                snprintf(endpoint, sizeof endpoint, "%s.2", node);
                int out_fd = open(endpoint, O_WRONLY | O_NONBLOCK);
                int out_errno = out_fd >= 0 ? 0 : errno;
                log_line("USB node probe: OUT endpoint node %s open rc %d errno %d (%s)",
                         endpoint, out_fd >= 0 ? 0 : -1, out_errno, strerror(out_errno));
                if (out_fd >= 0) close(out_fd);
                close(fd);
                return 1;
            }
            close(fd);
        }
      }
      if (attempt == 0)
          log_line("USB node probe: waiting up to 30 seconds for 057E:2009");
      log_poll();
      sleep(1);
    }
    log_line("USB node probe: 057E:2009 not present after 30 seconds");
    return 0;
}

int main(void)
{
    if (!log_open_port("/data/ps5-xinput", "/data/ps5-xinput/usb-inventory.log", 9025)) return 1;
    notify("PS5 XInput: USB diagnostic (no controller transfers)");
    log_line("USB inventory probe: read-only descriptors, UDP 9025");
    gip_log_inventory();
    probe_switch_endpoint_nodes();
    log_line("USB inventory probe complete");
    /* Give an already running PC viewer time to request the bounded log replay. */
    for (int i = 0; i < 20; ++i) { log_poll(); usleep(50000); }
    log_close(); return 0;
}
