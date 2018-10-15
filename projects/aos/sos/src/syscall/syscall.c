#include "syscall.h"
#include "../addrspace.h"
#include "../proc.h"
#include "../pagetable.h"
#include <fcntl.h>
#include <aos/debug.h>
#include <aos/sel4_zf_logif.h>
#include <cspace/cspace.h>
#include <picoro/picoro.h>
#include <serial/serial.h>
#include <stdlib.h>
#include <clock/clock.h>
#include "../network.h"
#include "../vfs/uio.h"

int get_header(void);

typedef  void *(*coro_t)(void *);
cspace_t *global_cspace;
struct serial *serial;

typedef struct coroutines {
    coro data;
    struct coroutines *next;
} coroutines;

static coroutines *coro_list = NULL;
static coroutines *tail = NULL;

static void wake_up(int pid)
{
    proc *p = NULL;
    for (int i = 0; i < PROCESS_ARRAY_SIZE; ++i) {
        p = get_process(i);
        if (p && p->state == ACTIVE && (p->waiting_pid == pid || p->waiting_pid == -1)) {
            p->waiting_pid = -99;
            printf("wake up process %d\n", p->status.pid);
            syscall_reply(p->reply, 0, 0);
        }
    }
}


void syscall_reply(seL4_CPtr reply, seL4_Word ret, seL4_Word err)
{
    seL4_MessageInfo_t reply_msg;
    reply_msg = seL4_MessageInfo_new(0, 0, 0, 2);
    /* Set the first (and only) word in the message to 0 */
    seL4_SetMR(0, ret);
    seL4_SetMR(1, err);
    /* Send the reply to the saved reply capability. */
    seL4_Send(reply, reply_msg);
    /* Free the slot we allocated for the reply - it is now empty, as the reply
         * capability was consumed by the send. */
    cspace_delete(global_cspace, reply);
    cspace_free_slot(global_cspace, reply);
}


static void add_coroutine(coroutines *coroutine)
{
    coroutine->next = NULL;
    if (!coro_list) {
        coro_list = tail = coroutine;
    } else {
        tail->next = coroutine;
        tail = tail->next;
    }
}

void create_coroutine(coro c)
{
    coroutines *list_node = (coroutines *)malloc(sizeof(coroutine));
    if (!list_node)
        return;
    list_node->data = c;
    list_node->next = NULL;
    add_coroutine(list_node);
}

static coroutines *pop_coroutine()
{
    if (coro_list) {
        coroutines *tmp = coro_list;
        coro_list = coro_list->next;
        return tmp;
    } else {
        return NULL;
    }
}

void run_coroutine(void *arg)
{
    while (coro_list) {
        coroutines *c = pop_coroutine();
        if (resumable(c->data)) {
            add_coroutine(c);
            resume(c->data, arg);
            return;
        } else {
            free(c);
        }
    }
}

