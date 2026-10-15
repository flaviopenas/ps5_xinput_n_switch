#define _DEFAULT_SOURCE
#include "manager_http.h"
#include "bt_session.h"
#include "bt_store.h"
#include "bt_hci.h"
#include "bt_crypto.h"
#include "log.h"
#include "usb_service.h"
static int usb_enabled;
#include "manager_assets.h"
#include <sys/stat.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifndef MANAGER_STATE_DIR
#define MANAGER_STATE_DIR "/data/ps5-xinput"
#endif
#ifndef MANAGER_PORT
#define MANAGER_PORT 9030
#endif
#define BONDS_DIR MANAGER_STATE_DIR "/bonds"
enum { NONE, SCAN, SCAN_SWITCH, PAIR, CONNECT, DISCONNECT, FORGET };
static bt_saved_device saved[BT_SAVED_MAX];
static int saved_count, pending, active, cancel_operation;
static int native_paused;
static char pending_id[BT_DEVICE_ID_SIZE], active_id[BT_DEVICE_ID_SIZE], auto_id[BT_DEVICE_ID_SIZE];
static char phase[64] = "Ready", last_error[128], token[33];
static bt_advert discovered[32];
static unsigned discovered_count;
static int64_t scan_until;
typedef struct {
    char id[BT_DEVICE_ID_SIZE], automatic[BT_DEVICE_ID_SIZE], stage[64];
    unsigned char report[24];
    unsigned long reports, delivered;
    int64_t retry_at;
} manager_link;
static manager_link controllers[BT_MAX_CONTROLLERS];
static int connecting_slot = -1;
static void update_links(void);
static void neutral(manager_link *link)
{
    memset(link->report, 0, sizeof link->report);
    for (unsigned i = 16; i < 20; ++i)
        link->report[i] = 128;
}
static int active_slot(const char *id)
{
    for (unsigned i = 0; i < BT_MAX_CONTROLLERS; ++i)
        if (controllers[i].id[0] && !strcmp(controllers[i].id, id))
            return (int)i;
    return -1;
}
static int free_slot(void)
{
    for (unsigned i = 0; i < BT_MAX_CONTROLLERS; ++i)
        if (!controllers[i].id[0])
            return (int)i;
    return -1;
}
static void disable_auto(unsigned slot)
{
    controllers[slot].automatic[0] = 0;
    (void)bt_store_auto_slot(BONDS_DIR, slot, "");
}
static void disable_auto_for(const char *id)
{
    for (unsigned i = 0; i < BT_MAX_CONTROLLERS; ++i)
        if (!strcmp(id, controllers[i].automatic))
            disable_auto(i);
}

