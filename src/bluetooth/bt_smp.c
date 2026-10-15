/* LE Secure Connections Just Works initiator. No legacy downgrade, passkey,
 * OOB, signing or cross-transport keys. All secret inputs remain off the log. */
#include "bt_smp.h"
#include "../../third_party/micro-ecc/uECC.h"
#include <string.h>

static int fail(bt_smp *s, unsigned reason)
{
    uint8_t pdu[2] = {5, (uint8_t)reason};
    s->phase = SMP_FAILED;
    s->failure = (int)reason;
    (void)s->send(pdu, sizeof pdu, s->context);
    return 0;
}
static int send(bt_smp *s, unsigned opcode, const uint8_t *data, size_t length)
{
    uint8_t pdu[65];
    pdu[0] = (uint8_t)opcode;
    memcpy(pdu + 1, data, length);
    int result = s->send(pdu, length + 1, s->context);
    bt_wipe(pdu, sizeof pdu);
    if (!result) {
        s->phase = SMP_FAILED;
        s->failure = 8;
    }
    return result;
}
int bt_smp_start(bt_smp *s)
{
    const uint8_t request[7] = {1, 3, 0, 9, 16, 0, 2};
    memcpy(s->request, request, sizeof request);
    memcpy(s->identity, s->peer, 7);
    s->phase = SMP_RESPONSE;
    return s->send(s->request, 7, s->context);
}
static void reverse32(uint8_t *out, const uint8_t *in)
{
    for (unsigned i = 0; i < 32; ++i)
        out[i] = in[31 - i];
}
int bt_smp_receive(bt_smp *s, const uint8_t *pdu, size_t length)
{
    uint8_t check[16], zero[16] = {0};
    if (!pdu || !length || s->phase == SMP_FAILED || s->phase == SMP_DONE)
        return 0;
    if (pdu[0] == 5 && length == 2) {
        s->phase = SMP_FAILED;
        s->failure = pdu[1];
        return 0;
    }
    if (pdu[0] == 0x0b && length == 2)
        return 1; /* Request already in progress. */
    if (s->phase == SMP_RESPONSE && pdu[0] == 2 && length == 7) {
        if (pdu[1] > 4 || pdu[2] != 0 || (pdu[3] & 3) != 1 || !(pdu[3] & 8))
            return fail(s, 3);
        if (pdu[4] != 16)
            return fail(s, 6);
        if (pdu[5] || (pdu[6] & ~2))
            return fail(s, 0x0a);
        memcpy(s->response, pdu, 7);
        s->need_identity = (pdu[6] & 2) != 0;
        s->phase = SMP_PUBLIC;
        return send(s, 0x0c, s->public_key, 64);
    }
    if (s->phase == SMP_PUBLIC && pdu[0] == 0x0c && length == 65) {
        uint8_t point[64], secret[32];
        memcpy(s->peer_key, pdu + 1, 64);
        reverse32(point, pdu + 1);
        reverse32(point + 32, pdu + 33);
        if (bt_equal(s->public_key, s->peer_key, 64) ||
            !uECC_valid_public_key(point, uECC_secp256r1()) ||
            !uECC_shared_secret(point, s->private_key, secret, uECC_secp256r1())) {
            bt_wipe(secret, sizeof secret);
            return fail(s, 0x0a);
        }
        /* Temporarily store DHKey in the no-longer-needed private-key field.
         * f5 runs once both nonces are available. */
        reverse32(s->private_key, secret);
        bt_wipe(secret, sizeof secret);
        bt_wipe(point, sizeof point);
        s->phase = SMP_CONFIRM;
        return 1;
    }
    if (s->phase == SMP_CONFIRM && pdu[0] == 3 && length == 17) {
        memcpy(s->confirm, pdu + 1, 16);
        s->phase = SMP_RANDOM;
        return send(s, 4, s->nonce, 16);
    }
    if (s->phase == SMP_RANDOM && pdu[0] == 4 && length == 17) {
        memcpy(s->peer_nonce, pdu + 1, 16);
        bt_f4(s->peer_key, s->public_key, s->peer_nonce, 0, check);
        if (!bt_equal(check, s->confirm, 16))
            return fail(s, 4);
        bt_f5(s->private_key, s->nonce, s->peer_nonce, s->local, s->peer, s->mac, s->ltk);
        bt_wipe(s->private_key, sizeof s->private_key);
        bt_f6(s->mac, s->nonce, s->peer_nonce, zero, s->request + 1, s->local, s->peer, check);
        s->phase = SMP_DHKEY;
        return send(s, 0x0d, check, 16);
    }
    if (s->phase == SMP_DHKEY && pdu[0] == 0x0d && length == 17) {
        bt_f6(s->mac, s->peer_nonce, s->nonce, zero, s->response + 1, s->peer, s->local, check);
        if (!bt_equal(check, pdu + 1, 16))
            return fail(s, 0x0b);
        s->phase = SMP_ENCRYPT;
        if (!s->encrypt(s->ltk, s->context)) {
            s->phase = SMP_FAILED;
            s->failure = 8;
            return 0;
        }
        return 1;
    }
    if (s->phase == SMP_IDENTITY && pdu[0] == 8 && length == 17 && !s->got_irk) {
        memcpy(s->irk, pdu + 1, 16);
        s->got_irk = 1;
        return 1;
    }
    if (s->phase == SMP_IDENTITY && pdu[0] == 9 && length == 8 && s->got_irk) {
        if (pdu[1] > 1 || (pdu[1] == 1 && (pdu[7] & 0xc0) != 0xc0))
            return fail(s, 0x0a);
        s->identity[6] = pdu[1];
        memcpy(s->identity, pdu + 2, 6);
        s->phase = SMP_DONE;
        return 1;
    }
    return fail(s, 0x0a);
}
int bt_smp_encrypted(bt_smp *s)
{
    if (s->phase != SMP_ENCRYPT)
        return 0;
    s->phase = s->need_identity ? SMP_IDENTITY : SMP_DONE;
    return 1;
}
