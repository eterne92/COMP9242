/*
 * Copyright 2018, Data61
 * Commonwealth Scientific and Industrial Research Organisation (CSIRO)
 * ABN 41 687 119 230.
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(DATA61_GPL)
 */
#include <assert.h>
#include <autoconf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <utils/util.h>

#include <aos/debug.h>
#include <aos/sel4_zf_logif.h>
#include <cspace/cspace.h>

#include <clock/clock.h>
#include <cpio/cpio.h>
#include <elf/elf.h>
#include <picoro/picoro.h>
#include <serial/serial.h>

#include "addrspace.h"
#include "bootstrap.h"
#include "drivers/uart.h"
#include "elfload.h"
#include "frametable.h"
#include "mapping.h"
#include "network.h"
#include "pagetable.h"
#include "proc.h"
#include "syscall.h"
#include "syscalls.h"
#include "tests.h"
#include "ut.h"
#include "vmem_layout.h"
#include "vfs/vfs.h"

#include <aos/vsyscall.h>

#include "sys/execinfo.h"

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

#define TTY_NAME "sosh"
#define TTY_PRIORITY (0)
#define TTY_EP_BADGE (101)

/*
 * A dummy starting syscall
 */

/* The linker will link this symbol to the start address  *
 * of an archive of attached applications.                */
extern char _cpio_archive[];
extern char __eh_frame_start[];
/* provided by gcc */
extern void(__register_frame)(void *);

/* root tasks cspace */
static cspace_t cspace;
cspace_t *global_cspace = &cspace;

/* serial */
extern struct serial *serial;

/* the one process we start */
static proc tty_test_process;

NORETURN void syscall_loop(seL4_CPtr ep)
{

    while (1) {
        seL4_Word badge = 0;
        /* Block on ep, waiting for an IPC sent over ep, or
         * a notification from our bound notification object */
        seL4_MessageInfo_t message = seL4_Recv(ep, &badge);
        /* Awake! We got a message - check the label and badge to
         * see what the message is about */
        seL4_Word label = seL4_MessageInfo_get_label(message);

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
            set_cur_proc(&tty_test_process);
            handle_syscall(badge, seL4_MessageInfo_get_length(message) - 1);
        } else {
            set_cur_proc(&tty_test_process);
            seL4_CPtr reply = cspace_alloc_slot(global_cspace);
            seL4_MessageInfo_t reply_msg;
            seL4_Error err = cspace_save_reply_cap(global_cspace, reply);
            ZF_LOGF_IFERR(err, "Failed to save reply");
            /* page fault handler */
            if (label == seL4_Fault_VMFault) {
                err = handle_page_fault(get_cur_proc(), seL4_GetMR(seL4_VMFault_Addr), seL4_GetMR(seL4_VMFault_FSR));
                if (err) {
                    /* we will deal with this later */
                    ZF_LOGF_IFERR(err, "Segment fault");
                }
                /* construct a reply message of length 1 */
                reply_msg = seL4_MessageInfo_new(0, 0, 0, 1);
                /* Set the first (and only) word in the message to 0 */
                seL4_SetMR(0, 0);
                /* Send the reply to the saved reply capability. */
                seL4_Send(reply, reply_msg);
                /* Free the slot we allocated for the reply - it is now empty, as the reply
                * capability was consumed by the send. */
                cspace_free_slot(global_cspace, reply);
            } else {
                /* some kind of fault */
                debug_print_fault(message, TTY_NAME);
                /* dump registers too */
                debug_dump_registers(tty_test_process.tcb);

                ZF_LOGF("The SOS skeleton does not know how to handle faults!");
            }
        }
        // resume
    }
}

/* helper to allocate a ut + cslot, and retype the ut into the cslot */
static ut_t *alloc_retype(seL4_CPtr *cptr, seL4_Word type, size_t size_bits)
{
    /* Allocate the object */
    ut_t *ut = ut_alloc(size_bits, global_cspace);
    if (ut == NULL) {
        ZF_LOGE("No memory for object of size %zu", size_bits);
        return NULL;
    }

    /* allocate a slot to retype the memory for object into */
    *cptr = cspace_alloc_slot(global_cspace);
    if (*cptr == seL4_CapNull) {
        ut_free(ut, size_bits);
        ZF_LOGE("Failed to allocate slot");
        return NULL;
    }

    /* now do the retype */
    seL4_Error err = cspace_untyped_retype(global_cspace, ut->cap, *cptr, type, size_bits);
    ZF_LOGE_IFERR(err, "Failed retype untyped");
    if (err != seL4_NoError) {
        ut_free(ut, size_bits);
        cspace_free_slot(global_cspace, *cptr);
        return NULL;
    }

    return ut;
}

