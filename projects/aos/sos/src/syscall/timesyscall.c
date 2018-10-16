#include "../proc.h"
#include "syscall.h"
#include <clock/clock.h>
#include <clock/timestamp.h>
#include <sel4/sel4.h>

static seL4_Word boottime;

void set_boottime(void)
{
    boottime = timestamp_us(timestamp_get_freq());
}

void _sos_sys_time_stamp(proc *cur_proc)
{
    seL4_CPtr reply = cur_proc->reply;
    seL4_Word t = timestamp_us(timestamp_get_freq());

    syscall_reply(reply, t - boottime, 0);
}

static void sleep_callback(uint64_t id, void *data)
{
    (void)id;
    seL4_CPtr reply = (seL4_CPtr)data;
    syscall_reply(reply, 0, 0);
}

void _sos_sys_usleep(proc *cur_proc)
{
    int msec = (int)seL4_GetMR(1) * 1000;
    seL4_CPtr reply = reply;
    register_timer(msec, sleep_callback, (void *)reply, F, ONE_SHOT);
}

unsigned get_now_since_boot(void)
{
    seL4_Word msec = (timestamp_us(timestamp_get_freq()) - boottime) / 1000;
    return msec;
}