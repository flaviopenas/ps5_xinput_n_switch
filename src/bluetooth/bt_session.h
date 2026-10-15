#ifndef PS5_BT_SESSION_H
#define PS5_BT_SESSION_H
#include <stdint.h>
#include "bt_advert.h"
#define BT_MAX_CONTROLLERS 4
typedef struct {
    uint8_t address[6], address_type;
    const char *bond_path;
    int fresh_pair;
    void (*tick)(void *);
    int (*cancelled)(void *);
    void (*stage)(const char *, void *);
    void (*input)(const unsigned char *, unsigned long, unsigned long, void *);
    void *context;
} bt_session_config;
/* Synchronous session pumps its nonblocking control interface while running.
 * 1: virtual pad was registered, 2: View+Menu, 0: setup failure/disconnection. */
/* 3: radio rejected initiation; do not automatically retry. */
/* 4: native classic controller requested connection; yield before retrying. */
int bt_session_run(const bt_session_config *config);
/* Manager sessions retain their own state and share one radio transport.
 * Setup is serialized; connected peers keep receiving input during setup. */
void bt_sessions_set_control(void (*tick)(void *), int (*cancelled)(void *), void *context);
void bt_sessions_set_advertisement(void (*callback)(const bt_advert *, void *), void *context);
int bt_sessions_open(void);
int bt_session_start(unsigned slot, const bt_session_config *config);
int bt_session_connected(unsigned slot);
int bt_session_result(unsigned slot);
/* 4: yielded to native controller; -1: transport failed; 0: normal. */
int bt_sessions_poll(unsigned timeout_ms);
void bt_session_disconnect(unsigned slot);
void bt_sessions_close(void);
#endif
