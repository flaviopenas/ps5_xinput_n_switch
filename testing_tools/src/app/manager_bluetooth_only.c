/* Isolated wireless manager: retain saved bonds/API without claiming USB pads. */
#include "usb_service.h"
int usb_service_init(void) { return 0; }
int usb_service_poll(void) { return 0; }
int usb_service_status(usb_controller_status out[USB_MAX_CONTROLLERS])
{
    (void)out;
    return 0;
}
void usb_service_close(void) {}
#include "../../../src/app/manager_service.c"
