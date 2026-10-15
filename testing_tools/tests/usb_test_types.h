/* Minimal host-only USB definitions. Tests simulate operations, not the target
 * ABI; the payload build always uses the SDK's real USB headers. */
#ifndef USB_TEST_TYPES_H
#define USB_TEST_TYPES_H
#include <stdint.h>
#define UDESC_INTERFACE 4
#define UDESC_ENDPOINT 5
#define UE_XFERTYPE 3
#define UE_INTERRUPT 3
#define UE_DIR_IN 0x80
#define USB_FS_FLAG_SINGLE_SHORT_OK 1
enum {
    USB_FS_INIT = 100, USB_FS_OPEN, USB_FS_START, USB_FS_COMPLETE,
    USB_FS_UNINIT, USB_SET_CONFIG, USB_CLAIM_INTERFACE,
    USB_GET_FULL_DESC, USB_GET_DEVICE_DESC, USB_GET_CONFIG
};
struct usb_fs_endpoint {
    void **ppBuffer;
    uint32_t *pLength;
    unsigned nFrames, aFrames, flags, timeout, status;
};
struct usb_fs_init { struct usb_fs_endpoint *pEndpoints; unsigned ep_index_max; };
struct usb_fs_open { unsigned max_bufsize, max_frames; uint8_t ep_index, ep_no; };
struct usb_fs_start { uint8_t ep_index; };
struct usb_fs_complete { uint8_t ep_index; };
struct usb_fs_uninit { unsigned dummy; };
struct usb_gen_descriptor {
    void *ugd_data;
    uint16_t ugd_maxlen, ugd_actlen;
    unsigned ugd_config_index;
};
struct usb_device_descriptor { uint16_t idVendor, idProduct; };
#endif
