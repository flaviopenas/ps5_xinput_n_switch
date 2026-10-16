#include "log.h"
#include "bt_hci.h"
#include "bt_advert.h"
#include "bt_smp.h"
#include "bt_hog.h"
#include "bt_session.h"
#include "bt_timing.h"
#include "bt_classic.h"
#include "bt_store.h"
#include "vpad.h"
#include "bt_xbox.h"
#include "gip.h"
#include "user.h"
#include "../../third_party/micro-ecc/uECC.h"
#include <sys/stat.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef BT_BOND_PATH
#define BT_BOND_PATH "/data/ps5-xinput/bt-bond.bin"
#endif

typedef struct {
    uint16_t cid;
    uint8_t data[128];
    size_t size;
    int64_t received_at;
} bt_packet;
typedef struct {
    bt_session_config config;
    char path[192];
    int occupied, ready, result;
    uint8_t target[6], target_type;
    const char *bond_path;
    bt_smp smp;
    int found, connected, encrypted, connection_pending, connection_status;
    unsigned handle, acl_payload, in_flight;
    int64_t sent_at;
    uint8_t reassembly[512];
    size_t received, expected;
    bt_packet queue[32];
    unsigned head, tail;
    int reconnecting, discovering, services_done;
    unsigned service_start, hid_first, hid_last, att_mtu;
    vpad *pad;
    vpad_input pad_input;
    int dirty, bridge_stop, submit_failures, was_busy;
    int64_t next_submit, quit_since;
    unsigned last_buttons;
    unsigned long input_count, delivered_count;
    int64_t packet_at;
    bt_input_timing timing;
    bt_hog hog;
    bt_classic classic;
    uint8_t local_address[6];
} bt_link;
static bt_link links[BT_MAX_CONTROLLERS] = {{.target = {0x80, 0xb2, 0xbc, 0xd7, 0x6a, 0xf4},
                                             .bond_path = BT_BOND_PATH,
                                             .connection_status = -1,
                                             .acl_payload = 27,
                                             .att_mtu = 23,
                                             .last_buttons = ~0u}};
