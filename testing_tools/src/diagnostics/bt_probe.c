/* Firmware metadata only: no HCI commands, USB transfers, pairing, or writes
 * to other processes. This executable is separate from the USB bridge. */
#include "log.h"
#include <ps5/kernel.h>
#include <ps5/mdbg.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/bus.h>
#include <sys/user.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <dev/usb/usb.h>
#include <dev/usb/usb_ioctl.h>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void directory(const char *path)
{
    DIR *dir = opendir(path);
    struct dirent *entry;
    unsigned found = 0;
    if (!dir) { log_line("BT probe: directory %s errno %d", path, errno); return; }
    while ((entry = readdir(dir)) != NULL) {
        if (!strstr(entry->d_name, "bluetooth") &&
            !strstr(entry->d_name, "Bluetooth") &&
            !strstr(entry->d_name, "ubt")) continue;
        log_line("BT probe: entry %s/%s", path, entry->d_name);
        ++found;
    }
    closedir(dir);
    log_line("BT probe: %s %u Bluetooth entries", path, found);
}

static unsigned word(const unsigned char *bytes)
{
    return bytes[0] | ((unsigned)bytes[1] << 8);
}

static void usb_layout(void)
{
    /* Descriptor queries only. Never detach drivers, select configurations,
     * claim interfaces, initialize USB_FS, or consume system event packets. */
    for (unsigned bus = 0; bus < 8; ++bus) {
        for (unsigned unit = 1; unit < 16; ++unit) {
            char node[48];
            struct usb_device_descriptor device;
            struct usb_gen_descriptor descriptor;
            unsigned char config[4096];
            unsigned count = 0;
            int hci_interface = 0;
            int fd;
            snprintf(node, sizeof node, "/dev/ugen%u.%u", bus, unit);
            fd = open(node, O_RDONLY);
            if (fd < 0) continue;
            memset(&descriptor, 0, sizeof descriptor);
            descriptor.ugd_data = config;
            descriptor.ugd_maxlen = sizeof config;
            if (ioctl(fd, USB_GET_DEVICE_DESC, &device) != 0 ||
                ioctl(fd, USB_GET_FULL_DESC, &descriptor) != 0 ||
                descriptor.ugd_actlen > sizeof config) { close(fd); continue; }
            for (unsigned at = 0; at + 2 <= descriptor.ugd_actlen;) {
                unsigned length = config[at];
                if (length < 2 || length > descriptor.ugd_actlen - at) break;
                if (config[at + 1] == UDESC_INTERFACE && length >= 9)
                    hci_interface = config[at + 3] == 0 && config[at + 5] == 0xe0 &&
                                    config[at + 6] == 1 && config[at + 7] == 1;
                /* SCO interfaces have the same class, but no interrupt IN. */
                if (hci_interface && config[at + 1] == UDESC_ENDPOINT && length >= 7 &&
                    (config[at + 2] & 0x80) && (config[at + 3] & 3) == 3) {
                    ++count;
                    hci_interface = 0;
                }
                at += length;
            }
            if (count) {
                log_line("BT probe: USB %s %04X:%04X HCI functions %u", node,
                         word((const unsigned char *)&device.idVendor),
                         word((const unsigned char *)&device.idProduct), count);
                int relevant = 0;
                for (unsigned at = 0; at + 2 <= descriptor.ugd_actlen;) {
                    unsigned length = config[at];
                    if (length < 2 || length > descriptor.ugd_actlen - at) break;
                    if (config[at + 1] == UDESC_INTERFACE && length >= 9) {
                        relevant = config[at + 3] == 0 && config[at + 5] == 0xe0;
                        if (relevant) log_line("BT probe: interface %u class %02X/%02X/%02X",
                            config[at + 2], config[at + 5], config[at + 6], config[at + 7]);
                    }
                    if (relevant && config[at + 1] == UDESC_ENDPOINT && length >= 7)
                        log_line("BT probe: endpoint %02X attributes %02X max %u",
                                 config[at + 2], config[at + 3], word(config + at + 4));
                    at += length;
                }
            }
            close(fd);
        }
    }
}

