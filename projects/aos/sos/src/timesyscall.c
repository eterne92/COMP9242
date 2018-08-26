#include "syscall.h"
#include <clock/timestamp.h>

int64_t sos_sys_time_stamp(void)
{
    return timestamp_ms(timestamp_get_freq());
}


