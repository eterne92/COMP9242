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
#include <sel4/sel4.h>
#include <sel4/sel4_arch/mapping.h>

#include "frametable.h"
#include "mapping.h"
#include "pagetable.h"
#include "ut.h"
#include "vmem_layout.h"
#include "proc.h"

/**
 * Retypes and maps a page table into the root servers page global directory
 * @param cspace that the cptrs refer to
 * @param vaddr  the virtual address of the mapping
 * @param ut     a 4k untyped object
 * @param empty  an empty slot to retype into a pt
 * @return 0 on success
 */
static seL4_Error retype_map_pt(cspace_t *cspace, seL4_CPtr vspace,
                                seL4_Word vaddr, seL4_CPtr ut, seL4_CPtr empty)
{

    seL4_Error err = cspace_untyped_retype(cspace, ut, empty,
                                           seL4_ARM_PageTableObject, seL4_PageBits);
    if (err) {
        return err;
    }

    return seL4_ARM_PageTable_Map(empty, vspace, vaddr,
                                  seL4_ARM_Default_VMAttributes);
}

/**
 * Retypes and maps a page directory into the root servers page global directory
 * @param cspace that the cptrs refer to
 * @param vaddr  the virtual address of the mapping
 * @param ut     a 4k untyped object
 * @param empty  an empty slot to retype into a pd
 * @return 0 on success
 */
static seL4_Error retype_map_pd(cspace_t *cspace, seL4_CPtr vspace,
                                seL4_Word vaddr, seL4_CPtr ut, seL4_CPtr empty)
{

    seL4_Error err = cspace_untyped_retype(cspace, ut, empty,
                                           seL4_ARM_PageDirectoryObject, seL4_PageBits);
    if (err) {
        return err;
    }

    return seL4_ARM_PageDirectory_Map(empty, vspace, vaddr,
                                      seL4_ARM_Default_VMAttributes);
}

/**
 * Retypes and maps a page upper directory into the root servers page global directory
 * @param cspace that the cptrs refer to
 * @param vaddr  the virtual address of the mapping
 * @param ut     a 4k untyped object
 * @param empty  an empty slot to retype into a pud
 * @return 0 on success
 */
static seL4_Error retype_map_pud(cspace_t *cspace, seL4_CPtr vspace,
                                 seL4_Word vaddr, seL4_CPtr ut,
                                 seL4_CPtr empty)
{

    seL4_Error err = cspace_untyped_retype(cspace, ut, empty,
                                           seL4_ARM_PageUpperDirectoryObject, seL4_PageBits);
    if (err) {
        return err;
    }
    return seL4_ARM_PageUpperDirectory_Map(empty, vspace, vaddr,
                                           seL4_ARM_Default_VMAttributes);
}

static seL4_Error map_frame_impl(cspace_t *cspace, seL4_CPtr frame_cap,
                                 seL4_CPtr vspace, seL4_Word vaddr,
                                 seL4_CapRights_t rights, seL4_ARM_VMAttributes attr,
                                 seL4_CPtr *free_slots, seL4_Word *used)
{
    /* Attempt the mapping */
    seL4_Error err = seL4_ARM_Page_Map(frame_cap, vspace, vaddr, rights, attr);
    for (size_t i = 0; i < MAPPING_SLOTS && err == seL4_FailedLookup; i++) {
        /* save this so nothing else trashes the message register value */
        seL4_Word failed = seL4_MappingFailedLookupLevel();

        /* Assume the error was because we are missing a paging structure */
        ut_t *ut = ut_alloc_4k_untyped(NULL);
        if (ut == NULL) {
            err = try_swap_out();
            if (err) {
                return err;
            }
            // printf("map_frame_impl\n");
            ut = ut_alloc_4k_untyped(NULL);
            // ZF_LOGE("Out of 4k untyped");
            // return -1;
            if (ut == NULL) return -1;
        }

        /* figure out which cptr to use to retype into*/
        seL4_CPtr slot;
        if (used != NULL) {
            slot = free_slots[i];
            *used |= BIT(i);
        } else {
            slot = cspace_alloc_slot(cspace);
        }

        if (slot == seL4_CapNull) {
            ZF_LOGE("No cptr to alloc paging structure");
            return -1;
        }

        switch (failed) {
        case SEL4_MAPPING_LOOKUP_NO_PT:
            err = retype_map_pt(cspace, vspace, vaddr, ut->cap, slot);
            break;
        case SEL4_MAPPING_LOOKUP_NO_PD:
            err = retype_map_pd(cspace, vspace, vaddr, ut->cap, slot);
            break;

        case SEL4_MAPPING_LOOKUP_NO_PUD:
            err = retype_map_pud(cspace, vspace, vaddr, ut->cap, slot);
            break;
        }

        if (!err) {
            /* Try the mapping again */
            err = seL4_ARM_Page_Map(frame_cap, vspace, vaddr, rights, attr);
        }
    }

    return err;
}