static int stack_write(seL4_Word *mapped_stack, int index, uintptr_t val)
{
    mapped_stack[index] = val;
    return index - 1;
}

/* set up System V ABI compliant stack, so that the process can
 * start up and initialise the C library */
static uintptr_t init_process_stack(cspace_t *cspace, seL4_CPtr local_vspace, char *elf_file)
{
    /* Create a stack frame */
    seL4_Error err;
    int frame = frame_alloc(NULL);
    err = sos_map_frame(cspace, frame, (seL4_Word)tty_test_process.pt, tty_test_process.vspace,
        USERSTACKTOP - PAGE_SIZE_4K, seL4_ReadWrite,
        seL4_ARM_Default_VMAttributes);
    // tty_test_process.stack_ut = alloc_retype(&tty_test_process.stack, seL4_ARM_SmallPageObject, seL4_PageBits);
    if (err != seL4_NoError) {
        ZF_LOGE("Failed to allocate stack");
        return 0;
    }

    /* virtual addresses in the target process' address space */
    uintptr_t stack_top = USERSTACKTOP;
    uintptr_t stack_bottom = stack_top - PAGE_SIZE_4K;
    /* virtual addresses in the SOS's address space */
    void *local_stack_top = (seL4_Word *)SOS_SCRATCH;
    uintptr_t local_stack_bottom = SOS_SCRATCH - PAGE_SIZE_4K;

    /* find the vsyscall table */
    uintptr_t sysinfo = *((uintptr_t *)elf_getSectionNamed(elf_file, "__vsyscall", NULL));
    if (sysinfo == 0) {
        ZF_LOGE("could not find syscall table for c library");
        return 0;
    }

    /* Map in the stack frame for the user app */
    // err = map_frame(cspace, tty_test_process.stack, tty_test_process.vspace, stack_bottom,
    //                            seL4_AllRights, seL4_ARM_Default_VMAttributes);
    // if (err != 0) {
    //     ZF_LOGE("Unable to map stack for user app");
    //     return 0;
    // }

    /* allocate a slot to duplicate the stack frame cap so we can map it into our address space */
    seL4_CPtr local_stack_cptr = cspace_alloc_slot(cspace);
    if (local_stack_cptr == seL4_CapNull) {
        ZF_LOGE("Failed to alloc slot for stack");
        return 0;
    }

    /* copy the stack frame cap into the slot */
    err = cspace_copy(cspace, local_stack_cptr, cspace,
        get_cap_from_vaddr(tty_test_process.pt, stack_bottom), seL4_AllRights);
    if (err != seL4_NoError) {
        cspace_free_slot(cspace, local_stack_cptr);
        ZF_LOGE("Failed to copy cap");
        return 0;
    }

    /* map it into the sos address space */
    err = map_frame(cspace, local_stack_cptr, local_vspace, local_stack_bottom, seL4_AllRights,
        seL4_ARM_Default_VMAttributes);
    if (err != seL4_NoError) {
        cspace_delete(cspace, local_stack_cptr);
        cspace_free_slot(cspace, local_stack_cptr);
        return 0;
    }

    int index = -2;

    /* null terminate the aux vectors */
    index = stack_write(local_stack_top, index, 0);
    index = stack_write(local_stack_top, index, 0);

    /* write the aux vectors */
    index = stack_write(local_stack_top, index, PAGE_SIZE_4K);
    index = stack_write(local_stack_top, index, AT_PAGESZ);

    index = stack_write(local_stack_top, index, sysinfo);
    index = stack_write(local_stack_top, index, AT_SYSINFO);

    /* null terminate the environment pointers */
    index = stack_write(local_stack_top, index, 0);

    /* we don't have any env pointers - skip */

    /* null terminate the argument pointers */
    index = stack_write(local_stack_top, index, 0);

    /* no argpointers - skip */

    /* set argc to 0 */
    stack_write(local_stack_top, index, 0);

    /* adjust the initial stack top */
    stack_top += (index * sizeof(seL4_Word));

    /* the stack *must* remain aligned to a double word boundary,
     * as GCC assumes this, and horrible bugs occur if this is wrong */
    assert(index % 2 == 0);
    assert(stack_top % (sizeof(seL4_Word) * 2) == 0);

    /* unmap our copy of the stack */
    err = seL4_ARM_Page_Unmap(local_stack_cptr);
    assert(err == seL4_NoError);

    /* delete the copy of the stack frame cap */
    err = cspace_delete(cspace, local_stack_cptr);
    assert(err == seL4_NoError);

    /* mark the slot as free */
    cspace_free_slot(cspace, local_stack_cptr);

    return stack_top;
}

