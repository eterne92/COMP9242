#pragma once

#include <sel4/sel4.h>
#include <utils/util.h>
#include <picoro/picoro.h>
/*
 * our new syscall
 */

typedef struct proc proc;

/* To differentiate between signals from notification objects and and IPC messages,
 * we assign a badge to the notification object. The badge that we receive will
 * be the bitwise 'OR' of the notification object badge and the badges
 * of all pending IPC messages. */
#define IRQ_EP_BADGE BIT(seL4_BadgeBits - 1)
/* All badged IRQs set high bet, then we use uniq bits to
 * distinguish interrupt sources */
#define IRQ_BADGE_NETWORK_IRQ BIT(0)
#define IRQ_BADGE_NETWORK_TICK BIT(1)
#define IRQ_BADGE_TIMER BIT(2)

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


struct proc;

void create_coroutine(coro c);
void run_coroutine(void *arg);
NORETURN void syscall_loop(seL4_CPtr ep);
void handle_syscall(seL4_Word badge, int num_args);

void syscall_reply(struct proc *process, seL4_Word ret, seL4_Word);

void set_boottime(void);

unsigned get_now_since_boot(void);

void _sos_sys_time_stamp(proc *cur_proc);

void _sos_sys_usleep(proc *cur_proc);
/* file syscalls */

int _sys_do_open(proc *cur_proc, char *path, seL4_Word openflags, int at);

void *_sys_open(proc *cur_proc);

void *_sys_read(proc *cur_proc);

void *_sys_write(proc *cur_proc);

void *_sys_close(proc *cur_proc);

void *_sys_getdirent(proc *cur_proc);

void *_sys_stat(proc *cur_proc);

void _sys_brk(proc *cur_proc);

void _sys_mmap(proc *cur_proc);

void *_sys_munmap(proc *cur_proc);

void *_sys_handle_page_fault(proc *cur_proc);

void *_sys_create_process(proc *cur_proc);

void _sys_process_wait(proc *cur_proc);

void *_sys_kill_process(proc *cur_proc);

void *_sys_process_status(proc *cur_proc);