static void native_fds(pid_t pid, const char *name)
{
    int mib[] = {CTL_KERN, KERN_PROC, KERN_PROC_FILEDESC, pid};
    size_t length = 0;
    if (sysctl(mib, 4, NULL, &length, NULL, 0) || !length || length > 8 * 1024 * 1024) {
        if (strstr(name, "SysCore")) log_line("BT native: fd inventory pid %d unavailable errno %d", pid, errno);
        return;
    }
    length += 4096;
    unsigned char *data = malloc(length);
    if (!data) return;
    int result = sysctl(mib, 4, data, &length, NULL, 0);
    if (strstr(name, "SysCore"))
        log_line("BT native: fd inventory pid %d rc %d errno %d bytes %lu path offset %lu",
                 pid, result, result ? errno : 0, (unsigned long)length,
                 (unsigned long)offsetof(struct kinfo_file, kf_path));
    if (!result) {
        for (size_t at = 0; at + sizeof(int) <= length;) {
            int stride; memcpy(&stride, data + at, sizeof stride);
            if (stride < (int)(offsetof(struct kinfo_file, kf_fd) + sizeof(int)) ||
                (size_t)stride > length - at) {
                if (strstr(name, "SysCore")) log_line("BT native: fd record layout unavailable, stride %d", stride);
                break;
            }
            struct kinfo_file entry = {0};
            size_t count = (size_t)stride < sizeof entry ? (size_t)stride : sizeof entry;
            memcpy(&entry, data + at, count);
            entry.kf_path[sizeof entry.kf_path - 1] = 0;
            if (strstr(name, "SysCore"))
                log_line("BT native: SysCore record fd %d type %d stride %d path %.180s",
                         entry.kf_fd, entry.kf_type, stride, entry.kf_path);
            if (strstr(entry.kf_path, "bluetooth") || strstr(entry.kf_path, "ugen0.2") ||
                strstr(entry.kf_path, "/usb/0.2"))
                log_line("BT native: pid %d %s fd %d type %d path %s", pid, name,
                         entry.kf_fd, entry.kf_type, entry.kf_path);
            at += (size_t)stride;
        }
    }
    free(data);
}
#ifdef BT_NATIVE_KERNEL_EXPERIMENT
/* Disabled in all build targets: one later device-list capture ended without
 * a completion marker and the loader became unreachable. Canonical pointer
 * checks alone do not establish that kernel memory is mapped/readable. */
static int native_pointer(uint64_t pointer, uint64_t arena)
{
    return pointer && ((pointer >> 32) == (arena >> 32) ||
        (pointer >= (uint64_t)KERNEL_ADDRESS_TEXT_BASE &&
         pointer < (uint64_t)KERNEL_ADDRESS_DATA_BASE + 0x8000000));
}

static int native_string(uint64_t pointer, uint64_t arena, char *text, size_t capacity)
{
    size_t length = capacity - 1;
    if (!native_pointer(pointer, arena)) return 0;
    if (length > 4096 - (pointer & 4095)) length = 4096 - (pointer & 4095);
    memset(text, 0, capacity);
    if (kernel_copyout((intptr_t)pointer, text, length) != 0) return 0;
    for (size_t i = 0; i < length && text[i]; ++i)
        if ((unsigned char)text[i] < 32 || (unsigned char)text[i] > 126) return 0;
    return text[0] != 0;
}

