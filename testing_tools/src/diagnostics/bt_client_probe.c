/* Read native client mappings/code. No Bluetooth device I/O or API calls. */
#include "log.h"
#include <ps5/kernel.h>
#include <ps5/mdbg.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <sys/mman.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

static unsigned hits;
static uint64_t scanned;
static time_t deadline;
static uint64_t client_base;
static uint64_t control_slot, control_start, control_end, control_functions[3];
static void descriptor_path(pid_t pid, int wanted)
{
    int mib[] = {CTL_KERN, KERN_PROC, KERN_PROC_FILEDESC, pid};
    size_t length = 0;
    if (sysctl(mib, 4, NULL, &length, NULL, 0) || !length || length > 8 * 1024 * 1024) return;
    length += 4096;
    unsigned char *data = malloc(length);
    if (!data) return;
    if (!sysctl(mib, 4, data, &length, NULL, 0)) {
        for (size_t at = 0; at + sizeof(int) <= length;) {
            int stride;
            memcpy(&stride, data + at, sizeof stride);
            if (stride < (int)(offsetof(struct kinfo_file, kf_fd) + sizeof(int)) ||
                stride > 8192 || (size_t)stride > length - at) break;
            struct kinfo_file entry = {0};
            size_t count = (size_t)stride < sizeof entry ? (size_t)stride : sizeof entry;
            memcpy(&entry, data + at, count);
            entry.kf_path[sizeof entry.kf_path - 1] = 0;
            if (entry.kf_fd == wanted)
                log_line("BT client: control descriptor %d type %d path %.180s", wanted,
                         entry.kf_type, entry.kf_path);
            at += (size_t)stride;
        }
    }
    free(data);
}

static void descriptor(pid_t pid, uint64_t address, const unsigned char *prefix)
{
    /* The observed 4.03 zero-argument wrapper: mov edi,[rip+disp32];
     * mov esi,request. Resolve only after matching those instruction bytes. */
    if (prefix[0] != 0x8b || prefix[1] != 0x3d || prefix[6] != 0xbe) return;
    int32_t displacement;
    memcpy(&displacement, prefix + 2, sizeof displacement);
    uint64_t slot = address - 1 + displacement;
    int protection = kernel_get_vmem_protection(pid, (intptr_t)slot, sizeof(int));
    int fd = -1;
    if (protection < 0 || !(protection & PROT_READ)) {
        log_line("BT client: control descriptor slot protection unavailable"); return;
    }
    int result = mdbg_copyout(pid, (intptr_t)slot, &fd, sizeof fd);
    log_line("BT client: control wrapper descriptor slot 0x%llX fd %d",
             (unsigned long long)slot, fd);
    if (result || fd < 0 || fd > 65535) return;
    descriptor_path(pid, fd);
}

static pid_t find_process(const char *wanted)
{
    int mib[] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
    size_t length = 0;
    if (sysctl(mib, 4, NULL, &length, NULL, 0) || !length || length > 8 * 1024 * 1024)
        return -1;
    length += 4096;
    unsigned char *data = malloc(length);
    if (!data) return -1;
    pid_t found = -1;
    if (!sysctl(mib, 4, data, &length, NULL, 0)) {
        for (size_t at = 0; at + sizeof(int) <= length;) {
            int stride;
            memcpy(&stride, data + at, sizeof stride);
            if (stride < (int)(offsetof(struct kinfo_proc, ki_comm) + COMMLEN + 1) ||
                stride > 4096 || (size_t)stride > length - at) break;
            char name[COMMLEN + 1];
            memcpy(name, data + at + offsetof(struct kinfo_proc, ki_comm), sizeof name);
            name[COMMLEN] = 0;
            if (!strcmp(name, wanted)) {
                memcpy(&found, data + at + offsetof(struct kinfo_proc, ki_pid), sizeof found);
                break;
            }
            at += (size_t)stride;
        }
    }
    free(data);
    return found;
}

static void capture_span(pid_t pid, uint64_t start, unsigned length)
{
    unsigned char bytes[4096];
    if (!length || length > sizeof bytes) return;
    memset(bytes, 0xcc, sizeof bytes);
    if (mdbg_copyout(pid, (intptr_t)start, bytes, length)) {
        log_line("BT client: candidate window unavailable at 0x%llX", (unsigned long long)start);
        return;
    }
    for (unsigned at = 0; at < length; at += 64) {
        unsigned count = length - at < 64 ? length - at : 64;
        char hex[129];
        for (unsigned i = 0; i < count; ++i) snprintf(hex + i * 2, 3, "%02x", bytes[at + i]);
        log_line("BT ABI: NativeClient%u 0x%llX +%04X %s", hits,
                 (unsigned long long)start, at, hex);
        log_poll(); usleep(10000);
    }
}

