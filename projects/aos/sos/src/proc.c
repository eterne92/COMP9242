#include "proc.h"
#include "addrspace.h"
#include "frametable.h"
#include "syscall/syscall.h"
#include <cpio/cpio.h>
#include <clock/timestamp.h>
#include <cspace/cspace.h>
#include <aos/sel4_zf_logif.h>
#include <aos/debug.h>
#include <stdbool.h>
#include "elfload.h"
#include "mapping.h"
#include "pagetable.h"
#include "syscall/filetable.h"
#include "vfs/uio.h"
#include "vfs/vfs.h"
#include "vfs/vnode.h"
#include <fcntl.h>
#include <picoro/picoro.h>
#include <string.h>

#define DEFAULT_PRIORITY (0)

static proc process_array[PROCESS_ARRAY_SIZE];

static proc *cur_proc;

static int available_pid = 0;
static int kill_lock = 0;

static int get_next_available_pid(void)
{
    int pid = -1;
    bool full = true;
    for (int i = 0; i < PROCESS_ARRAY_SIZE; ++i) {
        pid = (i + available_pid) % PROCESS_ARRAY_SIZE;
        if (process_array[pid].state == DEAD) {
            available_pid = pid + 1;
            full = false;
            break;
        }
    }
    if (full) {
        return -1;
    }

    return pid;
}

// void set_cur_proc(proc *p)
// {
//     cur_proc = p;
// }

// proc *get_cur_proc(void)
// {
//     return cur_proc;
// }

proc *get_process(int pid)
{
    int index = pid % PROCESS_ARRAY_SIZE;
    // if(process_array[index].status.pid != pid){
    //     printf("%d %d not same\n", process_array[index].status.pid, pid);
    //     return NULL;
    // }
    return &process_array[index];
}

