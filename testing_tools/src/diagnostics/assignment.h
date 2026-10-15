#ifndef PS5_XINPUT_ASSIGNMENT_H
#define PS5_XINPUT_ASSIGNMENT_H
#include <stdint.h>
/* Capture new kernel events around AddDevice for assignment diagnostics.
 * No local MBus call: it did not return a result during the FW 4.03 test. */
int assignment_begin(void);
void assignment_finish(int fd, uint32_t handle, uint32_t user);
uint64_t assignment_parse_event(const char *line);
#endif
