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
#include "syscalls.h"
#include "syscall/filetable.h"
#include "syscall/syscall.h"
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

typedef  void *(*coro_t)(void *);
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

seL4_CPtr ipc_ep;

/* the one process we start */



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
    seL4_Error err = cspace_untyped_retype(global_cspace, ut->cap, *cptr, type,
                                           size_bits);
    ZF_LOGE_IFERR(err, "Failed retype untyped");
    if (err != seL4_NoError) {
        ut_free(ut, size_bits);
        cspace_free_slot(global_cspace, *cptr);
        return NULL;
    }

    return ut;
}

// static int stack_write(seL4_Word *mapped_stack, int index, uintptr_t val)
// {
//     mapped_stack[index] = val;
//     return index - 1;
// }

// /* set up System V ABI compliant stack, so that the process can
//  * start up and initialise the C library */
// static uintptr_t init_process_stack(cspace_t *cspace, seL4_CPtr local_vspace, char *elf_file)
// {
//     /* Create a stack frame */
//     seL4_Error err;
//     int frame = frame_alloc(NULL);
//     err = sos_map_frame(cspace, frame, (seL4_Word)tty_test_process.pt, tty_test_process.vspace,
//         USERSTACKTOP - PAGE_SIZE_4K, seL4_ReadWrite,
//         seL4_ARM_Default_VMAttributes);
//     // tty_test_process.stack_ut = alloc_retype(&tty_test_process.stack, seL4_ARM_SmallPageObject, seL4_PageBits);
//     if (err != seL4_NoError) {
//         ZF_LOGE("Failed to allocate stack");
//         return 0;
//     }

//     /* virtual addresses in the target process' address space */
//     uintptr_t stack_top = USERSTACKTOP;
//     uintptr_t stack_bottom = stack_top - PAGE_SIZE_4K;
//     /* virtual addresses in the SOS's address space */
//     uintptr_t local_stack_bottom = (uintptr_t)(get_frame_from_vaddr(tty_test_process.pt, stack_bottom) * PAGE_SIZE_4K + FRAME_BASE);
//     void *local_stack_top =   (void *) (local_stack_bottom + PAGE_SIZE_4K);


//     /* find the vsyscall table */
//     uintptr_t sysinfo = *((uintptr_t *)elf_getSectionNamed(elf_file, "__vsyscall", NULL));
//     if (sysinfo == 0) {
//         ZF_LOGE("could not find syscall table for c library");
//         return 0;
//     }

//     int index = -2;

//     /* null terminate the aux vectors */
//     index = stack_write(local_stack_top, index, 0);
//     index = stack_write(local_stack_top, index, 0);

//     /* write the aux vectors */
//     index = stack_write(local_stack_top, index, PAGE_SIZE_4K);
//     index = stack_write(local_stack_top, index, AT_PAGESZ);

//     index = stack_write(local_stack_top, index, sysinfo);
//     index = stack_write(local_stack_top, index, AT_SYSINFO);

//     /* null terminate the environment pointers */
//     index = stack_write(local_stack_top, index, 0);

//     /* we don't have any env pointers - skip */

//     /* null terminate the argument pointers */
//     index = stack_write(local_stack_top, index, 0);

//     /* no argpointers - skip */

//     /* set argc to 0 */
//     stack_write(local_stack_top, index, 0);

//     /* adjust the initial stack top */
//     stack_top += (index * sizeof(seL4_Word));

//     /* the stack *must* remain aligned to a double word boundary,
//      * as GCC assumes this, and horrible bugs occur if this is wrong */
//     assert(index % 2 == 0);
//     assert(stack_top % (sizeof(seL4_Word) * 2) == 0);


//     return stack_top;
// }

/* Start the first process, and return true if successful
 *
 */
void *_start_process(char *app_name)
{
    int pid;
    start_process(app_name, ipc_ep, &pid);
    return (void *)pid;
}

void start_first_process(char *app_name)
{
    coro c = coroutine((coro_t)_start_process);
    resume(c, app_name);
    create_coroutine(c);
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
    seL4_Error err = cspace_mint(global_cspace, badged_cap, global_cspace, ntfn,
                                 seL4_AllRights, badge | IRQ_EP_BADGE);
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


NORETURN void *main_continued(UNUSED void *arg)
{
    /* Initialise other system compenents here */
    seL4_CPtr ntfn;
    sos_ipc_init(&ipc_ep, &ntfn);

    /* run sos initialisation tests */
    run_tests(global_cspace);

    /* Map the timer device (NOTE: this is the same mapping you will use for your timer driver -
     * sos uses the watchdog timers on this page to implement reset infrastructure & network ticks,
     * so touching the watchdog timers here is not recommended!) */
    void *timer_vaddr = sos_map_device(global_cspace, PAGE_ALIGN_4K(TIMER_PADDR),
                                       PAGE_SIZE_4K);

    /* Initialise the network hardware. */
    // printf("Network init\n");
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
    init_pcb();

    // frametable_test();
    /* Start the user application */
    // printf("Start first process\n");
    // pid_t pid;
    start_first_process(TTY_NAME);

    // printf("\nSOS entering syscall loop\n");
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

    // printf("\nSOS Starting...\n");

    set_boottime();

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
    // printf("SOS Started!\n");

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