static void native_btts_class(uint64_t address, uint64_t arena)
{
    uint64_t fields[6];
    if (!native_pointer(address, arena) ||
        kernel_copyout((intptr_t)address, fields, sizeof fields) != 0) return;
    log_line("BT kernel btts: class 0x%llX methods 0x%llX softc-size %llu",
             (unsigned long long)address, (unsigned long long)fields[1],
             (unsigned long long)fields[2]);
    if (fields[1] < (uint64_t)KERNEL_ADDRESS_TEXT_BASE ||
        fields[1] >= (uint64_t)KERNEL_ADDRESS_DATA_BASE + 0x8000000) return;
    for (unsigned i = 0; i < 12; ++i) {
        uint64_t method[2];
        if (kernel_copyout((intptr_t)(fields[1] + i * 16), method, sizeof method) != 0 ||
            !method[0]) break;
        log_line("BT kernel btts: method %u descriptor 0x%llX function 0x%llX", i,
                 (unsigned long long)method[0], (unsigned long long)method[1]);
        if (method[1] < (uint64_t)KERNEL_ADDRESS_TEXT_BASE ||
            method[1] + 512 > (uint64_t)KERNEL_ADDRESS_DATA_BASE) continue;
        unsigned char code[512];
        char hex[129];
        if (kernel_copyout((intptr_t)method[1], code, sizeof code) != 0) continue;
        for (unsigned at = 0; at < sizeof code; at += 64) {
            for (unsigned j = 0; j < 64; ++j) snprintf(hex + j * 2, 3, "%02x", code[at+j]);
            log_line("BT kernel code: method %u 0x%llX +%04X %s", i,
                     (unsigned long long)method[1], at, hex);
            usleep(10000);
        }
    }
}

static void native_kernel_head(void)
{
    uint64_t head[2], sample[18];
    /* Restrict this snapshot to the SDK's explicit FW 4.03 symbol mapping.
     * No guessed device offsets are followed and no callbacks are invoked. */
    if (kernel_get_fw_version() != 0x04030000 ||
        !KERNEL_ADDRESS_DATA_BASE || KERNEL_ADDRESS_BUS_DATA_DEVICES !=
        KERNEL_ADDRESS_DATA_BASE + 0x1E69678) {
        log_line("BT kernel bus: unsupported firmware or SDK symbol"); return;
    }
    if (kernel_copyout(KERNEL_ADDRESS_BUS_DATA_DEVICES, head, sizeof head) != 0) {
        log_line("BT kernel bus: list head unavailable errno %d", errno); return;
    }
    log_line("BT kernel bus: head 0x%llX first 0x%llX last-slot 0x%llX",
             (unsigned long long)KERNEL_ADDRESS_BUS_DATA_DEVICES,
             (unsigned long long)head[0], (unsigned long long)head[1]);
    if ((head[0] >> 48) != 0xFFFF || (head[0] & 7) ||
        (head[1] >> 48) != 0xFFFF || (head[1] & 7)) {
        log_line("BT kernel bus: head pointer validation failed"); return;
    }
    if (kernel_copyout((intptr_t)head[0], sample, sizeof sample) != 0) {
        log_line("BT kernel bus: first record unavailable errno %d", errno); return;
    }
    for (unsigned i = 0; i < sizeof sample / sizeof sample[0]; i += 3)
        log_line("BT kernel bus: first +%03X %016llX %016llX %016llX", i * 8,
                 (unsigned long long)sample[i], (unsigned long long)sample[i+1],
                 (unsigned long long)sample[i+2]);
    /* The first captured record agrees with FreeBSD 11's device prefix.
     * Treat this as a candidate layout, require the global-list backlink
     * and state on every record, and never follow softc or invoke methods. */
    uint64_t current = head[0], previous_slot = (uint64_t)KERNEL_ADDRESS_BUS_DATA_DEVICES;
    uint64_t btts_class = 0;
    for (unsigned i = 0; current && i < 256; ++i) {
        char name[32], description[64], driver[32] = "unknown";
        uint64_t driver_name;
        if ((current >> 32) != (head[0] >> 32) || (current & 7) ||
            kernel_copyout((intptr_t)current, sample, sizeof sample) != 0 ||
            sample[4] != previous_slot) {
            log_line("BT kernel bus: candidate list validation failed index %u", i); break;
        }
        unsigned state = (unsigned)(sample[13] >> 32);
        if (state != 10 && state != 20 && state != 25 && state != 30 && state != 40) {
            log_line("BT kernel bus: candidate state invalid index %u state %u", i, state); break;
        }
        if (!native_string(sample[11], head[0], name, sizeof name))
            snprintf(name, sizeof name, "unnamed");
        if (!native_string(sample[12], head[0], description, sizeof description))
            snprintf(description, sizeof description, "unavailable");
        if (native_pointer(sample[8], head[0]) &&
            kernel_copyout((intptr_t)sample[8], &driver_name, sizeof driver_name) == 0)
            (void)native_string(driver_name, head[0], driver, sizeof driver);
        log_line("BT kernel bus: candidate %u %s driver %s state %u desc %s",
                 i, name, driver, state, description);
        if (strcmp(driver, "btts") == 0) {
            log_line("BT kernel btts: device %s address 0x%llX parent 0x%llX softc 0x%llX",
                     name, (unsigned long long)current, (unsigned long long)sample[5],
                     (unsigned long long)sample[17]);
            btts_class = sample[8];
        }
        previous_slot = current + 0x18;
        current = sample[3];
        usleep(10000);
        if (!current) log_line("BT kernel bus: candidate list complete %u devices", i + 1);
    }
    if (btts_class) native_btts_class(btts_class, head[0]);
}

