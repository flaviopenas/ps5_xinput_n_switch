#include "bt_xbox.h"
#include "gip.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
void log_line(const char *format, ...) { (void)format; }
static unsigned buttons(const vpad_input *v)
{
    uint32_t b; memcpy(&b, v->report + 12, 4); return b;
}
static void put(uint8_t *p, unsigned n) { p[0] = (uint8_t)n; p[1] = (uint8_t)(n >> 8); }
int main(void)
{
    FILE *f = fopen("testing_tools/tests/xbox-series-bt-map.hex", "r"); assert(f);
    uint8_t map[1024]; size_t n = 0; unsigned byte;
    while (fscanf(f, "%2x", &byte) == 1) { assert(n < sizeof map); map[n++] = (uint8_t)byte; }
    fclose(f); assert(n == 283 && bt_xbox_map_supported(map, n));
    assert(!bt_xbox_map_supported(map, 190)); map[7] = 2; assert(!bt_xbox_map_supported(map, n));
    vpad_input v; vpad_input_init(&v);
    uint8_t p[16] = {0}; for (unsigned i = 0; i < 8; i += 2) put(p + i, 32768);
    unsigned b; assert(bt_xbox_update(&v, 1, p, sizeof p, &b));
    assert(!b && !buttons(&v) && !v.guide);
    for (unsigned i = 16; i < 20; ++i) assert(v.report[i] == 128);
    put(p, 0); put(p + 2, 0); put(p + 4, 65535); put(p + 6, 65535);
    put(p + 8, 1023); put(p + 10, 512);
    assert(bt_xbox_update(&v, 1, p, 16, &b));
    assert(v.report[16] == 0 && v.report[17] == 0 && v.report[18] == 255 && v.report[19] == 255);
    assert(v.report[20] == 255 && v.report[21] == 128 && (buttons(&v) & 0x300) == 0x300);
    memset(p + 8, 0, 8);
    const unsigned masks[] = {1,2,8,16,64,128,0x400,0x800,0x2000,0x4000};
    const unsigned gip_bits[] = {GIP_A,GIP_B,GIP_X,GIP_Y,GIP_LB,GIP_RB,GIP_VIEW,GIP_MENU,GIP_LS,GIP_RS};
    const unsigned ps_bits[] = {0x4000,0x2000,0x8000,0x1000,0x400,0x800,0x100000,8,2,4};
    for (unsigned i = 0; i < 10; ++i) {
        put(p + 13, masks[i]); assert(bt_xbox_update(&v, 1, p, 16, &b));
        assert(b == gip_bits[i] && buttons(&v) == ps_bits[i]);
    }
    put(p + 13, 0x1000); assert(bt_xbox_update(&v, 1, p, 16, &b));
    assert(v.guide && buttons(&v) == 0x10000);
    put(p + 13, 0); assert(bt_xbox_update(&v, 1, p, 16, &b)); assert(!v.guide && !buttons(&v));
    const unsigned hats[] = {0,0x10,0x30,0x20,0x60,0x40,0xc0,0x80,0x90};
    for (unsigned i = 0; i < 9; ++i) { p[12] = (uint8_t)i; assert(bt_xbox_update(&v, 1, p, 16, &b)); assert(buttons(&v) == hats[i]); }
    p[12] = 15; assert(bt_xbox_update(&v, 1, p, 16, &b)); assert(!buttons(&v));
    p[12] = 0; p[15] = 1; assert(bt_xbox_update(&v, 1, p, 16, &b)); assert(buttons(&v) == 0x100000);
    p[15] = 0; put(p + 13, 0xc00); assert(bt_xbox_update(&v, 1, p, 16, &b)); assert(b == (GIP_VIEW|GIP_MENU));
    vpad_input saved = v; assert(!bt_xbox_update(&v, 1, p, 15, &b)); assert(!memcmp(&v, &saved, sizeof v));
    assert(!bt_xbox_update(&v, 3, p, 16, &b)); assert(!memcmp(&v, &saved, sizeof v));
    puts("Xbox BLE mapping passed: profile guard, sticks, triggers, buttons, hats, Guide edges and malformed packets");
}
