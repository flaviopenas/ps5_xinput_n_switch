/* Console browser launch signature follows ps5-payload-sdk samples/browser.
 * This launcher exits; the separately loaded service retains the radio/pad. */
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "log.h"
#include "manager_service_blob.h"
static int connect_local(unsigned port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    struct timeval timeout = {2, 0};
    (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
#ifdef SO_NOSIGPIPE
    int yes = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof yes);
#endif
    struct sockaddr_in address;
    memset(&address, 0, sizeof address);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons((uint16_t)port);
#ifdef __FreeBSD__
    address.sin_len = sizeof address;
#endif
    if (connect(fd, (struct sockaddr *)&address, sizeof address)) {
        close(fd);
        return -1;
    }
    return fd;
}
int main(void)
{
    int fd = connect_local(9030);
    if (fd >= 0)
        close(fd);
    else {
        fd = connect_local(9021);
        if (fd < 0) {
            notify("PS5 XInput: start the ELF loader on port 9021 first");
            return 1;
        }
        size_t at = 0;
        while (at < sizeof manager_service_elf) {
            ssize_t n = send(fd, manager_service_elf + at, sizeof manager_service_elf - at, 0);
            if (n <= 0) {
                close(fd);
                notify("PS5 XInput: service launch transfer failed");
                return 1;
            }
            at += (size_t)n;
        }
        shutdown(fd, SHUT_WR);
        close(fd);
        for (unsigned i = 0; i < 100; ++i) {
            fd = connect_local(9030);
            if (fd >= 0) {
                close(fd);
                break;
            }
            usleep(100000);
        }
        if (fd < 0) {
            notify("PS5 XInput: controller service did not start");
            return 1;
        }
    }
    void *users = dlopen("libSceUserService.sprx", RTLD_NOW);
    int (*initialize)(const void *) = users ? dlsym(users, "sceUserServiceInitialize") : NULL;
    if (initialize)
        (void)initialize(NULL);
    void *system = dlopen("libSceSystemService.sprx", RTLD_NOW);
    int (*browser)(const char *, void *) =
        system ? dlsym(system, "sceSystemServiceLaunchWebBrowser") : NULL;
    struct timeval opened;
    gettimeofday(&opened, NULL);
    char url[128];
    snprintf(url, sizeof url, "http://127.0.0.1:9030/?open=%ld-%ld-%d", (long)opened.tv_sec,
             (long)opened.tv_usec, (int)getpid());
    int rc = browser ? browser(url, NULL) : -1;
    if (rc) {
        notify("PS5 XInput: browser launch failed (%08X); manager is available on port 9030",
               (unsigned)rc);
        return 1;
    }
    return 0;
}
