#include <cspace/cspace.h>

#include "addrspace.h"
#include "pagetable.h"
#include "proc.h"

proc *cur_proc;
addrspace *addrspace_init(void)
{
    addrspace *as;
    as = malloc(sizeof(addrspace));
    if (!as)
        return NULL;
    as->regions = NULL;
    as->heap = NULL;
    return as;
}

static int create_region(as_region *region,
                         seL4_Word vaddr, size_t memsize,
                         unsigned char flag)
{
    size_t npages;

    // align region
    memsize += vaddr & ~(seL4_Word)PAGE_FRAME;
    vaddr &= PAGE_FRAME;
    // align memsize
    memsize = (memsize + PAGE_SIZE_4K - 1) & PAGE_FRAME;
    // set npages
    npages = memsize / PAGE_SIZE_4K;

    // check userspace top
    if (vaddr + memsize > USERSPACETOP)
    {
        return -1;
    }
    // setup region
    if (region == NULL)
    {
        return -1;
    }
    region->next = NULL;
    // setup flags
    region->flags = flag;
    region->vaddr = vaddr;
    region->npages = npages;
    region->size = memsize;
    // TODO: add filesystem to support demand loading
    return 0;
}

/* destroying a region is just unmap all it's frame.
 * need to be careful since one frame may contain more than one
 * region.
 */
void as_destroy_region(addrspace *as, as_region *region)
{
    seL4_Word first_vaddr = region->vaddr & PAGE_FRAME;
    seL4_Word last_vaddr = (region->vaddr + region->size) & PAGE_FRAME;
    cur_proc = get_cur_proc();

    /* check first frame */
    as_region *tmp = as->regions;
    while (tmp)
    {
        if (tmp->vaddr + tmp->size > first_vaddr)
        {
            if (tmp->vaddr < first_vaddr)
            {
                /* region overlapped */
                /* start from second frame */
                first_vaddr += PAGE_SIZE_4K;
            }
            break;
        }
        tmp = tmp->next;
    }

    /* check last frame */
    if ((region->next->vaddr & PAGE_FRAME) == last_vaddr)
    {
        /* last frame overlap */
        last_vaddr -= PAGE_SIZE_4K;
    }
    for (seL4_Word i = first_vaddr; i <= last_vaddr; i += PAGE_SIZE_4K)
    {
        seL4_Word frame = get_frame_from_vaddr(cur_proc->pt, i);
        seL4_Word slot = get_cap_from_vaddr(cur_proc->pt, i);
        if(frame != 0 && slot != 0){
            seL4_ARM_Page_Unmap(slot);
            frame_free(frame);
        }
    }
    tmp = as->regions;

    /* we are first region */
    if(tmp == region){
        as->regions = tmp->next;
        free(region);
        return;
    }

    /* we are not first */
    while(tmp){
        if(tmp->next == region){
            tmp->next = region->next;
            free(region);
            return;
        }
        tmp = tmp->next;
    }

}
/* make region list ordered by check each region
 * this also prevent regions from overlap with each other
 */
static int insert_region(addrspace *as, as_region *region)
{
    seL4_Word vaddr;
    vaddr = region->vaddr;

    if (as->regions == NULL)
    {
        // first region
        as->regions = region;
    }
    else
    {
        as_region *tmp = as->regions;
        // check overlap
        while (tmp)
        {
            // overlap
            if (vaddr > tmp->vaddr &&
                vaddr - tmp->vaddr < tmp->size)
            {
                free(region);
                return -1;
            }
            // last region
            if (tmp->next == NULL)
            {
                tmp->next = region;
                break;
            }
            // first region
            else if (vaddr < tmp->vaddr)
            {
                // check overlap
                if (vaddr + region->size > tmp->vaddr)
                {
                    free(region);
                    return -1;
                }
                region->next = tmp;
                as->regions = region;
                break;
            }
            // in the middle
            // and we are in the right position
            else if (vaddr < tmp->next->vaddr)
            {
                // check overlap
                if (vaddr + region->size > tmp->next->vaddr)
                {
                    free(region);
                    return -1;
                }
                region->next = tmp->next;
                tmp->next = region;
                break;
            }
            tmp = tmp->next;
        }
    }

    return 0;
}

as_region *as_define_region(addrspace *as, seL4_Word vaddr, size_t memsize,
                            unsigned char flag)
{
    int result;
    as_region *region;

    // Should NEVER call this with a NULL as

    region = malloc(sizeof(as_region));
    result = create_region(region, vaddr, memsize, flag);
    if (result)
    {
        return NULL;
    }

    result = insert_region(as, region);
    if (result)
    {
        return NULL;
    }
    return region;
}

int as_define_stack(addrspace *as)
{
    /* Initial user-level stack pointer */
    as_region *region;
    int stacksize = USERSTACKSIZE; // 16M stack
    region = as_define_region(as,
                              USERSTACKTOP - stacksize,
                              stacksize,
                              RG_R | RG_W);
    if (region == NULL)
    {
        return -1;
    }

    as->stack = region;

    return 0;
}

int as_define_ipcbuffer(addrspace *as)
{
    /* Initial user-level ipcbuffer pointer */
    as_region *region;
    region = as_define_region(as,
                              USERIPCBUFFER,
                              PAGE_SIZE_4K,
                              RG_R | RG_W);
    if (region == NULL)
    {
        return -1;
    }

    as->ipcbuffer = region;

    return 0;
}

int as_define_heap(addrspace *as)
{
    /* Initial user-level stack pointer */
    as_region *region;
    region = as_define_region(as,
                              USERHEAPBASE,
                              0,
                              RG_R | RG_W);
    if (region == NULL)
    {
        return -1;
    }

    as->heap = region;

    return 0;
}