#ifndef XINPUT_BT_SMP_H
#define XINPUT_BT_SMP_H
#include "bt_crypto.h"
enum bt_smp_phase {
    SMP_RESPONSE,
    SMP_PUBLIC,
    SMP_CONFIRM,
    SMP_RANDOM,
    SMP_DHKEY,
    SMP_ENCRYPT,
    SMP_IDENTITY,
    SMP_DONE,
    SMP_FAILED
};
typedef struct bt_smp {
    enum bt_smp_phase phase;
    uint8_t request[7], response[7], local[7], peer[7], identity[7];
    uint8_t public_key[64], private_key[32], peer_key[64];
    uint8_t nonce[16], peer_nonce[16], confirm[16], mac[16], ltk[16], irk[16];
    int need_identity, got_irk, failure;
    int (*send)(const uint8_t *, size_t, void *);
    int (*encrypt)(const uint8_t *, void *);
    void *context;
} bt_smp;
int bt_smp_start(bt_smp *s);
int bt_smp_receive(bt_smp *s, const uint8_t *pdu, size_t length);
int bt_smp_encrypted(bt_smp *s);
#endif
