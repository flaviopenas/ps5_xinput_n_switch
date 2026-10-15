#include "user.h"
#include "log.h"
#include <dlfcn.h>
#include <stddef.h>

/* Resolve at runtime so unavailable exports produce a diagnostic instead of
 * preventing the payload from loading. Do not guess a user ID on failure. */
int current_user(uint32_t *user)
{
    typedef int (*get_user_fn)(int32_t *);
    typedef int (*init_fn)(const void *);
    void *module = dlopen("libSceUserService.sprx", RTLD_NOW);
    get_user_fn foreground, initial;
    init_fn init;
    int32_t id = -1;
    int rc = -1;
    if (!module) {
        log_line("UserService load failed: %s", dlerror());
        return 0;
    }
    init = (init_fn)dlsym(module, "sceUserServiceInitialize");
    foreground = (get_user_fn)dlsym(module, "sceUserServiceGetForegroundUser");
    initial = (get_user_fn)dlsym(module, "sceUserServiceGetInitialUser");
    if (init)
        log_line("UserService initialize: 0x%08X", (unsigned)init(NULL));
    if (foreground)
        rc = foreground(&id);
    if (rc != 0 && initial)
        rc = initial(&id);
    log_line("UserService result: 0x%08X, user: 0x%08X", (unsigned)rc, (unsigned)id);
    /* Keep the module loaded: it may be shared with the loader process. */
    if (rc != 0 || id < 0)
        return 0;
    *user = (uint32_t)id;
    return 1;
}
