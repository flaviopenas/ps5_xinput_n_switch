#ifndef PS5_BT_HOG_H
#define PS5_BT_HOG_H
#include <stddef.h>
#include <stdint.h>

/* Bounded HID-over-GATT setup at negotiated ATT MTU 23..128.
 * One request at a time; initialize a fresh struct for each connection. */
typedef struct {
    uint16_t value, reference, cccd;
    uint8_t id, type;
} bt_hog_report;
typedef struct {
    int (*send)(const uint8_t *, size_t, void *);
    void (*input)(uint8_t, const uint8_t *, size_t, void *);
    void *context;
    uint16_t first, last, cursor, map_handle;
    struct {
        uint16_t handle, uuid;
    } attributes[64];
    unsigned count, report_count, index;
    bt_hog_report reports[8];
    uint8_t map[1024];
    size_t map_size;
    unsigned mtu;
    int phase, ready, failed;
    int64_t deadline;
} bt_hog;
void bt_hog_start(bt_hog *h, uint16_t first, uint16_t last, int64_t now);
void bt_hog_receive(bt_hog *h, const uint8_t *p, size_t n, int64_t now);
void bt_hog_tick(bt_hog *h, int64_t now);
#endif
