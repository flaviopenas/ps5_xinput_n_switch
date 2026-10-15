#include "bt_advert.h"
#include <string.h>

int bt_advert_parse(const unsigned char *event, size_t length, bt_advert_fn fn, void *context)
{
    size_t at = 4;
    if (!event || length < 2 || length != (size_t)event[1] + 2)
        return -1;
    if (event[0] != 0x3e)
        return 0;
    if (length < 3)
        return -1;
    if (event[2] != 2)
        return 0;
    if (length < 4 || !event[3])
        return -1;
    for (unsigned i = 0; i < event[3]; ++i) {
        if (length - at < 10)
            return -1;
        unsigned size = event[at + 8];
        if (size > 31 || size + 10 > length - at)
            return -1;
        const unsigned char *ad = event + at + 9;
        for (unsigned p = 0; p < size;) {
            unsigned item = ad[p];
            if (!item)
                break;
            if (item > size - p - 1)
                return -1;
            p += item + 1;
        }
        at += size + 10;
    }
    if (at != length)
        return -1;
    at = 4;
    for (unsigned i = 0; i < event[3]; ++i) {
        bt_advert report;
        unsigned size = event[at + 8];
        const unsigned char *ad = event + at + 9;
        memset(&report, 0, sizeof report);
        report.event_type = event[at];
        report.address_type = event[at + 1];
        memcpy(report.address, event + at + 2, 6);
        report.rssi = (int)(int8_t)event[at + 9 + size];
        for (unsigned p = 0; p < size;) {
            unsigned item = ad[p];
            if (!item)
                break;
            unsigned type = ad[p + 1];
            if ((type == 8 || type == 9) && item > 1) {
                for (unsigned j = 0; j < item - 1 && j < sizeof report.name - 1; ++j) {
                    unsigned ch = ad[p + 2 + j];
                    report.name[j] = ch >= 32 && ch < 127 ? (char)ch : '?';
                }
            }
            if (type == 2 || type == 3) {
                for (unsigned j = 0; j + 1 < item - 1; j += 2)
                    if (ad[p + 2 + j] == 0x12 && ad[p + 3 + j] == 0x18)
                        report.hid_service = 1;
            }
            if (type == 0xff && item >= 3 && ad[p + 2] == 6 && ad[p + 3] == 0)
                report.microsoft = 1;
            p += item + 1;
        }
        if (fn)
            fn(&report, context);
        at += size + 10;
    }
    return event[3];
}
