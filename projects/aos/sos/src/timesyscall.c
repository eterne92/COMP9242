#include "syscall.h"
#include "proc.h"
#include <clock/timestamp.h>
#include <clock/clock.h>
#include <sel4/sel4.h>

int64_t _sos_sys_time_stamp(void)
{
    return timestamp_us(timestamp_get_freq());
}

static void sleep_callback(uint64_t id, void *data)
{
    seL4_CPtr reply = (seL4_CPtr) data;
    syscall_reply(reply, 0, 0);
}

void _sos_sys_usleep(void)
{
    int msec = (int)seL4_GetMR(1);
    seL4_CPtr reply = get_cur_proc()->reply;
    register_timer(msec, sleep_callback, reply, F, ONE_SHOT);
}