#endif

static void native_bus(void)
{
    struct u_businfo info;
    size_t size = sizeof info;
    int mib[CTL_MAXNAME + 2], lookup[] = {0, 3};
    size_t length = CTL_MAXNAME * sizeof(int);
    const char *name = "hw.bus.devices";
    if (sysctlbyname("hw.bus.info", &info, &size, NULL, 0) != 0) {
        log_line("BT native bus: info unavailable errno %d", errno); return;
    }
    if (size != sizeof info || info.ub_version != BUS_USER_VERSION) {
        log_line("BT native bus: unsupported layout bytes %zu version %d",
                 size, info.ub_version); return;
    }
    if (sysctl(lookup, 2, mib, &length, (void *)name, strlen(name)) != 0 ||
        length % sizeof(int) || length > CTL_MAXNAME * sizeof(int)) {
        log_line("BT native bus: device node unavailable errno %d", errno); return;
    }
    unsigned count = (unsigned)(length / sizeof(int));
    mib[count++] = info.ub_generation;
    log_line("BT native bus: version %d generation %d", info.ub_version, info.ub_generation);
    for (unsigned i = 0; i < 1000; ++i) {
        struct u_device device = {0};
        mib[count] = (int)i;
        size = sizeof device;
        if (sysctl(mib, count + 1, &device, &size, NULL, 0) != 0) {
            log_line("BT native bus: end index %u errno %d", i, errno); break;
        }
        if (size != sizeof device) {
            log_line("BT native bus: unsupported device layout bytes %zu", size); break;
        }
        device.dv_name[sizeof device.dv_name - 1] = 0;
        device.dv_desc[sizeof device.dv_desc - 1] = 0;
        device.dv_drivername[sizeof device.dv_drivername - 1] = 0;
        device.dv_location[sizeof device.dv_location - 1] = 0;
        if (strstr(device.dv_name, "usb") || strstr(device.dv_name, "ubt") ||
            strstr(device.dv_desc, "luetooth") || strstr(device.dv_drivername, "bt") ||
            strstr(device.dv_name, "hid"))
            log_line("BT native bus: %s driver %s state %d desc %s location %s",
                     device.dv_name, device.dv_drivername, device.dv_state,
                     device.dv_desc, device.dv_location);
    }
}

