#ifndef PS5_MANAGER_HTTP_H
#define PS5_MANAGER_HTTP_H
#include <stddef.h>
#include <stdint.h>
typedef struct {
    const uint8_t *data;
    size_t size;
    const char *type;
} manager_asset;
int manager_http_open(unsigned port, const char *token, const manager_asset assets[3],
                      int (*state)(char *, size_t), int (*command)(const char *, const char **));
void manager_http_poll(int64_t now);
void manager_http_close(void);
#endif
