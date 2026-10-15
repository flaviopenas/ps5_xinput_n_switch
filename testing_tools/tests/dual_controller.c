#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "usb_test_types.h"

/* Exercise the actual scheduler and drivers with simulated USB devices
 * and separate /dev/hid descriptors. No console services are used. */
static time_t fake_time = 100;
time_t test_time(time_t *out) { if (out) *out = fake_time; return fake_time; }
int test_clock_gettime(clockid_t clock, struct timespec *out)
{ (void)clock; out->tv_sec = fake_time; out->tv_nsec = 0; return 0; }
static int sleeps, stop_at;
int test_usleep(useconds_t duration)
{ assert(duration == 1000); ++sleeps; ++fake_time; return 0; }
#define time test_time
#define clock_gettime test_clock_gettime
#define usleep test_usleep
#define main payload_main
#ifdef USB_SERVICE
#include "../../src/usb/usb_bridge.c"
#else
#include "../src/usb/usb_bridge_legacy.c"
#endif
#undef main
#undef time
#undef clock_gettime
#undef usleep

typedef struct {
    int opened, open_calls, configs, closed, out_pending, in_pending;
    int gone, queued, length, commands[256], usb360, switch_pro, guide, native_led, reply_length;
    unsigned char packet[64], reply[64];
    struct usb_fs_endpoint *eps;
} fake_usb;
static fake_usb usb[USB_MAX_CONTROLLERS];
static void pack_switch(unsigned char *p, unsigned x, unsigned y)
{ p[0]=x; p[1]=(x>>8)|((y&15)<<4); p[2]=y>>4; }
static struct {
    int open, closed, handle, submits, deleted, busy, fatal;
    uint32_t user;
    unsigned char report[VPAD_REPORT_SIZE];
} hid[8];
static int hid_count, fail_add, acknowledgments[USB_MAX_CONTROLLERS];

static int input_logs, guide_logs;
void log_line(const char *fmt, ...)
{
    if (strstr(fmt, "USB input:")) ++input_logs;
    if (strstr(fmt, "USB Home:")) ++guide_logs;
}
void notify(const char *fmt, ...) { (void)fmt; }
void log_poll(void) {}
int log_stop_requested(void) { return stop_at && sleeps >= stop_at; }
int log_open(const char *dir, const char *path) { (void)dir; (void)path; return 1; }
void log_close(void) {}
int current_user(uint32_t *user) { *user = 1; return 1; }
void pad_abi_log(void) {}

