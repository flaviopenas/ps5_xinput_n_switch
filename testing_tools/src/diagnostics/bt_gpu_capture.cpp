#error "Parked experiment: current investigation is offline only; do not build or deploy"
/* Native Bluetooth investigation only. No HEN or Bluetooth calls.
 * GPU helper comes from pinned ps5-hen source. Default build is an own-buffer
 * self-test; BT_GPU_KERNEL_CAPTURE adds temporary TMR16 access and text reads.
 * Never disables nested paging, changes kernel text, or installs kexec. */
extern "C" {
#include "log.h"
#include <ps5/kernel.h>
}
#include "gpu_dma.h"
#include "util.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <time.h>

static uint64_t root, root_phys, dmap;
static bool word(uint64_t address, uint64_t &value)
{
    value = 0;
    return (address >> 48) == 0xffff && !(address & 7) &&
        !kernel_copyout(address, &value, sizeof value);
}
static bool preflight()
{
    uint64_t vmspace = 0, reference_root = 0;
    uint64_t proc = kernel_get_proc(0);
    if (!proc || !word(proc + KERNEL_OFFSET_PROC_P_VMSPACE, vmspace) ||
        !word(vmspace + KERNEL_OFFSET_VMSPACE_VM_PMAP + 0x20, root) ||
        !word(vmspace + KERNEL_OFFSET_VMSPACE_VM_PMAP + 0x28, root_phys) ||
        (root >> 48) != 0xffff || (root & 4095) || !root_phys || (root_phys & 4095) ||
        root_phys >= 0x1000000000ULL || root <= root_phys) return false;
    dmap = root - root_phys;
    if (!word(KERNEL_ADDRESS_TEXT_BASE + fw_off(0x0403, "OFF_PMAP_STORE") + 0x20, reference_root) ||
        reference_root != root || get_dmap_base() != dmap) return false;
    proc = kernel_get_proc(getpid());
    if (!proc || !word(proc + KERNEL_OFFSET_PROC_P_VMSPACE, vmspace)) return false;
    uint32_t vmid = 0;
    if (kernel_copyout(vmspace + fw_off(0x0403, "VMSPACE_VM_VMID"), &vmid, sizeof vmid) || vmid >= 16)
        return false;
    uint64_t gvm = KERNEL_ADDRESS_DATA_BASE + fw_off(0x0403, "DATA_BASE_GVMSPACE") + vmid * 0x100;
    uint64_t pdb, start, size;
    if (!word(gvm + 0x38, pdb) || (pdb >> 48) != 0xffff || (pdb & 63) ||
        !word(gvm + 0x08, start) || !word(gvm + 0x10, size) || !size ||
        size > 0x800000000000ULL || start > 0x800000000000ULL - size) return false;
    log_line("BT GPU: SDK/reference CPU roots agree; own GPU VMID %u metadata validated", vmid);
    return true;
}

#ifdef BT_GPU_KERNEL_CAPTURE
static bool physical(uint64_t address, uint64_t &pa, uint64_t &available)
{
    uint64_t entry = root_phys;
    for (int shift = 39; shift >= 12; shift -= 9) {
        uint64_t table = entry & 0x000ffffffffff000ULL;
        if (!table || table >= 0x1000000000ULL ||
            !word(dmap + table + ((address >> shift) & 511) * 8, entry) || !(entry & 1)) return false;
        if (shift == 12 || ((shift == 21 || shift == 30) && (entry & 0x80))) {
            uint64_t low = (1ULL << shift) - 1;
            pa = (entry & 0x000ffffffffff000ULL & ~low) | (address & low);
            available = (1ULL << shift) - (address & low);
            return pa < 0x1000000000ULL;
        }
    }
    return false;
}
static bool transmit(const unsigned char *data, uint32_t length)
{
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) return false;
    sockaddr_in address = {};
    address.sin_len = sizeof address; address.sin_family = AF_INET;
    address.sin_port = htons(9026); address.sin_addr.s_addr = INADDR_ANY;
    int yes = 1; setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    if (bind(listener, (sockaddr*)&address, sizeof address) || listen(listener, 1) ||
        fcntl(listener, F_SETFL, O_NONBLOCK)) { close(listener); return false; }
    log_line("BT GPU: verified repeated capture ready on TCP 9026 for 45 seconds");
    time_t deadline = time(nullptr) + 45;
    int peer = -1;
    while (time(nullptr) < deadline && !log_stop_requested()) {
        peer = accept(listener, nullptr, nullptr);
        if (peer >= 0) break;
        if (errno != EAGAIN && errno != EINTR) break;
        log_poll(); usleep(10000);
    }
    close(listener);
    if (peer < 0) return false;
    timeval timeout = {5, 0}; setsockopt(peer, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
    struct { char magic[8]; uint32_t firmware, length; uint64_t base, reserved; }
        header = {{'X','I','B','T','4','0','3',0}, 0x04030000, length,
                  (uint64_t)KERNEL_ADDRESS_TEXT_BASE, 0};
    static_assert(sizeof header == 32);
    bool success = true;
    for (unsigned phase = 0; phase < 2 && success; ++phase) {
        const unsigned char *source = phase ? data : (const unsigned char*)&header;
        size_t remaining = phase ? length : sizeof header;
        while (remaining && time(nullptr) < deadline && !log_stop_requested()) {
            ssize_t sent = send(peer, source, remaining > 65536 ? 65536 : remaining, MSG_NOSIGNAL);
            if (sent <= 0) { success = false; break; }
            source += sent; remaining -= sent; log_poll();
        }
        if (remaining) success = false;
    }
    close(peer); return success;
}
#endif

