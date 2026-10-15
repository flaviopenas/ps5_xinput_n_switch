/* 4.03 kernel text mapping metadata only. Never read kernel instructions or
 * change page tables. Walk follows the local SDK kernel_proc_getpaddr layout;
 * XOTEXT bit interpretation follows ps5-hen util.h, not a live patch contract. */
#include "log.h"
#include <ps5/kernel.h>
#include <stdint.h>
#include <unistd.h>

static int read_word(uint64_t address, uint64_t *value)
{
    if ((address >> 48) != 0xffff || (address & 7)) return -1;
    *value = 0;
    return kernel_copyout((intptr_t)address, value, sizeof *value);
}

static int inspect(uint64_t address, uint64_t pml4, uint64_t cr3)
{
    const uint64_t frame_mask = UINT64_C(0x000ffffffffff000);
    uint64_t entry = cr3;
    for (int shift = 39; shift >= 12; shift -= 9) {
        uint64_t physical = entry & frame_mask;
        uint64_t offset = ((address >> shift) & 511) * 8;
        /* PML4 VA minus CR3 is the SDK-derived direct-map base. Bound physical
         * table addresses to the PS5 memory aperture before dereferencing. */
        if (!physical || physical >= UINT64_C(0x1000000000) || pml4 < cr3) return -1;
        if (read_word(pml4 - cr3 + physical + offset, &entry)) return -1;
        if (!(entry & 1)) {
            log_line("BT kernel access: text offset 0x%llX not present at level %d",
                     (unsigned long long)(address - KERNEL_ADDRESS_TEXT_BASE), shift);
            return -1;
        }
        if (shift == 12 || ((shift == 21 || shift == 30) && (entry & 0x80))) {
            log_line("BT kernel access: text offset 0x%llX leaf shift %d XOTEXT %u writable %u NX %u",
                     (unsigned long long)(address - KERNEL_ADDRESS_TEXT_BASE), shift,
                     (unsigned)((entry >> 58) & 1), (unsigned)((entry >> 1) & 1),
                     (unsigned)(entry >> 63));
            return 0;
        }
    }
    return -1;
}

int main(void)
{
    if (!log_open_port("/data/ps5-xinput", "/data/ps5-xinput/bt-kernel-access.log", 9024)) return 1;
    for (unsigned i = 0; i < 15; ++i) { log_poll(); usleep(100000); }
    log_line("BT kernel access: FW 0x%08X; mapping metadata only; no code reads/writes", kernel_get_fw_version());
    if (kernel_get_fw_version() != 0x04030000 ||
        KERNEL_ADDRESS_DATA_BASE - KERNEL_ADDRESS_TEXT_BASE != 0xc00000 ||
        KERNEL_OFFSET_PROC_P_VMSPACE != 0x200 ||
        KERNEL_OFFSET_VMSPACE_VM_PMAP != 0x2e0) goto done;
    uint64_t process = kernel_get_proc(0), vmspace, pml4, cr3;
    if (!process || read_word(process + KERNEL_OFFSET_PROC_P_VMSPACE, &vmspace) || !vmspace ||
        read_word(vmspace + KERNEL_OFFSET_VMSPACE_VM_PMAP + 0x20, &pml4) ||
        read_word(vmspace + KERNEL_OFFSET_VMSPACE_VM_PMAP + 0x28, &cr3) ||
        (pml4 >> 48) != 0xffff || (pml4 & 4095) || !cr3 || (cr3 & 4095) ||
        cr3 >= UINT64_C(0x1000000000)) {
        log_line("BT kernel access: SDK mapping metadata unavailable; stopping"); goto done;
    }
    /* Six representative addresses, at most 24 page-table reads. This does
     * not assert complete text readability or absence of nested protections. */
    unsigned completed = 0;
    for (unsigned offset = 0; offset < 0xc00000 && !log_stop_requested(); offset += 0x200000) {
        if (inspect((uint64_t)KERNEL_ADDRESS_TEXT_BASE + offset, pml4, cr3)) break;
        ++completed; log_poll();
    }
    log_line("BT kernel access: inspected %u representative mappings; code readability not tested", completed);
done:
    notify("PS5 XInput: kernel mapping check complete");
    log_line("Cleanup complete; payload exiting");
    log_close(); return 0;
}
