#define _POSIX_C_SOURCE 200809L
#include "bt_store.h"
#include "bt_session.h"
#include "bt_crypto.h"
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static int digit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}
int bt_device_id_parse(const char *id, uint8_t address[6], uint8_t *type)
{
    if (!id || strlen(id) != 14 || id[12] != '-' || (id[13] < '0' || id[13] > '2'))
        return 0;
    for (unsigned i = 0; i < 6; ++i) {
        int a = digit(id[2 * i]), b = digit(id[2 * i + 1]);
        if (a < 0 || b < 0)
            return 0;
        address[5 - i] = (uint8_t)(a * 16 + b);
    }
    *type = (uint8_t)(id[13] - '0');
    return 1;
}
void bt_device_id(char id[BT_DEVICE_ID_SIZE], const uint8_t address[6], uint8_t type)
{
    snprintf(id, BT_DEVICE_ID_SIZE, "%02X%02X%02X%02X%02X%02X-%u", address[5], address[4],
             address[3], address[2], address[1], address[0], type <= 2 ? type : 0);
}
int bt_store_path(const char *dir, const char *id, char *path, size_t size)
{
    uint8_t address[6], type;
    if (!bt_device_id_parse(id, address, &type))
        return 0;
    int n = snprintf(path, size, "%s/%s.bin", dir, id);
    return n > 0 && (size_t)n < size;
}
static int read_full(int fd, void *data, size_t size)
{
    size_t at = 0;
    while (at < size) {
        ssize_t n = read(fd, (char *)data + at, size - at);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return 0;
        at += (size_t)n;
    }
    return 1;
}
int bt_store_read(const char *path, uint8_t record[64])
{
    struct stat st;
    int fd = open(path, O_RDONLY | O_NOFOLLOW);
    if (fd < 0)
        return 0;
    int valid = fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size == 64 &&
                !(st.st_mode & 077) && read_full(fd, record, 64);
    close(fd);
    valid =
        valid && record[14] == 0 && record[62] == 16 && record[63] == 0 &&
        ((!memcmp(record, "XBSC0001", 8) && record[21] <= 1 && record[60] <= 1 &&
          record[61] <= 1) ||
         (!memcmp(record, "XBCL0001", 8) && record[21] == 2 && record[60] == 2 && record[61] >= 1 &&
          record[61] <= 8 && record[61] != 3 && !memcmp(record + 15, record + 54, 7)));
    if (!valid)
        bt_wipe(record, 64);
    return valid;
}
int bt_store_list(const char *dir, bt_saved_device devices[BT_SAVED_MAX])
{
    struct stat st;
    if (lstat(dir, &st) != 0 || !S_ISDIR(st.st_mode) || (st.st_mode & 077))
        return -1;
    DIR *d = opendir(dir);
    if (!d)
        return -1;
    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(d)) && count < BT_SAVED_MAX) {
        if (strlen(entry->d_name) != 18 || strcmp(entry->d_name + 14, ".bin"))
            continue;
        char id[BT_DEVICE_ID_SIZE];
        memcpy(id, entry->d_name, 14);
        id[14] = 0;
        char path[192];
        uint8_t record[64], address[6], type;
        if (!bt_store_path(dir, id, path, sizeof path) || !bt_device_id_parse(id, address, &type) ||
            !bt_store_read(path, record))
            continue;
        if (memcmp(address, record + 54, 6) || type != record[60]) {
            bt_wipe(record, sizeof record);
            continue;
        }
        bt_saved_device *v = &devices[count++];
        memcpy(v->id, id, sizeof id);
        snprintf(v->path, sizeof v->path, "%s", path);
        memcpy(v->identity, record + 15, 7);
        memcpy(v->peer, record + 54, 7);
        bt_wipe(record, sizeof record);
    }
    closedir(d);
    return count;
}
static int atomic_write(const char *path, const void *data, size_t size)
{
    char temporary[224];
    int n = snprintf(temporary, sizeof temporary, "%s.%d.new", path, getpid());
    if (n <= 0 || (size_t)n >= sizeof temporary)
        return 0;
    int fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (fd < 0)
        return 0;
    size_t at = 0;
    while (at < size) {
        ssize_t written = write(fd, (const char *)data + at, size - at);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0)
            break;
        at += (size_t)written;
    }
    int ok = at == size && fsync(fd) == 0;
    if (close(fd))
        ok = 0;
    if (ok)
        ok = rename(temporary, path) == 0;
    if (!ok)
        unlink(temporary);
    return ok;
}
int bt_store_import(const char *dir, const char *legacy)
{
    uint8_t record[64];
    char id[BT_DEVICE_ID_SIZE], path[192];
    struct stat st;
    char marker[192];
    int n = snprintf(marker, sizeof marker, "%s/legacy-imported", dir);
    if (n <= 0 || (size_t)n >= sizeof marker)
        return 0;
    if (lstat(marker, &st) == 0)
        return S_ISREG(st.st_mode) && !(st.st_mode & 077);
    if (errno != ENOENT)
        return 0;
    if (!bt_store_read(legacy, record))
        return 0;
    bt_device_id(id, record + 54, record[60]);
    int ok = bt_store_path(dir, id, path, sizeof path);
    if (ok && lstat(path, &st) == 0)
        ok = 1; /* Never replace an existing managed bond. */
    else if (ok && errno == ENOENT)
        ok = atomic_write(path, record, sizeof record);
    else
        ok = 0;
    bt_wipe(record, sizeof record);
    if (ok)
        ok = atomic_write(marker, "XBIMPORT1\n", 10);
    return ok;
}
int bt_store_forget(const char *dir, const char *id)
{
    char path[192];
    return bt_store_path(dir, id, path, sizeof path) && unlink(path) == 0;
}
static int auto_path(const char *dir, unsigned slot, char *path, size_t size)
{
    if (slot >= BT_MAX_CONTROLLERS)
        return 0;
    int n = slot ? snprintf(path, size, "%s/auto%u.txt", dir, slot + 1)
                 : snprintf(path, size, "%s/auto.txt", dir);
    return n > 0 && (size_t)n < size;
}
int bt_store_auto_slot(const char *dir, unsigned slot, const char *id)
{
    uint8_t address[6], type;
    if (id[0] && !bt_device_id_parse(id, address, &type))
        return 0;
    char path[192];
    return auto_path(dir, slot, path, sizeof path) && atomic_write(path, id, strlen(id));
}
int bt_store_auto_read_slot(const char *dir, unsigned slot, char id[BT_DEVICE_ID_SIZE])
{
    id[0] = 0;
    char path[192];
    if (!auto_path(dir, slot, path, sizeof path))
        return 0;
    int fd = open(path, O_RDONLY | O_NOFOLLOW);
    if (fd < 0)
        return 0;
    struct stat st;
    int ok = fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size == 14 &&
             !(st.st_mode & 077) && read_full(fd, id, 14);
    close(fd);
    id[14] = 0;
    uint8_t address[6], type;
    if (!ok || !bt_device_id_parse(id, address, &type)) {
        id[0] = 0;
        return 0;
    }
    return 1;
}
int bt_store_auto(const char *dir, const char *id)
{
    return bt_store_auto_slot(dir, 0, id);
}
int bt_store_auto_read(const char *dir, char id[BT_DEVICE_ID_SIZE])
{
    return bt_store_auto_read_slot(dir, 0, id);
}

int bt_store_classic_save(const char *path, const uint8_t local[6], const uint8_t peer[6],
                          const uint8_t key[16], uint8_t key_type)
{
    if (key_type < 1 || key_type > 8 || key_type == 3)
        return 0;
    uint8_t record[64] = {0};
    memcpy(record, "XBCL0001", 8);
    memcpy(record + 8, local, 6);
    memcpy(record + 15, peer, 6);
    record[21] = 2;
    memcpy(record + 22, key, 16);
    memcpy(record + 54, peer, 6);
    record[60] = 2;
    record[61] = key_type;
    record[62] = 16;
    int ok = atomic_write(path, record, sizeof record);
    bt_wipe(record, sizeof record);
    return ok;
}
