#ifndef PS5_SWITCH_USB_H
#define PS5_SWITCH_USB_H
#include <stdint.h>
/* Official Nintendo Pro Controller 057E:2009, USB HID interface only. */
typedef struct {
    int center, below, above;
} switch_axis;
typedef struct {
    switch_axis axes[4];
    int stage, awaiting, retries, failed, calibrated;
    unsigned sequence;
    int64_t deadline;
} switch_usb;
void switch_usb_init(switch_usb *pad);
/* Peek the next output; commit only after USB_FS_START accepts it. */
int switch_usb_output(switch_usb *pad, int64_t now, unsigned char out[64]);
void switch_usb_sent(switch_usb *pad, int64_t now);
void switch_usb_receive(switch_usb *pad, const unsigned char *msg, int length);
int switch_usb_normalize(const switch_usb *pad, const unsigned char *msg, int length,
                         unsigned char out[18], int *home);
#endif