seL4_Error map_frame_cspace(cspace_t *cspace, seL4_CPtr frame_cap,
                            seL4_CPtr vspace, seL4_Word vaddr,
                            seL4_CapRights_t rights, seL4_ARM_VMAttributes attr,
                            seL4_CPtr free_slots[MAPPING_SLOTS], seL4_Word *used)
{
    if (cspace == NULL) {
        ZF_LOGE("Invalid arguments");
        return -1;
    }
    return map_frame_impl(cspace, frame_cap, vspace, vaddr, rights, attr,
                          free_slots, used);
}

seL4_Error map_frame(cspace_t *cspace, seL4_CPtr frame_cap, seL4_CPtr vspace,
                     seL4_Word vaddr, seL4_CapRights_t rights,
                     seL4_ARM_VMAttributes attr)
{
    return map_frame_impl(cspace, frame_cap, vspace, vaddr, rights, attr, NULL,
                          NULL);
}

seL4_Error sos_map_frame(cspace_t *cspace, int frame, proc *cur_proc,
                         seL4_Word vaddr, seL4_CapRights_t rights,
                         seL4_ARM_VMAttributes attr)
{
    seL4_Word page_table = (seL4_Word)cur_proc->pt;
    seL4_CPtr vspace = cur_proc->vspace;

    if (frame < 0) return seL4_NotEnoughMemory;

    /* allign vaddr */
    vaddr = vaddr & PAGE_FRAME;

    /* copy frame_cap into a new cap */
    seL4_CPtr origin_cap = frame_table.frames[frame].frame_cap;
    // printf("FRAME_CAP is %ld\n", origin_cap);
    seL4_CPtr frame_cap = cspace_alloc_slot(cspace);
    if (frame_cap == seL4_CapNull) {
        ZF_LOGE("OUT OF CAP\n");
    }
    seL4_Error err = cspace_copy(cspace, frame_cap, cspace, origin_cap, rights);
    if (err) {
        ZF_LOGE("FAILE TO COPY CAP, SOMETHING WRONG!");
    }
    err = seL4_ARM_Page_Map(frame_cap, vspace, vaddr, rights, attr);


    page_table_entry entry;
    ut_t *ut_array[MAPPING_SLOTS] = { 0, 0, 0 };
    int frame_array[MAPPING_SLOTS] = { -1, -1, -1 };
    seL4_CPtr slot_array[MAPPING_SLOTS] = { 0, 0, 0 };
    /* keep track of all allocated resources in case that allocation failed in some intermediate steps */
    for (size_t i = 0; i < MAPPING_SLOTS && err == seL4_FailedLookup; i++) {
        /* save this so nothing else trashes the message register value */
        seL4_Word failed = seL4_MappingFailedLookupLevel();

        /* Assume the error was because we are missing a paging structure */
        ut_t *ut = ut_alloc_4k_untyped(NULL);
        if (ut == NULL) {
            // ZF_LOGE("Out of 4k untyped");
            // err = -1;
            // goto cleanup;
            seL4_Error err = try_swap_out();
            if (err) {
                goto cleanup;
            }
            // printf("sos map\n");
            ut = ut_alloc_4k_untyped(NULL);
            if (ut == NULL) {
                goto cleanup;
            }
        }

        /* figure out which cptr to use to retype into*/
        seL4_CPtr slot = cspace_alloc_slot(cspace);

        if (slot == seL4_CapNull) {
            ZF_LOGE("No cptr to alloc paging structure");
            err = -1;
            goto cleanup;
        }
        seL4_Word page_table_addr; /* base addr of the shadow page table */

        slot_array[i] = slot;
        ut_array[i] = ut;

        /* fill up the pt */
        int page_frame, level;
        // printf("failed %d\n", failed);
        switch (failed) {
        case SEL4_MAPPING_LOOKUP_NO_PT:
            // printf("level 4\n");
            // level 4
            level = 4;
            err = retype_map_pt(cspace, vspace, vaddr, ut->cap, slot);
            /* only need two page in level 4 shadow page table */
            page_frame = frame_n_alloc(&page_table_addr, 2);
            frame_array[i] = page_frame;
            if (page_frame == -1) {
                goto cleanup;
            }
            entry.frame = -1;
            break;
        case SEL4_MAPPING_LOOKUP_NO_PD:
            // printf("level 3\n");
            // level 3
            level = 3;
            err = retype_map_pd(cspace, vspace, vaddr, ut->cap, slot);
            // allocate frame to keep track of level 3 shadow page table entry
            page_frame = frame_n_alloc(&page_table_addr, PAGE_TABLE_FRAME_SIZE);
            frame_array[i] = page_frame;
            if (page_frame == -1) {
                goto cleanup;
            }
            entry.frame = -1;
            break;
        case SEL4_MAPPING_LOOKUP_NO_PUD:
            // printf("level 2\n");
            // level 2
            level = 2;
            err = retype_map_pud(cspace, vspace, vaddr, ut->cap, slot);
            // allocate frame to keep track of level 3 shadow page table entry
            page_frame = frame_n_alloc(&page_table_addr, PAGE_TABLE_FRAME_SIZE);
            frame_array[i] = page_frame;
            if (page_frame == -1) {
                goto cleanup;
            }
            entry.frame = -1;
            break;
        }
        entry.slot = slot;
        entry.table_addr = page_table_addr;
        entry.ut = ut;
        insert_page_table_entry((page_table_t *)page_table, &entry, level, vaddr);
        if (!err) {
            /* Try the mapping again */
            err = seL4_ARM_Page_Map(frame_cap, vspace, vaddr, rights, attr);
        }
    }
    if (!err) {
        entry.frame = frame;
        entry.slot = frame_cap;
        update_level_4_page_table_entry((page_table_t *)page_table, &entry, vaddr);
        SET_PID(frame, cur_proc->status.pid);
        return err;
    }
cleanup:
    /* clean up all the resouces */
    for (size_t i = 0; i < MAPPING_SLOTS; ++i) {
        if (ut_array[i]) {
            ut_free(ut_array[i], seL4_PageBits);
        }
        if (frame_array[i] != -1) {
            frame_n_free(frame_array[i]);
        }
        if (slot_array[i]) {
            cspace_delete(cspace, slot_array[i]);
            cspace_free_slot(cspace, slot_array[i]);
        }
    }
    return err;
}