static void capture(pid_t pid, uint64_t address, uint64_t begin, uint64_t end)
{
    uint64_t start = address - begin >= 128 ? address - 128 : begin;
    unsigned length = end - start >= 384 ? 384 : (unsigned)(end - start);
    capture_span(pid, start, length);
}

static void scan(pid_t pid, const struct kinfo_vmentry *map)
{
    /* Validate via SDK as well as the returned metadata before reading code.
     * Invalid/unknown mappings are skipped; no guessed firmware addresses. */
    uint64_t begin = map->kve_start, end = map->kve_end;
    if (!begin || begin >= end || end > UINT64_C(0x800000000000) ||
        end - begin > 16 * 1024 * 1024 ||
        !(map->kve_protection & PROT_EXEC)) return;
    int protection = kernel_get_vmem_protection(pid, (intptr_t)begin, end - begin);
    if (protection < 0 || !(protection & PROT_EXEC)) return;
    static const uint32_t requests[] = {
        0x20046204, 0x20046205, 0x80086202, 0x80086203, 0x8008620a,
        0x80086234, 0x80186201, 0xc0106206, 0xc0106237,
        0x80046209, 0x80186223, 0x80186224, 0x80086225
    };
    unsigned char bytes[8192 + 3];
    for (uint64_t address = begin; address < end && hits < 32 &&
         scanned < 16 * 1024 * 1024 && time(NULL) < deadline; address += 8192) {
        size_t length = end - address < sizeof bytes ? (size_t)(end - address) : sizeof bytes;
        memset(bytes, 0xcc, sizeof bytes);
        if (mdbg_copyout(pid, (intptr_t)address, bytes, length)) {
            log_line("BT client: code read unavailable at 0x%llX; ending this mapping",
                     (unsigned long long)address);
            break;
        }
        size_t primary = end - address < 8192 ? (size_t)(end - address) : 8192;
        scanned += primary;
        for (size_t at = 0; at < primary && at + 4 <= length && hits < 32; ++at) {
            uint32_t value;
            memcpy(&value, bytes + at, sizeof value);
            for (unsigned i = 0; i < sizeof requests / sizeof requests[0]; ++i) {
                if (value != requests[i]) continue;
                ++hits;
                log_line("BT client: byte candidate %u request 0x%08X at 0x%llX",
                         hits, value, (unsigned long long)(address + at));
                if ((value == 0x20046204 || value == 0x20046205) && at >= 7)
                    descriptor(pid, address + at, bytes + at - 7);
                if (value == 0x20046204 && at >= 7 && bytes[at - 7] == 0x8b &&
                    bytes[at - 6] == 0x3d && bytes[at - 1] == 0xbe) {
                    int32_t displacement;
                    memcpy(&displacement, bytes + at - 5, sizeof displacement);
                    control_slot = address + at - 1 + displacement;
                    control_functions[0] = address + at - 7;
                    control_functions[1] = address + at + 25;
                    control_functions[2] = 0; /* Identified separately by its frame prologue. */
                    control_start = begin; control_end = end;
                }
                if (value == 0x8008620a && at >= 21 &&
                    !memcmp(bytes + at - 21, "\x55\x48\x89\xe5", 4))
                    control_functions[2] = address + at - 21;
                capture(pid, address + at, begin, end);
                break;
            }
        }
        log_poll(); usleep(1000);
    }
}

static void control_references(pid_t pid)
{
    unsigned char bytes[8192 + 6];
    unsigned count = 0;
    for (uint64_t address = control_start; control_slot && address < control_end &&
         count < 24 && time(NULL) < deadline; address += 8192) {
        size_t length = control_end - address < sizeof bytes ? (size_t)(control_end - address) : sizeof bytes;
        memset(bytes, 0xcc, sizeof bytes);
        if (mdbg_copyout(pid, (intptr_t)address, bytes, length)) break;
        for (size_t at = 0; at < 8192 && at + 6 <= length && count < 24; ++at) {
            uint64_t target = 0;
            int32_t displacement;
            int match = 0;
            if (bytes[at] == 0xe8 || bytes[at] == 0xe9) {
                memcpy(&displacement, bytes + at + 1, 4);
                target = address + at + 5 + displacement;
                for (unsigned i = 0; i < 3; ++i)
                    if (target && target == control_functions[i]) match = 1;
            } else if (bytes[at] == 0x89 && (bytes[at + 1] & 0xc7) == 0x05) {
                memcpy(&displacement, bytes + at + 2, 4);
                target = address + at + 6 + displacement;
                if (target == control_slot) match = 1;
            }
            if (!match) continue;
            ++count; ++hits;
            log_line("BT client: control reference candidate 0x%llX -> 0x%llX",
                     (unsigned long long)(address + at), (unsigned long long)target);
            capture(pid, address + at, control_start, control_end);
            if (target == control_slot && at >= 19 &&
                !memcmp(bytes + at - 19, "\x48\x8d\x3d", 3) &&
                bytes[at - 12] == 0xbe) {
                /* Capture setup and its adjacent event consumer within the
                 * already validated executable mapping, using a matched FD
                 * writer rather than a fixed address from another session. */
                uint64_t setup = address + at;
                uint64_t window = setup - control_start >= 2048 ? setup - 2048 : control_start;
                unsigned span = control_end - window >= 4096 ? 4096 : (unsigned)(control_end - window);
                ++hits;
                log_line("BT client: bounded native setup/event code window");
                capture_span(pid, window, span);
                int32_t path_displacement;
                memcpy(&path_displacement, bytes + at - 16, 4);
                uint64_t path_address = address + at - 12 + path_displacement;
                int protection = kernel_get_vmem_protection(pid, (intptr_t)path_address, 64);
                char path[64] = {0};
                if (protection >= 0 && (protection & PROT_READ) &&
                    !mdbg_copyout(pid, (intptr_t)path_address, path, sizeof path)) {
                    path[sizeof path - 1] = 0;
                    log_line("BT client: observed control open path %.63s", path);
                }
            }
        }
        log_poll(); usleep(1000);
    }
    log_line("BT client: control reference pass complete; %u candidates", count);
}

