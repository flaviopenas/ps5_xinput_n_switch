#include "bt_xbox.h"
#include "gip.h"
#include <string.h>
static const uint8_t input_descriptor[] = {
#include "bt_xbox_map.inc"
};
int bt_xbox_map_supported(const uint8_t *map, size_t size)
{
    return map && size >= sizeof input_descriptor &&
           !memcmp(map, input_descriptor, sizeof input_descriptor);
}
static unsigned word(const uint8_t *p)
{
    return p[0] | ((unsigned)p[1] << 8);
}
static void put(uint8_t *p, unsigned n)
{
    p[0] = (uint8_t)n;
    p[1] = (uint8_t)(n >> 8);
}
int bt_xbox_update(vpad_input *input, uint8_t id, const uint8_t *p, size_t n, unsigned *buttons)
{
    uint8_t gip[18] = {GIP_LL_INPUT_REPORT, 0, 0, 14};
    if (!input || !p || id != 1 || n != 16)
        return 0;
    unsigned b = word(p + 13), hat = p[12] & 15, translated = 0;
    if (b & 0x0001)
        translated |= GIP_A;
    if (b & 0x0002)
        translated |= GIP_B;
    if (b & 0x0008)
        translated |= GIP_X;
    if (b & 0x0010)
        translated |= GIP_Y;
    if (b & 0x0040)
        translated |= GIP_LB;
    if (b & 0x0080)
        translated |= GIP_RB;
    if (b & 0x0400)
        translated |= GIP_VIEW;
    if (b & 0x0800)
        translated |= GIP_MENU;
    if (b & 0x2000)
        translated |= GIP_LS;
    if (b & 0x4000)
        translated |= GIP_RS;
    if (p[15] & 1)
        translated |= GIP_VIEW; /* Share also provides touchpad click. */
    static const unsigned hats[9] = {0,
                                     GIP_UP,
                                     GIP_UP | GIP_RIGHT,
                                     GIP_RIGHT,
                                     GIP_RIGHT | GIP_DOWN,
                                     GIP_DOWN,
                                     GIP_DOWN | GIP_LEFT,
                                     GIP_LEFT,
                                     GIP_LEFT | GIP_UP};
    if (hat <= 8)
        translated |= hats[hat];
    put(gip + 4, translated);
    put(gip + 6, word(p + 8) & 1023);
    put(gip + 8, word(p + 10) & 1023);
    put(gip + 10, word(p) - 32768);
    put(gip + 12, 32767 - word(p + 2));
    put(gip + 14, word(p + 4) - 32768);
    put(gip + 16, 32767 - word(p + 6));
    input->guide = (b & 0x1000) != 0;
    if (buttons)
        *buttons = translated;
    return vpad_input_update(input, gip, sizeof gip);
}
