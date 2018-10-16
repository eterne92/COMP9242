#include <cspace/cspace.h>

#include "addrspace.h"
#include "pagetable.h"
#include "proc.h"

#define PRESENT (1lu << 50)
#define PAGE_RW (1lu << 51)
#define OFFSET 0xffffffffffff

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
    if (vaddr + memsize > USERSPACETOP) {
        return -1;
    }
    // setup region
    if (region == NULL) {
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
void as_destroy_region(addrspace *as, as_region *region, proc *cur_proc)
{
    seL4_Word first_vaddr = region->vaddr & PAGE_FRAME;
    seL4_Word last_vaddr = (region->vaddr + region->size - 1) & PAGE_FRAME;

    /* check first frame */
    as_region *tmp = as->regions;
    // while (tmp) {
    //     if (tmp->vaddr + tmp->size > first_vaddr) {
    //         if (tmp->vaddr < first_vaddr) {
    //             /* region overlapped */
    //             /* start from second frame */
    //             first_vaddr += PAGE_SIZE_4K;
    //         }
    //         break;
    //     }
    //     tmp = tmp->next;
    // }

    /* check last frame */
    if (region->next && (region->next->vaddr & PAGE_FRAME) == last_vaddr) {
        /* last frame overlap */
        last_vaddr -= PAGE_SIZE_4K;
    }

    //printf("first %p, last %p\n", (void *)first_vaddr, (void *)last_vaddr);

    printf("try clean up\n");
    for (seL4_Word i = first_vaddr; i <= last_vaddr; i += PAGE_SIZE_4K) {
        // printf("destroy %p\n", i);
        seL4_Word frame = _get_frame_from_vaddr(cur_proc->pt, i);
        seL4_Word slot = get_cap_from_vaddr(cur_proc->pt, i);
        if (frame == 0) {
            continue;
        } else if (!(frame & PRESENT)) {
            printf("clean swap\n");
            clean_up_swapping(frame & OFFSET);
            printf("clean swap done\n");
        } else if (frame != 0 && slot != 0) {
            frame = (int) frame;
            int clock_bit = FRAME_GET_BIT(frame, CLOCK);
            if (clock_bit) {
                seL4_ARM_Page_Unmap(slot);
                cspace_delete(global_cspace, slot);
                cspace_free_slot(global_cspace, slot);
            }
            frame_free(frame);
        }
    }
    tmp = as->regions;

    //printf("sort region\n");
    /* we are first region */
    if (tmp == region) {
        as->regions = tmp->next;
        free(region);
        return;
    }

    /* we are not first */
    while (tmp) {
        if (tmp->next == region) {
            tmp->next = region->next;
            free(region);
            return;
        }
        tmp = tmp->next;
    }
}

void destroy_regions(addrspace *as, proc *cur_proc)
{
    as_region *region = as->regions;
    while (region) {
        //printf("%p -> %p region destroyed\n", (void *)region->vaddr, region->vaddr + region->size);
        as_destroy_region(as, region, cur_proc);
        region = as->regions;
    }
}
/* make region list ordered by check each region
 * this also prevent regions from overlap with each other
 */
static int insert_region(addrspace *as, as_region *region)
{
    seL4_Word vaddr;
    vaddr = region->vaddr;

    if (as->regions == NULL) {
        // first region
        as->regions = region;
    } else {
        as_region *tmp = as->regions;
        as_region *prev = NULL;
        // check overlap
        while (tmp) {
            // overlap
            if (vaddr > tmp->vaddr && vaddr - tmp->vaddr < tmp->size) {
                free(region);
                return -1;
            }
            // first region
            if (vaddr < tmp->vaddr) {
                // check overlap
                if (vaddr + region->size > tmp->vaddr) {
                    free(region);
                    return -1;
                }
                region->next = tmp;

                if (prev == NULL) {
                    as->regions = region;
                } else {
                    prev->next = region;
                }
                break;
            }
            prev = tmp;
            tmp = tmp->next;
        }
        if (tmp == NULL) {
            prev->next = region;
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
    if (result) {
        return NULL;
    }

    result = insert_region(as, region);
    if (result) {
        return NULL;
    }
    return region;
}

int as_define_stack(addrspace *as)
{
    /* Initial user-level stack pointer */
    as_region *region;
    int stacksize = USERSTACKSIZE; // 16M stack
    region = as_define_region(as, USERSTACKTOP - stacksize, stacksize, RG_R | RG_W);
    if (region == NULL) {
        return -1;
    }

    as->stack = region;

    return 0;
}

int as_define_ipcbuffer(addrspace *as)
{
    /* Initial user-level ipcbuffer pointer */
    as_region *region;
    region = as_define_region(as, USERIPCBUFFER, PAGE_SIZE_4K, RG_R | RG_W);
    if (region == NULL) {
        return -1;
    }

    as->ipcbuffer = region;

    return 0;
}

int as_define_heap(addrspace *as)
{
    /* Initial user-level stack pointer */
    as_region *region;
    region = as_define_region(as, USERHEAPBASE, 0, RG_R | RG_W);
    if (region == NULL) {
        return -1;
    }

    as->heap = region;

    return 0;
}

bool validate_virtual_address(addrspace *as, seL4_Word vaddr, size_t size,
                              enum OPERATION operation)
{
    as_region *region = as->regions;
    while (region) {
        if (vaddr >= region->vaddr && vaddr + size < region->vaddr + region->size) {
            if (operation == READ) {
                // doing read means kernel will write to the buffer provided by user
                return region->flags & RG_W;
            } else if (operation == WRITE) {
                return region->flags & RG_R;
            }
        }
        region = region->next;
    }
    return false;
}


as_region *vaddr_get_region(addrspace *as, seL4_Word vaddr)
{
    as_region *region = as->regions;
    while (region) {
        if (vaddr >= region->vaddr && vaddr < region->vaddr + region->size) {
            return region;
        }
        region = region->next;
    }

    return NULL;
}