#ifndef XINPUT_BT_CRYPTO_H
#define XINPUT_BT_CRYPTO_H
#include <stddef.h>
#include <stdint.h>
/* Bluetooth inputs/outputs are little endian, including address || type. */
void bt_f4(const uint8_t u[32], const uint8_t v[32], const uint8_t x[16], uint8_t z,
           uint8_t out[16]);
void bt_f5(const uint8_t w[32], const uint8_t n1[16], const uint8_t n2[16], const uint8_t a1[7],
           const uint8_t a2[7], uint8_t mac[16], uint8_t ltk[16]);
void bt_f6(const uint8_t w[16], const uint8_t n1[16], const uint8_t n2[16], const uint8_t r[16],
           const uint8_t io[3], const uint8_t a1[7], const uint8_t a2[7], uint8_t out[16]);
void bt_wipe(void *data, size_t size);
int bt_equal(const void *a, const void *b, size_t size);
void bt_ah(const uint8_t irk[16], const uint8_t random[3], uint8_t hash[3]);
#endif
