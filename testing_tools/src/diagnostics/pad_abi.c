#include "pad_abi.h"
#include "log.h"
#include <ps5/kernel.h>
#include <ps5/mdbg.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/* Read the payload's own loaded library, without calling virtual-pad exports
 * or attaching to another process. These bytes establish the firmware ABI. */
void pad_abi_log(void)
{
    static const struct { const char *name; size_t bytes; } symbols[] = {
        {"scePadVirtualDeviceAddDevice", 2048},
        {"scePadVirtualDeviceInsertData", 2048},
        {"scePadVirtualDeviceDeleteDevice", 256},
        {"scePadSetProcessPrivilege", 256}
    };
    void *module = dlopen("libScePad.sprx", RTLD_NOW);
    if (!module) { log_line("Pad ABI: libScePad load failed: %s", dlerror()); return; }
    for (size_t i = 0; i < sizeof symbols / sizeof symbols[0]; ++i) {
        uintptr_t address = (uintptr_t)dlsym(module, symbols[i].name);
        unsigned char bytes[2048];
        char hex[64 * 2 + 1];
        int protection;
        if (!address) { log_line("Pad ABI: %s export missing", symbols[i].name); continue; }
        protection = kernel_get_vmem_protection(getpid(), address, symbols[i].bytes);
        if (protection < 0 || !(protection & PROT_EXEC)) {
            log_line("Pad ABI: %s executable range unavailable", symbols[i].name); continue;
        }
        memset(bytes, 0, sizeof bytes);
        if (mdbg_copyout(getpid(), (intptr_t)address, bytes, symbols[i].bytes) != 0) {
            log_line("Pad ABI: %s code read failed", symbols[i].name); continue;
        }
        for (size_t offset = 0; offset < symbols[i].bytes; offset += 64) {
            for (size_t j = 0; j < 64; ++j)
                snprintf(hex + j * 2, 3, "%02x", bytes[offset + j]);
            log_line("Pad ABI: %s 0x%llX +%04lX %s", symbols[i].name,
                     (unsigned long long)address, (unsigned long)offset, hex);
        }
    }
    /* Keep the module loaded, as with UserService, in case the loader shares it. */
}