void handle_syscall(seL4_Word badge, int num_args)
{
    (void)num_args;
    proc *cur_proc = get_process(badge);
    // printf("cur_proc is %p, proc is %p\n", cur_proc, process_array);
    /* allocate a slot for the reply tty_test_processcap */
    seL4_CPtr reply = cspace_alloc_slot(global_cspace);
    /* get the first word of the message, which in the SOS protocol is the number
     * of the SOS "syscall". */
    seL4_Word syscall_number = seL4_GetMR(0);
    /* Save the reply capability of the caller. If we didn't do this,
     * we coud just use seL4_Reply to respond directly to the reply capability.
     * However if SOS were to block (seL4_Recv) to receive another message, then
     * the existing reply capability would be deleted. So we save the reply capability
     * here, as in future you will want to reply to it later. Note that after
     * saving the reply capability, seL4_Reply cannot be used, as the reply capability
     * is moved from the internal slot in the TCB to our cspace, and the internal
     * slot is now empty. */
    seL4_Error err = cspace_save_reply_cap(global_cspace, reply);
    ZF_LOGF_IFERR(err, "Failed to save reply");
    /* Process system call */
    cur_proc->reply = reply;
    // printf("SYSCALL NO.%d IS CALLED, for process %d\n", syscall_number,
    //        (cur_proc - process_array));
    switch (syscall_number) {
    // case SOS_SYSCALL0:
    //     ZF_LOGV("syscall: thread example made syscall 0!\n");
    //     /* construct a reply message of length 1 */
    //     reply_msg = seL4_MessageInfo_new(0, 0, 0, 1);
    //     /* Set the first (and only) word in the message to 0 */
    //     seL4_SetMR(0, 0);
    //     /* Send the reply to the saved reply capability. */
    //     seL4_Send(reply, reply_msg);
    //     /* Free the slot we allocated for the reply - it is now empty, as the reply
    //      * capability was consumed by the send. */
    //     cspace_free_slot(global_cspace, reply);
    //     break;
    case SOS_SYS_OPEN: {
        coro c = coroutine((coro_t)&_sys_open);
        cur_proc->c = c;
        resume(c, cur_proc);
        create_coroutine(c);
        break;
    }
    case SOS_SYS_READ: {
        coro c = coroutine((coro_t)&_sys_read);
        cur_proc->c = c;
        resume(c, cur_proc);
        create_coroutine(c);
        break;
    }
    case SOS_SYS_WRITE: {
        coro c = coroutine((coro_t)&_sys_write);
        cur_proc->c = c;
        resume(c, cur_proc);
        create_coroutine(c);
        break;
    }
    case SOS_SYS_STAT: {
        coro c = coroutine((coro_t)&_sys_stat);
        cur_proc->c = c;
        resume(c, cur_proc);
        create_coroutine(c);
        break;
    }
    case SOS_SYS_CLOSE: {
        coro c = coroutine((coro_t)&_sys_close);
        cur_proc->c = c;
        resume(c, cur_proc);
        create_coroutine(c);
        break;
    }
    case SOS_SYS_GET_DIRDENTS: {
        coro c = coroutine((coro_t)_sys_getdirent);
        cur_proc->c = c;
        resume(c, cur_proc);
        create_coroutine(c);
        break;
    }
    case SOS_SYS_USLEEP:
        _sos_sys_usleep();
        break;
    case SOS_SYS_TIMESTAMP:
        _sos_sys_time_stamp();
        break;

    case SOS_SYSCALLBRK:
        _sys_brk(cur_proc);
        break;

    case SOS_SYSCALL_MMAP:
        _sys_mmap(cur_proc);
        break;

    case SOS_SYSCALL_MUNMAP:
        _sys_munmap(cur_proc);
        break;

    case SOS_SYS_PROCESS_CREATE: {
        coro c = coroutine((coro_t)_sys_create_process);
        cur_proc->c = c;
        resume(c, cur_proc);
        create_coroutine(c);
        break;
    }
    case SOS_SYS_PROCESS_WAIT: {
        _sys_process_wait(cur_proc);
        break;
    }

    case SOS_SYS_PROCESS_DELETE: {
        coro c = coroutine((coro_t)_sys_kill_process);
        cur_proc->c = c;
        resume(c, cur_proc);
        create_coroutine(c);
        break;
    }

    case SOS_SYS_MY_ID: {
        syscall_reply(cur_proc->reply, cur_proc->status.pid, 0);
        break;
    }

    case SOS_SYS_PROCESS_STATUS: {
        coro c = coroutine((coro_t)_sys_process_status);
        cur_proc->c = c;
        resume(c, cur_proc);
        create_coroutine(c);
        break;

    }

    default:
        ZF_LOGE("Unknown syscall %lu\n", syscall_number);
        /* don't reply to an unknown syscall */
    }
}


