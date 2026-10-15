#define _POSIX_C_SOURCE 200809L
#include "manager_http.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
enum { CLIENTS = 4, REQUEST = 4096, RESPONSE = 32768 };
static struct {
    int fd;
    char in[REQUEST], out[RESPONSE];
    size_t used, size, sent;
    int64_t deadline;
} clients[CLIENTS];
static int listener = -1;
static unsigned listen_port;
static char secret[33];
static manager_asset files[3];
static int (*get_state)(char *, size_t);
static int (*run_command)(const char *, const char **);
static void close_client(unsigned i)
{
    if (clients[i].fd >= 0)
        close(clients[i].fd);
    clients[i].fd = -1;
}
void manager_http_close(void)
{
    for (unsigned i = 0; i < CLIENTS; ++i)
        close_client(i);
    if (listener >= 0)
        close(listener);
    listener = -1;
}
static int nonblock(int fd)
{
    int f = fcntl(fd, F_GETFL, 0);
    return f >= 0 && fcntl(fd, F_SETFL, f | O_NONBLOCK) == 0;
}
int manager_http_open(unsigned port, const char *token, const manager_asset assets[3],
                      int (*state)(char *, size_t), int (*command)(const char *, const char **))
{
    for (unsigned i = 0; i < CLIENTS; ++i)
        clients[i].fd = -1;
    listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0)
        return 0;
    int yes = 1;
    (void)setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
#ifdef __FreeBSD__
    addr.sin_len = sizeof addr;
