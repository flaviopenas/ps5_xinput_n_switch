#include "bt_smp.h"
#include "../../third_party/micro-ecc/uECC.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t last[65];
static size_t last_size;
static unsigned encrypt_calls;
static int capture(const uint8_t *p, size_t n, void *ctx)
{
    (void)ctx; memcpy(last, p, n); last_size = n; return 1;
}
static int encrypt(const uint8_t *key, void *ctx)
{
    (void)key; (void)ctx; ++encrypt_calls; return 1;
}
static void point(uint8_t *le, unsigned scalar)
{
    uint8_t secret[32] = {0}, be[64]; secret[31] = (uint8_t)scalar;
    assert(uECC_compute_public_key(secret, be, uECC_secp256r1()));
    for (unsigned i = 0; i < 32; ++i) { le[i] = be[31-i]; le[32+i] = be[63-i]; }
}
static void setup(bt_smp *s)
{
    memset(s, 0, sizeof *s); s->private_key[31] = 2; point(s->public_key, 2);
    memset(s->nonce, 0x12, 16); memset(s->local, 0x34, 6); memset(s->peer, 0x56, 6);
    s->send = capture; s->encrypt = encrypt; encrypt_calls = 0;
    assert(bt_smp_start(s) && last_size == 7 && last[3] == 9 && last[5] == 0 && last[6] == 2);
}
static void to_random(bt_smp *s)
{
    uint8_t response[7] = {2,3,0,9,16,0,2}, pub[65] = {12}, confirm[17] = {3};
    uint8_t remote_nonce[16]; memset(remote_nonce, 0x78, 16);
    assert(bt_smp_receive(s, response, 7) && s->phase == SMP_PUBLIC && last[0] == 12);
    point(pub + 1, 3);
    assert(bt_smp_receive(s, pub, 65) && s->phase == SMP_CONFIRM);
    bt_f4(pub + 1, s->public_key, remote_nonce, 0, confirm + 1);
    assert(bt_smp_receive(s, confirm, 17) && s->phase == SMP_RANDOM && last[0] == 4);
}
int main(void)
{
    bt_smp s;
    uint8_t random[17] = {4}, check[17] = {13}, zero[16] = {0};
    memset(random + 1, 0x78, 16);
    setup(&s); assert(!bt_smp_encrypted(&s)); to_random(&s);
    assert(bt_smp_receive(&s, random, 17) && s.phase == SMP_DHKEY && last[0] == 13);
    bt_f6(s.mac, s.peer_nonce, s.nonce, zero, s.response + 1, s.peer, s.local, check + 1);
    assert(bt_smp_receive(&s, check, 17) && s.phase == SMP_ENCRYPT && encrypt_calls == 1);
    assert(bt_smp_encrypted(&s) && s.phase == SMP_IDENTITY);
    uint8_t irk[17] = {8}, identity[8] = {9,0,1,2,3,4,5,6};
    assert(bt_smp_receive(&s, irk, 17) && s.phase == SMP_IDENTITY);
    assert(bt_smp_receive(&s, identity, 8) && s.phase == SMP_DONE);
    assert(s.identity[0] == 1 && s.identity[5] == 6 && s.identity[6] == 0);
    setup(&s); to_random(&s); random[1] ^= 1;
    assert(!bt_smp_receive(&s, random, 17) && s.failure == 4 && encrypt_calls == 0);
    random[1] ^= 1;
    setup(&s); to_random(&s); assert(bt_smp_receive(&s, random, 17));
    memset(check + 1, 0, 16);
    assert(!bt_smp_receive(&s, check, 17) && s.failure == 11 && encrypt_calls == 0);
    setup(&s);
    uint8_t legacy[7] = {2,3,0,1,16,0,2};
    assert(!bt_smp_receive(&s, legacy, 7) && s.phase == SMP_FAILED);
    setup(&s); legacy[3] = 9; legacy[4] = 7;
    assert(!bt_smp_receive(&s, legacy, 7) && s.failure == 6);
    setup(&s); legacy[4] = 16; assert(bt_smp_receive(&s, legacy, 7));
    uint8_t invalid_point[65] = {12};
    assert(!bt_smp_receive(&s, invalid_point, 65) && encrypt_calls == 0);
    setup(&s);
    assert(!bt_smp_receive(&s, random, 16) && s.phase == SMP_FAILED);
    puts("Secure Connections state/verification tests passed"); return 0;
}
