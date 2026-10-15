#include "bt_advert.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned calls;
static bt_advert last;
static void accept(const bt_advert *report, void *unused)
{
    (void)unused;
    ++calls;
    last = *report;
}

int main(void)
{
    /* One complete event: Microsoft manufacturer, HID service and name. */
    unsigned char event[64] = {
        0x3e, 0, 2, 1, 0, 1, 1, 2, 3, 4, 5, 6, 14,
        3, 0xff, 6, 0, 3, 3, 0x12, 0x18, 5, 9, 'X', 'b', 'o', 'x', 0xd0
    };
    size_t size = 28;
    event[1] = (unsigned char)(size - 2);
    assert(bt_advert_parse(event, size, accept, NULL) == 1);
    assert(calls == 1 && last.rssi == -48 && last.address_type == 1);
    assert(last.address[0] == 1 && last.address[5] == 6);
    assert(last.microsoft && last.hid_service && !strcmp(last.name, "Xbox"));
    /* Every prefix must fail, without emitting a partial report. */
    for (size_t n = 0; n < size; ++n) {
        assert(bt_advert_parse(event, n, accept, NULL) == -1);
        assert(calls == 1);
    }
    event[13] = 31; /* AD field claims more bytes than the record contains. */
    assert(bt_advert_parse(event, size, accept, NULL) == -1 && calls == 1);
    event[13] = 3;
    event[12] = 32; /* Illegal legacy advertising length. */
    assert(bt_advert_parse(event, size, accept, NULL) == -1 && calls == 1);
    event[12] = 14;
    event[3] = 2; /* Missing second report must not emit the first. */
    assert(bt_advert_parse(event, size, accept, NULL) == -1 && calls == 1);
    event[3] = 1;
    event[23] = '\n';
    assert(bt_advert_parse(event, size, accept, NULL) == 1);
    assert(!strcmp(last.name, "?box"));
    event[2] = 1; /* LE Connection Complete is not advertising. */
    assert(bt_advert_parse(event, size, accept, NULL) == 0);
    assert(bt_advert_parse(NULL, 0, accept, NULL) == -1);
    puts("Bluetooth advertising validation passed");
    return 0;
}