/* Start the first process, and return true if successful
 *
 * This function will leak memory if the process does not start successfully.
 * TODO: avoid leaking memory once you implement real processes, otherwise a user
 *       can force your OS to run out of memory by creating lots of failed processes.
 */
bool start_first_process(char *app_name, seL4_CPtr ep)
{
    int frame;
    /* Create a VSpace */
    tty_test_process.vspace_ut = alloc_retype(&tty_test_process.vspace, seL4_ARM_PageGlobalDirectoryObject,
        seL4_PGDBits);
    if (tty_test_process.vspace_ut == NULL) {
        return false;
    }

    /* assign the vspace to an asid pool */
    seL4_Word err = seL4_ARM_ASIDPool_Assign(seL4_CapInitThreadASIDPool, tty_test_process.vspace);
    if (err != seL4_NoError) {
        ZF_LOGE("Failed to assign asid pool");
        return false;
    }

    /* create addrspace of ttytest */
    tty_test_process.as = addrspace_init();
    if (!tty_test_process.as) {
        ZF_LOGE("Failed to create address space");
        return false;
    }
    /* initialize level 1 shadow page table */
    printf("pt\n");
    tty_test_process.pt = initialize_page_table();
    if (!tty_test_process.pt) {
        ZF_LOGE("Failed to create shadow global page directory");
        return false;
    }
    /* Create a simple 1 level CSpace */
    printf("cspace\n");
    err = cspace_create_one_level(global_cspace, &tty_test_process.cspace);
    if (err != CSPACE_NOERROR) {
        ZF_LOGE("Failed to create cspace");
        return false;
    }

    /* Create an IPC buffer */
    printf("ipc\n");
    as_define_ipcbuffer(tty_test_process.as);
    frame = frame_alloc(NULL);
    err = sos_map_frame(global_cspace, frame, (seL4_Word)tty_test_process.pt, tty_test_process.vspace,
        USERIPCBUFFER, seL4_ReadWrite, seL4_ARM_Default_VMAttributes);
    // tty_test_process.ipc_buffer_ut = alloc_retype(&tty_test_process.ipc_buffer, seL4_ARM_SmallPageObject,
    //                                               seL4_PageBits);
    if (err != seL4_NoError) {
        ZF_LOGE("Failed to alloc ipc buffer ut");
        return false;
    }

    /* allocate a new slot in the target cspace which we will mint a badged endpoint cap into --
     * the badge is used to identify the process, which will come in handy when you have multiple
     * processes. */
    seL4_CPtr user_ep = cspace_alloc_slot(&tty_test_process.cspace);
    if (user_ep == seL4_CapNull) {
        ZF_LOGE("Failed to alloc user ep slot");
        return false;
    }

    /* now mutate the cap, thereby setting the badge */
    err = cspace_mint(&tty_test_process.cspace, user_ep, global_cspace, ep, seL4_AllRights, TTY_EP_BADGE);
    if (err) {
        ZF_LOGE("Failed to mint user ep");
        return false;
    }

    /* Create a new TCB object */
    tty_test_process.tcb_ut = alloc_retype(&tty_test_process.tcb, seL4_TCBObject, seL4_TCBBits);
    if (tty_test_process.tcb_ut == NULL) {
        ZF_LOGE("Failed to alloc tcb ut");
        return false;
    }

    /* Configure the TCB */
    err = seL4_TCB_Configure(tty_test_process.tcb, user_ep,
        tty_test_process.cspace.root_cnode, seL4_NilData,
        tty_test_process.vspace, seL4_NilData, USERIPCBUFFER,
        get_cap_from_vaddr(tty_test_process.pt, USERIPCBUFFER));
    if (err != seL4_NoError) {
        ZF_LOGE("Unable to configure new TCB");
        return false;
    }

    /* Set the priority */
    err = seL4_TCB_SetPriority(tty_test_process.tcb, seL4_CapInitThreadTCB, TTY_PRIORITY);
    if (err != seL4_NoError) {
        ZF_LOGE("Unable to set priority of new TCB");
        return false;
    }

    /* Provide a name for the thread -- Helpful for debugging */
    NAME_THREAD(tty_test_process.tcb, app_name);

    /* parse the cpio image */
    ZF_LOGI("\nStarting \"%s\"...\n", app_name);
    unsigned long elf_size;
    char *elf_base = cpio_get_file(_cpio_archive, app_name, &elf_size);
    if (elf_base == NULL) {
        ZF_LOGE("Unable to locate cpio header for %s", app_name);
        return false;
    }

    /* set up the stack */
    as_define_stack(tty_test_process.as);
    seL4_Word sp = init_process_stack(global_cspace, seL4_CapInitThreadVSpace, elf_base);

    /* load the elf image from the cpio file */
    err = elf_load(global_cspace, seL4_CapInitThreadVSpace, &tty_test_process, elf_base);
    if (err) {
        ZF_LOGE("Failed to load elf image");
        return false;
    }

    /* Map in the IPC buffer for the thread */
    // err = map_frame(&cspace, tty_test_process.ipc_buffer, tty_test_process.vspace, PROCESS_IPC_BUFFER,
    //                 seL4_AllRights, seL4_ARM_Default_VMAttributes);
    if (err != 0) {
        ZF_LOGE("Unable to map IPC buffer for user app");
        return false;
    }

    /* Start the new process */
    seL4_UserContext context = {
        .pc = elf_getEntryPoint(elf_base),
        .sp = sp,
    };
    printf("Starting ttytest at %p\n", (void *)context.pc);
    err = seL4_TCB_WriteRegisters(tty_test_process.tcb, 1, 0, 2, &context);
    ZF_LOGE_IF(err, "Failed to write registers");
    return err == seL4_NoError;
}

