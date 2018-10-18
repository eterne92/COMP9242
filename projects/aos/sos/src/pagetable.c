#include "pagetable.h"
#include "addrspace.h"
#include "frametable.h"
#include "mapping.h"
#include "proc.h"
#include "backtrace.h"

#include <string.h>

#define PRESENT  (1lu << 50)
#define PAGE_RW  (1lu << 51)
#define UNMAPPED (1lu << 52)
#define OFFSET 0xffffffffffff

extern cspace_t *global_cspace;

typedef struct page_table {
    seL4_Word page_obj_addr[PAGE_TABLE_SIZE];
} page_table_t;

typedef struct page_table_cap {
    seL4_Word cap[PAGE_TABLE_SIZE];
} page_table_cap;

typedef struct page_table_ut {
    ut_t *ut[PAGE_TABLE_SIZE];
} page_table_ut;


/* find out the frame contains the ut / cap */
static page_table_cap *get_page_table_cap(seL4_Word page_table)
{
    int frame = (page_table - FRAME_BASE) / PAGE_SIZE_4K;
    int cap_frame = frame_table.frames[frame].next;
    return (page_table_cap *)(cap_frame * PAGE_SIZE_4K + FRAME_BASE);
}

static page_table_ut *get_page_table_ut(seL4_Word page_table)
{
    int frame = (page_table - FRAME_BASE) / PAGE_SIZE_4K;
    int cap_frame = frame_table.frames[frame].next;
    int ut_frame = frame_table.frames[cap_frame].next;
    return (page_table_ut *)(ut_frame * PAGE_SIZE_4K + FRAME_BASE);
}

static int get_offset(seL4_Word vaddr, int n)
{
    seL4_Word mask = 0xff8000000000 >> (9 * (n - 1));
    seL4_Word offset = (mask & vaddr) >> (48 - 9 * n);
    return (int)offset;
}

static seL4_Word get_n_level_table(seL4_Word page_table, seL4_Word vaddr, int n)
{
    /* page_table is the 1st level, so we start from 2nd level */
    page_table_t *pt = (page_table_t *)page_table;
    for (int i = 1; i < n; i++) {
        int offset = get_offset(vaddr, i);
        pt = (page_table_t *)pt->page_obj_addr[offset];
        if (pt == NULL) {
            return 0;
        }
    }

    return (seL4_Word)pt;
}

page_table_t *initialize_page_table(void)
{
    seL4_Word page_table_addr;
    int page_frame = frame_n_alloc(&page_table_addr, 3);
    return page_frame != -1 ? (page_table_t *)page_table_addr : NULL;
}

seL4_Error insert_page_table_entry(page_table_t *table, page_table_entry *entry,
                                   int level, seL4_Word vaddr)
{
    int offset;
    page_table_t *pt;
    page_table_cap *pt_cap;
    page_table_ut *pt_ut;

    /* save nth level hardware page table caps in nth-1 level shadow page table */
    pt = (page_table_t *)get_n_level_table((seL4_Word)table, vaddr, level - 1);
    pt_cap = (page_table_cap *)get_page_table_cap((seL4_Word)pt);
    pt_ut = (page_table_ut *)get_page_table_ut((seL4_Word)pt);
    offset = get_offset(vaddr, level - 1);
    pt->page_obj_addr[offset] = entry->table_addr;
    pt_cap->cap[offset] = entry->slot;
    pt_ut->ut[offset] = entry->ut;

    return seL4_NoError;
}