/* helper to allocate a ut + cslot, and retype the ut into the cslot */
static ut_t *alloc_retype(seL4_CPtr *cptr, seL4_Word type, size_t size_bits)
{
    seL4_Error err;
    /* Allocate the object */
    ut_t *ut = ut_alloc(size_bits, global_cspace);
    if (ut == NULL) {
        err = try_swap_out();
        if (err) {
            ZF_LOGE("No memory for object of size %zu", size_bits);
            return NULL;
        }
        ut = ut_alloc(size_bits, global_cspace);
        if (ut == NULL) {
            ZF_LOGE("No memory for object of size %zu", size_bits);
            return NULL;
        }
    }

    /* allocate a slot to retype the memory for object into */
    *cptr = cspace_alloc_slot(global_cspace);
    if (*cptr == seL4_CapNull) {
        ut_free(ut, size_bits);
        ZF_LOGE("Failed to allocate slot");
        return NULL;
    }

    /* now do the retype */
    err = cspace_untyped_retype(global_cspace, ut->cap, *cptr, type,
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
static uintptr_t init_process_stack(int pid, cspace_t *cspace, char *elf_file,
                                    struct vnode *elf_vn)
{
    /* Create a stack frame */
    seL4_Error err;
    proc *process = &process_array[pid];
    int frame = frame_alloc(NULL);
    err = sos_map_frame(cspace, frame, process,
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

    /* since sysinfo is at some very high offset, do it by hand */
    struct Elf64_Header *fileHdr = (struct Elf64_Header *) elf_file;
    struct Elf64_Shdr sections[fileHdr->e_shentsize];
    struct uio k_uio;
    uio_kinit(&k_uio, sections, sizeof(Elf64_Shdr) * fileHdr->e_shentsize,
              fileHdr->e_shoff, UIO_READ);
    VOP_READ(elf_vn, &k_uio);

    printf("%d shstrndx\n", fileHdr->e_shstrndx);

    size_t string_table_offset = sections[fileHdr->e_shstrndx].sh_offset;
    char str[4096];
    uio_kinit(&k_uio, str, 4096, string_table_offset, UIO_READ);

    VOP_READ(elf_vn, &k_uio);
    size_t sysinfo_offset;

    for (int i = 0; i < fileHdr->e_shentsize; i++) {
        if (strcmp("__vsyscall", (char *) str + sections[i].sh_name) == 0) {
            printf("__vsyscall find %d\n", i);
            sysinfo_offset = sections[i].sh_offset;
            break;
        }
    }

    printf("%p\n", sysinfo_offset);
    uintptr_t sysinfo;
    uio_kinit(&k_uio, &sysinfo, sizeof(uintptr_t), sysinfo_offset, UIO_READ);
    VOP_READ(elf_vn, &k_uio);

    printf("read done\n");

    /* find the vsyscall table */
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

/* set up System V ABI compliant stack, so that the process can
 * start up and initialise the C library */
static uintptr_t cpio_init_process_stack(int pid, cspace_t *cspace,
        char *elf_file)
{
    /* Create a stack frame */
    seL4_Error err;
    proc *process = &process_array[pid];
    int frame = frame_alloc(NULL);
    err = sos_map_frame(cspace, frame, process,
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
    *ret_pid = pid;
    if (pid == -1)
        return false;
    proc *process = &process_array[pid];
    process->status.pid = pid;
    process->status.size = 0;


    printf("load elf\n");

    char elf_base[4096];
    struct vnode *elf_vn;

    int ret = vfs_open(app_name, O_RDONLY, 0, &elf_vn);
    if (ret) {
        return false;
    }

    printf("%p\n", elf_vn);

    struct uio k_uio;
    uio_kinit(&k_uio, elf_base, 4096, 0, UIO_READ);
    ret = VOP_READ(elf_vn, &k_uio);
    if (ret) {
        return ret;
    }

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
    err = sos_map_frame(global_cspace, frame, process, USERIPCBUFFER,
                        seL4_ReadWrite,
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
    process->user_endpoint = user_ep;

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
    // ZF_LOGI("\nStarting \"%s\"...\n", app_name);
    unsigned long elf_size;
    char *cpio_elf_base = cpio_get_file(_cpio_archive, app_name, &elf_size);
    if (elf_base == NULL) {
        ZF_LOGE("Unable to locate cpio header for %s", app_name);
        return false;
    }

    /* set up the stack */
    as_define_stack(process->as);
    seL4_Word sp = init_process_stack(pid, global_cspace, elf_base, elf_vn);
    // seL4_Word sp = cpio_init_process_stack(pid, global_cspace, cpio_elf_base);

    /* load the elf image from the cpio file */
    err = elf_load(global_cspace, seL4_CapInitThreadVSpace, process, elf_base,
                   elf_vn);
    // err = cpio_elf_load(global_cspace, seL4_CapInitThreadVSpace, process, cpio_elf_base, elf_vn);
    printf("elf load finished\n");
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

    // as_define_heap(process->as);

    /* open stdin, stdout, stderr */
    _sys_do_open(process, "console", 1, 1);
    _sys_do_open(process, "console", 1, 2);
    process->state = ACTIVE;
    process->waiting_pid = -99;
    process->c = 0;
    process->status.stime = get_now_since_boot();
    strcpy(process->status.command, app_name);
    vfs_close(elf_vn);
    return err == seL4_NoError;
}

void kill_process(int pid)
{
    while (kill_lock) yield(NULL);
    kill_lock = 1;
    proc *process = get_process(pid);
    if (!process) return;

    process->state = INACTIVE;
    // abort syscall
    if (resumable(process->c)) {
        resume(process->c, 1);
    }
    printf("try suspend\n");
    if (process->tcb != seL4_CapNull) seL4_TCB_Suspend(process->tcb);

    printf("try destroy regions\n");
    if (process->as) destroy_regions(process->as, process);

    printf("try destroy pt\n");
    if (process->pt) destroy_page_table(process->pt);


    printf("try destroy ft\n");
    if (process->openfile_table) filetable_destroy(process->openfile_table);

    if (process->user_endpoint != seL4_CapNull) {
        // cspace_revoke(global_cspace, ipc_ep);
        cspace_delete(&process->cspace, process->user_endpoint);
        cspace_free_slot(&process->cspace, process->user_endpoint);
    }

    if (process->vspace_ut) {
        ut_free(process->vspace_ut, seL4_PGDBits);
        if (process->vspace != seL4_CapNull) {
            cspace_delete(global_cspace, process->vspace);
            cspace_free_slot(global_cspace, process->vspace);
        }
    }

    printf("try destroy tcb\n");
    if (process->tcb_ut) {
        ut_free(process->tcb_ut, seL4_TCBBits);
        if (process->tcb != seL4_CapNull) {
            cspace_delete(global_cspace, process->tcb);
            cspace_free_slot(global_cspace, process->tcb);
        }
    }

    if (process->cspace.bootstrap)
        cspace_destroy(&process->cspace);
    process->state = DEAD;
    process->status.size = 0;
    process->status.pid = -1;
    process->status.stime = 0;
    process->waiting_pid = -99;
    process->c = 0;
    process->reply = seL4_CapNull;
    kill_lock = 0;
    printf("all done\n");
}