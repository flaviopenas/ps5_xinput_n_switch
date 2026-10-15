#include "gip.h"
#include "vpad.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* The test exercises translation on the host without console ioctls. */
void log_line(const char *fmt, ...) { (void)fmt; }

static uint32_t buttons(const unsigned char *out)
{
    uint32_t value;
    memcpy(&value, out + 12, sizeof value);
    return value;
}

int main(void)
{
    unsigned char in[18] = {0x20, 0, 1, 14};
    unsigned char out[VPAD_REPORT_SIZE];
    const struct { unsigned xbox; uint32_t ps; } cases[] = {
        {GIP_A, 0x4000}, {GIP_B, 0x2000}, {GIP_X, 0x8000},
        {GIP_Y, 0x1000}, {GIP_UP, 0x10}, {GIP_RIGHT, 0x20},
        {GIP_DOWN, 0x40}, {GIP_LEFT, 0x80}, {GIP_LB, 0x400},
        {GIP_RB, 0x800}, {GIP_LS, 2}, {GIP_RS, 4},
        {GIP_MENU, 8}, {GIP_VIEW, 0x100000}
    };
    vpad_translate(in, out);
    assert(buttons(out) == 0);
    assert(out[16] == 128 && out[17] == 127);
    assert(out[18] == 128 && out[19] == 127);
    assert(out[20] == 0 && out[21] == 0 && out[23] == 1);
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        in[4] = (unsigned char)cases[i].xbox;
        in[5] = (unsigned char)(cases[i].xbox >> 8);
        vpad_translate(in, out);
        assert(buttons(out) == cases[i].ps);
    }
    in[4] = in[5] = 0;
    in[6] = in[8] = 0xff;
    in[7] = in[9] = 3;
    /* X minimum and Y maximum should both map to zero. */
    in[10] = 0; in[11] = 0x80;
    in[12] = 0xff; in[13] = 0x7f;
    /* Right X maximum and Y minimum should map to 255. */
    in[14] = 0xff; in[15] = 0x7f;
    in[16] = 0; in[17] = 0x80;
    vpad_translate(in, out);
    assert(out[16] == 0 && out[17] == 0);
    assert(out[18] == 255 && out[19] == 255);
    assert(out[20] == 255 && out[21] == 255);
    assert(buttons(out) == 0x300);
    memset(in + 4, 0, 14);
    vpad_translate(in, out);
    assert(buttons(out) == 0 && out[20] == 0 && out[21] == 0);
    vpad_input state;
    unsigned char guide[5] = {GIP_CMD_GUIDE_BUTTON, GIP_OPT_ACK, 1, 1, 1};
    unsigned char saved[VPAD_REPORT_SIZE];
    vpad_input_init(&state);
    assert(state.report[16] == 128 && state.report[18] == 128);
    assert(vpad_input_update(&state, guide, 4) == 0);
    assert(buttons(state.report) == 0);
    assert(vpad_input_update(&state, guide, 5) == 1);
    assert(buttons(state.report) == 0x10000);
    in[4] = GIP_A;
    in[6] = 0xff; in[7] = 3;
    in[10] = 0xff; in[11] = 0x7f;
    assert(vpad_input_update(&state, in, sizeof in) == 1);
    assert(buttons(state.report) == (0x10000 | 0x4000 | 0x100));
    assert(state.report[16] == 255 && state.report[20] == 255);
    memcpy(saved, state.report, sizeof saved);
    guide[4] = 0;
    assert(vpad_input_update(&state, guide, 5) == 1);
    assert(buttons(state.report) == (0x4000 | 0x100));
    /* Only the PS bit changes on a separate Guide packet. */
    saved[14] &= 0xfe;
    assert(memcmp(saved, state.report, sizeof saved) == 0);
    guide[4] = 2;
    assert(vpad_input_update(&state, guide, 5) == 1 && state.guide);
    guide[3] = 0;
    assert(vpad_input_update(&state, guide, 5) == 0 && state.guide);
    guide[0] = 0xff;
    assert(vpad_input_update(&state, guide, 5) == 0);
    unsigned char native[20] = {0, 20};
    unsigned char normalized[18];
    int native_guide = 0;
    const struct { unsigned wire; uint32_t ps; } native_cases[] = {
        {0x0001, 0x10}, {0x0002, 0x40}, {0x0004, 0x80}, {0x0008, 0x20},
        {0x0010, 8}, {0x0020, 0x100000}, {0x0040, 2}, {0x0080, 4},
        {0x0100, 0x400}, {0x0200, 0x800}, {0x0400, 0x10000},
        {0x1000, 0x4000}, {0x2000, 0x2000}, {0x4000, 0x8000}, {0x8000, 0x1000}
    };
    vpad_input_init(&state);
    for (unsigned i = 0; i < sizeof native_cases / sizeof native_cases[0]; ++i) {
        native[2] = native_cases[i].wire;
        native[3] = native_cases[i].wire >> 8;
        assert(vpad_normalize_usb360(native, 20, normalized, &native_guide));
        state.guide = native_guide;
        assert(vpad_input_update(&state, normalized, sizeof normalized));
        assert(buttons(state.report) == native_cases[i].ps);
    }
    native[2] = native[3] = 0;
    native[4] = native[5] = 255;
    native[6] = 0; native[7] = 0x80;
    native[8] = 0xff; native[9] = 0x7f;
    native[10] = 0xff; native[11] = 0x7f;
    native[12] = 0; native[13] = 0x80;
    assert(vpad_normalize_usb360(native, 20, normalized, &native_guide));
    state.guide = native_guide;
    assert(vpad_input_update(&state, normalized, 18));
    assert(buttons(state.report) == 0x300);
    assert(state.report[16] == 0 && state.report[17] == 0);
    assert(state.report[18] == 255 && state.report[19] == 255);
    assert(state.report[20] == 255 && state.report[21] == 255);
    assert(!vpad_normalize_usb360(native, 19, normalized, &native_guide));
    native[0] = 1;
    assert(!vpad_normalize_usb360(native, 20, normalized, &native_guide));
    native[0] = 0; native[1] = 0;
    assert(!vpad_normalize_usb360(native, 20, normalized, &native_guide));
    puts("Mapping passed: GIP and USB360 buttons, sticks, triggers, Guide and malformed packets.");
    return 0;
}
