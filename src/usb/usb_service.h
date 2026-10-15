#ifndef PS5_USB_SERVICE_H
#define PS5_USB_SERVICE_H
#include <stdint.h>
#define USB_MAX_CONTROLLERS 4
typedef struct {
    int slot, ready;
    const char *name;
    unsigned long reports, delivered;
    uint32_t buttons;
} usb_controller_status;
int usb_service_init(void);
/* Cooperative polling: call from the manager and Bluetooth control ticks. */
int usb_service_poll(void);
/* Caller provides room for USB_MAX_CONTROLLERS entries. Returns populated count. */
int usb_service_status(usb_controller_status out[USB_MAX_CONTROLLERS]);
void usb_service_close(void);
#endif
