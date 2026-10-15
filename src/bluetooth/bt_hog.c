#include "bt_hog.h"
#include "log.h"
#include <string.h>
#include <stdio.h>

enum { FIND = 1, MAP, REFERENCE, SUBSCRIBE, READY };
static unsigned word(const uint8_t *p)
{
    return p[0] | ((unsigned)p[1] << 8);
}
static void put(uint8_t *p, unsigned n)
{
    p[0] = (uint8_t)n;
    p[1] = (uint8_t)(n >> 8);
}
static void fail(bt_hog *h, const char *why)
{
    h->failed = 1;
    h->ready = 0;
    log_line("BT HID: setup failed: %s", why);
}
static void request(bt_hog *h, const uint8_t *p, size_t n, int64_t now)
{
    h->deadline = now + 3000;
    if (!h->send || !h->send(p, n, h->context))
        fail(h, "ATT transport");
}
static void find(bt_hog *h, int64_t now)
{
    uint8_t p[5] = {4, 0, 0, 0, 0};
    put(p + 1, h->cursor);
    put(p + 3, h->last);
    request(h, p, sizeof p, now);
}
static void read_map(bt_hog *h, int64_t now)
{
    uint8_t p[5] = {h->map_size ? 0x0c : 0x0a, 0, 0, 0, 0};
    put(p + 1, h->map_handle);
    put(p + 3, (unsigned)h->map_size);
    request(h, p, h->map_size ? 5 : 3, now);
}
static void next_reference(bt_hog *h, int64_t now)
{
    if (h->index == h->report_count) {
        h->phase = SUBSCRIBE;
        h->index = 0;
    } else {
        uint8_t p[3] = {0x0a, 0, 0};
        put(p + 1, h->reports[h->index].reference);
        request(h, p, sizeof p, now);
        return;
    }
    while (h->index < h->report_count && h->reports[h->index].type != 1)
        ++h->index;
    if (h->index < h->report_count) {
        uint8_t p[5] = {0x12, 0, 0, 1, 0};
        if (!h->reports[h->index].cccd) {
            fail(h, "input report has no CCCD");
            return;
        }
        put(p + 1, h->reports[h->index].cccd);
        request(h, p, sizeof p, now);
    } else {
        unsigned inputs = 0;
        for (unsigned i = 0; i < h->report_count; ++i)
            inputs += h->reports[i].type == 1;
        if (!inputs) {
            fail(h, "no input report");
            return;
        }
        h->ready = 1;
        h->phase = READY;
        log_line("BT HID: ready, %u input reports, map %lu bytes", inputs,
                 (unsigned long)h->map_size);
    }
}
static void attributes_done(bt_hog *h, int64_t now)
{
    int current = -1;
    for (unsigned i = 0; i < h->count; ++i) {
        unsigned uuid = h->attributes[i].uuid, handle = h->attributes[i].handle;
        if (uuid == 0x2803)
            current = -1;
        else if (uuid == 0x2a4b)
            h->map_handle = (uint16_t)handle;
        else if (uuid == 0x2a4d) {
            if (h->report_count == 8) {
                fail(h, "too many reports");
                return;
            }
            current = (int)h->report_count++;
            h->reports[current].value = (uint16_t)handle;
        } else if (current >= 0 && uuid == 0x2902)
            h->reports[current].cccd = (uint16_t)handle;
        else if (current >= 0 && uuid == 0x2908)
            h->reports[current].reference = (uint16_t)handle;
    }
    if (!h->map_handle || !h->report_count) {
        fail(h, "missing map/reports");
        return;
    }
    for (unsigned i = 0; i < h->report_count; ++i)
        if (!h->reports[i].reference) {
            fail(h, "missing report reference");
            return;
        }
    h->phase = MAP;
    read_map(h, now);
}
void bt_hog_start(bt_hog *h, uint16_t first, uint16_t last, int64_t now)
{
    h->first = h->cursor = first;
    h->last = last;
    h->phase = FIND;
    if (!first || last < first) {
        fail(h, "invalid service range");
        return;
    }
    find(h, now);
}
void bt_hog_tick(bt_hog *h, int64_t now)
{
    if (h->phase && !h->ready && !h->failed && now >= h->deadline)
        fail(h, "ATT response timeout");
}
void bt_hog_receive(bt_hog *h, const uint8_t *p, size_t n, int64_t now)
{
    if (!p || !n || h->failed)
        return;
    if (p[0] == 0x1b && n >= 3) {
        if (!h->ready)
            return;
        for (unsigned i = 0; i < h->report_count; ++i)
            if (h->reports[i].type == 1 && h->reports[i].value == word(p + 1) && h->input)
                h->input(h->reports[i].id, p + 3, n - 3, h->context);
        return;
    }
    if (p[0] == 0x1d && n >= 3) {
        const uint8_t confirm = 0x1e;
        if (h->send)
            (void)h->send(&confirm, 1, h->context);
        return;
    }
    if (p[0] == 1 && n == 5) {
        unsigned expected = h->phase == FIND        ? 4
                            : h->phase == MAP       ? (h->map_size ? 0x0c : 0x0a)
                            : h->phase == REFERENCE ? 0x0a
                            : h->phase == SUBSCRIBE ? 0x12
                                                    : 0;
        if (p[1] != expected)
            return;
        if (h->phase == FIND && p[4] == 0x0a && word(p + 2) == h->cursor)
            attributes_done(h, now);
        else if (h->phase == MAP && h->map_size && p[4] == 7 && word(p + 2) == h->map_handle) {
            h->phase = REFERENCE;
            h->index = 0;
            next_reference(h, now);
        } else {
            log_line("BT HID: ATT error %02X for request %02X", p[4], p[1]);
            fail(h, "ATT error");
        }
        return;
    }
    if (h->phase == FIND && p[0] == 5 && n >= 2) {
        unsigned entry = p[1] == 1 ? 4 : p[1] == 2 ? 18 : 0;
        if (!entry || n <= 2 || n > (h->mtu ? h->mtu : 23) || (n - 2) % entry) {
            fail(h, "malformed attribute list");
            return;
        }
        unsigned last = h->cursor - 1;
        for (size_t at = 2; at < n; at += entry) {
            unsigned handle = word(p + at);
            if (handle <= last || handle > h->last || h->count == 64) {
                fail(h, "attribute order/capacity");
                return;
            }
            last = handle;
            h->attributes[h->count].handle = (uint16_t)handle;
            unsigned uuid = entry == 4 ? word(p + at + 2) : 0;
            const uint8_t base[12] = {0xfb, 0x34, 0x9b, 0x5f, 0x80, 0, 0, 0x80, 0, 0x10, 0, 0};
            const uint8_t zero_base[12] = {0};
            /* The tested Series firmware returns zero-expanded 2Axx UUIDs in
             * Find Information, rather than the Bluetooth base UUID. Limit
             * this compatibility rule to HID attributes within this service. */
            unsigned short_uuid = entry == 18 ? word(p + at + 14) : 0;
            int xbox_hid_uuid =
                short_uuid >= 0x2a4a && short_uuid <= 0x2a4e && !memcmp(p + at + 2, zero_base, 12);
            if (entry == 18 && (!memcmp(p + at + 2, base, 12) || xbox_hid_uuid) && !p[at + 16] &&
                !p[at + 17])
                uuid = word(p + at + 14);
            h->attributes[h->count++].uuid = (uint16_t)uuid;
            log_debug("BT HID: attribute %04X UUID %04X (%u bits)", handle, uuid,
                      entry == 4 ? 16 : 128);
            if (entry == 18 && !uuid) {
                char hex[33];
                for (unsigned i = 0; i < 16; ++i)
                    snprintf(hex + i * 2, 3, "%02X", p[at + 2 + i]);
                log_debug("BT HID: UUID bytes %s", hex);
            }
        }
        if (last == h->last)
            attributes_done(h, now);
        else {
            h->cursor = (uint16_t)(last + 1);
            find(h, now);
        }
    } else if (h->phase == MAP && p[0] == (h->map_size ? 0x0d : 0x0b)) {
        if (n > (h->mtu ? h->mtu : 23) || n - 1 > sizeof h->map - h->map_size) {
            fail(h, "report map capacity/MTU");
            return;
        }
        memcpy(h->map + h->map_size, p + 1, n - 1);
        h->map_size += n - 1;
        if (n == (h->mtu ? h->mtu : 23))
            read_map(h, now);
        else {
            h->phase = REFERENCE;
            h->index = 0;
            next_reference(h, now);
        }
    } else if (h->phase == REFERENCE && p[0] == 0x0b) {
        if (n != 3 || p[2] < 1 || p[2] > 3) {
            fail(h, "invalid report reference");
            return;
        }
        bt_hog_report *r = &h->reports[h->index];
        r->id = p[1];
        r->type = p[2];
        log_debug("BT HID: report ID %u type %u value %04X CCCD %04X", r->id, r->type, r->value,
                  r->cccd);
        ++h->index;
        next_reference(h, now);
    } else if (h->phase == SUBSCRIBE && p[0] == 0x13) {
        if (n != 1) {
            fail(h, "malformed write response");
            return;
        }
        ++h->index;
        /* next_reference also handles subscription advancement. */
        while (h->index < h->report_count && h->reports[h->index].type != 1)
            ++h->index;
        if (h->index < h->report_count) {
            uint8_t request_pdu[5] = {0x12, 0, 0, 1, 0};
            if (!h->reports[h->index].cccd) {
                fail(h, "input report has no CCCD");
                return;
            }
            put(request_pdu + 1, h->reports[h->index].cccd);
            request(h, request_pdu, sizeof request_pdu, now);
        } else {
            h->ready = 1;
            h->phase = READY;
            log_line("BT HID: notifications enabled; map %lu bytes", (unsigned long)h->map_size);
        }
    }
}
