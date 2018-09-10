#include "pagetable.h"
#include "addrspace.h"
#include "frametable.h"
#include "mapping.h"


#define PRESENT (1lu << 50)
#define PAGE_RW (1lu << 51)
#define OFFSET 0xffffffffffff

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
    seL4_CPtr vspace = cur_proc->vspace;
    as_region *region = cur_proc->as->regions;
    bool execute, read, write;
    seL4_Error err;

    region = cur_proc->as->regions;
    while (region) {
        if (vaddr >= region->vaddr && vaddr < region->vaddr + region->size) {
            execute = region->flags & RG_X;
            read = region->flags & RG_R;
            write = region->flags & RG_W;
            // write to a read-only page
            frame = get_frame_from_vaddr(cur_proc->pt, vaddr);
            if (frame == 0) {
                /* it's a vm fault without page */
                // allocate a frame
                frame = frame_alloc(NULL);
                // map it
                err = sos_map_frame(global_cspace, frame, (seL4_Word)cur_proc->pt, vspace,
                                    vaddr, seL4_CapRights_new(execute, read, write), seL4_ARM_Default_VMAttributes);
            } else if (frame & PRESENT) {
                /* the page is still there */
                frame = frame & OFFSET;
                err = sos_map_frame(global_cspace, frame, (seL4_Word)cur_proc->pt, vspace,
                                    vaddr, seL4_CapRights_new(execute, read, write), seL4_ARM_Default_VMAttributes);

            } else if (!(frame & PRESENT)) {
                // page is in swapping file
                seL4_Word offset = frame & OFFSET;
                frame = frame_alloc(NULL);
                err = sos_map_frame(global_cspace, frame, (seL4_Word)cur_proc->pt, vspace,
                                    vaddr, seL4_CapRights_new(execute, read, write), seL4_ARM_Default_VMAttributes);
                if (err) {
                    return err;
                }
                err = load_page(offset, vaddr & PAGE_FRAME);
            } else {
                /* it's a vm fault with permmision */
                /* for now it's segment fault */
                /* later it will be copy on write */
                return -1;
            }

            return err;
        }
        region = region->next;
    }
    /* failed */
    return -1;
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
    FRAME_CLEAR_BIT(entry->frame, PIN);
    FRAME_SET_BIT(entry->frame, CLOCK);
    frame_table.frames[entry->frame].vaddr = vaddr;
    /* TODO: SETPID */
}

seL4_CPtr get_cap_from_vaddr(page_table_t *table, seL4_Word vaddr)
{
    seL4_CPtr slot;
    int offset;
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

seL4_Word get_frame_from_vaddr(page_table_t *table, seL4_Word vaddr)
{
    int frame;
    int offset;
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
    seL4_Word frame = get_frame_from_vaddr(table, vaddr);
    if (frame) {
        return (FRAME_BASE + frame * PAGE_SIZE_4K) + (vaddr & PAGE_MASK_4K);
    }
    return 0;
}

void update_page_status(page_table_t *table, seL4_Word vaddr, bool present,
                        seL4_Word file_offset)
{
    page_table_t *pt = (page_table_t *)get_n_level_table((seL4_Word)table, vaddr,
                       4);
    int offset = get_offset(vaddr, 4);
    pt->page_obj_addr[offset] = present ? file_offset | PRESENT : file_offset & ~
                                (PRESENT);
    FRAME_CLEAR_BIT(pt->page_obj_addr[offset], PIN);
}