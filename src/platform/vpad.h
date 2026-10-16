/* Publishing a virtual controller to the PlayStation 5.
 *
 * libScePad's virtual-device functions turn out to be thin wrappers over three
 * ioctls on /dev/hid. The submission request and report layout below were
 * checked against FW 4.03 and 13.60 libScePad layouts. Runtime selection is
 * provisional: <=4.03 uses 16-byte submissions, newer uses 24-byte submissions.
 *
 * Virtual-pad exports are not called; their code is read separately for ABI
 * diagnostics. No remote shell binding is required for accepted submissions.
 */
#ifndef FGG_VPAD_H
#define FGG_VPAD_H

#include <stdint.h>

typedef struct vpad vpad;

/* Registers a separately allocated virtual controller owned by `user`.
 * Returns NULL on failure with a short reason in `why`. Multiple pads may
 * coexist; closing one does not change the others. */
vpad *vpad_open(uint32_t user, char *why, int whylen);

/* Removes the device and frees its state. A registered pad outlives the payload
 * unless removed. Do not reuse a pointer after closing it. NULL is harmless. */
void vpad_close(vpad *pad);

int vpad_handle(const vpad *pad);

/* Publishes one frame: 1 accepted, -1 temporarily busy, 0 other failure.
 * Busy can occur while the controller assignment screen is pending. */
int vpad_submit(vpad *pad, const unsigned char *report);

/* Translates a GIP input report into the report the kernel reads.
 *
 * `report` must be VPAD_REPORT_SIZE bytes. Note that this is emphatically not
 * a ScePadData: libScePad translates its caller's ScePadData into a
 * differently shaped buffer before handing it over, and passing a ScePadData
 * straight through appears to work -- the ioctl validates only its request
 * header, never the contents -- while the kernel reads the buttons out of what
 * was actually the orientation quaternion. The result is a controller the
 * system acknowledges and that does nothing at all.
 */
/* Both inspected wrappers initialize a 168-byte translated report buffer. */
#define VPAD_REPORT_SIZE 0xA8

void vpad_translate(const unsigned char *gip_report, unsigned char *report);

/* Guide arrives separately from sticks/buttons. Keep it held across ordinary
 * packets and publish both edges without requiring another stick movement. */
typedef struct {
    unsigned char report[VPAD_REPORT_SIZE];
    int guide;
} vpad_input;
void vpad_input_init(vpad_input *input);
int vpad_input_update(vpad_input *input, const unsigned char *msg, int length);

/* Convert a native 20-byte wired Xbox 360 report to the common GIP control
 * layout. Guide is returned separately; malformed/status packets are rejected. */
int vpad_normalize_usb360(const unsigned char *msg, int length, unsigned char normalized[18],
                          int *guide);

#endif
