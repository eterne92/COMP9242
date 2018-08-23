#pragma once

#include <sel4/sel4.h>

#define SOS_SYSCALL0 0
/* 
 * our new syscall
 */
#define SOS_SYSCALLMSG 100
#define SOS_SYSCALLBRK 101
#define SOS_SYSCALL_MMAP 102
#define SOS_SYSCALL_MUNMAP 200

void handle_syscall(seL4_Word badge, int num_args);