NORETURN void syscall_loop(seL4_CPtr ep)
{

    while (1) {
        seL4_Word badge;
        seL4_Word label;
        /* Block on ep, waiting for an IPC sent over ep, or
         * a notification from our bound notification object */
        seL4_MessageInfo_t message = seL4_Recv(ep, &badge);
        /* Awake! We got a message - check the label and badge to
         * see what the message is about */
        label = seL4_MessageInfo_get_label(message);

        if (badge & IRQ_EP_BADGE) {
            /* It's a notification from our bound notification
             * object! */
            if (badge & IRQ_BADGE_NETWORK_IRQ) {
                /* It's an interrupt from the ethernet MAC */
                network_irq();
            }
            if (badge & IRQ_BADGE_NETWORK_TICK) {
                /* It's an interrupt from the watchdog keeping our TCP/IP stack alive */
                network_tick();
            }
            if (badge & IRQ_BADGE_TIMER) {
                timer_interrupt(F);
            }
        } else if (label == seL4_Fault_NullFault) {
            /* It's not a fault or an interrupt, it must be an IPC
             * message from tty_test! */
            handle_syscall(badge, seL4_MessageInfo_get_length(message) - 1);
        } else {
            proc *cur_proc = get_process(badge);
            set_cur_proc(cur_proc);

            seL4_CPtr reply = cspace_alloc_slot(global_cspace);
            seL4_Error err = cspace_save_reply_cap(global_cspace, reply);
            ZF_LOGF_IFERR(err, "Failed to save reply");
            cur_proc->reply = reply;
            /* page fault handler */
            if (label == seL4_Fault_VMFault) {
                coro c = coroutine((coro_t)_sys_handle_page_fault);
                cur_proc->c = c;
                resume(c, cur_proc);
                create_coroutine(c);
            } else {
                printf("not page fault\n");
                char c[3];
                c[0] = badge / 10 - '0';
                c[1] = badge % 10 - '0';
                c[2] = 0;
                /* some kind of fault */
                debug_print_fault(message, c);
                /* dump registers too */
                debug_dump_registers(cur_proc->tcb);

                ZF_LOGF("The SOS skeleton does not know how to handle faults!");
            }
        }
        run_coroutine(NULL);
    }
}


void *_sys_handle_page_fault(proc *cur_proc)
{
    seL4_Word vaddr = seL4_GetMR(seL4_VMFault_Addr);
    seL4_Error err = handle_page_fault(cur_proc, vaddr,
                                       seL4_GetMR(seL4_VMFault_FSR));
    if (err) {
        /* we will deal with this later */
        int pid = cur_proc->status.pid;
        printf("process is %d    vaddr is %p\n", pid, (void *)vaddr);
        ZF_LOGE("Segment fault");
        kill_process(pid);
        wake_up(pid);
        return NULL;
    }
    syscall_reply(cur_proc->reply, 0, 0);
    return NULL;
}

void _sys_brk(proc *cur_proc)
{
    seL4_Error err;
    seL4_Word newbrk = seL4_GetMR(1);
    as_region *region;
    /* we are just assume it's tty_test here */
    if (cur_proc->as->heap == NULL) {
        err = as_define_heap(cur_proc->as);
        if (err != 0) {
            /* this should be delete process for later stuff */
            ZF_LOGE("region error");
        }
        //cur_proc->as->used_top = cur_proc->as->heap->vaddr;
    }
    region = cur_proc->as->heap;
    //printf("**************newbrk is %lx\n", newbrk);
    //printf("**************region->size %x region->vaddr %p\n", region->size, (void *)region->vaddr);
    if (!newbrk) {
    } else if (newbrk < region->size + region->vaddr) {
        /* shouldn't shrink heap */
        printf("should not be here\n");
    } else {
        int tmp = newbrk - region->vaddr;
        //printf("**************tmp is %x\n", tmp);
        if (tmp > (4096 * 2 * PAGE_SIZE_4K)) {
            assert(false);
            syscall_reply(cur_proc->reply, 0, 0);
            return;
        } else {
            region->size = newbrk - region->vaddr;
        }
            
    }
    seL4_Word ret = region->vaddr + region->size;
    syscall_reply(cur_proc->reply, ret, 0);
}