static void processes(void)
{
    static const char *modules[] = {
        "libSceBluetoothHid.sprx", "libSceBluetooth.sprx", "libSceBt.sprx"
    };
    int mib[] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
    size_t size = 0, offset = 0;
    unsigned char *buffer;
    if (sysctl(mib, 4, NULL, &size, NULL, 0) != 0 ||
        !size || size > 8 * 1024 * 1024) {
        log_line("BT probe: process inventory unavailable errno %d", errno); return;
    }
    size += 4096;
    buffer = malloc(size);
    if (!buffer) return;
    if (sysctl(mib, 4, buffer, &size, NULL, 0) != 0) { free(buffer); return; }
    while (offset + sizeof(int) <= size) {
        int stride;
        pid_t pid;
        char name[COMMLEN + 1];
        memcpy(&stride, buffer + offset, sizeof stride);
        if (stride < (int)(offsetof(struct kinfo_proc, ki_comm) + sizeof name) ||
            stride > 4096 || (size_t)stride > size - offset) break;
        memcpy(&pid, buffer + offset + offsetof(struct kinfo_proc, ki_pid), sizeof pid);
        memcpy(name, buffer + offset + offsetof(struct kinfo_proc, ki_comm), sizeof name);
        name[COMMLEN] = 0;
        if (pid > 0) {
            native_fds(pid, name);
            if (strstr(name, "Shell") || strstr(name, "SysCore") ||
                strstr(name, "luetooth")) log_line("BT probe: process %d %s", pid, name);
            for (unsigned i = 0; i < sizeof modules / sizeof modules[0]; ++i) {
                uint32_t handle = 0;
                if (kernel_dynlib_handle(pid, modules[i], &handle) == 0)
                    log_line("BT probe: process %d %s module %s handle %u base 0x%llX",
                        pid, name, modules[i], handle,
                        (unsigned long long)kernel_dynlib_mapbase_addr(pid, handle));
            }
        }
        offset += (size_t)stride;
    }
    free(buffer);
}

static void library_range(const char *name, uintptr_t address, unsigned length, int executable)
{
    unsigned char bytes[64];
    char hex[129];
    int protection = kernel_get_vmem_protection(getpid(), address, length);
    /* PS5 code mappings can be execute-only; mdbg performs the copy. */
    if (protection < 0 || !(protection & (executable ? PROT_EXEC : PROT_READ))) {
        log_line("BT probe: range %s protection unavailable", name); return;
    }
    for (unsigned at = 0; at < length; at += sizeof bytes) {
        if (mdbg_copyout(getpid(), (intptr_t)(address + at), bytes, sizeof bytes) != 0) {
            log_line("BT probe: range %s read failed at %u", name, at); return;
        }
        for (unsigned j = 0; j < sizeof bytes; ++j)
            snprintf(hex + j * 2, 3, "%02x", bytes[j]);
        log_line("BT ABI: %s 0x%llX +%04X %s", name,
                 (unsigned long long)address, at, hex);
        log_poll();
        usleep(10000);
    }
}

static void library_imports(uintptr_t anchor)
{
    /* Captured 4.03 PLT entries: FF 25 disp32, 16-byte stride. */
    for (unsigned i = 0; i < 17; ++i) {
        uintptr_t stub = anchor + 0x16d0 + i * 16;
        unsigned char code[6];
        int32_t displacement;
        uintptr_t target;
        Dl_info info;
        if (mdbg_copyout(getpid(), (intptr_t)stub, code, sizeof code) != 0 ||
            code[0] != 0xff || code[1] != 0x25) {
            log_line("BT import: stub %u signature unavailable", i); continue;
        }
        memcpy(&displacement, code + 2, sizeof displacement);
        uintptr_t slot = stub + 6 + displacement;
        if (mdbg_copyout(getpid(), (intptr_t)slot, &target, sizeof target) != 0) {
            log_line("BT import: stub %u slot unavailable", i); continue;
        }
        memset(&info, 0, sizeof info);
        int found = dladdr((void *)target, &info);
        log_line("BT import: index %u relative stub 0x%X target 0x%llX module %s symbol %s",
                 i, 0x16d0 + i * 16, (unsigned long long)target,
                 found && info.dli_fname ? info.dli_fname : "unknown",
                 found && info.dli_sname ? info.dli_sname : "unknown");
        log_poll(); usleep(10000);
    }
}

