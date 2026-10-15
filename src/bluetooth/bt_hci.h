#ifndef XINPUT_BT_HCI_H
#define XINPUT_BT_HCI_H
#include <stddef.h>
typedef void (*bt_hci_event_fn)(const unsigned char *, size_t, void *);
/* Callbacks queue work; they must not send commands or recursively poll. */
void bt_hci_set_handlers(bt_hci_event_fn event, bt_hci_event_fn acl, void *context);
void bt_hci_set_control(void (*tick)(void *), int (*cancelled)(void *), void *context);
int bt_hci_open_acl(void);
int bt_hci_send(unsigned opcode, const void *params, size_t length);
int bt_hci_acl_send(const unsigned char *packet, size_t length);
int bt_hci_open(void);
void bt_hci_close(void);
int bt_hci_poll(unsigned timeout_ms, bt_hci_event_fn handler, void *context);
/* Finish cleanup even after cancellation, without a busy wait. */
int bt_hci_cleanup_poll(unsigned timeout_ms);
/* Command Complete return parameters, beginning with status; -1 on failure.
 * Retries can lose replies to the system's shared reader. Callers must use
 * commands that tolerate repetition and clean up even on missing replies. */
int bt_hci_command(unsigned opcode, const void *params, size_t length, unsigned char *result,
                   size_t capacity);
int bt_hci_cleanup_command(unsigned opcode, const void *params, size_t length,
                           unsigned char *result, size_t capacity);
#endif