static int node_index(const char *path)
{
    for (int i = 0; i < USB_MAX_CONTROLLERS; ++i) {
        char node[32];
        snprintf(node, sizeof node, "/dev/ugen2.%d", i + 2);
        if (!strcmp(path, node)) return i;
    }
    return -1;
}
int __wrap_access(const char *path, int mode)
{ (void)mode; return !strcmp(path, "/dev/ugen0.2") || node_index(path) >= 0 ? 0 : -1; }
int __wrap_open(const char *path, int flags, ...)
{
#ifdef USB_SERVICE
    if (!strcmp(path, LOCK_PATH)) return 39;
#endif
    int i = node_index(path);
    (void)flags;
    if (i >= 0) {
        ++usb[i].open_calls;
        if (usb[i].gone) return -1;
        usb[i].opened = 1;
        return 40 + i;
    }
    assert(!strcmp(path, "/dev/hid"));
    assert(hid_count < 8);
    hid[hid_count].open = 1;
    return 50 + hid_count++;
}
int __wrap_close(int fd)
{
#ifdef USB_SERVICE
    if (fd == 39) return 0;
#endif
    if (fd >= 50) { hid[fd - 50].open = 0; ++hid[fd - 50].closed; }
    else { assert(fd >= 40 && fd < 40 + USB_MAX_CONTROLLERS); usb[fd - 40].opened = 0; ++usb[fd - 40].closed; }
    return 0;
}
int __wrap_ioctl(int fd, unsigned long request, ...)
{
    va_list args;
    void *data;
    va_start(args, request); data = va_arg(args, void *); va_end(args);
    if (fd >= 50) {
        int i = fd - 50;
        assert(hid[i].open);
        if (request == 0xC018482AUL) {
            struct req { uint32_t cmd, reserved; void *data, *out; } *r = data;
            assert(r->cmd == 3);
            if (fail_add) { errno = EINVAL; return -1; }
            memcpy(&hid[i].user, (unsigned char *)r->data + 0x1c, 4);
            hid[i].handle = 1000 + i;
            *(int32_t *)r->out = hid[i].handle;
            return 0;
        }
#ifdef VPAD_FW_1360
        if (request == 0x8018482CUL) {
#else
        if (request == 0x8010482CUL) {
#endif
            struct insert { uint32_t handle, reserved; const void *data; } *r = data;
            assert((int)r->handle == hid[i].handle && !r->reserved);
            ++hid[i].submits;
            if (hid[i].busy || hid[i].fatal) {
                errno = hid[i].busy ? EBUSY : EINVAL;
                return -1;
            }
            memcpy(hid[i].report, r->data, VPAD_REPORT_SIZE);
            return 0;
        }
        assert(request == 0x80104850UL);
        assert(((unsigned char *)data)[0] == 5);
        uint32_t handle;
        memcpy(&handle, (unsigned char *)data + 8, 4);
        assert((int)handle == hid[i].handle);
        ++hid[i].deleted;
        return 0;
    }
    assert(fd >= 40 && fd < 40 + USB_MAX_CONTROLLERS);
    fake_usb *u = &usb[fd - 40];
    assert(u->opened);
    switch (request) {
    case USB_GET_FULL_DESC: {
        unsigned char desc[] = {
            9, 2, 32, 0, 1, 1, 0, 0, 0,
            9, 4, 0, 0, 2, 0xff, 0x47, 0xd0, 0,
            7, 5, 0x82, 3, 64, 0, 1,
            7, 5, 0x02, 3, 64, 0, 1
        };
        if (u->switch_pro) { desc[14]=3;desc[15]=desc[16]=0;desc[20]=0x81; }
        if (u->usb360) { desc[15] = 0x5d; desc[16] = 1; }
        struct usb_gen_descriptor *gd = data;
        memcpy(gd->ugd_data, desc, sizeof desc);
        gd->ugd_actlen = sizeof desc;
        return 0;
    }
    case USB_GET_DEVICE_DESC: {
        if (u->gone) { errno = ENXIO; return -1; }
        struct usb_device_descriptor *dd = data;
        dd->idVendor = u->switch_pro ? 0x057e : 0x45e; dd->idProduct = u->switch_pro ? 0x2009 : 0xb12;
        return 0;
    }
    case USB_SET_CONFIG: ++u->configs; return 0;
    case USB_GET_CONFIG: *(int *)data = u->switch_pro ? 0 : -1; return 0;
    case USB_CLAIM_INTERFACE: return 0;
    case USB_FS_INIT:
        u->eps = ((struct usb_fs_init *)data)->pEndpoints;
        u->in_pending = u->out_pending = 0;
        return 0;
    case USB_FS_OPEN: return 0;
    case USB_FS_UNINIT: u->in_pending = u->out_pending = 0; return 0;
    case USB_FS_START: {
        int ep = ((struct usb_fs_start *)data)->ep_index;
        if (u->gone) { errno = ENXIO; return -1; }
        if (!ep) u->in_pending = 1;
        else {
            unsigned char *p = u->eps[ep].ppBuffer[0];
            assert(!u->out_pending);
            u->out_pending = 1;
            ++u->commands[p[0]];
            if (u->switch_pro) {
                assert(!u->reply_length);
                memset(u->reply,0,sizeof u->reply);
                if(p[0]==0x80){
                    assert(u->eps[ep].pLength[0] == 2);
                    if(p[1]!=4){u->reply[0]=0x81;u->reply[1]=p[1];u->reply_length=64;}
                }else{
                    assert(p[0]==1);u->reply[0]=0x21;u->reply[13]=0x80;u->reply[14]=p[10];u->reply_length=64;
                    if(p[10]==0x10){
                        memcpy(u->reply+15,p+11,5);
                        pack_switch(u->reply+20,1500,1500);pack_switch(u->reply+23,2000,2000);pack_switch(u->reply+26,1500,1500);
                        pack_switch(u->reply+29,2000,2000);pack_switch(u->reply+32,1500,1500);pack_switch(u->reply+35,1500,1500);
                    }
                }
            } else if (u->usb360) {
                assert(*u->eps[ep].pLength == 3 && p[0] == 1 && p[1] == 3 && p[2] == 2);
                ++u->native_led;
            } else if (p[0] == GIP_CMD_ACK) ++acknowledgments[fd - 40];
        }
        return 0;
    }
    case USB_FS_COMPLETE: {
        struct usb_fs_complete *comp = data;
        if (u->out_pending) { u->out_pending = 0; comp->ep_index = 1; return 0; }
        if(u->in_pending && u->reply_length){
            memcpy(u->eps[0].ppBuffer[0],u->reply,u->reply_length);
            *u->eps[0].pLength=u->reply_length;u->eps[0].aFrames=1;u->eps[0].status=0;
            u->reply_length=u->in_pending=0;comp->ep_index=0;return 0;
        }
        if (u->in_pending && u->queued) {
            memcpy(u->eps[0].ppBuffer[0], u->packet, u->length);
            *u->eps[0].pLength = u->length;
            u->eps[0].aFrames = 1;
            u->eps[0].status = 0;
            u->queued = u->in_pending = 0;
            comp->ep_index = 0;
            return 0;
        }
        errno = EAGAIN; return -1;
    }
    default: assert(0); return -1;
    }
}

static void queue_input(int i, unsigned buttons, unsigned trigger, int axis)
{
    fake_usb *u = &usb[i];
    assert(!u->queued);
    memset(u->packet, 0, sizeof u->packet);
    u->packet[0] = GIP_LL_INPUT_REPORT; u->packet[3] = 14;
    u->packet[4] = buttons; u->packet[5] = buttons >> 8;
    u->packet[6] = trigger; u->packet[7] = trigger >> 8;
    u->packet[10] = axis; u->packet[11] = (unsigned)axis >> 8;
    u->length = 18; u->queued = 1;
    if(u->switch_pro){
        memset(u->packet,0,sizeof u->packet);u->packet[0]=0x30;
        if(buttons&GIP_A)u->packet[3]|=4;
        if(buttons&GIP_B)u->packet[3]|=8;
        if(buttons&GIP_X)u->packet[3]|=1;
        if(buttons&GIP_Y)u->packet[3]|=2;
        if(buttons&GIP_RB)u->packet[3]|=0x40;
        if(buttons&GIP_LB)u->packet[5]|=0x40;
        if(buttons&GIP_VIEW)u->packet[4]|=1;
        if(buttons&GIP_MENU)u->packet[4]|=2;
        if(buttons&GIP_LS)u->packet[4]|=8;
        if(buttons&GIP_RS)u->packet[4]|=4;
        if(buttons&GIP_UP)u->packet[5]|=2;
        if(buttons&GIP_DOWN)u->packet[5]|=1;
        if(buttons&GIP_LEFT)u->packet[5]|=8;
        if(buttons&GIP_RIGHT)u->packet[5]|=4;
        if(trigger)u->packet[5]|=0x80;
        if(u->guide)u->packet[4]|=0x10;
        pack_switch(u->packet+6,2000+axis*1500/(axis<0?32768:32767),2000);
        pack_switch(u->packet+9,2000,2000);u->length=49;
    }
    if (u->usb360) {
        static const struct { unsigned gip, wire; } map[] = {
            {GIP_UP, 1}, {GIP_DOWN, 2}, {GIP_LEFT, 4}, {GIP_RIGHT, 8},
            {GIP_MENU, 0x10}, {GIP_VIEW, 0x20}, {GIP_LS, 0x40}, {GIP_RS, 0x80},
            {GIP_LB, 0x100}, {GIP_RB, 0x200}, {GIP_A, 0x1000}, {GIP_B, 0x2000},
            {GIP_X, 0x4000}, {GIP_Y, 0x8000}
        };
        unsigned wire = u->guide ? 0x400 : 0;
        for (unsigned j = 0; j < sizeof map / sizeof map[0]; ++j)
            if (buttons & map[j].gip) wire |= map[j].wire;
        memset(u->packet, 0, sizeof u->packet);
        u->packet[1] = 20;
        u->packet[2] = wire; u->packet[3] = wire >> 8;
        u->packet[4] = trigger >> 2;
        u->packet[6] = axis; u->packet[7] = (unsigned)axis >> 8;
        u->length = 20;
    }
}
static void queue_guide(int i, int pressed)
{
    assert(!usb[i].queued);
    if(usb[i].switch_pro){
        usb[i].guide=pressed;usb[i].packet[4]=(usb[i].packet[4]&~0x10)|(pressed?0x10:0);
        usb[i].length=49;usb[i].queued=1;return;
    }
    if (usb[i].usb360) {
        usb[i].guide = pressed;
        usb[i].packet[3] = (usb[i].packet[3] & ~4) | (pressed ? 4 : 0);
        usb[i].length = 20; usb[i].queued = 1;
        return;
    }
    unsigned char packet[5] = {GIP_CMD_GUIDE_BUTTON, GIP_OPT_ACK, 4, 1, pressed};
    memcpy(usb[i].packet, packet, sizeof packet);
    usb[i].length = 5; usb[i].queued = 1;
}
static uint32_t button_bits(int i)
{
    uint32_t bits; memcpy(&bits, hid[i].report + 12, 4); return bits;
}
static void pump(controller *c)
{
    for (int n = 0; n < 32; ++n)
        for (int i = 0; i < 2; ++i)
            if (c[i].dev) assert(!controller_step(&c[i]));
}

#ifdef USB_SERVICE
int __wrap_stat(const char *path, struct stat *st)
{ (void)path; (void)st; errno = ENOENT; return -1; }
int __wrap_utimes(const char *path, const struct timeval *times)
{ (void)path; (void)times; return 0; }
int __wrap_unlink(const char *path) { (void)path; return 0; }
int main(void)
{
    struct { usb_controller_status entries[USB_MAX_CONTROLLERS]; unsigned guard; } status_buffer;
    usb_controller_status *status = status_buffer.entries;
    status_buffer.guard = 0xfeed1234;
    for (int i = 0; i < USB_MAX_CONTROLLERS; ++i) usb[i].gone = 1;
    usb[1].switch_pro = 1;
    usb[2].usb360 = 1;
    assert(usb_service_init());
    for (int i = 0; i < 5; ++i) { assert(!usb_service_poll()); ++fake_time; }
    assert(usb_service_status(status) == 0); /* Remains alive without USB. */
    for (int i = 0; i < USB_MAX_CONTROLLERS; ++i) usb[i].gone = 0;
    for (int i = 0; i < 40; ++i) { assert(!usb_service_poll()); if (i < 3) ++fake_time; }
    assert(usb_service_status(status) == USB_MAX_CONTROLLERS);
    for (int i = 0; i < USB_MAX_CONTROLLERS; ++i) {
        assert(status[i].ready && status[i].slot == i + 1);
        assert(usb[i].opened && usb[i].configs == !usb[i].switch_pro);
    }
    int pads[USB_MAX_CONTROLLERS];
    for (int i = 0; i < USB_MAX_CONTROLLERS; ++i)
        pads[i] = vpad_handle(usb_controllers[i].pad) - 1000;
    queue_input(0, GIP_A, 1023, -32768);
    queue_input(1, GIP_B, 0, 32767);
    queue_input(2, GIP_X, 0, 0);
    queue_input(3, GIP_Y, 0, 0);
    for (int i = 0; i < 8; ++i) assert(!usb_service_poll());
    assert(button_bits(pads[0]) == 0x4100 && button_bits(pads[1]) == 0x2000);
    assert(button_bits(pads[2]) == 0x8000 && button_bits(pads[3]) == 0x1000);
    assert(usb_service_status(status) == USB_MAX_CONTROLLERS);
    for (int i = 0; i < USB_MAX_CONTROLLERS; ++i) assert(status[i].delivered);
    queue_guide(3, 1);
    for (int i = 0; i < 8; ++i) assert(!usb_service_poll());
    assert(button_bits(pads[3]) == 0x11000 && button_bits(pads[2]) == 0x8000);
    queue_guide(3, 0);
    for (int i = 0; i < 8; ++i) assert(!usb_service_poll());
    assert(button_bits(pads[3]) == 0x1000);
    hid[pads[3]].busy = 1;
    queue_input(3, GIP_A, 0, 0);
    queue_input(2, GIP_B, 0, 0);
    for (int i = 0; i < 8; ++i) assert(!usb_service_poll());
    assert(usb_controllers[3].busy_since && button_bits(pads[2]) == 0x2000);
    hid[pads[3]].busy = 0; ++fake_time;
    assert(!usb_service_poll());
    assert(!usb_controllers[3].busy_since && button_bits(pads[3]) == 0x4000);
    usb[1].gone = 1; fake_time += REINIT_SECONDS + 1;
    assert(!usb_service_poll());
    assert(usb_service_status(status) == USB_MAX_CONTROLLERS - 1 && !usb_controllers[1].dev);
    assert(status[1].slot == 3 && status[2].slot == 4);
    queue_input(3, GIP_Y, 0, 0);
    for (int i = 0; i < 8; ++i) assert(!usb_service_poll());
    assert(button_bits(pads[3]) == 0x1000 && !hid[pads[3]].deleted);
    usb[1].gone = 0;
    for (int i = 0; i < 40; ++i) { assert(!usb_service_poll()); if (i < 3) ++fake_time; }
    assert(usb_service_status(status) == USB_MAX_CONTROLLERS && status[1].ready);
    assert(status_buffer.guard == 0xfeed1234);
    usb_service_close();
    assert(usb_service_status(status) == 0 && hid_count == USB_MAX_CONTROLLERS + 1);
    for (int i = 0; i < hid_count; ++i) assert(hid[i].deleted == 1 && hid[i].closed == 1);
    for (int i = 0; i < USB_MAX_CONTROLLERS; ++i) assert(!usb[i].opened);
    puts("Four-controller USB service passed: empty startup, mixed hotplug, routing, Guide, busy retry, removal, replug and cleanup.");
    return 0;
}
#else
int main(int argc, char **argv)
{
    /* Keep the existing two-device regression scenarios. */
    for (int i = 2; i < USB_MAX_CONTROLLERS; ++i) usb[i].gone = 1;
    usb[1].switch_pro = argc>1 && !strcmp(argv[1],"switch");
    usb[1].usb360 = argc>1 && !usb[1].switch_pro;
    controller c[2];
    gip_device *claimed[2] = {0};
    char why[160];
    for (int i = 0; i < 2; ++i) {
        controller_init(&c[i], i + 1);
        c[i].dev = gip_open_next(claimed, i, why, sizeof why);
        assert(c[i].dev);
        assert(gip_prepare(c[i].dev));
        if (usb[i].switch_pro) {
            assert(usb[i].configs == 0);
            assert(usb[i].commands[0x80] == 4 && usb[i].commands[0x01] == 5);
        }
        claimed[i] = c[i].dev;
        c[i].pad = vpad_open(i + 1, why, sizeof why);
        assert(c[i].pad);
    }
    assert(c[0].dev != c[1].dev && c[0].pad != c[1].pad);
    assert(strcmp(gip_node(c[0].dev), gip_node(c[1].dev)));
    assert(usb[0].open_calls == 1 && usb[0].configs == 1);
    assert(!gip_open_next(claimed, 2, why, sizeof why));
    assert(usb[0].open_calls == 1 && usb[1].open_calls == 1);
    assert(hid[0].user == 1 && hid[1].user == 2);
    pump(c);
    assert(!sleeps); /* Initialisation never blocks either device. */
    for (int i = 0; i < 2; ++i) {
        assert(gip_protocol(c[i].dev) == (usb[i].switch_pro ? USB_SWITCH_PRO : usb[i].usb360 ? XINPUT_USB360 : XINPUT_GIP));
        assert(usb[i].commands[GIP_CMD_SET_DEVICE_STATE] == !(usb[i].usb360 || usb[i].switch_pro));
        assert(usb[i].commands[GIP_CMD_AUTHENTICATE] == !(usb[i].usb360 || usb[i].switch_pro));
        assert(usb[i].commands[GIP_CMD_LED] == !(usb[i].usb360 || usb[i].switch_pro));
        assert(usb[i].native_led == usb[i].usb360);
    }
    queue_input(0, GIP_A, 1023, -32768);
    queue_input(1, GIP_B, 0, 32767);
    pump(c);
    assert(button_bits(0) == 0x4100 && button_bits(1) == 0x2000);
    assert(hid[0].report[16] == 0 && hid[1].report[16] == 255);
    queue_guide(0, 1); pump(c);
    assert(button_bits(0) == 0x14100 && button_bits(1) == 0x2000);
    queue_input(0, GIP_X, 0, 0); queue_input(1, GIP_Y, 0, 0); pump(c);
    assert(button_bits(0) == 0x18000 && button_bits(1) == 0x1000);
    queue_guide(0, 0); pump(c);
    assert(button_bits(0) == 0x8000 && button_bits(1) == 0x1000);
    assert(acknowledgments[0] == 2 && acknowledgments[1] == 0);

    /* A failed second registration must not overwrite or delete the first. */
    fail_add = 1;
    assert(!vpad_open(3, why, sizeof why));
    fail_add = 0;
    assert(hid[2].closed == 1 && hid[0].deleted == 0 && hid[1].deleted == 0);

    /* Controller 2 keeps submitting while controller 1 awaits assignment. */
    hid[0].busy = 1;
    queue_input(0, GIP_A, 0, 0); queue_input(1, GIP_B, 0, 0); pump(c);
    assert(c[0].busy_since && !c[1].busy_since);
    long delivered = c[1].st.delivered;
    queue_input(1, GIP_Y, 0, 0); pump(c);
    assert(c[1].st.delivered > delivered && button_bits(1) == 0x1000);
    hid[0].busy = 0; ++fake_time; pump(c);
    assert(!c[0].busy_since && button_bits(0) == 0x4000);

    /* If one USB rebuild fails, only that virtual pad is removed. */
    usb[0].gone = 1;
    c[0].since_input = REINITS_BEFORE_REBUILD - 1;
    fake_time += REINIT_SECONDS;
    queue_input(1, GIP_B, 0, 0); pump(c);
    assert(!c[0].dev && !c[0].pad && c[1].dev && c[1].pad);
    assert(hid[0].deleted == 1 && hid[1].deleted == 0);
    queue_guide(1, 1); pump(c);
    assert(button_bits(1) == 0x12000 && acknowledgments[1] == !(usb[1].usb360 || usb[1].switch_pro));
    if (usb[1].usb360 || usb[1].switch_pro) {
        int configs = usb[1].configs;
        fake_time += UNPLUG_SECONDS + 1;
        pump(c);
        assert(c[1].dev && c[1].pad && !c[1].st.rebuilds);
        assert(usb[1].configs == configs); /* Silence must not reset the radio. */
    }

    /* The shared loop services remaining input and responds to remote stop. */
    queue_input(1, GIP_A, 0, 0);
    stop_at = sleeps + 2;
    run(c, 2);
    assert(log_stop_requested() && button_bits(1) == 0x14000);
    stop_at = 0;
    queue_input(1, GIP_VIEW | GIP_MENU, 0, 0); pump(c);
    fake_time += QUIT_HOLD_SECONDS;
    assert(controller_step(&c[1]) == 1);
    if (usb[1].usb360 || usb[1].switch_pro) {
        c[1].quit_since = 0;
        usb[1].gone = 1;
        fake_time += REINIT_SECONDS;
        assert(!controller_step(&c[1]) && !c[1].dev && !c[1].pad);
    }
    controller_close(&c[0]); controller_close(&c[1]);
    assert(hid[0].deleted == 1 && hid[1].deleted == 1);
    assert(hid[0].closed == 1 && hid[1].closed == 1);
#if XINPUT_VERBOSE
    assert(input_logs > 0 && guide_logs > 0);
#else
    assert(input_logs == 0 && guide_logs == 0);
#endif
    puts("Two-controller integration passed: routing, Guide, recovery, cleanup and release/debug logging.");
    return 0;
}
#endif