static unsigned char input_state[24];
static unsigned long report_count, delivered_count;
static volatile sig_atomic_t shutdown_requested;
static void shutdown_signal(int value)
{
    (void)value;
    shutdown_requested = 1;
}
static void reload(void);
static int64_t milliseconds(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static void set_stage(const char *value, void *unused)
{
    (void)unused;
    if (strcmp(phase, value))
        log_line("Manager: %s", value);
    snprintf(phase, sizeof phase, "%s", value);
}
static void tick(void *unused)
{
    (void)unused;
    if (usb_enabled && (shutdown_requested || log_stop_requested())) {
        usb_service_close();
        usb_enabled = 0;
    }
    if (usb_enabled && !shutdown_requested && !log_stop_requested() && usb_service_poll())
        shutdown_requested = 1;
    update_links();
    manager_http_poll(milliseconds());
}
static int cancelled(void *unused)
{
    (void)unused;
    return cancel_operation || shutdown_requested;
}
static int saved_index(const char *id)
{
    for (int i = 0; i < saved_count; ++i)
        if (!strcmp(id, saved[i].id))
            return i;
    return -1;
}
static int discovered_index(const char *id)
{
    char candidate[BT_DEVICE_ID_SIZE];
    for (unsigned i = 0; i < discovered_count; ++i) {
        bt_device_id(candidate, discovered[i].address, discovered[i].address_type);
        if (!strcmp(candidate, id))
            return (int)i;
    }
    return -1;
}
static void reload(void)
{
    int n = bt_store_list(BONDS_DIR, saved);
    saved_count = n >= 0 ? n : 0;
    if (n < 0)
        snprintf(last_error, sizeof last_error, "Saved-controller directory is unavailable");
}
static int append(char **at, size_t *left, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(*at, *left, fmt, args);
    va_end(args);
    if (n < 0 || (size_t)n >= *left)
        return 0;
    *at += n;
    *left -= (size_t)n;
    return 1;
}
static int quoted(char **at, size_t *left, const char *text)
{
    if (!append(at, left, "\""))
        return 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        if (*p == '"' || *p == '\\' || *p < 32 || *p >= 127) {
            if (!append(at, left, "\\u%04x", *p))
                return 0;
        } else if (!append(at, left, "%c", *p))
            return 0;
    }
    return append(at, left, "\"");
}
static int status_json(char *out, size_t size)
{
    char *at = out;
    size_t left = size;
#define ADD(...)                                                                                   \
    do {                                                                                           \
        if (!append(&at, &left, __VA_ARGS__))                                                      \
            return -1;                                                                             \
    } while (0)
#define QUOTE(s)                                                                                   \
    do {                                                                                           \
        if (!quoted(&at, &left, s))                                                                \
            return -1;                                                                             \
    } while (0)
    ADD("{\"token\":");
    QUOTE(token);
    ADD(",\"phase\":");
    QUOTE(phase);
    ADD(",\"error\":");
    QUOTE(last_error);
    ADD(",\"active\":");
    QUOTE(active_id);
    ADD(",\"auto\":");
    QUOTE(auto_id);
    ADD(",\"autos\":[");
    for (unsigned i = 0; i < BT_MAX_CONTROLLERS; ++i) {
        ADD("%s", i ? "," : "");
        QUOTE(controllers[i].automatic);
    }
    ADD("],\"bluetoothCapacity\":%u,\"bluetooth\":[", BT_MAX_CONTROLLERS);
    unsigned bluetooth_count = 0;
    for (unsigned i = 0; i < BT_MAX_CONTROLLERS; ++i) {
        manager_link *link = &controllers[i];
        if (!link->id[0])
            continue;
        uint32_t bits;
        memcpy(&bits, link->report + 12, 4);
        ADD("%s{\"slot\":%u,\"id\":", bluetooth_count++ ? "," : "", i + 1);
        QUOTE(link->id);
        ADD(",\"phase\":");
        QUOTE(link->stage);
        ADD(",\"connected\":%s,\"input\":{\"lx\":%u,\"ly\":%u,\"rx\":%u,\"ry\":%u,\"lt\":%u,\"rt\":"
            "%u,\"buttons\":%u,\"reports\":%lu,\"delivered\":%lu}}",
            bt_session_connected(i) ? "true" : "false", link->report[16], link->report[17],
            link->report[18], link->report[19], link->report[20], link->report[21], bits,
            link->reports, link->delivered);
    }
    ADD("]");
    ADD(",\"scanning\":%s,\"scanSeconds\":%lld,\"busy\":%s,\"devices\":[",
        scan_until ? "true" : "false",
        (long long)(scan_until > milliseconds() ? (scan_until - milliseconds() + 999) / 1000 : 0),
        active || pending || scan_until ? "true" : "false");
    unsigned count = 0;
    for (int i = 0; i < saved_count; ++i) {
        int d = discovered_index(saved[i].id);
        ADD("%s{\"id\":", count++ ? "," : "");
        QUOTE(saved[i].id);
        ADD(",\"name\":");
        QUOTE(d >= 0 && discovered[d].name[0] ? discovered[d].name
              : saved[i].peer[6] == 2         ? "Switch / 8BitDo controller"
                                              : "Xbox controller");
        ADD(",\"paired\":true,\"candidate\":true,\"rssi\":%d}", d >= 0 ? discovered[d].rssi : 0);
    }
    for (unsigned i = 0; i < discovered_count; ++i) {
        char id[BT_DEVICE_ID_SIZE];
        bt_device_id(id, discovered[i].address, discovered[i].address_type);
        if (saved_index(id) >= 0)
            continue;
        ADD("%s{\"id\":", count++ ? "," : "");
        QUOTE(id);
        ADD(",\"name\":");
        QUOTE(discovered[i].name[0] ? discovered[i].name : "Unnamed Bluetooth device");
        ADD(",\"paired\":false,\"candidate\":%s,\"rssi\":%d}",
            discovered[i].hid_service && discovered[i].event_type <= 1 ? "true" : "false",
            discovered[i].rssi);
    }
    ADD("],\"usbEnabled\":%s,\"usb\":[", usb_enabled ? "true" : "false");
    usb_controller_status usb[USB_MAX_CONTROLLERS];
    int usb_count = usb_service_status(usb);
    for (int i = 0; i < usb_count; ++i) {
        ADD("%s{\"slot\":%d,\"ready\":%s,\"name\":", i ? "," : "", usb[i].slot,
            usb[i].ready ? "true" : "false");
        QUOTE(usb[i].name);
        ADD(",\"reports\":%lu,\"delivered\":%lu,\"buttons\":%u}", usb[i].reports, usb[i].delivered,
            usb[i].buttons);
    }
    uint32_t buttons = 0;
    memcpy(&buttons, input_state + 12, 4);
    ADD("],\"input\":{\"lx\":%u,\"ly\":%u,\"rx\":%u,\"ry\":%u,\"lt\":%u,\"rt\":%u,\"buttons\":%u,"
        "\"reports\":%lu,\"delivered\":%lu}}",
        input_state[16], input_state[17], input_state[18], input_state[19], input_state[20],
        input_state[21], buttons, report_count, delivered_count);
    return (int)(at - out);
#undef ADD
#undef QUOTE
}
static int command(const char *body, const char **message)
{
    char verb[16], id[BT_DEVICE_ID_SIZE] = {0}, extra;
    int fields = sscanf(body, "%15s %14s %c", verb, id, &extra);
    if (fields < 1 || fields > 2) {
        *message = "Invalid command";
        return 400;
    }
    int preference = !strcmp(verb, "auto") ? 0
                     : strlen(verb) == 5 && !strncmp(verb, "auto", 4) && verb[4] >= '2' &&
                             verb[4] <= '0' + BT_MAX_CONTROLLERS
                         ? verb[4] - '1'
                         : -1;
    if (preference >= 0 && fields == 2) {
        unsigned slot = (unsigned)preference;
        const char *selected = !strcmp(id, "off") ? "" : id;
        if (selected[0] && saved_index(selected) < 0) {
            *message = "Controller is not paired";
            return 400;
        }
        for (unsigned other = 0; other < BT_MAX_CONTROLLERS; ++other)
            if (other != slot && selected[0] && !strcmp(selected, controllers[other].automatic)) {
                *message = "Controller already selected in another reconnect slot";
                return 409;
            }
        if (!bt_store_auto_slot(BONDS_DIR, slot, selected)) {
            *message = "Could not save reconnect preference";
            return 500;
        }
        snprintf(controllers[slot].automatic, sizeof controllers[slot].automatic, "%s", selected);
        controllers[slot].retry_at = milliseconds();
        update_links();
        *message = selected[0] ? "Automatic reconnect enabled" : "Automatic reconnect disabled";
        return 200;
    }
    if (pending) {
        *message = "A command is already pending";
        return 409;
    }
    int action = !strcmp(verb, "scan")          ? SCAN
                 : !strcmp(verb, "scan-switch") ? SCAN_SWITCH
                 : !strcmp(verb, "pair")        ? PAIR
                 : !strcmp(verb, "connect")     ? CONNECT
                 : !strcmp(verb, "disconnect")  ? DISCONNECT
                 : !strcmp(verb, "forget")      ? FORGET
                                                : NONE;
    if (!action || ((action == SCAN || action == SCAN_SWITCH) ? fields != 1
                    : action == DISCONNECT                    ? (fields != 1 && fields != 2)
                                                              : fields != 2)) {
        *message = "Invalid command";
        return 400;
    }
    if (action == CONNECT || action == FORGET) {
        if (saved_index(id) < 0) {
            *message = "Controller is not paired";
            return 404;
        }
    }
    if (action == CONNECT || action == PAIR) {
        if (active_slot(id) >= 0) {
            *message = "Controller is already connected or connecting";
            return 409;
        }
        if (free_slot() < 0) {
            *message = "Four Bluetooth controllers are already connected; disconnect one first";
            return 409;
        }
    }
    if (action == DISCONNECT && fields == 2) {
        uint8_t address[6], type;
        if (!bt_device_id_parse(id, address, &type)) {
            *message = "Invalid controller ID";
            return 400;
        }
        if (active_slot(id) < 0) {
            *message = "Controller is not connected";
            return 404;
        }
    }
    if (action == PAIR) {
        int index = discovered_index(id);
        if (index < 0 || !discovered[index].hid_service || discovered[index].event_type > 1) {
            *message = "Search for a connectable controller in pairing mode first";
            return 400;
        }
        if (saved_count == BT_SAVED_MAX && saved_index(id) < 0) {
            *message = "Saved-controller list is full; forget one first";
            return 409;
        }
    }
    if (action == DISCONNECT || action == FORGET) {
        for (unsigned i = 0; i < BT_MAX_CONTROLLERS; ++i) {
            if ((action == DISCONNECT && fields == 1) || !strcmp(id, controllers[i].automatic)) {
                if (!bt_store_auto_slot(BONDS_DIR, i, "")) {
                    *message = "Could not disable reconnect";
                    return 500;
                }
                controllers[i].automatic[0] = 0;
            }
        }
    }
    pending = action;
    snprintf(pending_id, sizeof pending_id, "%s", id);
    cancel_operation = 1;
    log_line("Manager: request %s%s%s", verb, fields == 2 ? " " : "", id);
    *message = "Command accepted";
    return 200;
}
static void advertisement(const bt_advert *r, void *unused)
{
    (void)unused;
    if (r->address_type > 2)
        return;
    unsigned index;
    for (index = 0; index < discovered_count; ++index)
        if (r->address_type == discovered[index].address_type &&
            !memcmp(r->address, discovered[index].address, 6))
            break;
    if (index == discovered_count) {
        if (discovered_count == 32)
            return;
        discovered[discovered_count++] = *r;
    } else {
        if (r->name[0])
            snprintf(discovered[index].name, sizeof discovered[index].name, "%s", r->name);
        discovered[index].rssi = r->rssi;
        discovered[index].hid_service |= r->hid_service;
        discovered[index].microsoft |= r->microsoft;
        if (r->event_type != 4)
            discovered[index].event_type = r->event_type;
    }
}
static void scan(int classic)
{
    const unsigned char params[7] = {1, 0xa0, 0, 0x50, 0, 0, 0}, enable[2] = {1, 0},
                        disable[2] = {0, 0};
    unsigned char reply[80];
    int attempted = 0, ok = 0;
    set_stage(classic ? "Searching Switch / 8BitDo" : "Searching", NULL);
    discovered_count = 0;
    bt_sessions_set_advertisement(advertisement, NULL);
    if (!bt_sessions_open())
        goto done;
    if (classic) {
        const uint8_t inquiry[5] = {0x33, 0x8b, 0x9e, 8, 0};
        attempted = 1;
        if (!bt_hci_send(0x0401, inquiry, sizeof inquiry))
            goto done;
        scan_until = milliseconds() + 12000;
        unsigned char named[32] = {0}, requested_address[6] = {0};
        int name_pending = 0;
        int64_t name_deadline = 0;
        while (milliseconds() < scan_until && !cancelled(NULL) && !log_stop_requested()) {
            int result = bt_sessions_poll(10);
            if (result == 4)
                native_paused = 1;
            if (result != 0)
                goto done;
            if (name_pending) {
                int found = 0;
                for (unsigned i = 0; i < discovered_count; ++i)
                    if (!memcmp(discovered[i].address, requested_address, 6) &&
                        discovered[i].name[0])
                        found = 1;
                if (found || milliseconds() >= name_deadline) {
                    if (!found)
                        (void)bt_hci_send(0x041a, requested_address, 6);
                    name_pending = 0;
                }
            }
            if (!name_pending)
                for (unsigned i = 0; i < discovered_count; ++i) {
                    if (named[i] || !discovered[i].hid_service || discovered[i].name[0] ||
                        discovered[i].address_type != 2)
                        continue;
                    uint8_t request[10] = {0};
                    memcpy(request, discovered[i].address, 6);
                    request[6] = 1;
                    named[i] = 1;
                    memcpy(requested_address, request, 6);
                    name_deadline = milliseconds() + 2000;
                    if (!bt_hci_send(0x0419, request, sizeof request))
                        goto done;
                    name_pending = 1;
                    break;
                }
        }
        if (name_pending)
            (void)bt_hci_send(0x041a, requested_address, 6);
        ok = 1;
        goto done;
    }
    int n = bt_hci_command(0x200b, params, sizeof params, reply, sizeof reply);
    if (n < 1 || reply[0])
        goto done;
    attempted = 1;
    n = bt_hci_command(0x200c, enable, sizeof enable, reply, sizeof reply);
    if (n > 0 && reply[0] != 0 && reply[0] != 0x0c)
        goto done;
    scan_until = milliseconds() + 20000;
    while (milliseconds() < scan_until && !cancelled(NULL) && !log_stop_requested()) {
        int result = bt_sessions_poll(10);
        if (result == 4)
            native_paused = 1;
        if (result != 0)
            goto done;
    }
    ok = 1;
done:
    if (attempted) {
        int n = bt_hci_cleanup_command(classic ? 0x0402 : 0x200c, classic ? NULL : disable,
                                       classic ? 0 : sizeof disable, reply, sizeof reply);
        if (n < 1 || (reply[0] && !(classic && reply[0] == 0x0c)))
            ok = 0;
    }
    bt_sessions_set_advertisement(NULL, NULL);
    scan_until = 0;
    if (native_paused || !active)
        bt_sessions_close();
    if (!ok)
        snprintf(last_error, sizeof last_error, "Search failed; see service logs");
    update_links();
}
static void input(const unsigned char *report, unsigned long count, unsigned long accepted,
                  void *context)
{
    manager_link *link = context;
    memcpy(link->report, report, sizeof link->report);
    link->reports = count;
    link->delivered = accepted;
}
static void link_stage(const char *value, void *context)
{
    manager_link *link = context;
    snprintf(link->stage, sizeof link->stage, "%s", value);
    if (!strcmp(value, "Connected")) {
        last_error[0] = 0;
        reload();
    }
    set_stage(value, NULL);
}
static void update_links(void)
{
    active = 0;
    active_id[0] = 0;
    memset(input_state, 0, sizeof input_state);
    for (unsigned i = 16; i < 20; ++i)
        input_state[i] = 128;
    report_count = delivered_count = 0;
    for (unsigned i = 0; i < BT_MAX_CONTROLLERS; ++i) {
        manager_link *link = &controllers[i];
        if (link->id[0] && connecting_slot != (int)i && !bt_session_connected(i)) {
            int result = bt_session_result(i);
            if (result == 2 || result == 3)
                disable_auto_for(link->id);
            link->id[0] = 0;
            neutral(link);
            link->retry_at = milliseconds() + 5000;
            if (result == 4)
                native_paused = 1;
        }
        if (!link->id[0])
            continue;
        ++active;
        if (!active_id[0]) {
            snprintf(active_id, sizeof active_id, "%s", link->id);
            memcpy(input_state, link->report, sizeof input_state);
            report_count = link->reports;
            delivered_count = link->delivered;
        }
    }
    snprintf(auto_id, sizeof auto_id, "%s", controllers[0].automatic);
    int automatic = 0;
    for (unsigned i = 0; i < BT_MAX_CONTROLLERS; ++i)
        automatic |= controllers[i].automatic[0] != 0;
    if (connecting_slot < 0 && !scan_until)
        set_stage(native_paused ? "Bluetooth paused for DualSense"
                  : active      ? "Connected"
                  : automatic   ? "Waiting for controller"
                                : "Ready",
                  NULL);
}
static void connect_device(const char *id, int fresh, unsigned slot)
{
    native_paused = 0;
    uint8_t address[6], type;
    char path[192];
    if (!bt_device_id_parse(id, address, &type) || !bt_store_path(BONDS_DIR, id, path, sizeof path))
        return;
    manager_link *link = &controllers[slot];
    snprintf(link->id, sizeof link->id, "%s", id);
    neutral(link);
    link->reports = link->delivered = 0;
    snprintf(link->stage, sizeof link->stage, "Connecting");
    connecting_slot = (int)slot;
    bt_session_config config = {0};
    memcpy(config.address, address, 6);
    config.address_type = type;
    config.bond_path = path;
    config.fresh_pair = fresh;
    config.cancelled = cancelled;
    config.stage = link_stage;
    config.input = input;
    config.context = link;
    int result = bt_session_start(slot, &config);
    connecting_slot = -1;
    reload();
    if (result != 1) {
        link->id[0] = 0;
        neutral(link);
        if (result == 3)
            disable_auto_for(id);
        if (result == 4) {
            native_paused = 1;
            notify("PS5 XInput: reconnect DualSense now; use Connect to resume Xbox Bluetooth");
        }
        if (!cancel_operation && !log_stop_requested() && !shutdown_requested)
            snprintf(last_error, sizeof last_error,
                     result == 3
                         ? "Bluetooth radio rejected the connection; automatic retries stopped"
                         : "Connection or profile setup failed; turn the controller on and retry");
    }
    link->retry_at = milliseconds() + (result == 1 ? 2000 : 5000);
    update_links();
}
static void disconnect_device(const char *id)
{
    for (unsigned i = 0; i < BT_MAX_CONTROLLERS; ++i) {
        if (!id[0] || !strcmp(id, controllers[i].id)) {
            bt_session_disconnect(i);
            controllers[i].id[0] = 0;
            neutral(&controllers[i]);
        }
    }
    update_links();
}
int main(void)
{
    signal(SIGHUP, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGTERM, shutdown_signal);
    signal(SIGINT, shutdown_signal);
    for (unsigned i = 16; i < 20; ++i)
        input_state[i] = 128;
    uint8_t random[16];
#ifdef MANAGER_HOST_TEST
    FILE *rng = fopen("/dev/urandom", "rb");
    if (!rng || fread(random, 1, sizeof random, rng) != sizeof random)
        return 1;
    fclose(rng);
#else
    void *module = dlopen("libSceRandom.sprx", RTLD_LAZY);
    int (*get_random)(uint8_t *, size_t) =
        module ? dlsym(module, "sceRandomGetRandomNumber") : NULL;
    if (!get_random || get_random(random, sizeof random))
        return 1;
#endif
    for (unsigned i = 0; i < sizeof random; ++i)
        snprintf(token + 2 * i, 3, "%02x", random[i]);
    bt_wipe(random, sizeof random);
    const manager_asset assets[3] = {
        {manager_html, sizeof manager_html - 1, "text/html; charset=utf-8"},
        {manager_css, sizeof manager_css - 1, "text/css; charset=utf-8"},
        {manager_js, sizeof manager_js - 1, "application/javascript"}};
    /* The listening port also prevents duplicate service instances before radio access. */
    if (!manager_http_open(MANAGER_PORT, token, assets, status_json, command))
        return 1;
    if (!log_open_port(MANAGER_STATE_DIR, MANAGER_STATE_DIR "/manager.log", 9024)) {
        manager_http_close();
        return 1;
    }
    if (mkdir(BONDS_DIR, 0700) != 0 && errno != EEXIST)
        goto done;
    struct stat directory;
    if (lstat(BONDS_DIR, &directory) || !S_ISDIR(directory.st_mode) || (directory.st_mode & 077))
        goto done;
    (void)bt_store_import(BONDS_DIR, MANAGER_STATE_DIR "/bt-bond-trial.bin");
    reload();
    for (unsigned i = 0; i < BT_MAX_CONTROLLERS; ++i) {
        neutral(&controllers[i]);
        (void)bt_store_auto_read_slot(BONDS_DIR, i, controllers[i].automatic);
        if (saved_index(controllers[i].automatic) < 0)
            controllers[i].automatic[0] = 0;
        for (unsigned earlier = 0; earlier < i; ++earlier)
            if (!strcmp(controllers[earlier].automatic, controllers[i].automatic))
                controllers[i].automatic[0] = 0;
    }
    bt_sessions_set_control(tick, cancelled, NULL);
    usb_enabled = usb_service_init();
    notify("PS5 XInput Controller Manager service ready, port %u", MANAGER_PORT);
    while (!log_stop_requested() && !shutdown_requested) {
        int radio = bt_sessions_poll(1);
        if (radio == 4)
            native_paused = 1;
        if (radio < 0)
            snprintf(last_error, sizeof last_error,
                     "Bluetooth transport failed; connections closed");
        tick(NULL);
        log_poll();
        if (pending) {
            int action = pending;
            char id[BT_DEVICE_ID_SIZE];
            memcpy(id, pending_id, sizeof id);
            pending = NONE;
            cancel_operation = 0;
            last_error[0] = 0;
            if (action == SCAN || action == SCAN_SWITCH)
                scan(action == SCAN_SWITCH);
            else if (action == CONNECT || action == PAIR) {
                int slot = free_slot();
                for (unsigned i = 0; i < BT_MAX_CONTROLLERS; ++i)
                    if (!controllers[i].id[0] && !controllers[i].automatic[0]) {
                        slot = (int)i;
                        break;
                    }
                for (unsigned i = 0; i < BT_MAX_CONTROLLERS; ++i)
                    if (!controllers[i].id[0] && !strcmp(id, controllers[i].automatic))
                        slot = (int)i;
                if (slot >= 0) {
                    /* Moving a preference into a free slot must not duplicate it. */
                    for (unsigned i = 0; i < BT_MAX_CONTROLLERS; ++i)
                        if ((int)i != slot && !strcmp(id, controllers[i].automatic))
                            disable_auto(i);
                    snprintf(controllers[slot].automatic, sizeof controllers[slot].automatic, "%s",
                             id);
                    if (!bt_store_auto_slot(BONDS_DIR, (unsigned)slot, id)) {
                        controllers[slot].automatic[0] = 0;
                        snprintf(last_error, sizeof last_error,
                                 "Could not save reconnect preference");
                    }
                    connect_device(id, action == PAIR, (unsigned)slot);
                    if (saved_index(controllers[slot].automatic) < 0)
                        disable_auto((unsigned)slot);
                }
            } else if (action == FORGET) {
                disconnect_device(id);
                if (!bt_store_forget(BONDS_DIR, id))
                    snprintf(last_error, sizeof last_error, "Could not forget controller");
                reload();
                update_links();
            } else if (action == DISCONNECT)
                disconnect_device(id);
        } else if (!native_paused && !cancel_operation) {
            /* Rotate attempts so an unavailable first Xbox cannot starve the second. */
            static unsigned next_auto;
            for (unsigned n = 0; n < BT_MAX_CONTROLLERS; ++n) {
                unsigned i = (next_auto + n) % BT_MAX_CONTROLLERS;
                manager_link *link = &controllers[i];
                if (!link->id[0] && link->automatic[0] && active_slot(link->automatic) < 0 &&
                    saved_index(link->automatic) >= 0 && milliseconds() >= link->retry_at) {
                    char id[BT_DEVICE_ID_SIZE];
                    memcpy(id, link->automatic, sizeof id);
                    next_auto = (i + 1) % BT_MAX_CONTROLLERS;
                    connect_device(id, 0, i);
                    break;
                }
            }
        }
        cancel_operation = pending != NONE;
        usleep(1000);
    }

done:
    usb_service_close();
    bt_sessions_close();
    manager_http_close();
    log_line("Manager: service stopped");
    log_close();
    return 0;
}
