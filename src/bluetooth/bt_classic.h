#ifndef PS5_BT_CLASSIC_H
#define PS5_BT_CLASSIC_H
#include "bt_advert.h"
#include "switch_usb.h"
#include <stdint.h>
/* Manager address type 2 denotes BR/EDR; 0 and 1 remain LE address types. */
#define BT_ADDRESS_CLASSIC 2
typedef struct {
    unsigned local, remote, mtu, connect_id, config_id;
    int requested, configured, peer_configured;
    unsigned connect_attempts;
    int connect_pending;
    int64_t connect_at;
} bt_hid_channel;
typedef struct {
    uint8_t address[6], key[16], key_type;
    int have_key, key_changed, authenticated, encrypted, failed, auth_sent, accept_pending;
    unsigned handle, sequence;
    struct {
        unsigned opcode;
        uint8_t data[32];
        size_t size;
    } commands[8];
    unsigned head, tail;
    bt_hid_channel channels[2];
    int64_t open_at, deadline;
    int info_verified, info_waiting, info_attempts, ready;
    uint32_t observed_reports, ready_reports;
    int input_seen;
    uint8_t initial_input[49];
    switch_usb controller;
    int (*command)(unsigned, const void *, size_t, void *);
    int (*send)(unsigned, const uint8_t *, size_t, void *);
    void (*input)(const uint8_t *, int, int, void *);
    void *context;
} bt_classic;
void bt_classic_init(bt_classic *, const uint8_t address[6]);
/* Event handler queues replies; tick sends them outside HCI callbacks. */
void bt_classic_event(bt_classic *, const uint8_t *, size_t, int64_t);
void bt_classic_receive(bt_classic *, unsigned cid, const uint8_t *, size_t);
void bt_classic_tick(bt_classic *, int64_t);
int bt_classic_normalize_simple(const uint8_t *, size_t, uint8_t out[18], int *home);
int bt_classic_advert_parse(const uint8_t *, size_t, bt_advert_fn, void *);
#endif
