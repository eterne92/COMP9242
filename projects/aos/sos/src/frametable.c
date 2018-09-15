#include "frametable.h"
#include "mapping.h"
#include "pagetable.h"
#include <stdlib.h>

#define UNTYPE_MEMEORY 0x1
#define FREE_MEMORY 0x2
#define USED_MEMORY 0x3
#define MEMORY_TYPE_MASK 0x3

/* TODO: frame reference count */

#define MOST_FREE 30

frame_table_t frame_table;
static cspace_t *root_cspace;

unsigned first_available_frame;

unsigned th;

static ut_t *alloc_retype(seL4_CPtr *cptr, seL4_Word type)
{
    /* Allocate the object */
    // ut_t *ut = ut_alloc_4k_untyped(NULL);
    ut_t *ut;
    // if(th > 2000){
    ut = ut_alloc_4k_untyped(NULL);
    if (ut == NULL) {
        /* try page */
        seL4_Error err = try_swap_out();
        assert(err == seL4_NoError);
        ut = ut_alloc_4k_untyped(NULL);
        assert(ut != NULL);
    }
    // else{

    //     ut = ut_alloc_4k_untyped(NULL);
    //     if(ut == NULL){
    //         return NULL;
    //     }
    // }

    /* allocate a slot to retype the memory for object into */
    *cptr = cspace_alloc_slot(root_cspace);
    if (*cptr == seL4_CapNull) {
        ut_free(ut, seL4_PageBits);
        return NULL;
    }
    // if(flag == 1)
    // printf("root_cspace %p, ut->cap %d, cptr %p\n", root_cspace, ut->cap, *cptr);
    /* now do the retype */
    seL4_Error err = cspace_untyped_retype(root_cspace, ut->cap, *cptr, type,
                                           seL4_PageBits);
    if (err != seL4_NoError) {
        ut_free(ut, seL4_PageBits);
        cspace_free_slot(root_cspace, *cptr);
        return NULL;
    }
    return ut;
}

void initialize_frame_table(cspace_t *cspace)
{
    ut_t *ut;
    root_cspace = cspace;
    seL4_Word vaddr = FRAME_BASE;
    frame_table.frames = (frame_table_obj *)FRAME_BASE;
    // the number of pages of all untyped memeory
    size_t n_frames = ut_size() / PAGE_SIZE_4K;
    frame_table.length = (int)n_frames;
    // the number of pages consumed by frame table
    size_t n_pages = (n_frames * sizeof(frame_table_obj) + PAGE_SIZE_4K - 1) /
                     PAGE_SIZE_4K;
    frame_table.untyped = (uint32_t)n_pages;
    for (size_t i = 0; i < n_pages; ++i) {
        seL4_CPtr frame_cap;
        ut = alloc_retype(&frame_cap, seL4_ARM_SmallPageObject);
        if (ut == NULL) {
            return;
        }
        // ZF_LOGF_IF(ut == NULL, "Failed to allocate frame table page");
        map_frame(cspace, frame_cap, seL4_CapInitThreadVSpace,
                  vaddr, seL4_AllRights, seL4_ARM_Default_VMAttributes);
        // ZF_LOGF_IFERR(err, "Failed to map frame table pages");
        frame_table.frames[i].ut = ut;
        frame_table.frames[i].next = -1;
        frame_table.frames[i].flag = USED_MEMORY;
        frame_table.frames[i].frame_cap = frame_cap;
        FRAME_SET_BIT(i, PIN);
        vaddr += PAGE_SIZE_4K;
    }
    printf("initial frametable done part I\n");
    printf("there is %lu frames\nframetable n_pages %lu\n", n_frames, n_pages);
    first_available_frame = n_pages;
    for (size_t i = n_pages; i < n_frames; ++i) {
        frame_table.frames[i].ut = NULL;
        frame_table.frames[i].next = i + 1;
        frame_table.frames[i].flag = UNTYPE_MEMEORY;
        FRAME_SET_BIT(i, PIN);
    }
    // the final frame will link back to itself
    frame_table.frames[n_frames - 1].next = n_frames - 1;
    printf("%d's next is %d\n", n_frames - 1,
           frame_table.frames[n_frames - 1].next);
    /* we are using water mark, so there is no free frame at first */
    // frame_table.free = -1;
    frame_table.max = n_frames - n_pages + 1;
    printf("initial frametable done part II\n");
    return;
}