/* Allocate an endpoint and a notification object for sos.
 * Note that these objects will never be freed, so we do not
 * track the allocated ut objects anywhere
 */
static void sos_ipc_init(seL4_CPtr *ipc_ep, seL4_CPtr *ntfn)
{
    /* Create an notification object for interrupts */
    ut_t *ut = alloc_retype(ntfn, seL4_NotificationObject, seL4_NotificationBits);
    ZF_LOGF_IF(!ut, "No memory for notification object");

    /* Bind the notification object to our TCB */
    seL4_Error err = seL4_TCB_BindNotification(seL4_CapInitThreadTCB, *ntfn);
    ZF_LOGF_IFERR(err, "Failed to bind notification object to TCB");

    /* Create an endpoint for user application IPC */
    ut = alloc_retype(ipc_ep, seL4_EndpointObject, seL4_EndpointBits);
    ZF_LOGF_IF(!ut, "No memory for endpoint");
}

static inline seL4_CPtr badge_irq_ntfn(seL4_CPtr ntfn, seL4_Word badge)
{
    /* allocate a slot */
    seL4_CPtr badged_cap = cspace_alloc_slot(global_cspace);
    ZF_LOGF_IF(badged_cap == seL4_CapNull, "Failed to allocate slot");

    /* mint the cap, which sets the badge */
    seL4_Error err = cspace_mint(global_cspace, badged_cap, global_cspace, ntfn, seL4_AllRights, badge | IRQ_EP_BADGE);
    ZF_LOGE_IFERR(err, "Failed to mint cap");

    /* return the badged cptr */
    return badged_cap;
}

/* called by crt */
seL4_CPtr get_seL4_CapInitThreadTCB(void)
{
    return seL4_CapInitThreadTCB;
}