static bt_link *current = &links[0];
static int transport_open;
static void (*manager_tick)(void *);
static int (*manager_cancel)(void *);
static void *manager_context;
static void (*scan_advertisement)(const bt_advert *, void *);
static void *scan_context;
static int (*classic_known)(const uint8_t *, void *);
static void *classic_known_context;
static void dispatch_event(const uint8_t *, size_t, void *);
static void dispatch_acl(const uint8_t *, size_t, void *);
static void radio_tick(void *);
static int radio_cancel(void *);
static int setup_poll(unsigned);
static void classic_drain(void);
static int classic_start(void);
/* Each LE link owns its protocol state; the HCI transport is shared. */
static int native_retry;
static void stage(const char *name)
{
    if (current->config.stage)
        current->config.stage(name, current->config.context);
}
static int session_stopping(void)
{
    return log_stop_requested() ||
           (current->config.cancelled && current->config.cancelled(current->config.context)) ||
           native_retry;
}
static int (*random_bytes)(uint8_t *, size_t);
static int64_t milliseconds(void);
static void bridge_submit(int force)
{
    if (!current->pad || !current->dirty || (!force && milliseconds() < current->next_submit))
        return;
    int rc = vpad_submit(current->pad, current->pad_input.report);
    current->next_submit = milliseconds() + (rc == -1 ? 100 : 4);
    if (rc == 1) {
        bt_timing_delivered(&current->timing, current->target_type == 2 ? "Switch" : "Xbox",
                            current->handle, milliseconds());
        if (!current->delivered_count || current->was_busy)
            log_line("BT input: virtual input accepted, handle %d", vpad_handle(current->pad));
        ++current->delivered_count;
        current->dirty = current->was_busy = current->submit_failures = 0;
    } else if (rc == -1) {
        if (!current->was_busy)
            log_line("BT input: waiting for user assignment");
        current->was_busy = 1;
    } else {
        current->dirty = 0;
        if (++current->submit_failures >= 10)
            current->bridge_stop = 1;
    }
    if (current->config.input)
        current->config.input(current->pad_input.report, current->input_count,
                              current->delivered_count, current->config.context);
}
static int send_att(const uint8_t *, size_t, void *);
static void capture_input(uint8_t id, const uint8_t *data, size_t size, void *unused)
{
    (void)unused;
    unsigned buttons;
    int old_guide = current->pad_input.guide;
    if (!bt_xbox_update(&current->pad_input, id, data, size, &buttons))
        return;
    bt_timing_input(&current->timing, current->packet_at, milliseconds(), current->dirty);
    ++current->input_count;
    current->dirty = 1;
    if (current->config.input)
        current->config.input(current->pad_input.report, current->input_count,
                              current->delivered_count, current->config.context);
    if (buttons != current->last_buttons || old_guide != current->pad_input.guide)
        log_debug("BT input: buttons %04X Guide %d", buttons, current->pad_input.guide);
    current->last_buttons = buttons;
    if ((buttons & (GIP_VIEW | GIP_MENU)) == (GIP_VIEW | GIP_MENU)) {
        if (!current->quit_since)
            current->quit_since = milliseconds();
    } else
        current->quit_since = 0;
    bridge_submit(old_guide != current->pad_input.guide);
}
static unsigned word(const uint8_t *p)
{
    return p[0] | ((unsigned)p[1] << 8);
}
static void put(uint8_t *p, unsigned value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}
static int64_t milliseconds(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static int rng(uint8_t *out, unsigned size)
{
    return random_bytes && random_bytes(out, size) == 0;
}

static void advertisement(const bt_advert *r, void *unused)
{
    (void)unused;
    int match = r->address_type == current->target_type && !memcmp(r->address, current->target, 6);
    if (current->reconnecting && r->address_type == current->smp.identity[6] &&
        !memcmp(r->address, current->smp.identity, 6))
        match = 1;
    if (!match && current->reconnecting && current->smp.got_irk && r->address_type == 1 &&
        (r->address[5] & 0xc0) == 0x40) {
        uint8_t hash[3];
        bt_ah(current->smp.irk, r->address + 3, hash);
        match = bt_equal(hash, r->address, 3);
    }
    if (!current->found && r->event_type != 4 && r->address_type <= 1 && match) {
        memcpy(current->smp.peer, r->address, 6);
        current->smp.peer[6] = r->address_type;
        current->found = 1;
    }
}
static void event(const uint8_t *p, size_t size, void *unused)
{
    (void)unused;
    if (current->target_type == BT_ADDRESS_CLASSIC) {
        bt_classic_event(&current->classic, p, size, milliseconds());
        if (p[0] == 0x03 && size == 13 && !memcmp(p + 5, current->target, 6) &&
            ((p[2] && current->connection_pending) ||
             (!p[2] && p[11] == 1 && !current->connected))) {
            current->connection_pending = 0;
            current->connection_status = p[2];
            if (!p[2]) {
                current->handle = word(p + 3);
                current->connected = 1;
            }
        }
        current->encrypted = current->classic.encrypted;
        if (p[0] == 5 && size == 6 && current->connected && word(p + 3) == current->handle) {
            log_line("BT Switch: radio disconnected handle %u status %02X reason %02X",
                     current->handle, p[2], p[5]);
            current->connected = 0;
            current->in_flight = 0;
        }
        if (p[0] == 0x13 && size >= 3)
            for (unsigned i = 0; i < p[2] && 7 + i * 4 <= size; ++i)
                if (word(p + 3 + i * 4) == current->handle) {
                    unsigned count = word(p + 5 + i * 4);
                    current->in_flight =
                        count >= current->in_flight ? 0 : current->in_flight - count;
                }
        if (p[0] == 0x0f && size == 6 && word(p + 4) == 0x0405 && p[2] && !current->connected &&
            !current->classic.accept_pending) {
            current->connection_pending = 0;
            current->connection_status = p[2];
        }
        return;
    }
    if ((p[0] == 0x04 && size == 12) || (p[0] == 0x03 && size == 13))
        log_line("BT radio: native BR/EDR connection event %02X, length %lu", p[0],
                 (unsigned long)size);
    if (p[0] == 0x3e && size >= 4) {
        if (p[2] == 2) {
            (void)bt_advert_parse(p, size, advertisement, NULL);
            return;
        }
        if (((p[2] == 1 && size == 21) || (p[2] == 0x0a && size == 33)) &&
            current->connection_pending) {
            if (p[3] == 0 && (p[6] != 0 || p[7] != current->smp.peer[6] ||
                              memcmp(p + 8, current->smp.peer, 6))) {
                log_line("BT pair: unrelated connection event ignored");
                return;
            }
            current->connection_pending = 0;
            current->connection_status = p[3];
            if (p[3] == 0 && p[6] == 0 && !memcmp(p + 8, current->smp.peer, 6)) {
                current->handle = word(p + 4);
                current->connected = 1;
                log_line("BT pair: LE connection established, handle %u", current->handle);
                log_debug("BT pair: interval %u units, latency %u, supervision timeout %u units",
                          word(p + (p[2] == 1 ? 14 : 26)), word(p + (p[2] == 1 ? 16 : 28)),
                          word(p + (p[2] == 1 ? 18 : 30)));
            } else
                log_line("BT pair: connection event status %u role %u", p[3], p[6]);
        } else if (p[2] != 2)
            log_debug("BT pair: LE event %02X length %lu", p[2], (unsigned long)size);
    } else if (p[0] == 0x0f && size == 6) {
        if (p[2])
            log_line("BT pair: command status %04X status %02X", word(p + 4), p[2]);
        else
            log_debug("BT pair: command status %04X status %02X", word(p + 4), p[2]);
        if (current->connection_pending && word(p + 4) == 0x200d && p[2]) {
            current->connection_pending = 0;
            current->connection_status = p[2];
        }
    } else if ((p[0] == 8 && size == 6) || (p[0] == 0x59 && size == 7)) {
        if (current->connected && word(p + 3) == current->handle) {
            current->encrypted = p[2] == 0 && p[5] != 0;
            log_line("BT pair: encryption status %02X enabled %u", p[2], p[5]);
            if (p[2]) {
                current->smp.phase = SMP_FAILED;
                current->smp.failure = p[2];
            }
        }
    } else if (p[0] == 5 && size == 6 && word(p + 3) == current->handle) {
        log_line("BT pair: disconnected, reason %02X", p[5]);
        current->connected = 0;
        current->in_flight = 0;
    } else if (p[0] == 0x13 && size >= 3) {
        for (unsigned i = 0; i < p[2] && 3 + i * 4 + 4 <= size; ++i)
            if (word(p + 3 + i * 4) == current->handle) {
                unsigned n = word(p + 5 + i * 4);
                current->in_flight = n >= current->in_flight ? 0 : current->in_flight - n;
            }
    }
}
static void acl(const uint8_t *p, size_t size, void *unused)
{
    (void)unused;
    if (!current->connected || size < 4 || word(p + 2) != size - 4 ||
        (word(p) & 0xfff) != current->handle)
        return;
    unsigned boundary = (word(p) >> 12) & 3;
    if (boundary == 0 || boundary == 2) {
        current->received = current->expected = 0;
        if (size < 8)
            return;
        current->expected = 4 + word(p + 4);
        if (current->expected > sizeof current->reassembly) {
            current->expected = 0;
            return;
        }
    } else if (boundary != 1 || !current->expected)
        return;
    if (size - 4 > current->expected - current->received) {
        current->received = current->expected = 0;
        return;
    }
    memcpy(current->reassembly + current->received, p + 4, size - 4);
    current->received += size - 4;
    if (current->received == current->expected) {
        if (current->expected - 4 <= sizeof current->queue[0].data &&
            current->tail - current->head < 32) {
            unsigned index = current->tail++ % 32;
            current->queue[index].cid = (uint16_t)word(current->reassembly + 2);
            current->queue[index].size = current->expected - 4;
            current->queue[index].received_at = milliseconds();
            memcpy(current->queue[index].data, current->reassembly + 4, current->expected - 4);
        } else
            log_line("BT pair: L2CAP queue/buffer overflow");
        current->received = current->expected = 0;
    }
}
static unsigned outstanding_credit(void)
{
    unsigned count = 0;
    for (unsigned i = 0; i < BT_MAX_CONTROLLERS; ++i)
        count += links[i].in_flight;
    return count;
}
static int credit_expired(void)
{
    for (unsigned i = 0; i < BT_MAX_CONTROLLERS; ++i)
        if (links[i].in_flight && milliseconds() - links[i].sent_at >= 1000)
            return 1;
    return 0;
}
static int send_l2cap(unsigned cid, const uint8_t *data, size_t size)
{
    uint8_t l2cap[132], packet[140];
    if (size > sizeof l2cap - 4)
        return 0;
    put(l2cap, (unsigned)size);
    put(l2cap + 2, cid);
    memcpy(l2cap + 4, data, size);
    for (size_t at = 0; at < size + 4;) {
        while (outstanding_credit() && current->connected && !session_stopping()) {
            if (credit_expired()) {
                log_line("BT pair: delivery credit event missed; stopping instead of exceeding "
                         "radio buffers");
                return 0;
            }
            if (bt_hci_poll(10, NULL, NULL) < 0)
                return 0;
        }
        if (!current->connected || session_stopping())
            return 0;
        size_t n = size + 4 - at;
        if (n > current->acl_payload)
            n = current->acl_payload;
        put(packet, current->handle | (at ? 0x1000 : 0));
        put(packet + 2, (unsigned)n);
        memcpy(packet + 4, l2cap + at, n);
        if (!bt_hci_acl_send(packet, n + 4))
            return 0;
        current->in_flight = 1;
        current->sent_at = milliseconds();
        at += n;
    }
    bt_wipe(l2cap, sizeof l2cap);
    bt_wipe(packet, sizeof packet);
    return 1;
}
static int send_smp(const uint8_t *data, size_t size, void *unused)
{
    (void)unused;
    log_debug("BT pair: SMP TX %02X length %lu", data[0], (unsigned long)size);
    return send_l2cap(6, data, size);
}
static int send_att(const uint8_t *data, size_t size, void *unused)
{
    (void)unused;
    return send_l2cap(4, data, size);
}
static int start_encryption(const uint8_t *ltk, void *unused)
{
    (void)unused;
    uint8_t params[28] = {0};
    put(params, current->handle);
    memcpy(params + 12, ltk, 16);
    int ok = bt_hci_send(0x2019, params, sizeof params);
    bt_wipe(params, sizeof params);
    return ok;
}
static int command(unsigned opcode, const uint8_t *params, size_t size, int cleanup)
{
    uint8_t reply[80];
    int n = cleanup ? bt_hci_cleanup_command(opcode, params, size, reply, sizeof reply)
                    : bt_hci_command(opcode, params, size, reply, sizeof reply);
    int status = n > 0 ? reply[0] : -1;
    if (status)
        log_line("BT pair: command %04X status %d", opcode, status);
    else
        log_debug("BT pair: command %04X status %d", opcode, status);
    return status;
}
static int save_bond(void)
{
    uint8_t record[64] = {0};
    char temp[96];
    memcpy(record, "XBSC0001", 8);
    memcpy(record + 8, current->smp.local, 7);
    memcpy(record + 15, current->smp.identity, 7);
    memcpy(record + 22, current->smp.ltk, 16);
    memcpy(record + 38, current->smp.irk, 16);
    memcpy(record + 54, current->smp.peer, 7);
    record[61] = (uint8_t)current->smp.got_irk;
    record[62] = 16;
    snprintf(temp, sizeof temp, "/data/ps5-xinput/bt-bond.%d.new", getpid());
    int output = open(temp, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (output < 0) {
        bt_wipe(record, sizeof record);
        return 0;
    }
    size_t at = 0;
    while (at < sizeof record) {
        ssize_t n = write(output, record + at, sizeof record - at);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            break;
        at += (size_t)n;
    }
    int ok = at == sizeof record && fsync(output) == 0;
    if (close(output) != 0)
        ok = 0;
    if (ok)
        ok = rename(temp, current->bond_path) == 0;
    if (!ok)
        unlink(temp);
    bt_wipe(record, sizeof record);
    return ok;
}
static int load_bond(void)
{
    uint8_t record[65] = {0};
    struct stat info;
    int input = open(current->bond_path, O_RDONLY | O_NOFOLLOW);
    if (input < 0)
        return errno == ENOENT ? 0 : -1;
    int valid = fstat(input, &info) == 0 && S_ISREG(info.st_mode) && info.st_size == 64 &&
                (info.st_mode & 077) == 0;
    size_t at = 0;
    while (valid && at < 64) {
        ssize_t n = read(input, record + at, 64 - at);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            break;
        at += (size_t)n;
    }
    close(input);
    valid = valid && at == 64 && !memcmp(record, "XBSC0001", 8) && record[14] == 0 &&
            record[21] <= 1 && record[60] <= 1 && record[61] <= 1 && record[62] == 16 &&
            record[63] == 0 && !memcmp(record + 54, current->target, 6) &&
            record[60] == current->target_type;
    if (valid) {
        memcpy(current->smp.local, record + 8, 7);
        memcpy(current->smp.identity, record + 15, 7);
        memcpy(current->smp.ltk, record + 22, 16);
        memcpy(current->smp.irk, record + 38, 16);
        memcpy(current->smp.peer, record + 54, 7);
        current->smp.got_irk = record[61];
    }
    bt_wipe(record, sizeof record);
    return valid ? 1 : -1;
}
static int discover_services(unsigned start)
{
    uint8_t request[7] = {0x10, 0, 0, 0xff, 0xff, 0, 0x28};
    put(request + 1, start);
    current->service_start = start;
    current->discovering = 1;
    return send_l2cap(4, request, sizeof request);
}
static void drain(void)
{
    if (current->target_type == BT_ADDRESS_CLASSIC) {
        classic_drain();
        return;
    }
    while (current->head != current->tail && current->connected &&
           current->smp.phase != SMP_FAILED) {
        unsigned index = current->head++ % 32;
        const uint8_t *p = current->queue[index].data;
        size_t size = current->queue[index].size;
        current->packet_at = current->queue[index].received_at;
        if (current->queue[index].cid == 6) {
            log_debug("BT pair: SMP RX %02X length %lu phase %u", size ? p[0] : 0,
                      (unsigned long)size, current->smp.phase);
            if (current->reconnecting && size == 2 && p[0] == 0x0b) {
                log_line("BT pair: peer security request; trying saved encryption key");
            } else if (!current->reconnecting)
                (void)bt_smp_receive(&current->smp, p, size);
        } else if (current->queue[index].cid == 4 && current->discovering && size >= 2 &&
                   p[0] == 0x11) {
            unsigned entry = p[1], last = 0;
            int valid = (entry == 6 || entry == 20) && size > 2 && (size - 2) % entry == 0;
            for (size_t at = 2; valid && at < size; at += entry) {
                unsigned first = word(p + at), end = word(p + at + 2);
                if (first < current->service_start || first <= last || end < first) {
                    valid = 0;
                    break;
                }
                last = end;
                if (entry == 6) {
                    log_debug("BT pair: GATT service %04X handles %04X-%04X", word(p + at + 4),
                              first, end);
                    if (word(p + at + 4) == 0x1812) {
                        current->hid_first = first;
                        current->hid_last = end;
                    }
                } else
                    log_debug("BT pair: GATT 128-bit service handles %04X-%04X", first, end);
            }
            if (!valid) {
                log_line("BT pair: malformed GATT service response");
                current->discovering = 0;
            } else if (last == 0xffff) {
                current->discovering = 0;
                current->services_done = 1;
            } else if (!discover_services(last + 1))
                current->discovering = 0;
        } else if (current->queue[index].cid == 4 && current->discovering && size == 5 &&
                   p[0] == 1 && p[1] == 0x10) {
            log_debug("BT pair: GATT discovery ended, ATT status %02X", p[4]);
            current->services_done = p[4] == 0x0a;
            current->discovering = 0;
        } else if (current->queue[index].cid == 4 && size == 3 && p[0] == 3) {
            unsigned peer_mtu = word(p + 1);
            if (peer_mtu >= 23)
                current->att_mtu = peer_mtu > 128 ? 128 : peer_mtu;
            log_debug("BT HID: negotiated ATT MTU %u", current->att_mtu);
        } else if (current->queue[index].cid == 4 && current->hog.phase) {
            bt_hog_receive(&current->hog, p, size, milliseconds());
        } else if (current->queue[index].cid == 4 && size == 3 && p[0] == 2) {
            const uint8_t reply[3] = {3, 23, 0};
            (void)send_l2cap(4, reply, sizeof reply);
        } else if (current->queue[index].cid == 5 && size == 12 && p[0] == 0x12 &&
                   word(p + 2) == 8) {
            uint8_t reply[6] = {0x13, p[1], 2,
                                0,    1,    0}; /* Reject parameter change for this short test. */
            (void)send_l2cap(5, reply, sizeof reply);
        } else
            log_debug("BT pair: L2CAP CID %u opcode %02X length %lu", current->queue[index].cid,
                      size ? p[0] : 0, (unsigned long)size);
        bt_wipe(current->queue[index].data, sizeof current->queue[index].data);
        current->packet_at = 0;
    }
}
static int setup_poll(unsigned timeout_ms)
{
    int result = bt_hci_poll(timeout_ms, NULL, NULL);
    bt_link *previous = current;
    if (!native_retry && result >= 0) {
        for (unsigned i = 0; i < BT_MAX_CONTROLLERS; ++i) {
            current = &links[i];
            if (current != previous && current->ready && current->connected) {
                drain();
                bridge_submit(0);
            }
        }
    }
    current = previous;
    return result;
}
int bt_session_start(unsigned slot, const bt_session_config *config)
{
    if (slot >= BT_MAX_CONTROLLERS || links[slot].occupied || !config || !config->bond_path ||
        strlen(config->bond_path) >= sizeof links[slot].path)
        return 0;
    for (unsigned i = 0; i < BT_MAX_CONTROLLERS; ++i)
        if (links[i].occupied && links[i].target_type == config->address_type &&
            !memcmp(links[i].target, config->address, 6))
            return 0;
    current = &links[slot];
    memset(current, 0, sizeof *current);
    current->occupied = 1;
    current->config = *config;
    memcpy(current->target, config->address, 6);
    current->target_type = config->address_type;
    snprintf(current->path, sizeof current->path, "%s", config->bond_path);
    current->bond_path = current->path;
    current->config.bond_path = current->path;
    current->connection_status = -1;
    current->acl_payload = 27;
    current->att_mtu = 23;
    current->last_buttons = ~0u;
    if (current->target_type == BT_ADDRESS_CLASSIC)
        return classic_start();
    if (current->target_type > 1) {
        current->occupied = 0;
        return 0;
    }
    int success = 0, scan_attempted = 0, radio_rejected = 0;
    uint8_t response[80];
    const uint8_t scan_params[7] = {1, 0xa0, 0, 0x50, 0, 0, 0}, enable[2] = {1, 0},
                  disable[2] = {0, 0};
    vpad_input_init(&current->pad_input);
    log_line("BT input: experimental Bluetooth controller bridge");
    stage("Connecting");
    log_line("BT pair: selected target %02X:%02X:%02X:%02X:%02X:%02X", current->target[5],
             current->target[4], current->target[3], current->target[2], current->target[1],
             current->target[0]);
    log_line("BT pair: bond file %s", current->bond_path);
    int loaded = config->fresh_pair ? 0 : load_bond();
    if (loaded < 0) {
        log_line("BT pair: existing bond invalid or inaccessible; leaving it unchanged");
        goto done;
    }
    current->reconnecting = loaded;
    if (current->reconnecting) {
        log_line("BT pair: saved bond loaded; reconnecting without new pairing");
        log_line("BT pair: saved identity %02X:%02X:%02X:%02X:%02X:%02X type %u",
                 current->smp.identity[5], current->smp.identity[4], current->smp.identity[3],
                 current->smp.identity[2], current->smp.identity[1], current->smp.identity[0],
                 current->smp.identity[6]);
    } else {
        void *random_module = dlopen("libSceRandom.sprx", RTLD_LAZY);
        if (!random_module || !(random_bytes = dlsym(random_module, "sceRandomGetRandomNumber"))) {
            log_line("BT pair: secure random service unavailable");
            goto done;
        }
        uECC_set_rng(rng);
        uint8_t public_be[64];
        if (!rng(current->smp.nonce, 16) ||
            !uECC_make_key(public_be, current->smp.private_key, uECC_secp256r1())) {
            log_line("BT pair: key generation failed");
            goto done;
        }
        for (unsigned i = 0; i < 32; ++i) {
            current->smp.public_key[i] = public_be[31 - i];
            current->smp.public_key[i + 32] = public_be[63 - i];
        }
        bt_wipe(public_be, sizeof public_be);
    }
    current->smp.send = send_smp;
    current->smp.encrypt = start_encryption;
    if (!bt_sessions_open()) {
        log_line("BT pair: transport unavailable");
        goto done;
    }
    int n = bt_hci_command(0x1009, NULL, 0, response, sizeof response);
    if (n != 7 || response[0]) {
        log_line("BT pair: local address unavailable");
        goto done;
    }
    if (current->reconnecting && memcmp(current->smp.local, response + 1, 6)) {
        log_line("BT pair: saved radio address mismatch; existing bond preserved");
        goto done;
    }
    memcpy(current->smp.local, response + 1, 6);
    current->smp.local[6] = 0;
    log_line("BT pair: local address %02X:%02X:%02X:%02X:%02X:%02X", current->smp.local[5],
             current->smp.local[4], current->smp.local[3], current->smp.local[2],
             current->smp.local[1], current->smp.local[0]);
    n = bt_hci_command(0x2002, NULL, 0, response, sizeof response);
    if (n >= 4 && !response[0] && word(response + 1) >= 27 && response[3])
        current->acl_payload = word(response + 1) > 128 ? 128 : word(response + 1);
    else {
        log_line("BT pair: usable LE ACL buffer capacity unavailable");
        goto done;
    }
    log_line("BT pair: LE ACL payload %u", current->acl_payload);
    if (current->reconnecting) {
        /* A fresh boot can leave legacy LE scanning enabled on function 0.
         * The verified discovery path already disables it before initiating. */
        if (command(0x200c, disable, sizeof disable, 0) != 0)
            goto done;
        memcpy(current->smp.peer, current->smp.identity, 7);
        current->found = 1;
        log_line("BT pair: initiating directly to saved identity, type %u", current->smp.peer[6]);
    } else {
        if (command(0x200b, scan_params, sizeof scan_params, 0) != 0)
            goto done;
        scan_attempted = 1;
        int scan_status = command(0x200c, enable, sizeof enable, 0);
        if (scan_status != 0 && scan_status != 0x0c && scan_status != -1)
            goto done;
        notify("PS5 XInput: Xbox Bluetooth pairing scan");
        int64_t scan_deadline = milliseconds() + 30000;
        while (!current->found && milliseconds() < scan_deadline && !session_stopping())
            if (setup_poll(20) < 0)
                goto done;
        if (command(0x200c, disable, sizeof disable, 1) != 0)
            goto done;
        scan_attempted = 0;
        int64_t settle = milliseconds() + 100;
        while (milliseconds() < settle)
            if (setup_poll(10) < 0)
                goto done;
    }
    if (!current->found) {
        log_line("BT pair: selected Xbox not found");
        goto done;
    }
    log_line("BT pair: opening LE connection to selected Xbox");
    uint8_t create[25] = {0};
    put(create, 0xa0);
    put(create + 2, 0x50);
    create[5] = current->smp.peer[6];
    memcpy(create + 6, current->smp.peer, 6);
    put(create + 13, 0x18);
    put(create + 15, 0x28);
    put(create + 19, 500);
    int64_t deadline;
    for (unsigned attempt = 0; attempt < 2 && !session_stopping(); ++attempt) {
        current->connection_pending = 1;
        current->connection_status = -1;
        if (!bt_hci_send(0x200d, create, sizeof create))
            goto done;
        deadline = milliseconds() + (current->reconnecting ? 45000 : 15000);
        while (!current->connected && current->connection_pending && milliseconds() < deadline &&
               !session_stopping())
            if (setup_poll(20) < 0)
                goto done;
        if (current->connected || session_stopping() || current->connection_status != 0x0c ||
            attempt)
            break;
        /* FW 4.03 has also exhibited this rejection in the standalone test.
         * Its previous cleanup issued LE Create Connection Cancel, after which
         * the next attempt succeeded. Recover once; never disconnect an
         * established foreign handle or reset either radio function. */
        log_line("BT pair: initiation rejected; canceling pending function-0 LE initiation before "
                 "one retry");
        int cancel_status = command(0x200e, NULL, 0, 1);
        if (cancel_status != 0 && cancel_status != 0x0c)
            break;
        int64_t settle = milliseconds() + 200;
        while (milliseconds() < settle && !session_stopping())
            if (setup_poll(10) < 0)
                goto done;
    }
    if (!current->connected) {
        radio_rejected = current->connection_status == 0x0c;
        log_line("BT pair: connection not established, status %d", current->connection_status);
        goto done;
    }
    if (current->reconnecting) {
        /* Check that ATT traffic works before starting the saved-key procedure.
         * This distinguishes link/transport failure from encryption failure. */
        const uint8_t mtu_request[3] = {2, 128, 0};
        log_line("BT pair: checking unencrypted ATT link before saved-key encryption");
        if (!send_l2cap(4, mtu_request, sizeof mtu_request))
            goto done;
        int64_t settle = milliseconds() + 1000;
        while (current->connected && milliseconds() < settle && !session_stopping()) {
            if (setup_poll(20) < 0)
                goto done;
            drain();
        }
        if (!current->connected || session_stopping())
            goto done;
        current->smp.phase = SMP_ENCRYPT;
        if (!start_encryption(current->smp.ltk, NULL))
            goto done;
    } else {
        stage("Pairing");
        if (!bt_smp_start(&current->smp))
            goto done;
    }
    deadline = milliseconds() + 45000;
    while (current->connected && milliseconds() < deadline && !session_stopping() &&
           current->smp.phase != SMP_DONE && current->smp.phase != SMP_FAILED) {
        if (setup_poll(20) < 0)
            break;
        if (current->encrypted && current->smp.phase == SMP_ENCRYPT)
            (void)bt_smp_encrypted(&current->smp);
        drain();
    }
    if (current->encrypted && current->smp.phase == SMP_DONE) {
        success = 1; /* Save a fresh bond only after its HID layout is verified. */
        stage("Reading controller profile");
        if (current->reconnecting)
            log_line("BT pair: saved-bond encrypted reconnect verified");
        else {
            log_line(
                "BT pair: encrypted pairing complete; checking HID profile before saving bond");
        }
        if (success && current->connected && !session_stopping()) {
            log_line("BT pair: discovering GATT services on encrypted connection");
            (void)discover_services(1);
            int64_t observe = INT64_MAX;
            int map_logged = 0;
            int64_t profile_deadline = milliseconds() + 15000;
            current->hog.send = send_att;
            current->hog.input = capture_input;
            while (current->connected && current->encrypted && current->smp.phase != SMP_FAILED &&
                   milliseconds() < observe && !session_stopping()) {
                if (setup_poll(20) < 0)
                    break;
                drain();
                if (current->services_done && !current->hog.phase) {
                    current->hog.mtu = current->att_mtu;
                    bt_hog_start(&current->hog, (uint16_t)current->hid_first,
                                 (uint16_t)current->hid_last, milliseconds());
                }
                bt_hog_tick(&current->hog, milliseconds());
                if (current->hog.failed)
                    break;
                if (!map_logged && milliseconds() >= profile_deadline) {
                    log_line("BT input: controller profile setup timed out");
                    break;
                }
                if (current->hog.ready && !map_logged) {
                    if (!bt_xbox_map_supported(current->hog.map, current->hog.map_size)) {
                        log_line(
                            "BT input: unsupported report map; leaving controller unregistered");
                        break;
                    }
                    if (!current->reconnecting && !save_bond()) {
                        stage("Could not save pairing");
                        success = 0;
                        break;
                    }
                    uint32_t user;
                    char why[128];
                    if (!current_user(&user) ||
                        !(current->pad = vpad_open(user, why, sizeof why))) {
                        log_line("BT input: virtual controller registration failed");
                        break;
                    }
                    log_line("BT input: virtual pad registered, handle %d",
                             vpad_handle(current->pad));
                    stage("Connected");
                    current->dirty = 1;
                    bridge_submit(1);
                    notify("PS5 XInput: Xbox Bluetooth controller ready");
                    current->ready = 1;
                    current->result = 1;
                    return 1; /* Keep this link and the shared radio running. */
                    map_logged = 1;
                }
                bridge_submit(0);
                if (current->bridge_stop ||
                    (current->quit_since && milliseconds() - current->quit_since >= 2000)) {
                    log_line("BT input: stop requested or View+Menu held");
                    break;
                }
            }
            log_line("BT pair: observation ended, connected %d encrypted %d services complete %d",
                     current->connected, current->encrypted, current->services_done);
        }
    } else
        log_line("BT pair: incomplete, phase %u failure %d connected %d encrypted %d",
                 current->smp.phase, current->smp.failure, current->connected, current->encrypted);
done:
    (void)success;
    if (scan_attempted && !native_retry)
        (void)command(0x200c, disable, sizeof disable, 1);
    int result = native_retry ? 4 : radio_rejected ? 3 : 0;
    current->result = result;
    bt_session_disconnect((unsigned)(current - links));
    if (native_retry)
        bt_sessions_close();
    return result;
}

static int classic_command(unsigned opcode, const void *p, size_t n, void *unused)
{
    (void)unused;
    return bt_hci_send(opcode, p, n);
}
static int classic_send(unsigned cid, const uint8_t *p, size_t n, void *unused)
{
    (void)unused;
    return send_l2cap(cid, p, n);
}
static void classic_input(const uint8_t *p, int n, int home, void *unused)
{
    (void)unused;
    int old = current->pad_input.guide;
    current->pad_input.guide = home;
    if (!vpad_input_update(&current->pad_input, p, n))
        return;
    bt_timing_input(&current->timing, current->packet_at, milliseconds(), current->dirty);
    ++current->input_count;
    current->dirty = 1;
    unsigned buttons = word(p + 4);
    if ((buttons & (GIP_VIEW | GIP_MENU)) == (GIP_VIEW | GIP_MENU)) {
        if (!current->quit_since)
            current->quit_since = milliseconds();
    } else
        current->quit_since = 0;
    if (current->config.input)
        current->config.input(current->pad_input.report, current->input_count,
                              current->delivered_count, current->config.context);
    bridge_submit(old != home);
}
static int classic_save(void)
{
    bt_classic *b = &current->classic;
    return b->have_key && bt_store_classic_save(current->bond_path, current->local_address,
                                                current->target, b->key, b->key_type);
}
static void classic_drain(void)
{
    unsigned queued = current->tail - current->head;
    while (queued-- && current->connected) {
        unsigned i = current->head++ % 32;
        bt_packet packet = current->queue[i];
        current->packet_at = packet.received_at;
        bt_wipe(current->queue[i].data, sizeof current->queue[i].data);
        if (packet.cid == 1 && (!current->classic.authenticated || !current->classic.encrypted))
            current->queue[current->tail++ % 32] = packet;
        else
            bt_classic_receive(&current->classic, packet.cid, packet.data, packet.size);
        bt_wipe(&packet, sizeof packet);
        current->packet_at = 0;
    }
    if (current->connected && !session_stopping())
        bt_classic_tick(&current->classic, milliseconds());
    if (current->ready && current->classic.key_changed) {
        if (!classic_save()) {
            log_line("BT Switch: updated bond save failed");
            current->classic.failed = 1;
        }
        current->classic.key_changed = 0;
    }
}
static int classic_start(void)
{
    bt_classic *b = &current->classic;
    bt_classic_init(b, current->target);
    b->command = classic_command;
    b->send = classic_send;
    b->input = classic_input;
    vpad_input_init(&current->pad_input);
    stage("Connecting Switch controller");
    uint8_t record[64] = {0}, reply[80];
    if (!current->config.fresh_pair) {
        if (!bt_store_read(current->bond_path, record) || memcmp(record, "XBCL0001", 8) ||
            memcmp(record + 54, current->target, 6)) {
            log_line("BT Switch: saved classic bond invalid or inaccessible");
            goto done;
        }
        memcpy(b->key, record + 22, 16);
        b->key_type = record[61];
        b->have_key = 1;
    }
    if (!bt_sessions_open())
        goto done;
    int n = bt_hci_command(0x1009, NULL, 0, reply, sizeof reply);
    if (n < 7 || reply[0])
        goto done;
    memcpy(current->local_address, reply + 1, 6);
    if (!current->config.fresh_pair && memcmp(record + 8, current->local_address, 6)) {
        log_line("BT Switch: bond belongs to another radio; pair again");
        goto done;
    }
    bt_wipe(record, sizeof record);
    n = bt_hci_command(0x1005, NULL, 0, reply, sizeof reply);
    if (n < 8 || reply[0] || word(reply + 1) < 27 || !word(reply + 4))
        goto done;
    current->acl_payload = word(reply + 1);
    if (current->acl_payload > 128)
        current->acl_payload = 128;
    /* PS5 already owns the radio configuration. Require SSP rather than silently
     * changing its global mode and disturbing native DualSense pairing. */
    n = bt_hci_command(0x0c55, NULL, 0, reply, sizeof reply);
    if (n < 2 || reply[0] || reply[1] != 1) {
        log_line("BT Switch: radio Secure Simple Pairing mode unavailable");
        goto done;
    }
    uint8_t create[13] = {0};
    memcpy(create, current->target, 6);
    /* DM1/DH1, DM3/DH3, DM5/DH5 ACL types; little-endian 18 CC. */
    put(create + 6, 0xcc18);
    create[8] = 1;
    create[12] = 1;
    /* A selected peer can page us during the read-only setup queries. Flush
     * its queued accept and keep its link instead of creating a duplicate. */
    bt_classic_tick(b, milliseconds());
    if (!current->connected) {
        current->connection_pending = 1;
        if (!b->accept_pending && !bt_hci_send(0x0405, create, sizeof create))
            goto done;
    }
    int64_t deadline = milliseconds() + 30000;
    int64_t authenticated_at = 0;
    int encryption_checked = 0;
    while (!session_stopping() && !b->failed && milliseconds() < deadline) {
        if (setup_poll(10) < 0)
            break;
        classic_drain();
        if (b->authenticated && !authenticated_at)
            authenticated_at = milliseconds();
        if (current->connected && b->authenticated && !b->encrypted && !encryption_checked &&
            milliseconds() - authenticated_at >= 2000) {
            uint8_t handle[2];
            put(handle, current->handle);
            encryption_checked = 1;
            n = bt_hci_command(0x1408, handle, sizeof handle, reply, sizeof reply);
            log_line("BT Switch: encryption readback length %d status %02X key bytes %u", n,
                     n > 0 ? reply[0] : 0xff, n >= 4 && !reply[0] ? reply[3] : 0);
            /* Read Encryption Key Size fails with 0x2F on an unencrypted link.
             * Recover a missed event only from a matching, still-live handle
             * and a controller-confirmed full-size key, never command status. */
            if (n == 4 && !reply[0] && word(reply + 1) == current->handle && reply[3] == 16 &&
                current->connected && !b->failed && !b->encrypted) {
                b->encrypted = current->encrypted = 1;
                b->open_at = milliseconds() + 200;
                log_line("BT Switch: encrypted link verified by radio readback");
            }
        }
        if (b->ready && current->connected && b->encrypted) {
            stage("Saving Switch pairing");
            if (b->key_changed || current->config.fresh_pair) {
                if (!classic_save()) {
                    log_line("BT Switch: cannot save verified pairing");
                    break;
                }
                b->key_changed = 0;
            }
            uint32_t user;
            char why[128];
            if (!current_user(&user) || !(current->pad = vpad_open(user, why, sizeof why)))
                break;
            current->ready = current->result = 1;
            current->dirty = 1;
            log_line("BT Switch: virtual pad registered, handle %d", vpad_handle(current->pad));
            stage("Connected");
            bridge_submit(1);
            notify("PS5 XInput: Switch Bluetooth controller ready");
            return 1;
        }
        if (!current->connected && !current->connection_pending)
            break;
    }
    log_line("BT Switch: setup failed; status %d authenticated %d encrypted %d profile %d stage %d",
             current->connection_status, b->authenticated, b->encrypted, b->info_verified,
             b->controller.stage);
    for (unsigned i = 0; i < 2; ++i)
        log_line("BT Switch: channel %04X remote %04X requested %d configured %d peer %d",
                 b->channels[i].local, b->channels[i].remote, b->channels[i].requested,
                 b->channels[i].configured, b->channels[i].peer_configured);
done:
    bt_wipe(record, sizeof record);
    current->result = native_retry ? 4 : current->connection_status == 0x0c ? 3 : 0;
    int result = current->result;
    bt_session_disconnect((unsigned)(current - links));
    return result;
}

/* Event callbacks only update state or queue packets; they never poll/send. */
static void dispatch_event(const uint8_t *p, size_t size, void *unused)
{
    (void)unused;
    if (size < 2)
        return;
    bt_link *previous = current;
    if (p[0] == 0x04 && size == 12 && p[11] == 1) {
        int owned = 0;
        for (unsigned i = 0; i < BT_MAX_CONTROLLERS; ++i)
            if (links[i].occupied && links[i].target_type == BT_ADDRESS_CLASSIC &&
                !memcmp(p + 2, links[i].target, 6))
                owned = 1;
        int saved = !owned && classic_known && classic_known(p + 2, classic_known_context);
        log_line("BT radio: classic request %02X:%02X:%02X:%02X:%02X:%02X; active %d saved %d",
                 p[7], p[6], p[5], p[4], p[3], p[2], owned, saved);
        if (!owned && saved) {
            /* A saved Switch requesting reconnect is not a native controller.
             * Leave connection setup to the manager; preserve ready peer input. */
            log_line("BT radio: saved Switch request; preserving managed links");
            return;
        }
        if (!owned) {
            native_retry = 1;
            log_line("BT radio: native controller requested connection; yielding managed links");
            return;
        }
    }
    if (scan_advertisement) {
        (void)bt_advert_parse(p, size, scan_advertisement, scan_context);
        (void)bt_classic_advert_parse(p, size, scan_advertisement, scan_context);
    }
    for (unsigned i = 0; i < BT_MAX_CONTROLLERS; ++i) {
        if (!links[i].occupied)
            continue;
        current = &links[i];
        event(p, size, NULL);
    }
    current = previous;
}
static void dispatch_acl(const uint8_t *p, size_t size, void *unused)
{
    (void)unused;
    if (size < 4)
        return;
    unsigned id = word(p) & 0xfff;
    bt_link *previous = current;
    for (unsigned i = 0; i < BT_MAX_CONTROLLERS; ++i) {
        if (!links[i].occupied || !links[i].connected || links[i].handle != id)
            continue;
        current = &links[i];
        acl(p, size, NULL);
        break;
    }
    current = previous;
}
/* During command/setup waits, service ready peers' notifications without
 * recursively polling HCI or sending ATT commands from the control callback. */
static void radio_tick(void *unused)
{
    (void)unused;
    if (manager_tick)
        manager_tick(manager_context);
    if (native_retry)
        return;
    bt_link *previous = current;
    for (unsigned i = 0; i < BT_MAX_CONTROLLERS; ++i) {
        current = &links[i];
        if (!current->occupied || !current->ready || !current->connected)
            continue;
        unsigned queued = current->tail - current->head;
        while (queued--) {
            unsigned n = current->head++ % 32;
            bt_packet packet = current->queue[n];
            current->packet_at = packet.received_at;
            bt_wipe(current->queue[n].data, sizeof current->queue[n].data);
            if (current->target_type == BT_ADDRESS_CLASSIC && packet.cid == 0x41 && packet.size &&
                packet.data[0] == 0xa1)
                bt_classic_receive(&current->classic, packet.cid, packet.data, packet.size);
            else if (current->target_type != BT_ADDRESS_CLASSIC && packet.cid == 4 && packet.size &&
                     packet.data[0] == 0x1b)
                bt_hog_receive(&current->hog, packet.data, packet.size, milliseconds());
            else
                current->queue[current->tail++ % 32] = packet;
            bt_wipe(&packet, sizeof packet);
            current->packet_at = 0;
        }
        bridge_submit(0);
    }
    current = previous;
}
static int radio_cancel(void *unused)
{
    (void)unused;
    return native_retry || (manager_cancel && manager_cancel(manager_context));
}
void bt_sessions_set_control(void (*tick)(void *), int (*cancelled)(void *), void *context)
{
    manager_tick = tick;
    manager_cancel = cancelled;
    manager_context = context;
    bt_hci_set_control(radio_tick, radio_cancel, NULL);
}
void bt_sessions_set_advertisement(void (*callback)(const bt_advert *, void *), void *context)
{
    scan_advertisement = callback;
    scan_context = context;
}
void bt_sessions_set_classic_known(int (*callback)(const uint8_t *, void *), void *context)
{
    classic_known = callback;
    classic_known_context = context;
}
int bt_sessions_open(void)
{
    bt_hci_set_handlers(dispatch_event, dispatch_acl, NULL);
    if (transport_open)
        return !native_retry;
    native_retry = 0;
    bt_hci_set_control(radio_tick, radio_cancel, NULL);
    if (!bt_hci_open() || !bt_hci_open_acl()) {
        bt_hci_close();
        return 0;
    }
    transport_open = 1;
    return 1;
}
int bt_session_connected(unsigned slot)
{
    return slot < BT_MAX_CONTROLLERS && links[slot].occupied && links[slot].ready &&
           links[slot].connected;
}
int bt_session_result(unsigned slot)
{
    return slot < BT_MAX_CONTROLLERS ? links[slot].result : 0;
}
static void release_link(bt_link *link)
{
    int result = link->result;
    if (link->pad)
        vpad_close(link->pad);
    bt_wipe(link, sizeof *link);
    link->result = result;
}
void bt_sessions_close(void)
{
    /* On native handoff, do not wait for replies or consume native events.
     * Otherwise drain owned disconnects before cancelling the shared reads. */
    unsigned char requested[BT_MAX_CONTROLLERS] = {0};
    for (unsigned i = 0; i < BT_MAX_CONTROLLERS; ++i) {
        bt_link *link = &links[i];
        if (link->connected) {
            uint8_t disconnect[3] = {0, 0, 0x13};
            put(disconnect, link->handle);
            (void)bt_hci_send(0x0406, disconnect, sizeof disconnect);
            requested[i] = 1;
        }
        if (link->connection_pending) {
            if (link->target_type == BT_ADDRESS_CLASSIC)
                (void)bt_hci_send(0x0408, link->target, 6);
            else
                (void)bt_hci_send(0x200e, NULL, 0);
        }
    }
    int64_t deadline = milliseconds() + 1500;
    while (transport_open && !native_retry && milliseconds() < deadline) {
        int remaining = 0;
        for (unsigned i = 0; i < BT_MAX_CONTROLLERS; ++i) {
            bt_link *link = &links[i];
            if (link->connected && !requested[i]) {
                uint8_t disconnect[3] = {0, 0, 0x13};
                put(disconnect, link->handle);
                (void)bt_hci_send(0x0406, disconnect, sizeof disconnect);
                requested[i] = 1;
            }
            remaining += link->connected || link->connection_pending;
        }
        if (!remaining || bt_hci_cleanup_poll(10) < 0)
            break;
    }
    bt_hci_close();
    transport_open = 0;
    for (unsigned i = 0; i < BT_MAX_CONTROLLERS; ++i) {
        if (native_retry)
            links[i].result = 4;
        release_link(&links[i]);
    }
}
void bt_session_disconnect(unsigned slot)
{
    if (slot >= BT_MAX_CONTROLLERS || !links[slot].occupied)
        return;
    bt_link *previous = current;
    current = &links[slot];
    if (current->target_type == BT_ADDRESS_CLASSIC)
        log_line("BT Switch: cleanup slot %u connected %d ready %d encrypted %d failed %d "
                 "pad stop %d result %d",
                 slot + 1, current->connected, current->ready, current->encrypted,
                 current->classic.failed, current->bridge_stop, current->result);
    if (native_retry) {
        bt_sessions_close();
        current = previous;
        return;
    }
    if (current->connection_pending) {
        if (current->target_type == BT_ADDRESS_CLASSIC) {
            int status = command(0x0408, current->target, 6, 1);
            if (!status)
                current->connection_pending = 0;
        } else
            (void)command(0x200e, NULL, 0, 1);
        int64_t deadline = milliseconds() + 1500;
        while (current->connection_pending && milliseconds() < deadline && !native_retry)
            if (bt_hci_cleanup_poll(10) < 0)
                break;
    }
    if (current->connected && !native_retry) {
        uint8_t disconnect[3] = {0, 0, 0x13};
        put(disconnect, current->handle);
        (void)bt_hci_send(0x0406, disconnect, sizeof disconnect);
        int64_t deadline = milliseconds() + 1500;
        while (current->connected && milliseconds() < deadline && !native_retry)
            if (bt_hci_cleanup_poll(10) < 0)
                break;
    }
    if (current->connected || current->connection_pending || native_retry) {
        log_line("BT radio: link cleanup incomplete; closing shared transport");
        bt_sessions_close();
    } else {
        release_link(current);
        int occupied = 0;
        for (unsigned i = 0; i < BT_MAX_CONTROLLERS; ++i)
            occupied += links[i].occupied;
        if (!occupied && !scan_advertisement) {
            bt_hci_close();
            transport_open = 0;
        }
    }
    current = previous;
}
int bt_sessions_poll(unsigned timeout_ms)
{
    if (!transport_open)
        return 0;
    int rc = bt_hci_poll(timeout_ms, NULL, NULL);
    if (native_retry) {
        bt_sessions_close();
        return 4;
    }
    if (rc < 0) {
        bt_sessions_close();
        return -1;
    }
    bt_link *previous = current;
    for (unsigned i = 0; i < BT_MAX_CONTROLLERS; ++i) {
        current = &links[i];
        if (!current->occupied || !current->ready)
            continue;
        if (current->connected) {
            drain();
            bridge_submit(0);
        }
        if (!current->connected || !current->encrypted ||
            (current->target_type == BT_ADDRESS_CLASSIC ? current->classic.failed
                                                        : current->smp.phase == SMP_FAILED) ||
            current->bridge_stop ||
            (current->quit_since && milliseconds() - current->quit_since >= 2000)) {
            current->result = current->bridge_stop || current->quit_since ? 2 : 0;
            bt_session_disconnect(i);
        }
    }
    current = previous;
    if (native_retry) {
        bt_sessions_close();
        return 4;
    }
    return 0;
}
int bt_session_run(const bt_session_config *config)
{
    bt_sessions_set_control(config->tick, config->cancelled, config->context);
    int result = bt_session_start(0, config);
    if (result != 1)
        return result;
    while (bt_session_connected(0) && !radio_cancel(NULL) && !log_stop_requested())
        if (bt_sessions_poll(10) != 0)
            break;
    result = bt_session_result(0);
    bt_session_disconnect(0);
    return result;
}