static void mappings(pid_t pid)
{
    int mib[] = {CTL_KERN, KERN_PROC, KERN_PROC_VMMAP, pid};
    size_t length = 0;
    if (sysctl(mib, 4, NULL, &length, NULL, 0) || !length || length > 8 * 1024 * 1024) {
        log_line("BT client: native VM inventory unavailable"); return;
    }
    length += 4096;
    unsigned char *data = malloc(length);
    if (!data) return;
    if (sysctl(mib, 4, data, &length, NULL, 0)) {
        log_line("BT client: native VM inventory read failed"); free(data); return;
    }
    unsigned valid = 0, selected = 0;
    for (size_t at = 0; at + sizeof(int) <= length;) {
        int stride;
        memcpy(&stride, data + at, sizeof stride);
        if (stride < (int)(offsetof(struct kinfo_vmentry, kve_path) + 1) ||
            stride > 8192 || (size_t)stride > length - at) {
            log_line("BT client: VM layout unavailable at %lu stride %d",
                     (unsigned long)at, stride); break;
        }
        struct kinfo_vmentry map = {0};
        size_t count = (size_t)stride < sizeof map ? (size_t)stride : sizeof map;
        memcpy(&map, data + at, count);
        map.kve_path[sizeof map.kve_path - 1] = 0;
        ++valid;
        if (map.kve_protection & PROT_EXEC) {
            log_line("BT client: executable map 0x%llX-0x%llX protection %X stride %d path %.180s",
                     (unsigned long long)map.kve_start, (unsigned long long)map.kve_end,
                     map.kve_protection, stride, map.kve_path);
        }
        if (map.kve_protection & PROT_EXEC) {
            ++selected;
            log_line("BT client: map 0x%llX-0x%llX protection %X path %.180s",
                     (unsigned long long)map.kve_start, (unsigned long long)map.kve_end,
                     map.kve_protection, map.kve_path);
            scan(pid, &map);
        }
        at += (size_t)stride;
    }
    log_line("BT client: %u VM records, %u matching mappings", valid, selected);
    free(data);
}

int main(void)
{
    if (!log_open_port("/data/ps5-xinput", "/data/ps5-xinput/bt-client-probe.log", 9024)) return 1;
    for (unsigned i = 0; i < 15; ++i) { log_poll(); usleep(100000); }
    uint32_t firmware = kernel_get_fw_version();
    log_line("BT client: FW 0x%08X; process metadata and bounded native client code only", firmware);
    if (firmware == 0x04030000) {
        static const char *clients[] = {"SceShellUI"};
        for (unsigned i = 0; i < sizeof clients / sizeof clients[0]; ++i) {
            pid_t pid = find_process(clients[i]);
            log_line("BT client: process %s pid %d", clients[i], pid);
            deadline = time(NULL) + 20;
            /* Preserve cumulative hit numbering, but bound each client separately. */
            uint64_t previous = scanned;
            scanned = 0;
            if (pid <= 0) { scanned = previous; continue; }
            uint32_t handle;
            if (kernel_dynlib_handle(pid, clients[i], &handle) == 0) {
                client_base = kernel_dynlib_mapbase_addr(pid, handle);
                log_line("BT client: main module handle %u base 0x%llX", handle,
                         (unsigned long long)client_base);
            } else log_line("BT client: named main module lookup unavailable");
            mappings(pid);
            control_references(pid);
            scanned += previous;
        }
    }
    log_line("BT client: complete; %llu bytes scanned, %u byte candidates; no Bluetooth operations",
             (unsigned long long)scanned, hits);
    notify("PS5 XInput: native client inspection complete");
    for (unsigned i = 0; i < 15; ++i) { log_poll(); usleep(100000); }
    log_close(); return 0;
}
