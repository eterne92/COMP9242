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

void inline syscall_reply(seL4_CPtr reply, seL4_Word ret, seL4_Word errno)
{
    seL4_MessageInfo_t reply_msg;
    reply_msg = seL4_MessageInfo_new(0, 0, 0, 1);
    /* Set the first (and only) word in the message to 0 */
    seL4_SetMR(0, ret);
    seL4_SetMR(1, errno);
    /* Send the reply to the saved reply capability. */
    seL4_Send(reply, reply_msg);
    /* Free the slot we allocated for the reply - it is now empty, as the reply
         * capability was consumed by the send. */
    cspace_free_slot(global_cspace, reply);
}

/* file syscalls */