int frame_alloc(seL4_Word *vaddr)
{
    seL4_Word _vaddr;
    int page = frame_table.free;
    // if (page > 0) {
    //     /* we got a free frame, just use it */
    //     _vaddr = page * PAGE_SIZE_4K + FRAME_BASE;
    //     frame_table.free = frame_table.frames[page].next;
    //     frame_table.num_frees--;
    //     memset((void *)_vaddr, 0, PAGE_SIZE_4K);
    //     frame_table.frames[page].next = -1;
    //     if (vaddr) {
    //         *vaddr = _vaddr;
    //     }

    //     FRAME_SET_BIT(page, PIN);
    //     if(page > frame_table.max){
    //         frame_table.max = page;
    //     }
    //     return page;
    // }
    /* otherwise we need to get one from untyped mem */
    seL4_CPtr frame_cap;
    _vaddr = 0;
    /* always try to get mem from ut_table */
    ut_t *ut = alloc_retype(&frame_cap, seL4_ARM_SmallPageObject);
    if (ut == NULL) {
        // out of memory
        if (vaddr != NULL) {
            *vaddr = _vaddr;
        }
        return -1;
    }
    page = frame_table.untyped;
    assert(page != -1);
    _vaddr = page * PAGE_SIZE_4K + FRAME_BASE;
    map_frame(root_cspace, frame_cap, seL4_CapInitThreadVSpace,
              _vaddr, seL4_AllRights, seL4_ARM_Default_VMAttributes);
    /*
    if(err != seL4_NoError) {
        return -1;
    }
    */
    frame_table.frames[page].ut = ut;
    frame_table.frames[page].flag |= USED_MEMORY;
    frame_table.frames[page].frame_cap = frame_cap;
    frame_table.untyped = frame_table.frames[page].next;


    frame_table.frames[page].next = -1;
    memset((void *)_vaddr, 0, PAGE_SIZE_4K);
    if (vaddr) {
        *vaddr = _vaddr;
    }
    FRAME_SET_BIT(page, PIN);
    if (page > frame_table.max) {
        frame_table.max = page;
    }
    return page;
}

int frame_n_alloc(seL4_Word *vaddr, int nframes)
{
    int base_frame = frame_alloc(vaddr);
    int frame = 0, tmp = 0;
    if (base_frame == -1)
        return -1;
    frame = base_frame;
    for (int i = 1; i < nframes; ++i) {
        frame_table.frames[frame].next = frame_alloc(NULL);
        if (frame_table.frames[frame].next == -1) {
            // out of memory need clean up all pre-allocated frames
            tmp = base_frame;
            while (tmp != -1) {
                frame = frame_table.frames[tmp].next;
                frame_free(tmp);
                tmp = frame;
            }
            return -1;
        }
        frame = frame_table.frames[frame].next;
    }
    return base_frame;
}

void frame_n_free(int frames)
{
    int frame = 0, tmp = frames;
    while (tmp != -1) {
        frame = frame_table.frames[tmp].next;
        frame_free(tmp);
        tmp = frame;
    }
}

static void free_to_untype(int frame)
{
    /* ut_free this frame */
    seL4_ARM_Page_Unmap(frame_table.frames[frame].frame_cap);
    cspace_delete(root_cspace, frame_table.frames[frame].frame_cap);
    cspace_free_slot(root_cspace, frame_table.frames[frame].frame_cap);
    ut_free(frame_table.frames[frame].ut, seL4_PageBits);
    frame_table.frames[frame].ut = NULL;
    frame_table.frames[frame].frame_cap = 0;
    frame_table.frames[frame].flag |= UNTYPE_MEMEORY;
    frame_table.frames[frame].vaddr = 0;
    /* set this frame to untyped list */
    frame_table.frames[frame].next = frame_table.untyped;
    assert(frame_table.untyped != -1);
    frame_table.untyped = frame;
    frame_table.num_frees--;
}

void frame_free(int frame)
{
    if (frame < 0 || frame >= frame_table.length)
        assert(false);
    // frame_table.frames[frame].next = frame_table.free;
    // frame_table.frames[frame].flag = FREE_MEMORY;
    // frame_table.free = frame;
    // frame_table.num_frees++;
    FRAME_SET_BIT(frame, PIN);
    // if (frame_table.num_frees >= MOST_FREE) {
    // printf("try to real free\n");
    free_to_untype(frame);
    // }
}