void _sys_mmap(proc *cur_proc)
{
    printf("enter _sys_mmap -> mmap called\n");
    seL4_Error err;
    as_region *region;
    if (cur_proc->as->heap == NULL) {
        err = as_define_heap(cur_proc->as);
        if (err) {
            /* should not fail here */
            ZF_LOGE("region error");
        }
        cur_proc->as->used_top = cur_proc->as->heap->vaddr;
    }
    seL4_Word size = seL4_GetMR(2);

    region = cur_proc->as->regions;
    as_region *ret = NULL;
    while (region->next != NULL) {
        seL4_Word base = region->vaddr + region->size;
        seL4_Word top = region->next->vaddr;
        if (top - base > size) {
            ret = as_define_region(cur_proc->as, base, size, RG_R | RG_W);
            break;
        }
        region = region->next;
    }
    if (ret) {
        syscall_reply(cur_proc->reply, ret->vaddr, 0);
    } else {
        syscall_reply(cur_proc->reply, 0, 0);
    }
}

void _sys_munmap(proc *cur_proc)
{
    printf("munmap called\n");
    seL4_Word ret;
    as_region *region = cur_proc->as->regions;
    seL4_Word base = seL4_GetMR(1);
    while (region) {
        if (region->vaddr == base) {
            as_destroy_region(cur_proc->as, region, cur_proc);
            break;
        }
        region = region->next;
    }
    if (region) {
        ret = region->vaddr;
    } else {
        ret = 0;
    }
    syscall_reply(cur_proc->reply, ret, 0);
}

void *_sys_create_process(proc *cur_proc)
{
    printf("in create process\n");
    seL4_Word path = seL4_GetMR(1);
    char app_name[N_NAME];
    int ret_pid;

    int path_length = copystr(cur_proc, (char *)path, app_name, N_NAME, COPYIN);
    if (path_length == -1) {
        syscall_reply(cur_proc->reply, -1, -1);
        return NULL;
    }

    bool success = start_process(app_name, ipc_ep, &ret_pid);
    if (!success) {
        if (ret_pid == -1) {
            syscall_reply(cur_proc->reply, -1, -1);
            return NULL;
        } else {
            kill_process(ret_pid);
        }
    }
    printf("ret_pid is %d\n", ret_pid);
    syscall_reply(cur_proc->reply, ret_pid, 0);
    return NULL;
}

void _sys_process_wait(proc *cur_proc)
{
    proc *process;
    int pid = seL4_GetMR(1);
    cur_proc->waiting_pid = pid;
}

void *_sys_kill_process(proc *cur_proc)
{
    proc *process;
    int pid = seL4_GetMR(1);
    if (pid < 0 || pid > PROCESS_ARRAY_SIZE - 1) {
        syscall_reply(cur_proc->reply, 0, 0);
        return NULL;
    }

    process = get_process(pid);
    if (process->state == DEAD) {
        syscall_reply(cur_proc->reply, 0, 0);
        return NULL;
    }

    kill_process(pid);
    printf("$$$$$$$$$swapping header is %d\n", get_header());
    wake_up(pid);
    printf("wakeup done\n");
    if (cur_proc->state == ACTIVE) {
        printf("send wakeup reply\n");
        syscall_reply(cur_proc->reply, 0, 0);
    }
    return NULL;
}

void *_sys_process_status(proc *cur_proc)
{
    void *u_ptr = (void *)seL4_GetMR(1);
    int max = seL4_GetMR(2);

    sos_process_t k_processes[max];

    int index = 0;
    for (int i = 0; i < PROCESS_ARRAY_SIZE; i++) {
        if (get_process(i) && get_process(i)->state == ACTIVE) {
            k_processes[index] = get_process(i)->status;
            index++;
            if (index == max) {
                break;
            }
        }
    }

    int ret = mem_move(cur_proc, (seL4_Word) u_ptr, (seL4_Word) &k_processes,
                       sizeof(sos_process_t) * index, READ);

    if (ret == -1) {
        syscall_reply(cur_proc->reply, 0, -1);
        return NULL;
    }
    syscall_reply(cur_proc->reply, index, 0);
    return NULL;
}