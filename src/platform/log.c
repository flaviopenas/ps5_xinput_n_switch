#include "log.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef struct notify_request {
    char useless1[45];
    char message[3075];
} notify_request_t;

int sceKernelSendNotificationRequest(int, notify_request_t *, size_t, int);

static FILE *g_log;
static int g_stream = -1;
static struct sockaddr_in g_destination;
static int g_connected;
static char g_recent[32][1100];
static unsigned g_count;
static int g_stop_requested;

int log_stop_requested(void)
{
    return g_stop_requested;
}

/* The viewer sends this from its bound UDP socket, giving us the reachable
 * return address without requiring a PC IP to be compiled into the payload. */
void log_poll(void)
{
    char request[64];
    struct sockaddr_in peer;
    socklen_t size = sizeof peer;
    ssize_t n;
    int i;
    if (g_stream < 0)
        return;
    for (i = 0; i < 4; ++i) {
        size = sizeof peer;
        n = recvfrom(g_stream, request, sizeof request, 0, (struct sockaddr *)&peer, &size);
        if (n < 0)
            return;
        /* The connected log PC may request the normal controller cleanup.
         * Accept only its address; this never terminates the console or shell. */
        if (g_connected && peer.sin_addr.s_addr == g_destination.sin_addr.s_addr &&
            n == (ssize_t)(sizeof "PS5_XINPUT_STOP\n" - 1) &&
            memcmp(request, "PS5_XINPUT_STOP\n", sizeof "PS5_XINPUT_STOP\n" - 1) == 0) {
            const char ack[] = "PS5_XINPUT_STOP_ACK\n";
            g_stop_requested = 1;
            (void)sendto(g_stream, ack, sizeof ack - 1, 0, (struct sockaddr *)&peer, size);
            continue;
        }
        if (n != (ssize_t)(sizeof "PS5_XINPUT_LOG_HELLO\n" - 1) ||
            memcmp(request, "PS5_XINPUT_LOG_HELLO\n", sizeof "PS5_XINPUT_LOG_HELLO\n" - 1) != 0)
            continue;
        if (g_connected && peer.sin_addr.s_addr == g_destination.sin_addr.s_addr &&
            peer.sin_port == g_destination.sin_port)
            continue;
        g_destination = peer;
        g_connected = 1;
        {
            const char ready[] = "--:--:-- [ps5-xinput] Direct UDP log connection established\n";
            unsigned first = g_count > 32 ? g_count - 32 : 0;
            unsigned j;
            (void)sendto(g_stream, ready, sizeof ready - 1, 0, (struct sockaddr *)&peer,
                         sizeof peer);
            for (j = first; j < g_count; ++j) {
                const char *line = g_recent[j % 32];
                (void)sendto(g_stream, line, strlen(line), 0, (struct sockaddr *)&peer,
                             sizeof peer);
            }
        }
    }
}

/* Best-effort LAN broadcast. Nonblocking sends keep a missing or slow viewer
 * out of the controller input path. Start the PC listener before the payload. */
static void stream_open(unsigned port)
{
    int enabled = 1;
    int flags;
    struct sockaddr_in local;
    g_stream = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_stream < 0)
        return;
    flags = fcntl(g_stream, F_GETFL, 0);
    if (flags < 0 || fcntl(g_stream, F_SETFL, flags | O_NONBLOCK) < 0 ||
        setsockopt(g_stream, SOL_SOCKET, SO_BROADCAST, &enabled, sizeof enabled) < 0) {
        close(g_stream);
        g_stream = -1;
        return;
    }
    memset(&g_destination, 0, sizeof g_destination);
    g_destination.sin_family = AF_INET;
    g_destination.sin_port = htons((uint16_t)port);
    g_destination.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    memset(&local, 0, sizeof local);
    local.sin_family = AF_INET;
    local.sin_port = htons((uint16_t)port);
    local.sin_addr.s_addr = htonl(INADDR_ANY);
#ifdef __FreeBSD__
    local.sin_len = sizeof local;
    g_destination.sin_len = sizeof g_destination;
#endif
    if (bind(g_stream, (struct sockaddr *)&local, sizeof local) < 0) {
        close(g_stream);
        g_stream = -1;
        return;
    }
    g_connected = 0;
    g_count = 0;
}

int log_open(const char *dir, const char *path)
{
    return log_open_port(dir, path, 9023);
}

int log_open_port(const char *dir, const char *path, unsigned port)
{
    if (port == 0 || port > 65535)
        return 0;
    g_stop_requested = 0;
    mkdir(dir, 0755);
    g_log = fopen(path, "ab");
    stream_open(port);
    /* Catch a running viewer before startup failures can terminate the payload. */
    if (g_stream >= 0) {
        int i;
        for (i = 0; i < 20 && !g_connected; ++i) {
            log_poll();
            if (!g_connected)
                usleep(50000);
        }
    }
    if (!g_log) {
        log_line("Could not open console log file: %s", path);
        log_close();
        return 0;
    }
    log_line("LAN log stream UDP %u: %s", port, g_stream >= 0 ? "enabled" : "unavailable");
    return g_log != NULL;
}

void log_close(void)
{
    if (g_stream >= 0) {
        close(g_stream);
        g_stream = -1;
    }
    if (g_log) {
        fclose(g_log);
        g_log = NULL;
    }
}

void log_line(const char *fmt, ...)
{
    char stamp[32];
    time_t now = time(NULL);
    struct tm tmv;
    va_list ap;
    char message[1024];
    char line[1100];
    int length;

    if (!g_log && g_stream < 0)
        return;

    gmtime_r(&now, &tmv);
    if (strftime(stamp, sizeof stamp, "%H:%M:%S", &tmv) == 0)
        strcpy(stamp, "00:00:00");

    va_start(ap, fmt);
    vsnprintf(message, sizeof message, fmt, ap);
    va_end(ap);
    length = snprintf(line, sizeof line, "%s [ps5-xinput] %s\n", stamp, message);
    if (length < 0)
        return;
    if ((size_t)length >= sizeof line)
        length = (int)sizeof line - 1;
    log_poll();
    memcpy(g_recent[g_count % 32], line, (size_t)length + 1);
    ++g_count;

    /* Flushed per line on purpose: an unflushed buffer is lost if the payload
     * faults, and a fault is exactly when the log matters most. */
    if (g_log) {
        fwrite(line, 1, (size_t)length, g_log);
        fflush(g_log);
    }
    if (g_stream >= 0) {
        if (sendto(g_stream, line, (size_t)length, 0, (struct sockaddr *)&g_destination,
                   sizeof g_destination) < 0 &&
            g_log) {
            fprintf(g_log, "%s [ps5-xinput] UDP send failed: errno %d (%s)\n", stamp, errno,
                    strerror(errno));
            fflush(g_log);
        }
    }
}

void notify(const char *fmt, ...)
{
    notify_request_t req;
    char msg[512];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);

    memset(&req, 0, sizeof req);
    snprintf(req.message, sizeof req.message, "%s", msg);
    sceKernelSendNotificationRequest(0, &req, sizeof req, 0);
}
