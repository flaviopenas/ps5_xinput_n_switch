/* Protocol reference: Linux hid-nintendo and dekuNukem's Switch protocol notes.
 * Independent implementation; no controller pairing or flash writes. */
#include "switch_usb.h"
#include "gip.h"
#include <string.h>

static unsigned packed(const unsigned char *p, int second)
{
    return second ? ((unsigned)p[1] >> 4) | ((unsigned)p[2] << 4)
                  : p[0] | (((unsigned)p[1] & 15) << 8);
}
void switch_usb_init(switch_usb *p)
{
    memset(p, 0, sizeof *p);
    for (int i = 0; i < 4; ++i) {
        p->axes[i].center = 2000;
        p->axes[i].below = p->axes[i].above = 1500;
    }
    p->stage = 1;
}
/* USB handshake, baud rate, handshake again, no timeout, factory calibration,
 * IMU sampling, full input mode, vibration enable, and one player light.
 * IMU samples are ignored and motors receive only neutral rumble. */
static unsigned command(int stage)
{
    static const unsigned commands[] = {0, 2, 3, 2, 4, 0x10, 0x40, 0x03, 0x48, 0x30};
    return stage >= 1 && stage <= 9 ? commands[stage] : 0;
}
int switch_usb_output(switch_usb *p, int64_t now, unsigned char out[64])
{
    if (!command(p->stage) || p->failed)
        return 0;
    if (p->awaiting) {
        if (now < p->deadline)
            return 0;
        p->awaiting = 0;
        if (p->retries >= 3) {
            /* Calibration is optional; never guess that handshake/mode worked. */
            if (p->stage == 5 || p->stage == 9) {
                ++p->stage;
                p->retries = 0;
            } else {
                p->failed = 1;
                return 0;
            }
        }
    }
    if (!command(p->stage))
        return 0;
    memset(out, 0, 64);
    if (p->stage <= 4) {
        out[0] = 0x80;
        out[1] = (unsigned char)command(p->stage);
        /* The USB commands contain just the report ID and command byte,
         * as in Linux hid-nintendo. Do not append a full input-report pad. */
        return 2;
    }
    out[0] = 1;
    out[1] = (unsigned char)(p->sequence & 15);
    /* Neutral HD rumble encoding for both motors. */
    out[3] = out[7] = 1;
    out[4] = out[8] = 0x40;
    out[5] = out[9] = 0x40;
    out[10] = (unsigned char)command(p->stage);
    if (p->stage == 5) {
        out[11] = 0x3d;
        out[12] = 0x60;
        out[15] = 18;
        return 64;
    }
    out[11] = p->stage == 7 ? 0x30 : 1;
    return 64;
}
void switch_usb_sent(switch_usb *p, int64_t now)
{
    ++p->sequence;
    if (p->stage == 4) {
        ++p->stage;
        p->retries = 0;
        return;
    }
    ++p->retries;
    p->awaiting = 1;
    p->deadline = now + 1000;
}
static int calibration(switch_usb *p, const unsigned char *data)
{
    switch_axis axes[4];
    for (int stick = 0; stick < 2; ++stick) {
        const unsigned char *d = data + stick * 9;
        int c = stick ? 0 : 3, lo = stick ? 3 : 6, hi = stick ? 6 : 0;
        for (int xy = 0; xy < 2; ++xy) {
            switch_axis *a = &axes[stick * 2 + xy];
            a->center = (int)packed(d + c, xy);
            a->below = (int)packed(d + lo, xy);
            a->above = (int)packed(d + hi, xy);
            if (a->center < 500 || a->center > 3500 || a->below < 100 || a->above < 100 ||
                a->below > a->center || a->above > 4095 - a->center)
                return 0;
        }
    }
    memcpy(p->axes, axes, sizeof axes);
    p->calibrated = 1;
    return 1;
}
void switch_usb_receive(switch_usb *p, const unsigned char *msg, int n)
{
    if (!p->awaiting || n < 2)
        return;
    if (p->stage <= 3) {
        if (msg[0] != 0x81 || msg[1] != command(p->stage))
            return;
    } else {
        if (n < 15 || msg[0] != 0x21 || !(msg[13] & 0x80) || msg[14] != command(p->stage))
            return;
        if (p->stage == 5) {
            if (n < 38 || msg[15] != 0x3d || msg[16] != 0x60 || msg[17] || msg[18] || msg[19] != 18)
                return;
            (void)calibration(p, msg + 20);
        }
    }
    ++p->stage;
    p->awaiting = p->retries = 0;
}
static int axis(const switch_axis *a, unsigned value)
{
    int delta = (int)value - a->center;
    /* A small center deadzone suppresses idle drift without learning a held stick. */
    int range = delta < 0 ? a->below : a->above;
    int dead = range > 320 ? 160 : range / 2;
    if (delta >= -dead && delta <= dead)
        return 0;
    int v = delta < 0 ? (delta + dead) * 32768 / (range - dead)
                      : (delta - dead) * 32767 / (range - dead);
    return v < -32768 ? -32768 : v > 32767 ? 32767 : v;
}
int switch_usb_normalize(const switch_usb *p, const unsigned char *msg, int n,
                         unsigned char out[18], int *home)
{
    if (p->failed || p->stage < 10 || n < 13 || (msg[0] != 0x30 && msg[0] != 0x21))
        return 0;
    unsigned b = 0;
    /* Match physical positions: Nintendo B (south) is PS Cross; A is Circle. */
    if (msg[3] & 0x01)
        b |= GIP_X; /* Y, west */
    if (msg[3] & 0x02)
        b |= GIP_Y; /* X, north */
    if (msg[3] & 0x04)
        b |= GIP_A; /* B, south */
    if (msg[3] & 0x08)
        b |= GIP_B; /* A, east */
    if (msg[3] & 0x40)
        b |= GIP_RB;
    if (msg[4] & 0x01)
        b |= GIP_VIEW;
    if (msg[4] & 0x02)
        b |= GIP_MENU;
    if (msg[4] & 0x04)
        b |= GIP_RS;
    if (msg[4] & 0x08)
        b |= GIP_LS;
    if (msg[5] & 0x01)
        b |= GIP_DOWN;
    if (msg[5] & 0x02)
        b |= GIP_UP;
    if (msg[5] & 0x04)
        b |= GIP_RIGHT;
    if (msg[5] & 0x08)
        b |= GIP_LEFT;
    if (msg[5] & 0x40)
        b |= GIP_LB;
    /* Capture takes precedence over Minus as the touchpad/share shortcut. */
    if (msg[4] & 0x20)
        b |= GIP_VIEW;
    *home = !!(msg[4] & 0x10);
    memset(out, 0, 18);
    out[0] = GIP_LL_INPUT_REPORT;
    out[3] = 14;
    out[4] = (unsigned char)b;
    out[5] = (unsigned char)(b >> 8);
    if (msg[5] & 0x80) {
        out[6] = 0xfc;
        out[7] = 3;
    }
    if (msg[3] & 0x80) {
        out[8] = 0xfc;
        out[9] = 3;
    }
    for (int i = 0; i < 4; ++i) {
        int v = axis(&p->axes[i], packed(msg + 6 + (i / 2) * 3, i & 1));
        out[10 + i * 2] = (unsigned char)v;
        out[11 + i * 2] = (unsigned char)((unsigned)v >> 8);
    }
    return 1;
}
