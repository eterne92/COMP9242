#include "syscall.h"
#include "addrspace.h"
#include "proc.h"
#include "vfs/vfs.h"
#include "vfs/vnode.h"
#include "vfs/uio.h"
#include <fcntl.h>
#include <aos/debug.h>
#include <aos/sel4_zf_logif.h>
#include <cspace/cspace.h>
#include <picoro/picoro.h>
#include <serial/serial.h>
#include <stdlib.h>

cspace_t *global_cspace;
proc *cur_proc;
struct serial *serial;

typedef struct coroutines {
    coro data;
    struct coroutines *next;
} coroutines;

static coroutines *coro_list = NULL;
static coroutines *tail = NULL;

static struct vnode *vn;

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
    cur_proc = get_cur_proc();

    (void)badge;
    (void)num_args;
    /* allocate a slot for the reply tty_test_processcap */
    seL4_MessageInfo_t reply_msg;
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
    as_region *region;
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
        printf("in open\n");
        vfs_open("console:", O_RDWR,0, &vn);
        printf("%p %p %p\n", vn, vn->vn_data, vn->vn_fs);
        syscall_reply(cur_proc->reply, 4, 0);
        break;

    }
    case SOS_SYS_READ:{
        printf("in read\n");
        seL4_Word path = seL4_GetMR(1);
        seL4_Word vaddr = seL4_GetMR(2);
        seL4_Word length = seL4_GetMR(3);
        struct uio my_uio;
        uio_init(&my_uio, vaddr, length, 0, UIO_READ, cur_proc);
        VOP_READ(vn, &my_uio);
        // coro c = coroutine(NULL);
        // create_coroutine(c);
        break;
    }
    case SOS_SYS_WRITE:{
        printf("in write\n");
        seL4_Word vaddr = seL4_GetMR(2);
        seL4_Word length = seL4_GetMR(3);
        printf("vaddr %p, length %ld;\n", vaddr, length);
        struct uio my_uio;
        uio_init(&my_uio, vaddr, length, 0, UIO_WRITE, cur_proc);
        VOP_WRITE(vn, &my_uio);
        // coro c = coroutine(NULL);
        // create_coroutine(c);
        break;
    }
    case SOS_SYS_STAT:
        break;
    case SOS_SYS_CLOSE:
        break;
    case SOS_SYS_USLEEP:
        _sos_sys_usleep();
        break;
    case SOS_SYS_TIMESTAMP:
        _sos_sys_time_stamp();
        break;

    case SOS_SYSCALLMSG:
        (void)err;
        /* MSG size each timdumpe should be less then 120 */
        char data[120];
        /* get dacoroutinestasize */
        int len = seL4_GetMR(1);
        /* get realdata */
        for (int i = 0; i < len; i++) {
            data[i] = seL4_GetMR(i + 2);
        }
        /* magic */
        for (int i = 0; i < 7500000; i++)
            ;
        /* send them to serial */
        serial_send(serial, data, len);

        /* construct a reply message of length 1 */
        reply_msg = seL4_MessageInfo_new(0, 0, 0, 1);
        /* Set the first (and only) word in the message to 0 */
        seL4_SetMR(0, 0);
        /* Send the reply to the saved reply capability. */
        seL4_Send(reply, reply_msg);
        /* Free the slot we allocated for the reply - it is now empty, as the reply
         * capability was consumed by the send. */
        cspace_free_slot(global_cspace, reply);
        break;

    case SOS_SYSCALLBRK:
        (void)err;
        seL4_Word newbrk = seL4_GetMR(1);
        /* we are just assume it's tty_test here */
        reply_msg = seL4_MessageInfo_new(0, 0, 0, 1);
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
        seL4_SetMR(0, region->vaddr + region->size);
        seL4_Send(reply, reply_msg);
        /* Free the slot we allocated for the reply - it is now empty, as the reply
         * capability was consumed by the send. */
        cspace_free_slot(global_cspace, reply);
        break;

    case SOS_SYSCALL_MMAP:
        printf("mmap called\n");
        (void)err;
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

        seL4_SetMR(0, region->vaddr);
        seL4_Send(reply, reply_msg);
        /* Free the slot we allocated for the reply - it is now empty, as the reply
         * capability was consumed by the send. */
        cspace_free_slot(global_cspace, reply);
        break;

    case SOS_SYSCALL_MUNMAP:
        printf("munmap called\n");
        region = cur_proc->as->regions;
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
            seL4_SetMR(0, region->vaddr);
        } else {
            seL4_SetMR(0, 0);
        }
        seL4_Send(reply, reply_msg);
        cspace_free_slot(global_cspace, reply);
        break;

    default:
        ZF_LOGE("Unknown syscall %lu\n", syscall_number);
        /* don't reply to an unknown syscall */
    }
    run_coroutine(NULL);
}