/* tell muslc about our "syscalls", which will bve called by muslc on invocations to the c library */
void init_muslc(void)
{
    muslcsys_install_syscall(__NR_set_tid_address, sys_set_tid_address);
    muslcsys_install_syscall(__NR_writev, sys_writev);
    muslcsys_install_syscall(__NR_exit, sys_exit);
    muslcsys_install_syscall(__NR_rt_sigprocmask, sys_rt_sigprocmask);
    muslcsys_install_syscall(__NR_gettid, sys_gettid);
    muslcsys_install_syscall(__NR_getpid, sys_getpid);
    muslcsys_install_syscall(__NR_tgkill, sys_tgkill);
    muslcsys_install_syscall(__NR_tkill, sys_tkill);
    muslcsys_install_syscall(__NR_exit_group, sys_exit_group);
    muslcsys_install_syscall(__NR_ioctl, sys_ioctl);
    muslcsys_install_syscall(__NR_mmap, sys_mmap);
    muslcsys_install_syscall(__NR_brk, sys_brk);
    muslcsys_install_syscall(__NR_clock_gettime, sys_clock_gettime);
    muslcsys_install_syscall(__NR_nanosleep, sys_nanosleep);
    muslcsys_install_syscall(__NR_getuid, sys_getuid);
    muslcsys_install_syscall(__NR_getgid, sys_getgid);
    muslcsys_install_syscall(__NR_openat, sys_openat);
    muslcsys_install_syscall(__NR_close, sys_close);
    muslcsys_install_syscall(__NR_socket, sys_socket);
    muslcsys_install_syscall(__NR_bind, sys_bind);
    muslcsys_install_syscall(__NR_listen, sys_listen);
    muslcsys_install_syscall(__NR_connect, sys_connect);
    muslcsys_install_syscall(__NR_accept, sys_accept);
    muslcsys_install_syscall(__NR_sendto, sys_sendto);
    muslcsys_install_syscall(__NR_recvfrom, sys_recvfrom);
    muslcsys_install_syscall(__NR_readv, sys_readv);
    muslcsys_install_syscall(__NR_getsockname, sys_getsockname);
    muslcsys_install_syscall(__NR_getpeername, sys_getpeername);
    muslcsys_install_syscall(__NR_fcntl, sys_fcntl);
    muslcsys_install_syscall(__NR_setsockopt, sys_setsockopt);
    muslcsys_install_syscall(__NR_getsockopt, sys_getsockopt);
    muslcsys_install_syscall(__NR_ppoll, sys_ppoll);
    muslcsys_install_syscall(__NR_madvise, sys_madvise);
}

void anotherdummycallback(uint64_t id, void *data)
{
    (void)data;
    uint64_t now = timestamp_us(timestamp_get_freq());
    printf("timstamp = %ld;\t id = %ld; diff = %ld\n", now, id, now - id);
}

void dummycallback(uint64_t id, void *data)
{
    (void)data;
    uint64_t now = timestamp_us(timestamp_get_freq());
    printf("timstamp = %ld;\t id = %ld; diff = %ld\n", now, id, now - id);
    register_timer(500000, &anotherdummycallback, NULL, F, ONE_SHOT);
    register_timer(1000000, &anotherdummycallback, NULL, F, ONE_SHOT);
}

void frametable_test()
{
    /* Allocate 10 pages and make sure you can touch them all */
    for (int i = 0; i < 10; i++) {
        /* Allocate a page */
        seL4_Word vaddr;
        frame_alloc(&vaddr);
        assert(vaddr);

        /* Test you can touch the page */
        *(int *)vaddr = 0x37;
        assert(*(int *)vaddr == 0x37);

        printf("Page #%d allocated at %p\n", i, (void *)vaddr);
    }

    printf("TEST 1 past\n");

    /* Test that you never run out of memory if you always free frames. */
    for (int i = 0; i < 1000000; i++) {
        /* Allocate a page */
        seL4_Word vaddr;
        int page = frame_alloc(&vaddr);
        assert(vaddr != 0);

        /* Test you can touch the page */
        *(int *)vaddr = 0x37;
        assert(*(int *)vaddr == 0x37);

        /* print every 1000 iterations */
        if (i % 1000 == 0) {
            printf("Page #%d allocated at %p\n", i, (int *)vaddr);
        }

        frame_free(page);
    }

    printf("TEST 2 past\n");
    /* Test that you eventually run out of memory gracefully,
   and doesn't crash */
    int cnt = 0;
    while (true) {
        /* Allocate a page */
        seL4_Word vaddr;

        frame_alloc(&vaddr);
        if (!vaddr) {
            printf("Out of memory!\n");
            break;
        }
        cnt++;

        /* Test you can touch the page */
        *(int *)vaddr = 0x37;
        assert(*(int *)vaddr == 0x37);
    }

    printf("TEST 3 past\n");
    /* finally clean up all the memory allocated above */
    /* try to free them all */
    for (int i = 0; i < cnt; i++) {
        frame_free(3989 + i);
    }
}

