#include "syscall.h"
#include "addrspace.h"
#include "proc.h"
#include "pagetable.h"
#include <fcntl.h>
#include <aos/debug.h>
#include <aos/sel4_zf_logif.h>
#include <cspace/cspace.h>
#include <picoro/picoro.h>
#include <serial/serial.h>
#include <stdlib.h>

typedef  void *(*coro_t)(void *);
cspace_t *global_cspace;
struct serial *serial;

typedef struct coroutines {
    coro data;
    struct coroutines *next;
} coroutines;

static coroutines *coro_list = NULL;
static coroutines *tail = NULL;


void syscall_reply(seL4_CPtr reply, seL4_Word ret, seL4_Word errno)
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

static void create_coroutine(coro c)
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

static void run_coroutine(void *arg)
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
    (void)badge;
    (void)num_args;
    proc *cur_proc = get_cur_proc();
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
    printf("SYSCALL NO.%d IS CALLED\n", syscall_number);
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
    case SOS_SYS_OPEN:{
        _sys_open(cur_proc);
        break;
    }
    case SOS_SYS_READ:{
        coro c = coroutine((coro_t)&_sys_read);
        resume(c, cur_proc);
        create_coroutine(c);
        break;
    }
    case SOS_SYS_WRITE:{
        coro c = coroutine((coro_t)&_sys_write);
        resume(c, cur_proc);
        create_coroutine(c);
        break;
    }
    case SOS_SYS_STAT:
        break;
    case SOS_SYS_CLOSE:
        _sys_close(cur_proc);
        break;
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

    default:
        ZF_LOGE("Unknown syscall %lu\n", syscall_number);
        /* don't reply to an unknown syscall */
    }
    run_coroutine(NULL);
}

void _sys_brk(proc *cur_proc){
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
        cur_proc->as->used_top = cur_proc->as->heap->vaddr;
    }
    region = cur_proc->as->heap;

    if (!newbrk) {
    } else if (newbrk < region->size + region->vaddr) {
        /* shouldn't shrink heap */
    } else {
        region->size = newbrk - region->vaddr;
    }
    seL4_Word ret = region->vaddr + region->size;
    syscall_reply(cur_proc->reply, ret, 0);
}

void _sys_mmap(proc *cur_proc){
    printf("mmap called\n");
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
    seL4_Word vtop = cur_proc->as->used_top;
    seL4_Word vbase = vtop - size;
    region = as_define_region(cur_proc->as, vbase, size, RG_R | RG_W);

    syscall_reply(cur_proc->reply, region->vaddr, 0);
}

void _sys_munmap(proc *cur_proc){
    printf("munmap called\n");
    seL4_Word ret;
    as_region *region = cur_proc->as->regions;
    seL4_Word base = seL4_GetMR(1);
    while (region) {
        if (region->vaddr == base) {
            cur_proc->as->used_top = region->vaddr + region->size;
            as_destroy_region(cur_proc->as, region);
            break;
        }
        region = region->next;
    }
    if (region == NULL) {
        ret = region->vaddr;
    } else {
        ret = 0;
    }
    syscall_reply(cur_proc->reply, ret, 0);
}