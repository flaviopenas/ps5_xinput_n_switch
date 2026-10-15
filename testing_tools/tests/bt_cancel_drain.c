#include "bt_cancel_drain.h"
#include <assert.h>
#include <stdio.h>
typedef struct { int sequence[12]; unsigned at; int64_t time; } fake;
static int next(void *context, unsigned *index)
{
    fake *f = context;
    int value = f->at < 12 ? f->sequence[f->at++] : -1;
    if (value == -2) return -1;
    if (value < 0) return 0;
    *index = (unsigned)value;
    return 1;
}
static int64_t now(void *context) { return ((fake *)context)->time; }
static void idle(void *context) { ++((fake *)context)->time; }
int main(void)
{
    unsigned left;
    fake delayed = {.sequence = {-1,0,0,-1,2}};
    unsigned char pending[] = {1,0,1};
    bt_cancel_ops ops = {next, now, idle, &delayed};
    assert(bt_cancel_drain(pending,3,10,&ops,&left) == 0 && left == 0);
    assert(delayed.at == 5 && delayed.time == 5);
    fake absent = {.sequence = {-1,-1,-1,-1,-1}};
    ops.context = &absent; pending[2] = 1;
    assert(bt_cancel_drain(pending,3,3,&ops,&left) == 1 && left == 1 && pending[2]);
    fake error = {.sequence = {-2}};
    ops.context = &error;
    assert(bt_cancel_drain(pending,3,10,&ops,&left) == -1 && left == 1);
    fake invalid = {.sequence = {3}};
    ops.context = &invalid;
    assert(bt_cancel_drain(pending,3,10,&ops,&left) == -1 && pending[2]);
    pending[2] = 0;
    assert(bt_cancel_drain(pending,3,10,&ops,&left) == 0 && invalid.at == 1);
    puts("Cancellation drain: delayed/duplicate completions, timeout, error, bounds and empty queue passed");
}
