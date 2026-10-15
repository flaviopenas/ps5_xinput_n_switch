#ifndef XINPUT_BT_ADVERT_H
#define XINPUT_BT_ADVERT_H
#include <stddef.h>
#include <stdint.h>
typedef struct bt_advert {
    unsigned char address[6];
    unsigned char address_type, event_type;
    int rssi;
    char name[32];
    int hid_service, microsoft;
} bt_advert;
typedef void (*bt_advert_fn)(const bt_advert *, void *);
/* Validate an entire legacy LE Advertising Report before emitting records.
 * Returns record count, 0 for unrelated events, -1 for malformed events. */
int bt_advert_parse(const unsigned char *event, size_t length, bt_advert_fn fn, void *context);
#endif
