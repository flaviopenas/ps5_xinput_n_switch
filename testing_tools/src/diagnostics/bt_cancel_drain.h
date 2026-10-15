#ifndef BT_CANCEL_DRAIN_H
#define BT_CANCEL_DRAIN_H
#include <stddef.h>
#include <stdint.h>
typedef struct {
    /* 1 completion, 0 temporarily empty, -1 error. Never restart transfers. */
    int (*next)(void *, unsigned *);
    int64_t (*now)(void *);
    void (*idle)(void *);
    void *context;
} bt_cancel_ops;
/* 0 drained, 1 timed out, -1 error. Only the caller's pending slots change. */
int bt_cancel_drain(unsigned char *, size_t, unsigned, const bt_cancel_ops *, unsigned *);
#endif
