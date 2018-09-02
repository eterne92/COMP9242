#include "../proc.h"
#include "syscall.h"
#include <clock/clock.h>
#include <clock/timestamp.h>
#include <sel4/sel4.h>

void _sos_sys_time_stamp(void)
{
    seL4_CPtr reply = get_cur_proc()->reply;
    syscall_reply(reply, timestamp_us(timestamp_get_freq()), 0);
}

static void sleep_callback(uint64_t id, void *data)
{
    (void)id;
    seL4_CPtr reply = (seL4_CPtr)data;
    syscall_reply(reply, 0, 0);
}

void _sos_sys_usleep(void)
{
    int msec = (int)seL4_GetMR(1) * 1000;
    seL4_CPtr reply = get_cur_proc()->reply;
    register_timer(msec, sleep_callback, (void *)reply, F, ONE_SHOT);
}