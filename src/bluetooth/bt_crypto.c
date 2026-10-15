/* Bluetooth Core f4/f5/f6 contracts cross-checked against BlueZ crypto.c.
 * AES primitive: pinned tiny-AES-c, third_party/tiny-aes/unlicense.txt. */
#include "bt_crypto.h"
#include "../../third_party/tiny-aes/aes.h"
#include <string.h>

void bt_wipe(void *data, size_t size)
{
    volatile uint8_t *p = data;
    while (size--)
        *p++ = 0;
}
int bt_equal(const void *a, const void *b, size_t size)
{
    const uint8_t *x = a, *y = b;
    unsigned difference = 0;
    for (size_t i = 0; i < size; ++i)
        difference |= x[i] ^ y[i];
    return difference == 0;
}
static void reverse(uint8_t *out, const uint8_t *in, size_t size)
{
    for (size_t i = 0; i < size; ++i)
        out[i] = in[size - 1 - i];
}
static void double_block(uint8_t out[16], const uint8_t in[16])
{
    unsigned carry = 0;
    for (int i = 15; i >= 0; --i) {
        unsigned next = in[i] >> 7;
        out[i] = (uint8_t)((in[i] << 1) | carry);
        carry = next;
    }
    if (carry)
        out[15] ^= 0x87;
}
void bt_ah(const uint8_t irk[16], const uint8_t random[3], uint8_t hash[3])
{
    struct AES_ctx aes;
    uint8_t key[16], block[16] = {0};
    reverse(key, irk, 16);
    for (unsigned i = 0; i < 3; ++i)
        block[15 - i] = random[i];
    AES_init_ctx(&aes, key);
    AES_ECB_encrypt(&aes, block);
    for (unsigned i = 0; i < 3; ++i)
        hash[i] = block[15 - i];
    bt_wipe(&aes, sizeof aes);
    bt_wipe(key, sizeof key);
    bt_wipe(block, sizeof block);
}
static void cmac_le(const uint8_t key[16], const uint8_t *message, size_t size, uint8_t out[16])
{
    struct AES_ctx aes;
    uint8_t k[16], m[80], state[16] = {0}, sub[16] = {0}, last[16] = {0};
    reverse(k, key, 16);
    reverse(m, message, size);
    AES_init_ctx(&aes, k);
    AES_ECB_encrypt(&aes, sub);
    double_block(sub, sub);
    size_t blocks = (size + 15) / 16;
    if (!blocks)
        blocks = 1;
    size_t remaining = size - (blocks - 1) * 16;
    if (remaining)
        memcpy(last, m + (blocks - 1) * 16, remaining);
    if (remaining != 16) {
        last[remaining] = 0x80;
        double_block(sub, sub);
    }
    for (size_t i = 0; i < 16; ++i)
        last[i] ^= sub[i];
    for (size_t block = 0; block < blocks; ++block) {
        const uint8_t *p = block + 1 == blocks ? last : m + block * 16;
        for (size_t i = 0; i < 16; ++i)
            state[i] ^= p[i];
        AES_ECB_encrypt(&aes, state);
    }
    reverse(out, state, 16);
    bt_wipe(&aes, sizeof aes);
    bt_wipe(k, sizeof k);
    bt_wipe(m, sizeof m);
    bt_wipe(state, sizeof state);
    bt_wipe(sub, sizeof sub);
    bt_wipe(last, sizeof last);
}
void bt_f4(const uint8_t u[32], const uint8_t v[32], const uint8_t x[16], uint8_t z,
           uint8_t out[16])
{
    uint8_t m[65];
    m[0] = z;
    memcpy(m + 1, v, 32);
    memcpy(m + 33, u, 32);
    cmac_le(x, m, sizeof m, out);
    bt_wipe(m, sizeof m);
}
void bt_f5(const uint8_t w[32], const uint8_t n1[16], const uint8_t n2[16], const uint8_t a1[7],
           const uint8_t a2[7], uint8_t mac[16], uint8_t ltk[16])
{
    const uint8_t salt[16] = {0xbe, 0x83, 0x60, 0x5a, 0xdb, 0x0b, 0x37, 0x60,
                              0x38, 0xa5, 0xf5, 0xaa, 0x91, 0x83, 0x88, 0x6c};
    uint8_t t[16], m[53] = {0, 1};
    cmac_le(salt, w, 32, t);
    memcpy(m + 2, a2, 7);
    memcpy(m + 9, a1, 7);
    memcpy(m + 16, n2, 16);
    memcpy(m + 32, n1, 16);
    memcpy(m + 48, "eltb", 4);
    cmac_le(t, m, sizeof m, mac);
    m[52] = 1;
    cmac_le(t, m, sizeof m, ltk);
    bt_wipe(t, sizeof t);
    bt_wipe(m, sizeof m);
}
void bt_f6(const uint8_t w[16], const uint8_t n1[16], const uint8_t n2[16], const uint8_t r[16],
           const uint8_t io[3], const uint8_t a1[7], const uint8_t a2[7], uint8_t out[16])
{
    uint8_t m[65];
    memcpy(m, a2, 7);
    memcpy(m + 7, a1, 7);
    memcpy(m + 14, io, 3);
    memcpy(m + 17, r, 16);
    memcpy(m + 33, n2, 16);
    memcpy(m + 49, n1, 16);
    cmac_le(w, m, sizeof m, out);
    bt_wipe(m, sizeof m);
}