NORETURN void *main_continued(UNUSED void *arg)
{
    /* Initialise other system compenents here */
    seL4_CPtr ipc_ep, ntfn;
    sos_ipc_init(&ipc_ep, &ntfn);

    /* run sos initialisation tests */
    run_tests(global_cspace);

    /* Map the timer device (NOTE: this is the same mapping you will use for your timer driver -
     * sos uses the watchdog timers on this page to implement reset infrastructure & network ticks,
     * so touching the watchdog timers here is not recommended!) */
    void *timer_vaddr = sos_map_device(global_cspace, PAGE_ALIGN_4K(TIMER_PADDR), PAGE_SIZE_4K);

    /* Initialise the network hardware. */
    printf("Network init\n");
    network_init(global_cspace,
        badge_irq_ntfn(ntfn, IRQ_BADGE_NETWORK_IRQ),
        badge_irq_ntfn(ntfn, IRQ_BADGE_NETWORK_TICK),
        timer_vaddr);

    start_timer(global_cspace,
        badge_irq_ntfn(ntfn, IRQ_BADGE_TIMER),
        timer_vaddr,
        F);

    /* Initialise libserial */
    vfs_bootstrap();

    // frametable_test();
    /* Start the user application */
    printf("Start first process\n");
    bool success = start_first_process(TTY_NAME, ipc_ep);
    ZF_LOGF_IF(!success, "Failed to start first process");

    printf("\nSOS entering syscall loop\n");
    syscall_loop(ipc_ep);
}
/*
 * Main entry point - called by crt.
 */
int main(void)
{
    init_muslc();

    /* bootinfo was set as an environment variable in _sel4_start */
    char *bi_string = getenv("bootinfo");
    ZF_LOGF_IF(!bi_string, "Could not parse bootinfo from env.");

    /* register the location of the unwind_tables -- this is required for
     * backtrace() to work */
    __register_frame(&__eh_frame_start);

    seL4_BootInfo *boot_info;
    if (sscanf(bi_string, "%p", &boot_info) != 1) {
        ZF_LOGF("bootinfo environment value '%s' was not valid.", bi_string);
    }

    debug_print_bootinfo(boot_info);

    printf("\nSOS Starting...\n");

    NAME_THREAD(seL4_CapInitThreadTCB, "SOS:root");

    /* Initialise the cspace manager, ut manager and dma */
    sos_bootstrap(global_cspace, boot_info);

    /* switch to the real uart to output (rather than seL4_DebugPutChar, which only works if the
     * kernel is built with support for printing, and is much slower, as each character print
     * goes via the kernel)
     *
     * NOTE we share this uart with the kernel when the kernel is in debug mode. */
    uart_init(global_cspace);
    update_vputchar(uart_putchar);

    /* test print */
    printf("SOS Started!\n");

    /* allocate a bigger stack and switch to it -- we'll also have a guard page, which makes it much
     * easier to detect stack overruns */
    seL4_Word vaddr = SOS_STACK;
    for (int i = 0; i < SOS_STACK_PAGES; i++) {
        seL4_CPtr frame_cap;
        ut_t *frame = alloc_retype(&frame_cap, seL4_ARM_SmallPageObject, seL4_PageBits);
        ZF_LOGF_IF(frame == NULL, "Failed to allocate stack page");
        seL4_Error err = map_frame(global_cspace, frame_cap, seL4_CapInitThreadVSpace,
            vaddr, seL4_AllRights, seL4_ARM_Default_VMAttributes);
        ZF_LOGF_IFERR(err, "Failed to map stack");
        vaddr += PAGE_SIZE_4K;
    }

    initialize_frame_table(global_cspace);

    utils_run_on_stack((void *)vaddr, main_continued, NULL);

    UNREACHABLE();
}
