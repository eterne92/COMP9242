#include "frametable.h"
#include "ut.h"
#include "mapping.h"

#define FRAME_BASE 0xA000000000
#define UNTYPE_MEMEORY 0x1
#define FREE_MEMORY 0x2
#define USED_MEMORY 0x3
#define MEMORY_TYPE_MASK 0x3

typedef struct frame_table_obj {
    ut_t *ut;
    int next;
    seL4_CPtr frame_cap;
    uint16_t flag;
} frame_table_obj;

typedef struct frame_table {
    int free;
    frame_table_obj *frames;
    int length;
} frame_table_t;

static frame_table_t frame_table;
static cspace_t *root_cspace;

static ut_t *alloc_retype(seL4_CPtr *cptr, seL4_Word type)
{
    /* Allocate the object */
    ut_t *ut = ut_alloc_4k_untyped(NULL);
    if (ut == NULL) {
        // ZF_LOGE("No untyped pages");
        return NULL;
    }

    /* allocate a slot to retype the memory for object into */
    *cptr = cspace_alloc_slot(root_cspace);
    if (*cptr == seL4_CapNull) {
        ut_free(ut, seL4_PageBits);
        // ZF_LOGE("Failed to allocate slot");
        return NULL;
    }

    /* now do the retype */
    seL4_Error err = cspace_untyped_retype(root_cspace, ut->cap, *cptr, type, seL4_PageBits);
    // ZF_LOGE_IFERR(err, "Failed retype untyped");
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
    frame_table.frames = (frame_table_obj *) FRAME_BASE;
    // the number of pages of all untyped memeory
    size_t n_frames = ut_size() / PAGE_SIZE_4K;
    frame_table.length = (int) n_frames;
    // the number of pages consumed by frame table
    size_t n_pages = (n_frames * sizeof(frame_table_obj) + PAGE_SIZE_4K - 1) / PAGE_SIZE_4K;
    frame_table.free = (uint32_t) n_pages;
    for(size_t i = 0; i < n_pages; ++i) {
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
        vaddr += PAGE_SIZE_4K;
    }
    printf("initial frametable done part I\n");
    printf("there is %lu frames\nframetable n_pages %lu\n", n_frames, n_pages);
    for(size_t i = n_pages; i < n_frames; ++i) {
        frame_table.frames[i].ut = NULL;
        frame_table.frames[i].next = i + 1;
        frame_table.frames[i].flag = UNTYPE_MEMEORY;
    }
    frame_table.frames[n_frames - 1].next = -1;
    printf("initial frametable done part II\n");
    return;
}

int frame_alloc(seL4_Word *vaddr)
{
    int page = frame_table.free;
    seL4_CPtr frame_cap;
    *vaddr = 0;
    if(page == -1) {
        return -1;
    }
    ut_t *ut = alloc_retype(&frame_cap, seL4_ARM_SmallPageObject);
    if (ut == NULL) {
        return -1;
    }
    *vaddr = page * PAGE_SIZE_4K + FRAME_BASE;
    map_frame(root_cspace, frame_cap, seL4_CapInitThreadVSpace,
                                *vaddr, seL4_AllRights, seL4_ARM_Default_VMAttributes);
    /*
    if(err != seL4_NoError) {
        return -1;
    }
    */
    frame_table.frames[page].ut = ut;
    frame_table.frames[page].flag = USED_MEMORY;
    frame_table.frames[page].frame_cap = frame_cap;
    frame_table.free = frame_table.frames[page].next;
    return page;
    
}

void frame_free(int frame)
{
    if(frame < 0 || frame >= frame_table.length) return;
    frame_table.frames[frame].next = frame_table.free;
    frame_table.frames[frame].flag = UNTYPE_MEMEORY;
    frame_table.free = frame;
    cspace_delete(root_cspace, frame_table.frames[frame].frame_cap);
    cspace_free_slot(root_cspace, frame_table.frames[frame].frame_cap);
    ut_free(frame_table.frames[frame].ut, seL4_PageBits);
    frame_table.frames[frame].ut = NULL;
    frame_table.frames[frame].frame_cap = 0;
}