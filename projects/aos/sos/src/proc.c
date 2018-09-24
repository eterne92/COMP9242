#include "proc.h"
#include "addrspace.h"
#include "frametable.h"
#include "syscall/syscall.h"
#include <cpio/cpio.h>
#include <cspace/cspace.h>
#include <aos/sel4_zf_logif.h>
#include <aos/debug.h>
#include <stdbool.h>
#include "elfload.h"
#include "mapping.h"
#include "pagetable.h"
#include "syscall/filetable.h"

#define SIZE 32

#define DEFAULT_PRIORITY (0)

#define GET_BIT(number, bit) (((number) >> (bit)) & 1)
#define SET_BIT(number, bit) ((number) |= (1 << (bit)))
#define RST_BIT(number, bit) ((number) &= ~(1 << (bit)))

proc process_array[SIZE];

static int available_pid = 0;

static int get_next_available_pid(void)
{
    int pid = -1;
    for (int i = 0; i < SIZE; ++i) {
        pid = (i + available_pid) % SIZE;
        if (process_array[pid].state == DEAD) {
            available_pid = pid + 1;
            break;
        }
    }
    return pid;
}

void set_cur_proc(proc *p)
{
    cur_proc = p;
}

proc *get_cur_proc(void)
{
    return cur_proc;
}

