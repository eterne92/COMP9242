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
#include <cspace/cspace.h>
#include <elf/elf.h>
#include <sel4/sel4.h>
#include <stdbool.h>
#include <string.h>
#include <utils/util.h>

#include "addrspace.h"
#include "elfload.h"
#include "frametable.h"
#include "mapping.h"
#include "proc.h"
#include "vfs/uio.h"
#include "vfs/vnode.h"
#include "vfs/vfs.h"
#include "ut.h"
#include "vmem_layout.h"
/*
 * Convert ELF permissions into seL4 permissions.
 */
static inline seL4_CapRights_t get_sel4_rights_from_elf(
    unsigned long permissions)
{
    bool canRead = permissions & PF_R || permissions & PF_X;
    bool canWrite = permissions & PF_W;

    if (!canRead && !canWrite) {
        return seL4_AllRights;
    }

    return seL4_CapRights_new(false, canRead, canWrite);
}

/*
 * Load an elf segment into the given vspace.
 *
 * TODO: The current implementation maps the frames into the loader vspace AND the target vspace
 *       and leaves them there. Additionally, if the current implementation fails, it does not
 *       clean up after itself.
 *
 *       This is insufficient, as you will run out of resouces quickly, and will be completely fixed
 *       throughout the duration of the project, as different milestones are completed.
 *
 *       Be *very* careful when editing this code. Most students will experience at least one elf-loading
 *       bug.
 *
 * The content to load is either zeros or the content of the ELF
 * file itself, or both.
 * The split between file content and zeros is a follows.
 *
 * File content: [dst, dst + file_size)
 * Zeros:        [dst + file_size, dst + segment_size)
 *
 * Note: if file_size == segment_size, there is no zero-filled region.
 * Note: if file_size == 0, the whole segment is just zero filled.
 *
 * @param cspace        of the loader, to allocate slots with
 * @param loader        vspace of the loader
 * @param loadee        vspace to load the segment in to
 * @param src           pointer to the content to load
 * @param segment_size  size of segment to load
 * @param file_size     end of section that should be zero'd
 * @param dst           destination base virtual address to load
 * @param permissions   for the mappings in this segment
 * @return
 *
 */
static int load_segment_into_vspace(cspace_t *cspace,
                                    proc *cur_proc,
                                    size_t pm_offset, size_t segment_size,
                                    size_t file_size, uintptr_t dst,
                                    seL4_CapRights_t permissions,
                                    struct vnode *elf_vn)
{
    assert(file_size <= segment_size);
    (void) cspace;
    (void) permissions;
    /* We work a page at a time in the destination vspace. */
    //unsigned int pos = 0;
    seL4_Error err = seL4_NoError;

    struct uio u_uio;
    uio_uinit(&u_uio, dst, file_size, pm_offset, UIO_READ, cur_proc);
    err = VOP_READ(elf_vn, &u_uio);

    return err;
}

int elf_load(cspace_t *cspace, seL4_CPtr loader_vspace, proc *cur_proc,
             char *elf_file, struct vnode *elf_vn)
{
    (void)loader_vspace;
    /* Ensure that the file is an elf file. */
    if (elf_file == NULL || elf_checkFile(elf_file)) {
        ZF_LOGE("Invalid elf file");
        return -1;
    }

    /* create addrspace of the process  */

    int num_headers = elf_getNumProgramHeaders(elf_file);
    for (int i = 0; i < num_headers; i++) {

        /* Skip non-loadable segments (such as debugging data). */
        if (elf_getProgramHeaderType(elf_file, i) != PT_LOAD) {
            continue;
        }

        /* Fetch information about this segment. */
        size_t pm_offset = elf_getProgramHeaderOffset(elf_file, i);
        size_t file_size = elf_getProgramHeaderFileSize(elf_file, i);
        size_t segment_size = elf_getProgramHeaderMemorySize(elf_file, i);
        uintptr_t vaddr = elf_getProgramHeaderVaddr(elf_file, i);
        seL4_Word flags = elf_getProgramHeaderFlags(elf_file, i);

        /* create regions of the process iamge */
        as_region *region = as_define_region(cur_proc->as, vaddr, segment_size,
                                             (unsigned char)flags);
        // printf("vaddr %p, flags %x\n", (void *)vaddr, flags);
        //printf("region->vaddr %p, region->flags %x\n", (void *)region->vaddr, region->flags);
        if (region == NULL) {
            ZF_LOGE("elf loading region alloc failed!");
        }

        /* Copy it across into the vspace. */
        ZF_LOGD(" * Loading segment %p-->%p\n", (void *)vaddr,
                (void *)(vaddr + segment_size));
        // printf(" * Loading segment %p-->%p\n", (void *)vaddr, (void *)(vaddr + segment_size));
        // printf("offset is %u\n", pm_offset);
        //printf("try load\n");
        int err = load_segment_into_vspace(cspace, cur_proc,
                                           pm_offset, segment_size, file_size, vaddr,
                                           get_sel4_rights_from_elf(flags), elf_vn);
        if (err) {
        ZF_LOGE("Elf loading failed!");
            return -1;
        }
    }

    return 0;
}