static void library(void)
{
    static const char *symbols[] = {
        "sceBluetoothHidInit", "sceBluetoothHidParamInitialize",
        "sceBluetoothHidRegisterDevice", "sceBluetoothHidGetDeviceInfo",
        "sceBluetoothHidGetReportDescriptor", "sceBluetoothHidGetInputReport",
        "sceBluetoothHidRegisterCallback"
    };
    log_line("BT probe: loading own BluetoothHid module for export inspection");
    void *module = dlopen("libSceBluetoothHid.sprx", RTLD_NOW);
    if (!module) { log_line("BT probe: load failed: %s", dlerror()); return; }
    for (unsigned i = 0; i < sizeof symbols / sizeof symbols[0]; ++i) {
        uintptr_t address = (uintptr_t)dlsym(module, symbols[i]);
        unsigned char bytes[512];
        char hex[129];
        int protection;
        if (!address) { log_line("BT probe: export %s missing", symbols[i]); continue; }
        protection = kernel_get_vmem_protection(getpid(), address, sizeof bytes);
        if (protection < 0 || !(protection & PROT_EXEC) ||
            mdbg_copyout(getpid(), (intptr_t)address, bytes, sizeof bytes) != 0) {
            log_line("BT probe: code %s unavailable", symbols[i]); continue;
        }
        for (unsigned at = 0; at < sizeof bytes; at += 64) {
            for (unsigned j = 0; j < 64; ++j) snprintf(hex + j * 2, 3, "%02x", bytes[at+j]);
            log_line("BT ABI: %s 0x%llX +%04X %s", symbols[i],
                     (unsigned long long)address, at, hex);
        }
    }
    /* These relative ranges come from the existing 4.03 export capture.
     * Read only this process's module; never invoke the captured routines. */
#ifdef BT_LIBRARY_ONLY
    uintptr_t anchor = (uintptr_t)dlsym(module, "sceBluetoothHidInit");
    unsigned char signature[4];
    if (kernel_get_fw_version() == 0x04030000 && anchor &&
        mdbg_copyout(getpid(), (intptr_t)anchor, signature, sizeof signature) == 0 &&
        !memcmp(signature, "\x55\x48\x89\xe5", sizeof signature)) {
        library_range("HidReportWorker", anchor + 0xb80, 384, 1);
        library_range("HidImportStubs", anchor + 0x16d0, 512, 1);
        library_range("HidPathStrings", anchor + 0x4340, 256, 0);
        library_imports(anchor);
    } else log_line("BT probe: extended ranges skipped; firmware or anchor mismatch");
#endif
    /* No initialization/pairing exports are called. Keep module loaded until exit. */
}

int main(void)
{
    if (!log_open_port("/data/ps5-xinput", "/data/ps5-xinput/bt-probe.log", 9024)) return 1;
#if defined(BT_NATIVE_ONLY) || defined(BT_LIBRARY_ONLY)
    /* Let the PC subscribe before this short metadata-only probe exits. */
    for (unsigned i = 0; i < 15; ++i) { log_poll(); usleep(100000); }
#endif
    log_line("BT probe: begin firmware 0x%08X pid %d", kernel_get_fw_version(), getpid());
#ifndef BT_LIBRARY_ONLY
    directory("/dev");
    directory("/system/common/lib");
#ifndef BT_NATIVE_ONLY
    usb_layout();
#endif
    processes();
    native_bus();
#ifdef BT_NATIVE_KERNEL_EXPERIMENT
    native_kernel_head();
#endif
#endif
#ifndef BT_NATIVE_ONLY
    library();
#endif
    log_line("BT probe: complete; no HCI commands or controller assignment attempted");
    notify("PS5 XInput: Bluetooth diagnostic complete");
#if defined(BT_NATIVE_ONLY) || defined(BT_LIBRARY_ONLY)
    for (unsigned i = 0; i < 15; ++i) { log_poll(); usleep(100000); }
#endif
    log_close();
    return 0;
}