int main()
{
    if (!log_open_port("/data/ps5-xinput", "/data/ps5-xinput/bt-gpu-capture.log", 9024)) return 1;
    for (unsigned i = 0; i < 15; ++i) { log_poll(); usleep(100000); }
    uint64_t saved_auth = kernel_get_ucred_authid(getpid());
    bool auth_changed = false, success = false;
    unsigned char *capture = nullptr;
#ifdef BT_GPU_KERNEL_CAPTURE
    uint32_t saved_tmr = 0; bool tmr_changed = false;
#endif
    log_line("BT GPU: FW 0x%08X; isolated code-access investigation", kernel_get_fw_version());
    g_fw() = 0x0403;
    if (kernel_get_fw_version() != 0x04030000 ||
        KERNEL_ADDRESS_DATA_BASE - KERNEL_ADDRESS_TEXT_BASE != 0xc00000 ||
        KERNEL_OFFSET_VMSPACE_VM_PMAP != 0x2e0 || !preflight()) goto cleanup;
    if (kernel_set_ucred_authid(getpid(), 0x4800000000000007ULL)) goto cleanup;
    auth_changed = true;
    {
        gpu_kernel_offsets go = {};
        go.proc_vmspace = KERNEL_OFFSET_PROC_P_VMSPACE;
        go.vmspace_vm_vmid = fw_off(0x0403, "VMSPACE_VM_VMID");
        go.data_base_gvmspace = fw_off(0x0403, "DATA_BASE_GVMSPACE");
        go.sizeof_gvmspace = 0x100; go.gvmspace_page_dir_va = 0x38;
        go.gvmspace_size = 0x10; go.gvmspace_start_va = 0x08;
        gpu_set_offsets(&go);
        if (gpu_init()) { log_line("BT GPU: initialization failed"); goto cleanup; }
        gpu_ctx *gpu = gpu_get_ctx();
        unsigned char expected[64], received[64];
        for (unsigned i = 0; i < sizeof expected; ++i) expected[i] = (unsigned char)(i * 13 + 7);
        memcpy((void*)gpu->victim_va, expected, sizeof expected);
        memset(received, 0, sizeof received);
        if (gpu_read_phys(gpu->victim_real_pa, received, sizeof received) ||
            memcmp(expected, received, sizeof expected)) {
            log_line("BT GPU: own-buffer read failed; no protected-region access attempted"); goto cleanup;
        }
        log_line("BT GPU: own-buffer physical read passed");
    }
#ifdef BT_GPU_KERNEL_CAPTURE
    {
        capture = (unsigned char*)malloc(0xc00000);
        if (!capture) goto cleanup;
        uint64_t first_pa, available;
        if (!physical(KERNEL_ADDRESS_TEXT_BASE, first_pa, available)) goto cleanup;
        uint32_t base = tmr_read(dmap, TMR_BASE(16)), limit = tmr_read(dmap, TMR_LIMIT(16));
        saved_tmr = tmr_read(dmap, TMR_CONFIG(16));
        if (!(saved_tmr & 1) || !base || limit < base || (first_pa >> 16) < base ||
            (first_pa >> 16) > limit || limit - base > 0x1000) goto cleanup;
        tmr_changed = true;
        tmr_write(dmap, TMR_CONFIG(16), TMR_CFG_PERMISSIVE);
        if (tmr_read(dmap, TMR_CONFIG(16)) != TMR_CFG_PERMISSIVE) goto cleanup;
        log_line("BT GPU: TMR16 access changed temporarily; no VMCB/page-table protection changes");
        unsigned char repeated[65536];
        time_t deadline = time(nullptr) + 120;
        for (uint32_t offset = 0; offset < 0xc00000;) {
            uint64_t pa, count;
            if (log_stop_requested() || time(nullptr) >= deadline ||
                !physical(KERNEL_ADDRESS_TEXT_BASE + offset, pa, count) ||
                (pa >> 16) < base || (pa >> 16) > limit) goto cleanup;
            if (count > sizeof repeated) count = sizeof repeated;
            uint64_t gpu_remaining = 0x200000 - (pa & 0x1fffff);
            if (count > gpu_remaining) count = gpu_remaining;
            if (count > 0xc00000 - offset) count = 0xc00000 - offset;
            if (!count || gpu_read_phys(pa, capture + offset, (uint32_t)count) ||
                gpu_read_phys(pa, repeated, (uint32_t)count) ||
                memcmp(capture + offset, repeated, count)) {
                log_line("BT GPU: read mismatch at text offset 0x%X; discarding capture", offset); goto cleanup;
            }
            offset += (uint32_t)count; log_poll();
            if (!(offset & 0xfffff)) log_line("BT GPU: repeated text capture %u/12 MiB", offset >> 20);
        }
        tmr_write(dmap, TMR_CONFIG(16), saved_tmr);
        if (tmr_read(dmap, TMR_CONFIG(16)) != saved_tmr) goto cleanup;
        tmr_changed = false;
        log_line("BT GPU: TMR16 restored before exposing capture");
        success = transmit(capture, 0xc00000);
    }
#else
    success = true;
#endif
cleanup:
#ifdef BT_GPU_KERNEL_CAPTURE
    if (tmr_changed) {
        tmr_write(dmap, TMR_CONFIG(16), saved_tmr);
        log_line("BT GPU: TMR16 cleanup restoration %s",
                 tmr_read(dmap, TMR_CONFIG(16)) == saved_tmr ? "verified" : "FAILED");
    }
#endif
    free(capture); gpu_cleanup();
    if (auth_changed)
        log_line("BT GPU: payload credentials restore result %d", kernel_set_ucred_authid(getpid(), saved_auth));
    log_line("BT GPU: finished success %u", (unsigned)success);
    notify("PS5 XInput: GPU code-access test finished");
    log_line("Cleanup complete; payload exiting");
    log_close(); return success ? 0 : 1;
}
