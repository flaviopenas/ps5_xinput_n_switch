#include "log.h"
#include "bt_hci.h"
#include <stdio.h>

int main(void)
{
    static const unsigned opcodes[] = {0x1001, 0x1003, 0x1002, 0x2003, 0x1009};
    if (!log_open_port("/data/ps5-xinput", "/data/ps5-xinput/bt-radio-probe.log", 9024)) return 1;
    log_line("BT radio: feature queries only; no reset, scan, connection, or pairing");
    if (bt_hci_open()) {
        for (unsigned i = 0; i < sizeof opcodes / sizeof opcodes[0] && !log_stop_requested(); ++i) {
            unsigned char result[80];
            char hex[161];
            int count = bt_hci_command(opcodes[i], NULL, 0, result, sizeof result);
            if (count < 1) { log_line("BT radio: query %04X unavailable", opcodes[i]); continue; }
            for (int j = 0; j < count; ++j) snprintf(hex + j * 2, 3, "%02x", result[j]);
            log_line("BT radio: query %04X status %02X response %s", opcodes[i], result[0], hex);
            if (opcodes[i] == 0x1003 && count >= 9 && result[0] == 0)
                log_line("BT radio: LE controller support %s", (result[5] & 0x40) ? "yes" : "no");
        }
    }
    bt_hci_close();
    log_line("BT radio: complete, own event transfers closed");
    notify("PS5 XInput: Bluetooth feature check complete");
    log_close();
    return 0;
}
