#include "assignment.h"
#include <assert.h>
#include <stdio.h>
void log_line(const char *fmt, ...) { (void)fmt; }
void log_poll(void) {}
int shell_bind_device(uint64_t device, uint32_t user)
{ (void)device; (void)user; return 0; }
int main(void)
{
    assert(assignment_parse_event("DEVICE_ADDED subType=22 DeviceId=0x000c030d") == 0xc030d);
    assert(assignment_parse_event("device_added subtype: 0x16 deviceId: C030D") == 0xc030d);
    assert(assignment_parse_event("DeviceAdded SubType=22 dev_id=c030d") == 0xc030d);
    assert(assignment_parse_event("DEVICE_ADDED subType=23 DeviceId=0xc030d") == 0);
    assert(assignment_parse_event("DEVICE_ADDED subType=220 DeviceId=0xc030d") == 0);
    assert(assignment_parse_event("DEVICE_ADDED DeviceId=0xc030d") == 0);
    assert(assignment_parse_event("DEVICE_REMOVED subType=22 DeviceId=0xc030d") == 0);
    assert(assignment_parse_event("DEVICE_ADDED subType=22 DeviceId=not-a-number") == 0);
    assert(assignment_parse_event("DEVICE_ADDED subType=22 DeviceId=fffffffffffffffff") == 0);
    puts("Assignment parser passed: virtual device events accepted; unrelated/malformed events rejected.");
    return 0;
}
