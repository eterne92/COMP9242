#pragma once

#include <sel4/sel4.h>
/* 
 * our new syscall
 */

/* System calls for SOS */
#define SOS_SYS_READ                0
#define SOS_SYS_WRITE               1
#define SOS_SYS_OPEN                2
#define SOS_SYS_CLOSE               3
#define SOS_SYS_STAT                4
#define SOS_SYS_GET_DIRDENTS        5
#define SOS_SYS_MY_ID               6
#define SOS_SYS_PROCESS_CREATE      7   
#define SOS_SYS_PROCESS_DELETE      8
#define SOS_SYS_PROCESS_STATUS      9
#define SOS_SYS_PROCESS_WAIT        10
#define SOS_SYS_TIMESTAMP           11
#define SOS_SYS_USLEEP              12
#define SOS_SYSCALLMSG              100
#define SOS_SYSCALLBRK              101
#define SOS_SYSCALL_MMAP            102
#define SOS_SYSCALL_MUNMAP          200


void handle_syscall(seL4_Word badge, int num_args);

void syscall_reply(seL4_CPtr reply, seL4_Word ret, seL4_Word errno);


void _sos_sys_time_stamp(void);

void _sos_sys_usleep(void);
/* file syscalls */