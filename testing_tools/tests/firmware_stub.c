#include <stdint.h>

/* Existing host simulations default to the console ABI they exercise.
 * Boundary tests provide a strong definition with their selected version. */
__attribute__((weak)) uint32_t kernel_get_fw_version(void)
{
    return 0x04030000U;
}