seL4_Error handle_page_fault(proc *cur_proc, seL4_Word vaddr,
                             seL4_Word fault_info)
{
    // need to figure out which process triggered the page fault
    // right now, there is only one process (tty_test)
    (void)fault_info;
    seL4_Word frame;
    as_region *region = cur_proc->as->regions;
    bool execute, read, write;
    seL4_Error err;

    region = cur_proc->as->regions;
    // printf("handle page fault for vaddr %p\n", vaddr);
    while (region) {
        if (vaddr >= region->vaddr && vaddr < region->vaddr + region->size) {
            execute = region->flags & RG_X;
            read = region->flags & RG_R;
            write = region->flags & RG_W;
            // write to a read-only page
            frame = _get_frame_from_vaddr(cur_proc->pt, vaddr);
            if (frame == 0) {
                /* it's a vm fault without page */
                // allocate a frame
                int frame = frame_alloc(NULL);
                if (frame <= 0) {
                    // printf("not enough mem\n");
                    return -1;
                }
                // map it
                err = sos_map_frame(global_cspace, frame, cur_proc,
                                    vaddr, seL4_CapRights_new(execute, read, write), seL4_ARM_Default_VMAttributes);
                // update process_status->size
                ++cur_proc->status.size;
            } else if ((frame & PRESENT) && (frame & UNMAPPED))  {
                /* the page is still there and is not swapped*/
                frame = frame & OFFSET;
                err = sos_map_frame(global_cspace, frame, cur_proc,
                                    vaddr, seL4_CapRights_new(execute, read, write), seL4_ARM_Default_VMAttributes);

            } else if ((frame & PRESENT) && (frame & UNMAPPED) == false) {
                // write on read-only page segmentation fault
                // printf("write on read only\n");
                return seL4_RangeError;
            } else if (!(frame & PRESENT)) {
                // page is in swapping file
                //seL4_Word offset = frame & OFFSET;
                int frame_handle = frame_alloc(NULL);
                if (frame_handle <= 0) {
                    return -1;
                }
                err = load_page(cur_proc, vaddr, frame_handle * PAGE_SIZE_4K + FRAME_BASE);
                if (err) {
                    // printf("load page fail\n");
                    frame_free(frame_handle);
                    return err;
                }
                err = sos_map_frame(global_cspace, frame_handle, cur_proc,
                                    vaddr, seL4_CapRights_new(execute, read, write), seL4_ARM_Default_VMAttributes);
            } else {
                return seL4_RangeError;
            }
            return err;
        }
        region = region->next;
    }
    /* failed */
    // printf("no region\n");
    return seL4_RangeError;
}

void update_level_4_page_table_entry(page_table_t *table,
                                     page_table_entry *entry, seL4_Word vaddr)
{
    /* save backend frame in level 4 shadow page table */
    page_table_t *pt = (page_table_t *)get_n_level_table((seL4_Word)table, vaddr,
                       4);
    page_table_cap *pt_cap = (page_table_cap *)get_page_table_cap((seL4_Word)pt);
    int offset = get_offset(vaddr, 4);
    pt->page_obj_addr[offset] = entry->frame | PRESENT;
    pt_cap->cap[offset] = entry->slot;
    if (vaddr != USERIPCBUFFER) {
        FRAME_CLEAR_BIT(entry->frame, PIN);
    }
    FRAME_SET_BIT(entry->frame, CLOCK);
    frame_table.frames[entry->frame].vaddr = vaddr;
    /* TODO: SETPID */
    // printf("frame %d, vaddr %d\n", entry->frame, vaddr);
}

seL4_CPtr get_cap_from_vaddr(page_table_t *table, seL4_Word vaddr)
{
    seL4_CPtr slot;
    seL4_Word offset;
    page_table_t *pt;
    page_table_cap *pt_cap;
    pt = (page_table_t *)get_n_level_table((seL4_Word)table, vaddr, 4);
    /* we haven't got that vaddr yet */
    if (pt == NULL) {
        return 0;
    }
    pt_cap = (page_table_cap *)get_page_table_cap((seL4_Word)pt);
    offset = get_offset(vaddr, 4);
    slot = pt_cap->cap[offset];
    return slot;
}

/*
* This function will return the 32bit frame table index
*/
seL4_Word get_frame_from_vaddr(page_table_t *table, seL4_Word vaddr)
{
    return (_get_frame_from_vaddr(table, vaddr) & (~PRESENT));
}

seL4_Word _get_frame_from_vaddr(page_table_t *table, seL4_Word vaddr)
{
    seL4_Word frame;
    seL4_Word offset;
    page_table_t *pt;
    pt = (page_table_t *)get_n_level_table((seL4_Word)table, vaddr, 4);
    /* we haven't got that vaddr yet */
    if (pt == NULL) {
        return 0;
    }
    offset = get_offset(vaddr, 4);
    frame = pt->page_obj_addr[offset];
    return frame;
}