#endif
    if (!nonblock(listener) || bind(listener, (struct sockaddr *)&addr, sizeof addr) ||
        listen(listener, 8)) {
        manager_http_close();
        return 0;
    }
    listen_port = port;
    snprintf(secret, sizeof secret, "%s", token);
    memcpy(files, assets, sizeof files);
    get_state = state;
    run_command = command;
    return 1;
}
static void reply(unsigned i, unsigned status, const char *type, const void *body, size_t size)
{
    int n = snprintf(
        clients[i].out, RESPONSE,
        "HTTP/1.1 %u %s\r\nContent-Type: %s\r\nContent-Length: %lu\r\nConnection: close\r\n"
        "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n"
        "Content-Security-Policy: default-src 'self'; frame-ancestors 'none'\r\n\r\n",
        status, status == 200 ? "OK" : "Error", type, (unsigned long)size);
    if (n <= 0 || (size_t)n + size > RESPONSE) {
        close_client(i);
        return;
    }
    memcpy(clients[i].out + n, body, size);
    clients[i].size = (size_t)n + size;
}
static void error(unsigned i, unsigned status, const char *message)
{
    reply(i, status, "text/plain; charset=utf-8", message, strlen(message));
}
static int valid_host(const char *host)
{
    char value[80];
    snprintf(value, sizeof value, "%s", host);
    char *colon = strchr(value, ':');
    if (!colon)
        return 0;
    *colon++ = 0;
    char port[8];
    snprintf(port, sizeof port, "%u", listen_port);
    if (strcmp(colon, port))
        return 0;
    struct in_addr address;
    return !strcmp(value, "localhost") || inet_pton(AF_INET, value, &address) == 1;
}
static void handle(unsigned i)
{
    char *end = strstr(clients[i].in, "\r\n\r\n");
    if (!end)
        return;
    size_t header_size = (size_t)(end - clients[i].in) + 4;
    char header[REQUEST];
    memcpy(header, clients[i].in, header_size);
    header[header_size - 2] = 0;
    char method[8], path[80], version[16], trailing;
    char *line = strstr(header, "\r\n");
    if (!line) {
        error(i, 400, "Malformed request");
        return;
    }
    *line = 0;
    if (sscanf(header, "%7s %79s %15s %c", method, path, version, &trailing) != 3 ||
        strcmp(version, "HTTP/1.1")) {
        error(i, 400, "Malformed request");
        return;
    }
    char host[80] = {0}, token[80] = {0}, origin[160] = {0};
    size_t length = 0;
    int length_seen = 0, host_seen = 0, token_seen = 0;
    line += 2;
    while (*line) {
        char *next = strstr(line, "\r\n");
        if (!next)
            break;
        *next = 0;
        char *colon = strchr(line, ':');
        if (!colon) {
            error(i, 400, "Malformed header");
            return;
        }
        *colon++ = 0;
        while (*colon == ' ' || *colon == '\t')
            ++colon;
        if (!strcasecmp(line, "Host")) {
            if (host_seen++ || strlen(colon) >= sizeof host) {
                error(i, 400, "Invalid Host");
                return;
            }
            snprintf(host, sizeof host, "%s", colon);
        } else if (!strcasecmp(line, "Content-Length")) {
            if (length_seen++ || !*colon) {
                error(i, 400, "Invalid length");
                return;
            }
            for (const char *p = colon; *p; ++p)
                if (*p < '0' || *p > '9') {
                    error(i, 400, "Invalid length");
                    return;
                }
            if (strlen(colon) > 3) {
                error(i, 413, "Body too large");
                return;
            }
            length = (size_t)strtoul(colon, NULL, 10);
        } else if (!strcasecmp(line, "Transfer-Encoding")) {
            error(i, 400, "Chunked requests unsupported");
            return;
        } else if (!strcasecmp(line, "X-XInput-Token")) {
            if (token_seen++ || strlen(colon) >= sizeof token) {
                error(i, 403, "Invalid token");
                return;
            }
            snprintf(token, sizeof token, "%s", colon);
        } else if (!strcasecmp(line, "Origin")) {
            if (origin[0] || strlen(colon) >= sizeof origin) {
                error(i, 403, "Invalid origin");
                return;
            }
            snprintf(origin, sizeof origin, "%s", colon);
        }
        line = next + 2;
    }
    if (!host_seen || !valid_host(host)) {
        error(i, 403, "Invalid Host");
        return;
    }
    if (length > 127 || header_size + length >= REQUEST) {
        error(i, 413, "Body too large");
        return;
    }
    if (clients[i].used < header_size + length)
        return;
    if (!strcmp(method, "GET")) {
        if (length) {
            error(i, 400, "Unexpected body");
            return;
        }
        char *query = strchr(path, '?');
        if (query)
            *query = 0;
        int index = !strcmp(path, "/")          ? 0
                    : !strcmp(path, "/app.css") ? 1
                    : !strcmp(path, "/app.js")  ? 2
                                                : -1;
        if (index >= 0) {
            reply(i, 200, files[index].type, files[index].data, files[index].size);
            return;
        }
        if (!strcmp(path, "/api/status")) {
            char body[16384];
            int n = get_state(body, sizeof body);
            if (n < 0 || (size_t)n >= sizeof body)
                error(i, 500, "Status too large");
            else
                reply(i, 200, "application/json", body, (size_t)n);
            return;
        }
        error(i, 404, "Not found");
        return;
    }
    if (strcmp(method, "POST") || strcmp(path, "/api/command")) {
        error(i, 405, "Method unsupported");
        return;
    }
    char expected[160];
    snprintf(expected, sizeof expected, "http://%s", host);
    if (strcmp(token, secret) || (origin[0] && strcmp(origin, expected))) {
        error(i, 403, "Open the controller manager first");
        return;
    }
    char body[128];
    memcpy(body, clients[i].in + header_size, length);
    body[length] = 0;
    if (memchr(body, 0, length) || memchr(body, '\r', length) || memchr(body, '\n', length)) {
        error(i, 400, "Invalid command");
        return;
    }
    const char *message = "Command accepted";
    int status = run_command(body, &message);
    error(i, (unsigned)status, message);
}
void manager_http_poll(int64_t now)
{
    if (listener < 0)
        return;
    for (unsigned i = 0; i < CLIENTS; ++i) {
        if (clients[i].fd < 0) {
            int fd = accept(listener, NULL, NULL);
            if (fd >= 0) {
                if (!nonblock(fd)) {
                    close(fd);
                    continue;
                }
#ifdef SO_NOSIGPIPE
                int yes = 1;
                (void)setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof yes);
#endif
                clients[i].fd = fd;
                clients[i].used = clients[i].size = clients[i].sent = 0;
                clients[i].deadline = now + 3000;
            }
        }
        if (clients[i].fd < 0)
            continue;
        if (now >= clients[i].deadline) {
            close_client(i);
            continue;
        }
        if (!clients[i].size) {
            size_t available = REQUEST - 1 - clients[i].used;
            if (!available) {
                error(i, 413, "Request too large");
            } else {
                ssize_t n = recv(clients[i].fd, clients[i].in + clients[i].used, available, 0);
                if (n == 0 ||
                    (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
                    close_client(i);
                    continue;
                }
                if (n > 0) {
                    clients[i].used += (size_t)n;
                    clients[i].in[clients[i].used] = 0;
                    if (memchr(clients[i].in, 0, clients[i].used))
                        error(i, 400, "NUL in request");
                    else
                        handle(i);
                }
            }
        }
        if (clients[i].size) {
            ssize_t n = send(clients[i].fd, clients[i].out + clients[i].sent,
                             clients[i].size - clients[i].sent,
#ifdef MSG_NOSIGNAL
                             MSG_NOSIGNAL
#else
                             0
#endif
            );
            if (n > 0)
                clients[i].sent += (size_t)n;
            if (clients[i].sent == clients[i].size ||
                (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR))
                close_client(i);
        }
    }
}
