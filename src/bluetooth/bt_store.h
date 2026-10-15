#ifndef PS5_BT_STORE_H
#define PS5_BT_STORE_H
#include <stdint.h>
#include <stddef.h>
#define BT_SAVED_MAX 8
#define BT_DEVICE_ID_SIZE 15
typedef struct {
    char id[BT_DEVICE_ID_SIZE], path[192];
    uint8_t identity[7], peer[7];
} bt_saved_device;
int bt_device_id_parse(const char *id, uint8_t address[6], uint8_t *type);
void bt_device_id(char id[BT_DEVICE_ID_SIZE], const uint8_t address[6], uint8_t type);
int bt_store_path(const char *dir, const char *id, char *path, size_t size);
int bt_store_read(const char *path, uint8_t record[64]);
int bt_store_list(const char *dir, bt_saved_device devices[BT_SAVED_MAX]);
int bt_store_import(const char *dir, const char *legacy);
int bt_store_forget(const char *dir, const char *id);
int bt_store_auto(const char *dir, const char *id);
int bt_store_auto_read(const char *dir, char id[BT_DEVICE_ID_SIZE]);
int bt_store_auto_slot(const char *dir, unsigned slot, const char *id);
int bt_store_auto_read_slot(const char *dir, unsigned slot, char id[BT_DEVICE_ID_SIZE]);
int bt_store_classic_save(const char *, const uint8_t local[6], const uint8_t peer[6],
                          const uint8_t key[16], uint8_t key_type);
#endif
