/* Inspect this SDK's own cached SPRX symbols. No Bluetooth calls or device I/O. */
#include "log.h"
#include <ps5/mdbg.h>
#include <dlfcn.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include "rtld.h"
#include "elf.h"

/* Exact prefix/layout from the local SDK's crt/rtld_sprx.c. */
typedef struct {
    rtld_lib_t loader;
    int handle;
    char *strings;
    Elf64_Sym *symbols;
    unsigned long symbol_bytes;
    int unload;
} sprx_cache;

static int copy(uintptr_t address, void *data, size_t size)
{
    return address && address <= UINTPTR_MAX - size &&
           mdbg_copyout(getpid(), (intptr_t)address, data, size) == 0;
}

static int read_name(uintptr_t address, char name[64])
{
    for (unsigned i = 0; i < 64; ++i) {
        if (!copy(address + i, name + i, 1)) return 0;
        if (!name[i]) return i != 0;
    }
    return 0;
}

#ifdef BT_EXPORT_CAPTURE_UNKNOWN
static int capture_unknown(const sprx_cache *cache, const Elf64_Sym *symbol, const char *name)
{
    static const char *unknown[] = {"L5jHxZ-J630", "eGVPAJ+M5A8", "ud-L5OvD5bA", "o+5KjZ3r4O4"};
    for (unsigned i = 0; i < sizeof unknown / sizeof unknown[0]; ++i) {
        if (strncmp(name, unknown[i], 11)) continue;
        if ((symbol->st_info & 15) != 2 || !symbol->st_size || symbol->st_size > 512 ||
            symbol->st_value >= cache->loader.mapsize ||
            symbol->st_size > cache->loader.mapsize - symbol->st_value) return 0;
        uintptr_t address = (uintptr_t)cache->loader.mapbase + symbol->st_value;
        for (unsigned at = 0; at < symbol->st_size; at += 64) {
            unsigned char bytes[64];
            char hex[129];
            unsigned amount = symbol->st_size - at;
            if (amount > sizeof bytes) amount = sizeof bytes;
            if (!copy(address + at, bytes, amount)) return 0;
            for (unsigned j = 0; j < amount; ++j) {
                static const char digits[] = "0123456789abcdef";
                hex[j*2] = digits[bytes[j] >> 4];
                hex[j*2+1] = digits[bytes[j] & 15];
            }
            hex[amount*2] = 0;
            log_line("BT ABI: HidUnknown%u 0x%llX +%04X %s", i,
                     (unsigned long long)address, at, hex);
            log_poll(); usleep(20000);
        }
    }
    return 1;
}
#endif

int main(void)
{
    if (!log_open_port("/data/ps5-xinput", "/data/ps5-xinput/bt-exports.log", 9024)) return 1;
    for (unsigned i = 0; i < 15; ++i) { log_poll(); usleep(100000); }
    void *module = dlopen("libSceBluetoothHid.sprx", RTLD_NOW);
    sprx_cache cache;
    uintptr_t anchor = module ? (uintptr_t)dlsym(module, "sceBluetoothHidInit") : 0;
    if (!anchor || !copy((uintptr_t)module, &cache, sizeof cache) ||
        !memchr(cache.loader.soname, 0, sizeof cache.loader.soname) ||
        !strstr(cache.loader.soname, "libSceBluetoothHid.sprx") ||
        !cache.symbols || !cache.strings || !cache.loader.mapbase ||
        !cache.symbol_bytes || cache.symbol_bytes > 4096 * sizeof(Elf64_Sym) ||
        cache.symbol_bytes % sizeof(Elf64_Sym) ||
        anchor < (uintptr_t)cache.loader.mapbase ||
        anchor - (uintptr_t)cache.loader.mapbase >= cache.loader.mapsize) {
        log_line("BT exports: SDK cache layout validation failed; stopping");
        log_close(); return 1;
    }
    unsigned count = cache.symbol_bytes / sizeof(Elf64_Sym), exported = 0;
    /* Verify the table contains the exact resolved anchor before logging it. */
    int verified = 0;
    for (unsigned i = 0; i < count; ++i) {
        Elf64_Sym symbol;
        if (!copy((uintptr_t)cache.symbols + i * sizeof symbol, &symbol, sizeof symbol)) break;
        if (symbol.st_value == anchor - (uintptr_t)cache.loader.mapbase && symbol.st_size)
            verified = 1;
    }
    if (!verified) {
        log_line("BT exports: anchor not found in cached table; stopping");
        log_close(); return 1;
    }
    log_line("BT exports: verified SDK cache; %u symbol records", count);
    for (unsigned i = 0; i < count; ++i) {
        Elf64_Sym symbol;
        char name[64];
        if (!copy((uintptr_t)cache.symbols + i * sizeof symbol, &symbol, sizeof symbol)) {
            log_line("BT exports: symbol read failed at %u; incomplete", i);
            log_close(); return 1;
        }
        if (!symbol.st_shndx || !(symbol.st_info >> 4)) continue;
        if (symbol.st_name > 1024 * 1024 ||
            !read_name((uintptr_t)cache.strings + symbol.st_name, name)) {
            log_line("BT exports: name unavailable at %u; incomplete", i);
            log_close(); return 1;
        }
        log_line("BT export: %s value 0x%llX size %llu type %u", name,
                 (unsigned long long)symbol.st_value,
                 (unsigned long long)symbol.st_size, symbol.st_info & 15);
#ifdef BT_EXPORT_CAPTURE_UNKNOWN
        if (!capture_unknown(&cache, &symbol, name)) {
            log_line("BT exports: unknown capture failed; stopping");
            log_close(); return 1;
        }
#endif
        ++exported; log_poll(); usleep(20000);
    }
    log_line("BT exports: complete; %u defined nonlocal symbols; no Bluetooth calls", exported);
    notify("PS5 XInput: native HID export inventory complete");
    for (unsigned i = 0; i < 15; ++i) { log_poll(); usleep(100000); }
    log_close(); return 0;
}
