/* USB Xbox-to-PS5 bridge. Each controller owns its transfers, virtual pad,
 * Guide state, retry timers and counters. Remote shell calls remain disabled. */
#include "gip.h"
#include "log.h"
#include "vpad.h"
#include "user.h"
#include "pad_abi.h"
#include "usb_service.h"
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define STATE_DIR "/data/ps5-xinput"
#define LOG_PATH STATE_DIR "/xsense.log"
#define LOCK_PATH STATE_DIR "/xsense.lock"
#define MAX_CONTROLLERS USB_MAX_CONTROLLERS
#define LOCK_STALE_SECONDS 15
#define UNPLUG_SECONDS 30
#define REINIT_SECONDS 3
#define KEEPALIVE_SECONDS 20
#define REINITS_BEFORE_REBUILD 3
#define QUIT_HOLD_SECONDS 2
#define REPORT_SECONDS 60
#define ASSIGNMENT_WAIT_SECONDS 120

static int lock_take(void)
{
    struct stat stv;
    int fd;
    if (stat(LOCK_PATH, &stv) == 0 && time(NULL) - stv.st_mtime < LOCK_STALE_SECONDS)
        return 0;
    fd = open(LOCK_PATH, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return 0;
    close(fd);
    return 1;
}
static void lock_refresh(void)
{
    utimes(LOCK_PATH, NULL);
}
static void lock_release(void)
{
    unlink(LOCK_PATH);
}

typedef struct {
    long reports, delivered, failed, acked;
    int reinits, rebuilds;
} stats;

typedef struct {
    gip_device *dev;
    vpad *pad;
    vpad_input input;
    stats st;
    int number, since_input, submit_failures, dirty;
    unsigned previous_buttons;
    time_t last_input, last_reinit, last_keepalive, last_note, last_axis_log;
    time_t quit_since, busy_since, busy_note;
    struct timespec next_submit;
} controller;

static void controller_init(controller *c, int number)
{
    memset(c, 0, sizeof *c);
    c->number = number;
    c->previous_buttons = 0xffffffff;
    c->last_input = c->last_reinit = c->last_keepalive = c->last_note = time(NULL);
    vpad_input_init(&c->input);
}

static void controller_close(controller *c)
{
    if (!c->dev && !c->pad)
        return;
    log_line("[C%d] total: %ld read, %ld delivered, %ld failed, %ld acked, "
             "%d reinit(s), %d rebuild(s)",
             c->number, c->st.reports, c->st.delivered, c->st.failed, c->st.acked, c->st.reinits,
             c->st.rebuilds);
    log_line("[C%d] Cleanup: removing virtual controller", c->number);
    vpad_close(c->pad);
    c->pad = NULL;
    log_line("[C%d] Cleanup: closing USB controller", c->number);
    gip_close(c->dev);
    c->dev = NULL;
}

#ifndef USB_SERVICE
static int controller_attach(controller *c, uint32_t user, gip_device **claimed, int index)
{
    char why[160];
    c->dev = gip_open_next(claimed, index, why, sizeof why);
    if (!c->dev) {
        log_debug("[C%d] Discovery: %s", c->number, why);
        return 0;
    }
    /* Only publish the controller once its USB protocol is ready. */
    if (!gip_prepare(c->dev)) {
        log_line("[C%d] USB initialization failed; virtual pad not registered", c->number);
        gip_close(c->dev);
        c->dev = NULL;
        return 0;
    }
    claimed[index] = c->dev;
    log_line("[C%d] controller %s  %04X:%04X  %s", c->number, gip_node(c->dev), gip_vendor(c->dev),
             gip_product(c->dev), why);
    c->pad = vpad_open(user, why, sizeof why);
    if (!c->pad) {
        log_line("[C%d] Could not register virtual pad: %s", c->number, why);
        controller_close(c);
        claimed[index] = NULL;
        return 0;
    }
    c->last_input = c->last_reinit = c->last_keepalive = c->last_note = time(NULL);
    log_line("[C%d] virtual pad registered, %s; select this controller's user on PS5", c->number,
             why);
    return 1;
}
#endif

static void submit_input(controller *c, int guide_packet)
{
    struct timespec tick;
    int result;
    clock_gettime(CLOCK_MONOTONIC, &tick);
    /* Retain the newest frame while busy, including a pending Guide release. */
    if (!guide_packet && c->busy_since &&
        (tick.tv_sec < c->next_submit.tv_sec ||
         (tick.tv_sec == c->next_submit.tv_sec && tick.tv_nsec < c->next_submit.tv_nsec)))
        return;
    result = vpad_submit(c->pad, c->input.report);
    if (guide_packet)
        log_debug("[C%d] USB Home: virtual submission result %d", c->number, result);
    if (result == 1) {
        if (c->busy_since || c->st.delivered == 0)
            log_line("[C%d] Virtual input accepted, handle %d", c->number, vpad_handle(c->pad));
        ++c->st.delivered;
        c->submit_failures = 0;
        c->busy_since = 0;
        c->dirty = 0;
    } else if (result == -1) {
        if (!c->busy_since) {
            c->busy_since = c->busy_note = time(NULL);
            log_line("[C%d] InsertData EBUSY: waiting up to 120s for assignment", c->number);
            notify("PS5 XInput: controller %d waiting for user assignment", c->number);
        }
        c->next_submit = tick;
        c->next_submit.tv_nsec += 100000000;
        if (c->next_submit.tv_nsec >= 1000000000) {
            ++c->next_submit.tv_sec;
            c->next_submit.tv_nsec -= 1000000000;
        }
        c->submit_failures = 0;
    } else {
        /* Retry fatal errors on new input rather than every idle iteration. */
        c->dirty = 0;
        if (++c->submit_failures >= 10) {
            log_line("[C%d] Virtual input rejected 10 times; removing this controller", c->number);
            controller_close(c);
        }
    }
}

/* A USB failure affects just this controller. View+Menu deliberately exits all. */
static int controller_step(controller *c)
{
    const unsigned char *msg = NULL;
    unsigned char normalized[18];
    time_t now;
    int guide_packet = 0;
    int usb360 = gip_protocol(c->dev) == XINPUT_USB360;
    int switch_pro = gip_protocol(c->dev) == USB_SWITCH_PRO;
    int n = gip_read(c->dev, &msg);
    if (n == -2) {
        controller_close(c);
        return 0;
    }
    if (n < 0)
        ++c->st.failed;
    else if (n >= 4) {
        c->last_input = time(NULL);
        c->since_input = 0;
        if (usb360 || switch_pro) {
            int guide;
            int valid = switch_pro ? gip_normalize_switch(c->dev, msg, n, normalized, &guide)
                                   : vpad_normalize_usb360(msg, n, normalized, &guide);
            if (!valid)
                goto packet_done;
            guide_packet = guide != c->input.guide;
            c->input.guide = guide;
            msg = normalized;
            n = sizeof normalized;
        }
        if (msg[1] & GIP_OPT_ACK) {
            gip_acknowledge(c->dev, msg);
            ++c->st.acked;
        }
        if (vpad_input_update(&c->input, msg, n)) {
            ++c->st.reports;
            c->dirty = 1;
            guide_packet = guide_packet || msg[0] == GIP_CMD_GUIDE_BUTTON;
            if (guide_packet) {
                log_debug("[C%d] USB Home: %s; forwarding PS button", c->number,
                          c->input.guide ? "pressed" : "released");
            }
            if (msg[0] != GIP_CMD_GUIDE_BUTTON) {
                unsigned buttons = (unsigned)msg[4] | ((unsigned)msg[5] << 8);
                if (buttons != c->previous_buttons || time(NULL) - c->last_axis_log >= 5) {
                    log_debug(
                        "[C%d] USB input: buttons 0x%04X; LT %u RT %u; LX %d LY %d RX %d RY %d",
                        c->number, buttons, (unsigned)msg[6] | ((unsigned)msg[7] << 8),
                        (unsigned)msg[8] | ((unsigned)msg[9] << 8),
                        (int16_t)((unsigned)msg[10] | ((unsigned)msg[11] << 8)),
                        (int16_t)((unsigned)msg[12] | ((unsigned)msg[13] << 8)),
                        (int16_t)((unsigned)msg[14] | ((unsigned)msg[15] << 8)),
                        (int16_t)((unsigned)msg[16] | ((unsigned)msg[17] << 8)));
                    c->last_axis_log = time(NULL);
                }
                c->previous_buttons = buttons;
                if ((buttons & (GIP_VIEW | GIP_MENU)) == (GIP_VIEW | GIP_MENU)) {
                    if (!c->quit_since)
                        c->quit_since = time(NULL);
                } else
                    c->quit_since = 0;
            }
        }
    }
packet_done:
    now = time(NULL);
    if (c->quit_since && now - c->quit_since >= QUIT_HOLD_SECONDS) {
        log_line("[C%d] %s held -- stopping payload", c->number,
                 switch_pro ? "Minus+Plus" : "View+Menu");
        return 1;
    }
    if (c->dirty)
        submit_input(c, guide_packet);
    if (!c->dev)
        return 0;
    if (c->busy_since && now - c->busy_since >= ASSIGNMENT_WAIT_SECONDS) {
        log_line("[C%d] Assignment/busy wait expired; removing this controller", c->number);
        controller_close(c);
        return 0;
    }
    if (c->busy_since && now - c->busy_note >= 10) {
        c->busy_note = now;
        log_line("[C%d] Still waiting: InsertData busy for %ld seconds", c->number,
                 (long)(now - c->busy_since));
    }
    /* Wired-format receivers may stop sending reports while idle or while
     * their wireless controller is off. Check USB presence instead of removing
     * a healthy receiver or resetting its radio connection every few seconds. */
    if ((usb360 || switch_pro) && now - c->last_input >= REINIT_SECONDS &&
        now - c->last_reinit >= REINIT_SECONDS) {
        c->last_reinit = now;
        if (!gip_present(c->dev)) {
            log_line("[C%d] USB device disconnected; removing this controller", c->number);
            controller_close(c);
            return 0;
        }
    }
    if (!usb360 && !switch_pro && now - c->last_input >= UNPLUG_SECONDS) {
        log_line("[C%d] No input for %d seconds; removing this controller", c->number,
                 UNPLUG_SECONDS);
        controller_close(c);
        return 0;
    }
    if (!switch_pro && now - c->last_keepalive >= KEEPALIVE_SECONDS) {
        c->last_keepalive = now;
        gip_init(c->dev);
    }
    if (!usb360 && !switch_pro && now - c->last_input >= REINIT_SECONDS &&
        now - c->last_reinit >= REINIT_SECONDS) {
        c->last_reinit = c->last_keepalive = now;
        ++c->st.reinits;
        gip_init(c->dev);
        if (++c->since_input >= REINITS_BEFORE_REBUILD) {
            c->since_input = 0;
            ++c->st.rebuilds;
            log_line("[C%d] Rebuilding USB connection", c->number);
            if (!gip_rebuild(c->dev)) {
                log_line("[C%d] USB rebuild failed; removing this controller", c->number);
                controller_close(c);
                return 0;
            }
        }
    }
    if (now - c->last_note >= REPORT_SECONDS) {
        c->last_note = now;
        log_debug("[C%d] %ld read, %ld delivered, %ld acked, %d reinit(s), %d rebuild(s)",
                  c->number, c->st.reports, c->st.delivered, c->st.acked, c->st.reinits,
                  c->st.rebuilds);
    }
    return 0;
}

#ifndef USB_SERVICE
static void run(controller *controllers, int count)
{
    time_t last_beat = 0;
    for (;;) {
        int i, active = 0;
        time_t now = time(NULL);
        if (now != last_beat) {
            last_beat = now;
            log_poll();
            lock_refresh();
        }
        if (log_stop_requested()) {
            log_line("Remote stop requested -- stopping through normal cleanup");
            return;
        }
        for (i = 0; i < count; ++i) {
            if (!controllers[i].dev)
                continue;
            if (controller_step(&controllers[i]))
                return;
            if (log_stop_requested())
                return;
            if (controllers[i].dev)
                ++active;
        }
        if (!active)
            return;
        /* Back off once, after servicing all devices. */
        usleep(1000);
    }
}

int main(void)
{
    controller controllers[MAX_CONTROLLERS];
    gip_device *claimed[MAX_CONTROLLERS] = {0};
    uint32_t user;
    int i, count = 0, rc = 1;
    long delivered = 0;
    for (i = 0; i < MAX_CONTROLLERS; ++i)
        controller_init(&controllers[i], i + 1);
#ifdef XINPUT_USB_TEST_PORT
    if (!log_open_port(STATE_DIR, LOG_PATH, XINPUT_USB_TEST_PORT))
        return 1;
#else
    if (!log_open(STATE_DIR, LOG_PATH))
        return 1;
#endif
    log_line("========================================");
    log_line("PS5 XInput -- FW 4.03, up to %d USB controllers, %s build", MAX_CONTROLLERS,
             XINPUT_VERBOSE ? "diagnostic" : "release");
    if (!lock_take()) {
        log_line("Another copy is already running -- exiting");
        notify("PS5 XInput: already running");
        log_close();
        return 1;
    }
    if (!current_user(&user)) {
        notify("PS5 XInput: no active user; sign in and check the log");
        goto done;
    }
#if XINPUT_VERBOSE
#ifndef XINPUT_USB_TEST_PORT
    pad_abi_log();
    gip_log_inventory();
#endif
#endif
    for (i = 0; i < MAX_CONTROLLERS; ++i) {
        controller *c = &controllers[i];
        if (log_stop_requested())
            goto done;
        count += controller_attach(c, user, claimed, i);
    }
    if (!count) {
        time_t deadline = time(NULL) + 60;
        time_t last_scan = 0;
        log_line("No ready USB controller at launch; waiting up to 60 seconds for connection");
        notify("PS5 XInput: waiting up to 60s for USB controller");
        while (!count && time(NULL) < deadline) {
            time_t now = time(NULL);
            if (now != last_scan) {
                last_scan = now;
                log_poll();
                lock_refresh();
                for (i = 0; i < MAX_CONTROLLERS; ++i) {
                    if (!controllers[i].dev && !log_stop_requested())
                        count += controller_attach(&controllers[i], user, claimed, i);
                }
            }
            if (log_stop_requested())
                goto done;
            usleep(100000);
        }
    }
    if (!count) {
        notify("PS5 XInput: no controllers registered; check the log");
        goto done;
    }
    notify("PS5 XInput: %d USB controller(s) ready - select a user for each", count);
    log_line("Running with %d controller(s); hold View+Menu (Xbox) or Minus+Plus (Switch) to stop",
             count);
    run(controllers, MAX_CONTROLLERS);
    rc = 0;
done:
    for (i = 0; i < MAX_CONTROLLERS; ++i) {
        delivered += controllers[i].st.delivered;
        controller_close(&controllers[i]);
    }
    lock_release();
    log_line("Cleanup complete; payload exiting; %ld reports accepted overall", delivered);
    if (!rc)
        notify("PS5 XInput: stopped - %ld reports accepted", delivered);
    log_close();
    return rc;
}
#else
static controller usb_controllers[MAX_CONTROLLERS];
static uint32_t usb_user;
static int usb_started;
static time_t usb_scan_at, usb_beat;

int usb_service_init(void)
{
    if (usb_started)
        return 1;
    if (!lock_take()) {
        log_line("Manager USB: another USB payload is running");
        return 0;
    }
    if (!current_user(&usb_user)) {
        lock_release();
        return 0;
    }
    for (int i = 0; i < MAX_CONTROLLERS; ++i)
        controller_init(&usb_controllers[i], i + 1);
    usb_started = 1;
    usb_scan_at = usb_beat = 0;
    log_line("Manager USB: ready for up to %d controllers; hotplug enabled", MAX_CONTROLLERS);
    return 1;
}

int usb_service_poll(void)
{
    if (!usb_started)
        return 0;
    time_t now = time(NULL);
    if (now != usb_beat) {
        usb_beat = now;
        lock_refresh();
    }
    for (int i = 0; i < MAX_CONTROLLERS; ++i) {
        controller *c = &usb_controllers[i];
        if (!c->dev)
            continue;
        if (!c->pad) {
            const unsigned char *msg;
            int n = gip_read(c->dev, &msg);
            if (n == -2 || !gip_present(c->dev)) {
                controller_close(c);
                continue;
            }
            if (!gip_ready(c->dev))
                continue;
            char why[160];
            c->pad = vpad_open(usb_user, why, sizeof why);
            if (!c->pad) {
                log_line("Manager USB: registration failed: %s", why);
                controller_close(c);
                continue;
            }
            c->last_input = c->last_reinit = c->last_keepalive = time(NULL);
            log_line("Manager USB: controller %d registered, handle %d", c->number,
                     vpad_handle(c->pad));
            notify("PS5 XInput: USB controller %d ready - select its user", c->number);
        } else if (controller_step(c))
            return 1;
    }
    if (now >= usb_scan_at && !log_stop_requested()) {
        usb_scan_at = now + 1;
        gip_device *claimed[MAX_CONTROLLERS];
        for (int i = 0; i < MAX_CONTROLLERS; ++i)
            claimed[i] = usb_controllers[i].dev;
        for (int i = 0; i < MAX_CONTROLLERS; ++i) {
            controller *c = &usb_controllers[i];
            if (c->dev)
                continue;
            char why[160];
            c->dev = gip_open_next(claimed, MAX_CONTROLLERS, why, sizeof why);
            if (c->dev) {
                log_line("Manager USB: controller %d discovered, %04X:%04X", c->number,
                         gip_vendor(c->dev), gip_product(c->dev));
                break; /* Bound discovery work in one control tick. */
            }
        }
    }
    return 0;
}

int usb_service_status(usb_controller_status out[USB_MAX_CONTROLLERS])
{
    int count = 0;
    for (int i = 0; usb_started && i < MAX_CONTROLLERS; ++i) {
        controller *c = &usb_controllers[i];
        if (!c->dev)
            continue;
        usb_controller_status *s = &out[count++];
        s->slot = c->number;
        s->ready = c->pad != NULL;
        s->name = gip_protocol(c->dev) == USB_SWITCH_PRO  ? "Switch-compatible controller"
                  : gip_protocol(c->dev) == XINPUT_USB360 ? "XInput controller / receiver"
                                                          : "Xbox One / Series controller";
        s->reports = c->st.reports;
        s->delivered = c->st.delivered;
        memcpy(&s->buttons, c->input.report + 12, sizeof s->buttons);
    }
    return count;
}

void usb_service_close(void)
{
    if (!usb_started)
        return;
    for (int i = 0; i < MAX_CONTROLLERS; ++i)
        controller_close(&usb_controllers[i]);
    lock_release();
    usb_started = 0;
    log_line("Manager USB: cleanup complete");
}
#endif
