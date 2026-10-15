/* Reading Xbox One/Series GIP or wired-format Xbox 360 XInput over USB.
 *
 * The PS5 enumerates a GIP gamepad and then leaves it alone -- unconfigured,
 * unlit and silent. Everything needed to change that lives here: finding the
 * device, claiming it, moving data, and speaking enough of the protocol to
 * keep it awake.
 *
 * Two details about this platform shape the whole interface. The per-endpoint
 * /dev/ugenX.Y.Z nodes do not exist, so transfers go through the USB_FS_*
 * ioctls on the device node rather than through read() and write(). And a
 * transfer, once queued, must be left pending until it completes: cancelling
 * and requeueing an interrupt endpoint several times a second is read by the
 * controller as a host that has gone wrong, and it stops talking.
 */
#ifndef FGG_GIP_H
#define FGG_GIP_H

#include <stdint.h>

/* Message types. Only the ones this program acts on are named. */
#define GIP_CMD_ACK 0x01
#define GIP_CMD_SET_DEVICE_STATE 0x05
#define GIP_CMD_AUTHENTICATE 0x06
#define GIP_CMD_GUIDE_BUTTON 0x07
#define GIP_CMD_LED 0x0A
#define GIP_LL_INPUT_REPORT 0x20

/* Options byte flags. SYSTEM marks a system message; ACK means the controller
 * wants this message acknowledged and will wait until it is. */
#define GIP_OPT_SYSTEM 0x20
#define GIP_OPT_ACK 0x10

/* Buttons, as they appear in an input report's first two payload bytes. Each
 * was verified against hardware by pressing it alone and reading the raw
 * bytes, rather than taken from the Xbox layout on trust. */
#define GIP_MENU 0x0004
#define GIP_VIEW 0x0008
#define GIP_A 0x0010
#define GIP_B 0x0020
#define GIP_X 0x0040
#define GIP_Y 0x0080
#define GIP_UP 0x0100
#define GIP_DOWN 0x0200
#define GIP_LEFT 0x0400
#define GIP_RIGHT 0x0800
#define GIP_LB 0x1000
#define GIP_RB 0x2000
#define GIP_LS 0x4000
#define GIP_RS 0x8000

enum { XINPUT_GIP = 1, XINPUT_USB360 = 2, USB_SWITCH_PRO = 3 };

/* An open, configured controller. Treat as opaque. */
typedef struct gip_device gip_device;
int gip_protocol(const gip_device *dev);
/* Complete Switch USB negotiation before registering a virtual pad. */
int gip_prepare(gip_device *dev);
int gip_ready(const gip_device *dev);
/* Check the owned USB descriptor without resetting or claiming the device. */
int gip_present(const gip_device *dev);

/* Finds the first supported controller and brings it up: configures it, claims the
 * interface, prepares the endpoints and runs the initialisation sequence.
 *
 * Devices are matched on interface signatures FF/47/D0 (GIP) or FF/5D/01
 * (wired-format Xbox 360) rather than a list of vendor IDs, so an untested
 * controller still works and, just as importantly, nothing else on the bus is
 * opened, configured or claimed. An earlier version acted on every non-hub
 * device it found and claimed interfaces on the console's own Bluetooth radio.
 *
 * Returns NULL if no controller is present or it could not be brought up;
 * `why` receives a short explanation either way.
 */
gip_device *gip_open(char *why, int whylen);

/* Read-only startup inventory for identifying unsupported USB protocols.
 * No interfaces are claimed or reconfigured. Quiet release builds omit it. */
void gip_log_inventory(void);

/* Finds another controller without opening or reconfiguring nodes already
 * owned by this payload. Every returned device has independent USB state. */
gip_device *gip_open_next(gip_device *const *claimed, int count, char *why, int whylen);

void gip_close(gip_device *dev);

/* Identification, for logging. */
const char *gip_node(const gip_device *dev);
unsigned gip_vendor(const gip_device *dev);
unsigned gip_product(const gip_device *dev);

/* Collects the next native message, if one has arrived. Consult gip_protocol()
 * before decoding it or looking at GIP header flags.
 *
 * Returns its length and points `msg` at it; 0 when nothing is ready yet, and
 * -1 on a transfer error, -2 on a Switch USB initialization timeout.
 * Transfer errors are ordinary here -- the transfer layer
 * refuses often enough that a healthy controller still only delivers a
 * fraction of its reports -- so a single failure means nothing and only
 * sustained silence indicates a real problem.
 */
int gip_read(gip_device *dev, const unsigned char **msg);
int gip_normalize_switch(const gip_device *dev, const unsigned char *msg, int length,
                         unsigned char normalized[18], int *home);

/* Acknowledges a message whose options byte had GIP_OPT_ACK set. The
 * controller waits for this and stops talking to a host that ignores it. */
void gip_acknowledge(gip_device *dev, const unsigned char *msg);

/* Schedules the initialisation sequence: power on, authenticate, light the guide.
 *
 * Also the keepalive. The authentication message is the one that matters for
 * third-party pads -- Microsoft controllers do not need it, others power
 * themselves down a few seconds after connecting without it. Xbox 360-format
 * devices receive no GIP commands. Writes progress
 * during gip_read(), so one controller never sleeps while the other needs input.
 */
void gip_init(gip_device *dev);

/* Tears the USB connection down and rebuilds it on the same device.
 *
 * Resending messages cannot rescue an endpoint that has wedged, so this is the
 * heavier remedy when re-initialising repeatedly fails to bring input back.
 * Returns 0 if the device could not be reopened.
 */
int gip_rebuild(gip_device *dev);

#endif
