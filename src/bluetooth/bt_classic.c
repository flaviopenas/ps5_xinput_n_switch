/* Independent BR/EDR HID host for Switch-compatible Pro controllers.
 * Switch report protocol references: Linux hid-nintendo and Bluepad32.
 * No radio reset, event mask, local name, class or scan-enable writes. */
#include "bt_classic.h"
#include "log.h"
#include "gip.h"
#include <string.h>
static unsigned word(const uint8_t *p)
{
    return p[0] | ((unsigned)p[1] << 8);
}
static void put(uint8_t *p, unsigned v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
static unsigned next_id(bt_classic *b)
{
    if (++b->sequence > 255)
        b->sequence = 1;
    return b->sequence;
}
static void enqueue(bt_classic *b, unsigned op, const uint8_t *p, size_t n)
{
    if (b->tail - b->head == 8 || n > 32) {
        b->failed = 1;
        return;
    }
    unsigned i = b->tail++ % 8;
    b->commands[i].opcode = op;
    b->commands[i].size = n;
    memcpy(b->commands[i].data, p, n);
}
void bt_classic_init(bt_classic *b, const uint8_t address[6])
{
    memset(b, 0, sizeof *b);
    memcpy(b->address, address, 6);
    for (unsigned i = 0; i < 2; ++i) {
        b->channels[i].local = 0x40 + i;
        b->channels[i].mtu = 672;
    }
    switch_usb_init(&b->controller);
    b->controller.stage = 5;
}
void bt_classic_event(bt_classic *b, const uint8_t *p, size_t n, int64_t now)
{
    if (n < 2 || p[1] != n - 2)
        return;
    if (!b->ready && p[0] == 0x0f && n == 6 &&
        (word(p + 4) == 0x0405 || word(p + 4) == 0x0409 ||
         word(p + 4) == 0x0411 || word(p + 4) == 0x0413))
        log_line("BT Switch: HCI command %04X status %02X", word(p + 4), p[2]);
    uint8_t reply[32] = {0};
    if (p[0] == 0x03 && n == 13 && !memcmp(p + 5, b->address, 6) && (p[2] || p[11] == 1) &&
        !b->auth_sent) {
        if (p[2]) {
            b->failed = 1;
            return;
        }
        b->accept_pending = 0;
        b->handle = word(p + 3);
        b->encrypted = p[12] != 0;
        log_line("BT Switch: BR/EDR connection handle %u", b->handle);
        put(reply, b->handle);
        enqueue(b, 0x0411, reply, 2);
        b->auth_sent = 1;
    } else if (p[0] == 0x04 && n == 12 && !memcmp(p + 2, b->address, 6) && p[11] == 1 &&
               !b->auth_sent) {
        b->accept_pending = 1;
        memcpy(reply, b->address, 6);
        reply[6] = 1;
        enqueue(b, 0x0409, reply, 7);
    } else if (p[0] == 0x17 && n == 8 && !memcmp(p + 2, b->address, 6)) {
        memcpy(reply, b->address, 6);
        if (b->have_key) {
            memcpy(reply + 6, b->key, 16);
            enqueue(b, 0x040b, reply, 22);
        } else
            enqueue(b, 0x040c, reply, 6);
    } else if (p[0] == 0x18 && n == 25 && !memcmp(p + 2, b->address, 6)) {
        /* Unit/debug keys are inappropriate for a persisted authenticated link. */
        if (p[24] == 0 || p[24] == 3 || p[24] > 8) {
            b->failed = 1;
            return;
        }
        memcpy(b->key, p + 8, 16);
        b->key_type = p[24];
        b->have_key = b->key_changed = 1;
    } else if (p[0] == 0x31 && n == 8 && !memcmp(p + 2, b->address, 6)) {
        memcpy(reply, b->address, 6);
        reply[6] = 3;
        reply[8] = 4;
        enqueue(b, 0x042b, reply, 9);
    } else if (p[0] == 0x33 && n == 12 && !memcmp(p + 2, b->address, 6)) {
        memcpy(reply, b->address, 6);
        enqueue(b, 0x042c, reply, 6);
    } else if (p[0] == 0x16 && n == 8 && !memcmp(p + 2, b->address, 6)) {
        /* Switch Pro uses SSP. Do not guess PINs for another controller. */
        memcpy(reply, b->address, 6);
        enqueue(b, 0x040e, reply, 6);
        b->failed = 1;
        log_line("BT Switch: legacy PIN requested; unsupported pairing profile");
    } else if (p[0] == 0x06 && n == 5 && b->auth_sent && word(p + 3) == b->handle) {
        if (p[2]) {
            b->failed = 1;
            log_line("BT Switch: authentication failed, status %02X", p[2]);
            return;
        }
        b->authenticated = 1;
        log_line("BT Switch: authentication complete");
        if (!b->encrypted) {
            put(reply, b->handle);
            reply[2] = 1;
            enqueue(b, 0x0413, reply, 3);
        } else
            b->open_at = now + 200;
    } else if (p[0] == 0x08 && n == 6 && b->auth_sent && word(p + 3) == b->handle) {
        b->encrypted = p[2] == 0 && p[5] != 0;
        if (!b->encrypted)
            b->failed = 1;
        else if (b->authenticated) {
            b->open_at = now + 200;
            log_line("BT Switch: encrypted HID link ready");
        }
    } else if (p[0] == 0x36 && n == 9 && !memcmp(p + 3, b->address, 6) && p[2])
        b->failed = 1;
    else if (!b->ready && p[0] == 0x0f && n == 6 && p[2] &&
             (word(p + 4) == 0x0405 || word(p + 4) == 0x0411 || word(p + 4) == 0x0413)) {
        if (word(p + 4) == 0x0405 && (b->auth_sent || b->accept_pending))
            return;
        log_line("BT Switch: command %04X rejected, status %02X", word(p + 4), p[2]);
        b->failed = 1;
    }
}
static int signal_packet(bt_classic *b, unsigned code, unsigned id, const uint8_t *data, size_t n)
{
    uint8_t packet[128];
    if (n > sizeof packet - 4 || !id)
        return 0;
    packet[0] = (uint8_t)code;
    packet[1] = (uint8_t)id;
    put(packet + 2, (unsigned)n);
    memcpy(packet + 4, data, n);
    return b->send && b->send(1, packet, n + 4, b->context);
}
static bt_hid_channel *channel(bt_classic *b, unsigned local)
{
    for (unsigned i = 0; i < 2; ++i)
        if (b->channels[i].local == local)
            return &b->channels[i];
    return NULL;
}
static int configure(bt_classic *b, bt_hid_channel *c)
{
    uint8_t data[8] = {0};
    put(data, c->remote);
    data[4] = 1;
    data[5] = 2;
    put(data + 6, 128);
    c->config_id = next_id(b);
    return signal_packet(b, 4, c->config_id, data, 8);
}
static int options(const uint8_t *p, size_t n, unsigned *mtu, uint8_t unknown[32],
                   size_t *unknown_n)
{
    *unknown_n = 0;
    for (size_t at = 0; at < n;) {
        if (n - at < 2 || (size_t)p[at + 1] > n - at - 2)
            return -1;
        unsigned type = p[at], size = p[at + 1];
        if ((type & 127) == 1) {
            if (size != 2)
                return -1;
            *mtu = word(p + at + 2);
        } else if ((type & 127) == 2) {
            if (size != 2 || word(p + at + 2) != 0xffff)
                return 1;
        } else if (!(type & 128)) {
            if (*unknown_n + size + 2 > 32)
                return -1;
            memcpy(unknown + *unknown_n, p + at, size + 2);
            *unknown_n += size + 2;
        }
        at += size + 2;
    }
    return *unknown_n ? 3 : 0;
}
static void signalling(bt_classic *b, const uint8_t *p, size_t n)
{
    /* Validate all command boundaries before processing any command. */
    for (size_t at = 0; at < n;) {
        if (n - at < 4 || !p[at + 1] || word(p + at + 2) > n - at - 4)
            return;
        at += 4 + word(p + at + 2);
    }
    for (size_t at = 0; at < n;) {
        unsigned code = p[at], id = p[at + 1], len = word(p + at + 2);
        const uint8_t *v = p + at + 4;
        if (!b->ready)
            log_line("BT Switch: L2CAP signal %02X id %u length %u", code, id, len);
        uint8_t reply[64] = {0};
        size_t reply_size = 0;
        unsigned reply_code = 0;
        if (code == 2 && len == 4) {
            unsigned psm = word(v), remote = word(v + 2);
            bt_hid_channel *c = psm == 0x11   ? &b->channels[0]
                                : psm == 0x13 ? &b->channels[1]
                                              : NULL;
            put(reply + 2, remote);
            reply_size = 8;
            reply_code = 3;
            if (!c || remote < 0x40 || (c->remote && c->remote != remote))
                put(reply + 4, c ? 4 : 2);
            else {
                c->remote = remote;
                c->requested = 1;
                c->connect_id = 0;
                put(reply, c->local);
                if (!signal_packet(b, 3, id, reply, 8) || !configure(b, c))
                    b->failed = 1;
                at += 4 + len;
                continue;
            }
        } else if (code == 3 && len == 8) {
            bt_hid_channel *c = channel(b, word(v + 2));
            if (c && c->connect_id == id) {
                unsigned result = word(v + 4);
                log_line("BT Switch: HID channel %04X connection result %u status %u",
                         c->local, result, word(v + 6));
                if (!result && word(v) >= 0x40) {
                    c->remote = word(v);
                    c->connect_id = 0;
                    c->connect_pending = 0;
                    log_line("BT Switch: HID channel %04X opened", c->local);
                    if (!configure(b, c))
                        b->failed = 1;
                } else if (result == 1) {
                    c->connect_pending = 1;
                } else {
                    b->failed = 1;
                    log_line("BT Switch: HID channel rejected, result %u", result);
                }
            }
        } else if (code == 4 && len >= 4) {
            bt_hid_channel *c = channel(b, word(v));
            if (c && c->remote) {
                unsigned mtu = c->mtu;
                size_t extra;
                uint8_t unknown[32];
                int result = options(v + 4, len - 4, &mtu, unknown, &extra);
                if (result < 0) {
                    at += 4 + len;
                    continue;
                }
                /* Continuation fragments and non-basic modes aren't accepted. */
                if (word(v + 2))
                    result = 2;
                if (!result && mtu < 48)
                    result = 1;
                put(reply, c->remote);
                put(reply + 4, (unsigned)result);
                reply_code = 5;
                reply_size = 6;
                if (result == 3) {
                    memcpy(reply + 6, unknown, extra);
                    reply_size += extra;
                } else if (result == 1) {
                    reply[6] = 1;
                    reply[7] = 2;
                    put(reply + 8, 48);
                    reply_size = 10;
                }
                if (!result) {
                    c->mtu = mtu;
                    c->peer_configured = 1;
                }
            }
        } else if (code == 5 && len >= 6) {
            /* SCID in a configuration response is our local source CID. */
            bt_hid_channel *c = channel(b, word(v));
            if (c && c->config_id == id) {
                if (!word(v + 2) && !word(v + 4))
                    c->configured = 1;
                else if (word(v + 4) != 4)
                    b->failed = 1;
            }
        } else if (code == 6 && len == 4) {
            bt_hid_channel *c = channel(b, word(v));
            if (c && c->remote == word(v + 2)) {
                memcpy(reply, v, 4);
                reply_code = 7;
                reply_size = 4;
                b->failed = 1;
            }
        } else if (code == 8 && len <= sizeof reply) {
            memcpy(reply, v, len);
            reply_code = 9;
            reply_size = len;
        } else if (code == 0x0a && len == 2) {
            unsigned type = word(v);
            put(reply, type);
            reply_code = 0x0b;
            if (type == 2)
                reply_size = 8;
            else if (type == 3) {
                reply[4] = 2;
                reply_size = 12;
            } else {
                put(reply + 2, 1);
                reply_size = 4;
            }
        } else if (code != 1 && code != 7 && code != 9 && code != 0x0b) {
            reply_code = 1;
            reply_size = 2;
        }
        if (reply_code && !signal_packet(b, reply_code, id, reply, reply_size))
            b->failed = 1;
        at += 4 + len;
    }
}
static int channels_ready(const bt_classic *b)
{
    for (unsigned i = 0; i < 2; ++i)
        if (!b->channels[i].configured || !b->channels[i].peer_configured)
            return 0;
    return 1;
}
/* Switch simple HID report: ID, two button bytes, hat, four LE16 axes.
 * Only used after the encrypted Pro profile and full-mode setup are verified. */
int bt_classic_normalize_simple(const uint8_t *p, size_t n, uint8_t out[18], int *home)
{
    if (n != 12 || p[0] != 0x3f || p[3] > 8)
        return 0;
    unsigned buttons = 0;
    static const unsigned face[6] = {GIP_A, GIP_B, GIP_X, GIP_Y, GIP_LB, GIP_RB};
    for (unsigned i = 0; i < 6; ++i)
        if (p[1] & (1u << i))
            buttons |= face[i];
    if (p[2] & 0x01)
        buttons |= GIP_VIEW;
    if (p[2] & 0x02)
        buttons |= GIP_MENU;
    if (p[2] & 0x04)
        buttons |= GIP_LS;
    if (p[2] & 0x08)
        buttons |= GIP_RS;
    if (p[2] & 0x20)
        buttons |= GIP_VIEW;
    static const unsigned hats[9] = {GIP_UP,   GIP_UP | GIP_RIGHT,  GIP_RIGHT, GIP_RIGHT | GIP_DOWN,
                                     GIP_DOWN, GIP_DOWN | GIP_LEFT, GIP_LEFT,  GIP_LEFT | GIP_UP,
                                     0};
    buttons |= hats[p[3]];
    *home = !!(p[2] & 0x10);
    memset(out, 0, 18);
    out[0] = GIP_LL_INPUT_REPORT;
    out[3] = 14;
    put(out + 4, buttons);
    if (p[1] & 0x40)
        put(out + 6, 1020);
    if (p[1] & 0x80)
        put(out + 8, 1020);
    for (unsigned i = 0; i < 4; ++i) {
        int v = (int)word(p + 4 + 2 * i) - 32768;
        if (i & 1)
            v = 32767 - (int)word(p + 4 + 2 * i);
        const int dead = 1024;
        if (v >= -dead && v <= dead)
            v = 0;
        else
            v = v < 0 ? (v + dead) * 32768 / (32768 - dead) : (v - dead) * 32767 / (32767 - dead);
        put(out + 10 + 2 * i, (unsigned)v);
    }
    return 1;
}
void bt_classic_receive(bt_classic *b, unsigned cid, const uint8_t *p, size_t n)
{
    if (cid == 1) {
        if (b->encrypted && b->authenticated)
            signalling(b, p, n);
        return;
    }
    if (!channels_ready(b) || !n)
        return;
    if (cid == b->channels[0].local) {
        if (p[0] == 0 && n == 1)
            return; /* HID successful handshake. */
        if (p[0] == 0x15)
            b->failed = 1; /* Virtual cable unplug. */
        return;
    }
    if (cid != b->channels[1].local || p[0] != 0xa1 || n < 2)
        return;
    ++p;
    --n;
    uint32_t bit = (uint32_t)1 << (p[0] & 31);
    if (b->ready && !(b->ready_reports & bit)) {
        b->ready_reports |= bit;
        log_line("BT Switch: running report %02X length %lu", p[0], (unsigned long)n);
    }
    if (!(b->observed_reports & bit)) {
        b->observed_reports |= bit;
        log_line("BT Switch: input report %02X length %lu, setup stage %d", p[0], (unsigned long)n,
                 b->controller.stage);
    }
    if (!b->info_verified) {
        if (b->info_waiting && n >= 25 && p[0] == 0x21 && (p[13] & 0x80) && p[14] == 2) {
            if (p[17] != 3) {
                b->failed = 1;
                log_line("BT Switch: device-info profile is not a Pro controller");
                return;
            }
            b->info_verified = 1;
            b->info_waiting = 0;
            log_line("BT Switch: Pro protocol verified, firmware %u.%u", p[15], p[16]);
        }
        return;
    }
    int previous_stage = b->controller.stage;
    switch_usb_receive(&b->controller, p, (int)n);
    if (previous_stage != b->controller.stage)
        log_line("BT Switch: setup stage %d acknowledged; next %d", previous_stage,
                 b->controller.stage);
    uint8_t normalized[18];
    int home;
    if (b->ready && b->input &&
        ((n >= 49 && p[0] == 0x30 &&
          switch_usb_normalize(&b->controller, p, (int)n, normalized, &home)) ||
         bt_classic_normalize_simple(p, n, normalized, &home)))
        b->input(normalized, 18, home, b->context);
    /* An idle third-party controller need not repeat its first full report
     * after the LED acknowledgment. Retain it across the remaining setup. */
    if (!b->ready && n >= 49 && p[0] == 0x30) {
        memcpy(b->initial_input, p, sizeof b->initial_input);
        b->input_seen = 1;
    }
}
void bt_classic_tick(bt_classic *b, int64_t now)
{
    while (b->head != b->tail) {
        unsigned i = b->head++ % 8;
        if (!b->command || !b->command(b->commands[i].opcode, b->commands[i].data,
                                       b->commands[i].size, b->context))
            b->failed = 1;
        memset(b->commands[i].data, 0, sizeof b->commands[i].data);
    }
    if (b->failed || !b->authenticated || !b->encrypted || now < b->open_at)
        return;
    for (unsigned i = 0; i < 2; ++i) {
        bt_hid_channel *c = &b->channels[i];
        if (i && (!b->channels[0].configured || !b->channels[0].peer_configured))
            return;
        int retry = c->requested && c->connect_id && !c->connect_pending &&
                    now >= c->connect_at;
        if (!c->requested || retry) {
            if (retry && c->connect_attempts >= 3) {
                b->failed = 1;
                log_line("BT Switch: HID channel %04X response timeout after %u requests",
                         c->local, c->connect_attempts);
                return;
            }
            uint8_t request[4];
            put(request, i ? 0x13 : 0x11);
            put(request + 2, c->local);
            c->requested = 1;
            if (!retry)
                c->connect_id = next_id(b);
            ++c->connect_attempts;
            /* L2CAP RTX: duplicate requests retain the identifier; double
             * the wait after each retry (1, 2, 4 seconds), then stop. */
            c->connect_at = now + (1000 << (c->connect_attempts - 1));
            log_line("BT Switch: requesting HID PSM %04X local %04X id %u",
                     word(request), c->local, c->connect_id);
            if (!signal_packet(b, 2, c->connect_id, request, 4))
                b->failed = 1;
            return;
        }
    }
    if (!channels_ready(b) || b->ready)
        return;
    if (b->info_verified && b->controller.stage >= 10 && !b->controller.failed && b->input_seen) {
        uint8_t normalized[18];
        int home;
        if (switch_usb_normalize(&b->controller, b->initial_input, sizeof b->initial_input,
                                 normalized, &home)) {
            b->ready = 1;
            if (b->input)
                b->input(normalized, sizeof normalized, home, b->context);
        }
        return;
    }
    uint8_t report[65] = {0xa2};
    size_t report_size = 12;
    if (!b->info_verified) {
        if (b->info_waiting && now < b->deadline)
            return;
        if (b->info_attempts >= 3) {
            b->failed = 1;
            log_line("BT Switch: no verified device-info reply");
            return;
        }
        report[1] = 1;
        report[2] = (uint8_t)(b->controller.sequence++ & 15);
        report[4] = report[8] = 1;
        report[5] = report[9] = 0x40;
        report[6] = report[10] = 0x40;
        report[11] = 2;
        ++b->info_attempts;
        b->info_waiting = 1;
        b->deadline = now + 1000;
    } else {
        if (!switch_usb_output(&b->controller, now, report + 1)) {
            b->failed = b->controller.failed;
            return;
        }
        /* Keep IMU sampling enabled to test full-report streaming; gyro values
         * are ignored. Rumble remains disabled. */
        if (b->controller.stage == 8)
            report[12] = 0;
        report_size = b->controller.stage == 5 ? 17 : 13;
        switch_usb_sent(&b->controller, now);
    }
    if (!b->send(b->channels[1].remote, report, report_size, b->context))
        b->failed = 1;
}
int bt_classic_advert_parse(const uint8_t *p, size_t n, bt_advert_fn fn, void *context)
{
    if (!p || n < 2 || (p[0] != 0x22 && p[0] != 0x2f && p[0] != 0x07 && p[0] != 0x02))
        return 0;
    if (p[1] != n - 2 || n < 3)
        return -1;
    if (p[0] == 0x07) {
        if (n != 257 || p[2])
            return n == 257 ? 0 : -1;
        bt_advert r = {0};
        memcpy(r.address, p + 3, 6);
        r.address_type = BT_ADDRESS_CLASSIC;
        r.event_type = 4;
        size_t size = 0;
        while (size < 31 && p[9 + size])
            ++size;
        memcpy(r.name, p + 9, size);
        if (fn)
            fn(&r, context);
        return 1;
    }
    unsigned stride = p[0] == 0x2f ? 254 : p[0] == 0x02 ? 14 : 14;
    if (n != 3 + (size_t)p[2] * stride || (p[0] == 0x2f && p[2] != 1))
        return -1;
    /* Validate all EIR fields before emitting any discovery records. */
    if (p[0] == 0x2f)
        for (size_t at = 17; at < n && p[at];) {
            if ((size_t)p[at] > n - at - 1)
                return -1;
            at += 1 + p[at];
        }
    for (unsigned i = 0; i < p[2]; ++i) {
        const uint8_t *v = p + 3 + i * stride;
        bt_advert r = {0};
        memcpy(r.address, v, 6);
        r.address_type = BT_ADDRESS_CLASSIC;
        const uint8_t *cod = v + (p[0] == 0x02 ? 9 : 8);
        r.hid_service = (cod[1] & 0x1f) == 5 && (cod[0] & 0x0c) != 0;
        r.rssi = p[0] == 0x02 ? 0 : (int)(int8_t)v[13];
        if (p[0] == 0x2f)
            for (size_t at = 17; at < n && p[at]; at += 1 + p[at]) {
                if (p[at] >= 2 && (p[at + 1] == 8 || p[at + 1] == 9)) {
                    size_t len = p[at] - 1;
                    if (len > 31)
                        len = 31;
                    memcpy(r.name, p + at + 2, len);
                    r.name[len] = 0;
                }
            }
        if (fn)
            fn(&r, context);
    }
    return p[2];
}