proc *get_process(int pid)
{
    if (pid < 0 || pid > 32)
        return NULL;
    return &process_array[pid];
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

static int stack_write(seL4_Word *mapped_stack, int index, uintptr_t val)
{
    mapped_stack[index] = val;
    return index - 1;
}

/* set up System V ABI compliant stack, so that the process can
 * start up and initialise the C library */
static uintptr_t init_process_stack(int pid, cspace_t *cspace, char *elf_file)
{
    /* Create a stack frame */
    seL4_Error err;
    proc *process = &process_array[pid];
    int frame = frame_alloc(NULL);
    err = sos_map_frame(cspace, frame, (seL4_Word)process->pt, process->vspace,
                        USERSTACKTOP - PAGE_SIZE_4K, seL4_ReadWrite,
                        seL4_ARM_Default_VMAttributes);

    if (err != seL4_NoError) {
        ZF_LOGE("Failed to allocate stack");
        return 0;
    }

    /* virtual addresses in the target process' address space */
    uintptr_t stack_top = USERSTACKTOP;
    uintptr_t stack_bottom = stack_top - PAGE_SIZE_4K;
    /* virtual addresses in the SOS's address space */
    int offset = get_frame_from_vaddr(process->pt, stack_bottom) * PAGE_SIZE_4K;
    uintptr_t local_stack_bottom = (uintptr_t)(offset + FRAME_BASE);
    void *local_stack_top = (void *)(local_stack_bottom + PAGE_SIZE_4K);

    /* find the vsyscall table */
    uintptr_t sysinfo = *((uintptr_t *)elf_getSectionNamed(elf_file, "__vsyscall",
                          NULL));
    if (sysinfo == 0) {
        ZF_LOGE("could not find syscall table for c library");
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

    return stack_top;
}

/* Start the first process, and return true if successful
 *
 * This function will leak memory if the process does not start successfully.
 * TODO: avoid leaking memory once you implement real processes, otherwise a user
 *       can force your OS to run out of memory by creating lots of failed processes.
 */
bool start_process(char *app_name, seL4_CPtr ep, int *ret_pid)
{
    int frame;
    int pid = get_next_available_pid();
    if (pid == -1)
        return false;
    proc *process = &process_array[pid];
    /* Create a VSpace */
    process->vspace_ut = alloc_retype(&(process->vspace),
                                      seL4_ARM_PageGlobalDirectoryObject, seL4_PGDBits);
    if (process->vspace_ut == NULL) {
        return false;
    }

    /* assign the vspace to an asid pool */
    seL4_Word err = seL4_ARM_ASIDPool_Assign(seL4_CapInitThreadASIDPool,
                    process->vspace);
    if (err != seL4_NoError) {
        ZF_LOGE("Failed to assign asid pool");
        return false;
    }

    /* create addrspace of ttytest */
    process->as = addrspace_init();
    if (!process->as) {
        ZF_LOGE("Failed to create address space");
        return false;
    }
    /* initialize level 1 shadow page table */
    process->pt = initialize_page_table();
    if (!process->pt) {
        ZF_LOGE("Failed to create shadow global page directory");
        return false;
    }
    /* Create a simple 1 level CSpace */

    err = cspace_create_one_level(global_cspace, &process->cspace);
    if (err != CSPACE_NOERROR) {
        ZF_LOGE("Failed to create cspace");
        return false;
    }

    /* Create open file table */
    process->openfile_table = filetable_create();

    /* Create an IPC buffer */

    as_define_ipcbuffer(process->as);
    frame = frame_alloc(NULL);
    err = sos_map_frame(global_cspace, frame, (seL4_Word)process->pt,
                        process->vspace, USERIPCBUFFER, seL4_ReadWrite,
                        seL4_ARM_Default_VMAttributes);

    if (err != seL4_NoError) {
        ZF_LOGE("Failed to alloc ipc buffer ut");
        return false;
    }

    /* allocate a new slot in the target cspace which we will mint a badged endpoint cap into --
     * the badge is used to identify the process, which will come in handy when you have multiple
     * processes. */
    seL4_CPtr user_ep = cspace_alloc_slot(&(process->cspace));
    if (user_ep == seL4_CapNull) {
        ZF_LOGE("Failed to alloc user ep slot");
        return false;
    }

    /* now mutate the cap, thereby setting the badge */
    /* badge is process id */
    err = cspace_mint(&(process->cspace), user_ep, global_cspace, ep,
                      seL4_AllRights, pid);
    if (err) {
        ZF_LOGE("Failed to mint user ep");
        return false;
    }

    /* Create a new TCB object */
    process->tcb_ut = alloc_retype(&(process->tcb), seL4_TCBObject, seL4_TCBBits);
    if (process->tcb_ut == NULL) {
        ZF_LOGE("Failed to alloc tcb ut");
        return false;
    }

    /* Configure the TCB */
    err = seL4_TCB_Configure(process->tcb, user_ep,
                             process->cspace.root_cnode, seL4_NilData,
                             process->vspace, seL4_NilData, USERIPCBUFFER,
                             get_cap_from_vaddr(process->pt, USERIPCBUFFER));

    if (err != seL4_NoError) {
        ZF_LOGE("Unable to configure new TCB");
        return false;
    }

    /* Set the priority */
    err = seL4_TCB_SetPriority(process->tcb, seL4_CapInitThreadTCB,
                               DEFAULT_PRIORITY);
    if (err != seL4_NoError) {
        ZF_LOGE("Unable to set priority of new TCB");
        return false;
    }

    /* Provide a name for the thread -- Helpful for debugging */
    NAME_THREAD(process->tcb, app_name);

    /* parse the cpio image */
    ZF_LOGI("\nStarting \"%s\"...\n", app_name);
    unsigned long elf_size;
    char *elf_base = cpio_get_file(_cpio_archive, app_name, &elf_size);
    if (elf_base == NULL) {
        ZF_LOGE("Unable to locate cpio header for %s", app_name);
        return false;
    }

    /* set up the stack */
    as_define_stack(process->as);
    seL4_Word sp = init_process_stack(pid, global_cspace, elf_base);

    /* load the elf image from the cpio file */
    err = elf_load(global_cspace, seL4_CapInitThreadVSpace, process, elf_base);
    if (err) {
        ZF_LOGE("Failed to load elf image");
        return false;
    }

    if (err != 0) {
        ZF_LOGE("Unable to map IPC buffer for user app");
        return false;
    }

    /* Start the new process */
    seL4_UserContext context = {
        .pc = elf_getEntryPoint(elf_base),
        .sp = sp,
    };

    err = seL4_TCB_WriteRegisters(process->tcb, 1, 0, 2, &context);
    ZF_LOGE_IF(err, "Failed to write registers");
    /* open stdin, stdout, stderr */
    _sys_do_open(process, "console", 1, 1);
    _sys_do_open(process, "console", 1, 2);
    *ret_pid = pid;
    return err == seL4_NoError;
}

void kill_process(int pid)
{
    proc *process = get_process(pid);
    if (!prcess) return;
    destroy_regions(process->as);
    destroy_page_table(process->pt);
    filetable_destroy(process->openfile_table);
    cspace_destroy(&process->cspace);
    cspace_delete(global_cspace, process->tcb);
    cspace_free_slot(global_cspace, process->tcb);
    ut_free(process->tcb_ut, seL4_TCBBits);
    cspace_delete(global_cspace, process->vspace);
    cspace_free_slot(global_cspace, process->vspace);
    ut_free(process->vspace_ut, seL4_PGDBits);
    process->state = DEAD;
    
}