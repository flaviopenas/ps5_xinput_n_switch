/* Firmware inventory only: no device opens or Bluetooth API invocation. */
#include "log.h"
#include <ps5/kernel.h>
#include <dirent.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

static void inventory(const char *path)
{
    DIR *directory = opendir(path);
    if (!directory) { log_line("BT inventory: directory %s unavailable", path); return; }
    struct dirent *entry;
    while ((entry = readdir(directory))) {
        const char *name = entry->d_name;
        if (strcasestr(name, "bluetooth") || !strncmp(name, "bt", 2) ||
            !strncmp(name, "libSceBt", 8)) {
            log_line("BT inventory: entry %s/%s", path, name);
            log_poll(); usleep(20000);
        }
    }
    closedir(directory);
}

int main(void)
{
    if (!log_open_port("/data/ps5-xinput", "/data/ps5-xinput/bt-inventory.log", 9024)) return 1;
    for (unsigned i = 0; i < 15; ++i) { log_poll(); usleep(100000); }
    log_line("BT inventory: firmware 0x%08X; no device opens or Bluetooth calls", kernel_get_fw_version());
    inventory("/dev");
    inventory("/system/common/lib");
    inventory("/system/priv/lib");
    /* This module passed the clean-baseline load-only comparison. */
    void *module = dlopen("libSceBluetoothHid.sprx", RTLD_NOW);
    static const char *symbols[] = {
        "sceBluetoothHidInit", "sceBluetoothHidTerm", "sceBluetoothHidRegisterCallback",
        "sceBluetoothHidUnregisterCallback", "sceBluetoothHidGetDeviceInfo",
        "sceBluetoothHidRegisterDevice", "sceBluetoothHidUnregisterDevice",
        "sceBluetoothHidGetInputReport", "sceBluetoothHidGetReportDescriptor",
        "sceBluetoothHidGetFeatureReport", "sceBluetoothHidSetFeatureReport",
        "sceBluetoothHidSetOutputReport", "sceBluetoothHidConnect", "sceBluetoothHidDisconnect",
        "sceBluetoothHidStartScan", "sceBluetoothHidPair", "sceBluetoothHidGetDeviceList",
        "sceBluetoothHidGetConnectionStatus"
    };
    if (!module) log_line("BT inventory: HID module load failed: %s", dlerror());
    else for (unsigned i = 0; i < sizeof symbols / sizeof symbols[0]; ++i) {
        void *address = dlsym(module, symbols[i]);
        log_line("BT inventory: candidate %s %s", symbols[i], address ? "present" : "absent");
        log_poll(); usleep(20000);
    }
    log_line("BT inventory: complete; candidate-name checks are not a full export list");
    notify("PS5 XInput: native Bluetooth inventory complete");
    for (unsigned i = 0; i < 15; ++i) { log_poll(); usleep(100000); }
    log_close(); return 0;
}
