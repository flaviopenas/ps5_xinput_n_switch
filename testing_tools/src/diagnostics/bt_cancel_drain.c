#include "bt_cancel_drain.h"
int bt_cancel_drain(unsigned char *pending, size_t count, unsigned budget, const bt_cancel_ops *ops,
                    unsigned *remaining)
{
    *remaining = 0;
    for (size_t i = 0; i < count; ++i)
        *remaining += pending[i] != 0;
    int64_t deadline = ops->now(ops->context) + budget;
    while (*remaining) {
        if (ops->now(ops->context) >= deadline)
            return 1;
        unsigned index;
        int result = ops->next(ops->context, &index);
        if (result < 0 || (result > 0 && index >= count))
            return -1;
        if (result > 0 && pending[index]) {
            pending[index] = 0;
            --*remaining;
        }
        /* Yield on duplicate completions as well as empty polls. */
        ops->idle(ops->context);
    }
    return 0;
}
