#ifndef PS5_XINPUT_SHELL_BIND_H
#define PS5_XINPUT_SHELL_BIND_H
#include <stdint.h>
/* Experimental: invoke the initialized shell's MBus binding implementation.
 * Returns 1 only when the call returned zero and debugger cleanup succeeded. */
int shell_bind_device(uint64_t device, uint32_t user);
#endif
