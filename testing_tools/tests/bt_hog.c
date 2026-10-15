#include "bt_hog.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
void log_line(const char *format, ...) { (void)format; }
static unsigned writes, inputs;
static uint8_t sent[23];
static int send(const uint8_t *p, size_t n, void *unused)
{
    (void)unused; assert(n <= sizeof sent); memcpy(sent, p, n); ++writes; return 1;
}
static void input(uint8_t id, const uint8_t *p, size_t n, void *unused)
{
    (void)unused; assert(id == 2 && n == 1 && p[0] == 1); ++inputs;
}
static bt_hog fresh(void)
{
    bt_hog h = {0}; h.send = send; h.input = input; return h;
}
static void attributes(bt_hog *h, unsigned first, unsigned last)
{
    const unsigned uuids[] = {0,0x2800,0x2a4b,0x2803,0x2a4d,0x2902,0x2908,
                             0x2803,0x2a4d,0x2908,0x2803,0x2a4d,0x2902,0x2908};
    uint8_t p[23] = {5,1}; unsigned n = 2;
    for (unsigned i = first; i <= last; ++i) {
        p[n++] = (uint8_t)i; p[n++] = 0;
        p[n++] = (uint8_t)uuids[i]; p[n++] = (uint8_t)(uuids[i] >> 8);
    }
    bt_hog_receive(h, p, n, 10);
}
int main(void)
{
    bt_hog h = fresh(); bt_hog_start(&h, 1, 13, 0);
    assert(sent[0] == 4 && sent[1] == 1 && sent[3] == 13);
    attributes(&h, 1, 5); attributes(&h, 6, 10); attributes(&h, 11, 13);
    assert(sent[0] == 0x0a && sent[1] == 2);
    uint8_t map[23] = {0x0b}; memset(map + 1, 0xaa, 22);
    bt_hog_receive(&h, map, sizeof map, 20);
    assert(sent[0] == 0x0c && sent[1] == 2 && sent[3] == 22);
    const uint8_t end[] = {0x0d,0xbb}; bt_hog_receive(&h, end, sizeof end, 30);
    assert(h.map_size == 23 && h.map[22] == 0xbb && sent[1] == 6);
    const uint8_t main_ref[] = {0x0b,1,1}, output_ref[] = {0x0b,3,2}, guide_ref[] = {0x0b,2,1};
    bt_hog_receive(&h, main_ref, 3, 40); assert(sent[1] == 9);
    bt_hog_receive(&h, output_ref, 3, 50); assert(sent[1] == 13);
    bt_hog_receive(&h, guide_ref, 3, 60); assert(sent[0] == 0x12 && sent[1] == 5);
    const uint8_t ack = 0x13;
    bt_hog_receive(&h, &ack, 1, 70); assert(sent[1] == 12);
    bt_hog_receive(&h, &ack, 1, 80); assert(h.ready && !h.failed);
    const uint8_t notification[] = {0x1b,11,0,1};
    bt_hog_receive(&h, notification, sizeof notification, 90); assert(inputs == 1);
    const uint8_t unknown[] = {0x1b,8,0,1};
    bt_hog_receive(&h, unknown, sizeof unknown, 90); assert(inputs == 1);
    const uint8_t indication[] = {0x1d,1,0};
    bt_hog_receive(&h, indication, sizeof indication, 90); assert(sent[0] == 0x1e);
    h = fresh(); bt_hog_start(&h, 1, 13, 0);
    const uint8_t truncated[] = {5,1,1,0,0};
    bt_hog_receive(&h, truncated, sizeof truncated, 1); assert(h.failed);
    h = fresh(); bt_hog_start(&h, 1, 13, 0);
    const uint8_t unordered[] = {5,1,2,0,0,0,1,0,0,0};
    bt_hog_receive(&h, unordered, sizeof unordered, 1); assert(h.failed);
    h = fresh(); bt_hog_start(&h, 1, 13, 0); bt_hog_tick(&h, 3000); assert(h.failed);
    h = fresh(); bt_hog_start(&h, 1, 3, 0);
    uint8_t wide[20] = {5,2,1,0}; wide[16] = 0x4b; wide[17] = 0x2a;
    bt_hog_receive(&h, wide, sizeof wide, 1);
    const uint8_t base[12] = {0xfb,0x34,0x9b,0x5f,0x80,0,0,0x80,0,0x10,0,0};
    wide[2] = 2; memcpy(wide + 4, base, 12); wide[16] = 0x4d;
    bt_hog_receive(&h, wide, sizeof wide, 2);
    const uint8_t reference_attr[] = {5,1,3,0,8,0x29};
    bt_hog_receive(&h, reference_attr, sizeof reference_attr, 3);
    assert(!h.failed && h.map_handle == 1 && h.report_count == 1 && h.reports[0].value == 2);
    assert(writes > 0); puts("HID-over-GATT discovery, long map reads, subscriptions and validation passed");
}
