#include "frametable.h"
#include "pagetable.h"
#include "mapping.h"

void handle_page_fault(proc *cur_proc, seL4_Word vaddr, seL4_Word fault_info)
{
    // need to figure out which process triggered the page fault
    // right now, there is only one process (tty_test)
    (void)fault_info;
    int frame;
    seL4_CPtr vspace = cur_proc->vspace;
    as_region *region = cur_proc->as->regions;
    bool execute, read, write;
    while (region)
    {
        if (vaddr >= region->vaddr && vaddr < region->vaddr + region->size)
        {
            execute = region->flags & RG_X;
            read = region->flags & RG_R;
            write = region->flags & RG_W;
            // write to a read-only page

            // accessing a non-existing page

            // allocate a frame
            frame = frame_alloc(NULL);
            sos_map_frame(&cur_proc->cspace, frame, (seL4_Word)cur_proc->pt, vspace, vaddr, seL4_CapRights_new(execute, read, write), seL4_ARM_Default_VMAttributes);

            break;
        }
        else
        {
            // illegal access
        }
        region = region->next;
    }
}

page_table_t *initialize_page_table(void)
{
    seL4_Word page_table_addr;
    int page_frame = frame_n_alloc(&page_table_addr, 3);
    return page_frame != -1 ? (page_table_t *)page_table_addr : NULL;
}

/* find out the frame contains the ut / cap */
page_table_ut *get_page_table_ut(seL4_Word page_table)
{
    int frame = (page_table - FRAME_BASE) / PAGE_SIZE_4K;
    int ut_frame = frame_table.frames[frame].next;
    return (page_table_ut *)(ut_frame * PAGE_SIZE_4K + FRAME_BASE);
}

page_table_cap *get_page_table_cap(seL4_Word page_table)
{
    int frame = (page_table - FRAME_BASE) / PAGE_SIZE_4K;
    int ut_frame = frame_table.frames[frame].next;
    int cap_frame = frame_table.frames[ut_frame].next;
    return (page_table_cap *)(cap_frame * PAGE_SIZE_4K + FRAME_BASE);
}


int get_offset(seL4_Word vaddr, int n)
{
    seL4_Word mask = 0x7fc0000000;
    int offset = (mask & vaddr) >> (48 - 9 * n);
    return offset;
}

seL4_Word get_n_level_table(seL4_Word page_table, seL4_Word vaddr, int n)
{
    /* page_table is the 1st level, so we start from 2nd level */
    seL4_Word mask = 0x7fc0000000;
    page_table_t *pt = (page_table_t *)page_table;
    for (int i = 1; i < n; i++)
    {
        int offset = get_offset(vaddr, n);
        pt = (page_table_t *)pt->page_obj_addr[offset];
        mask = mask >> 9;
    }

    return (seL4_Word)pt;
}