seL4_Word get_sos_virtual_address(page_table_t *table, seL4_Word vaddr)
{
    seL4_Word frame = _get_frame_from_vaddr(table, vaddr);
    if (frame & PRESENT) {
        frame = (int) frame;
        return (FRAME_BASE + frame * PAGE_SIZE_4K) + (vaddr & PAGE_MASK_4K);
    }
    return 0;
}

void update_page_status(page_table_t *table, seL4_Word vaddr, bool present,
                        bool unmap, seL4_Word file_offset)
{
    page_table_t *pt = (page_table_t *)get_n_level_table((seL4_Word)table, vaddr,
                       4);
    // if(pt == NULL){
    //     print_backtrace();
    //     printf("%p\n", vaddr);
    // }

    // assert(pt);
    int offset = get_offset(vaddr, 4);
    if (unmap) {
        // clock hand will iterate through all the pages
        // set set the unmap bit
        pt->page_obj_addr[offset] |= UNMAPPED;
    }
    if (!present) {
        pt->page_obj_addr[offset] = file_offset & (~PRESENT);
    }


    // seL4_Word cap = get_cap_from_vaddr(table, vaddr);
    // printf("cap %d\n", cap);
    // seL4_Error err = seL4_ARM_Page_Unmap(cap);
    // assert(err == 0);
    // cspace_delete(global_cspace, cap);
    // cspace_free_slot(global_cspace, cap);
}

void destroy_page_table(page_table_t *table)
{

    page_table_cap *caps_1 = get_page_table_cap((seL4_Word)table);
    page_table_ut *uts_1 = get_page_table_ut((seL4_Word)table);
    for (int i = 0; i < PAGE_TABLE_SIZE; i++) {
        if (table->page_obj_addr[i] == 0) continue;

        page_table_t *table_2 = (page_table_t *) table->page_obj_addr[i];
        page_table_cap *caps_2 = get_page_table_cap((seL4_Word)table_2);
        page_table_ut *uts_2 = get_page_table_ut((seL4_Word)table_2);
        for (int j = 0; j < PAGE_TABLE_SIZE; j++) {
            if (table_2->page_obj_addr[j] == 0) continue;

            page_table_t *table_3 = (page_table_t *) table_2->page_obj_addr[j];
            page_table_cap *caps_3 = get_page_table_cap((seL4_Word)table_3);
            page_table_ut *uts_3 = get_page_table_ut((seL4_Word)table_3);
            for (int k = 0; k < PAGE_TABLE_SIZE; k++) {
                if (table_3->page_obj_addr[k] == 0) continue;
                seL4_Word vaddr = table_3->page_obj_addr[k];

                frame_n_free((vaddr - FRAME_BASE) / PAGE_SIZE_4K);

                seL4_CPtr cp = caps_3->cap[k];
                cspace_delete(global_cspace, cp);
                cspace_free_slot(global_cspace, cp);

                ut_t *u = uts_3->ut[k];
                ut_free(u, seL4_PageBits);
            }

            seL4_Word vaddr = table_2->page_obj_addr[j];

            frame_n_free((vaddr - FRAME_BASE) / PAGE_SIZE_4K);

            seL4_CPtr cp = caps_2->cap[j];
            cspace_delete(global_cspace, cp);
            cspace_free_slot(global_cspace, cp);

            ut_t *u = uts_2->ut[j];
            ut_free(u, seL4_PageBits);
        }

        seL4_Word vaddr = table->page_obj_addr[i];

        frame_n_free((vaddr - FRAME_BASE) / PAGE_SIZE_4K);

        seL4_CPtr cp = caps_1->cap[i];
        cspace_delete(global_cspace, cp);
        cspace_free_slot(global_cspace, cp);

        ut_t *u = uts_1->ut[i];
        ut_free(u, seL4_PageBits);
    }

    seL4_Word vaddr = (seL4_Word)table;

    frame_n_free((vaddr - FRAME_BASE) / PAGE_SIZE_4K);
}