static uintptr_t device_virt = SOS_DEVICE_START;

void *sos_map_device(cspace_t *cspace, uintptr_t addr, size_t size)
{
    assert(cspace != NULL);
    void *vstart = (void *)device_virt;

    for (uintptr_t curr = addr; curr < (addr + size); curr += PAGE_SIZE_4K) {
        ut_t *ut = ut_alloc_4k_device(curr);
        if (ut == NULL) {
            ZF_LOGE("Failed to find ut for phys address %p", (void *)curr);
            return NULL;
        }

        /* allocate a slot to retype into */
        seL4_CPtr frame = cspace_alloc_slot(cspace);
        if (frame == seL4_CapNull) {
            ZF_LOGE("Out of caps");
            return NULL;
        }

        /* retype */
        seL4_Error err = cspace_untyped_retype(cspace, ut->cap, frame,
                                               seL4_ARM_SmallPageObject,
                                               seL4_PageBits);
        if (err != seL4_NoError) {
            ZF_LOGE("Failed to retype %lx", ut->cap);
            cspace_free_slot(cspace, frame);
            return NULL;
        }

        /* map */
        err = map_frame(cspace, frame, seL4_CapInitThreadVSpace, device_virt,
                        seL4_AllRights, false);
        if (err != seL4_NoError) {
            ZF_LOGE("Failed to map device frame at %p", (void *)device_virt);
            cspace_delete(cspace, frame);
            cspace_free_slot(cspace, frame);
            return NULL;
        }

        device_virt += PAGE_SIZE_4K;
    }

    return vstart;
}
