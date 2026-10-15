#ifndef PS5_BT_XBOX_H
#define PS5_BT_XBOX_H
#include <stddef.h>
#include <stdint.h>
#include "vpad.h"
/* Fail closed for unknown firmware report layouts. BLE values omit report ID. */
int bt_xbox_map_supported(const uint8_t *map, size_t size);
int bt_xbox_update(vpad_input *input, uint8_t id, const uint8_t *data, size_t size,
                   unsigned *buttons);
#endif
