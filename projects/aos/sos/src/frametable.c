#include "frametable.h"
#include "ut.h"

#define FRAME_BASE 0xA000000000

typedef struct frame_table_obj {
    ut_t *ut;
    uint16_t flag;
} frame_table_obj;

static frame_table_obj *frame_table;

static ut_t *alloc_retype(seL4_CPtr *cptr, seL4_Word type)
{
    /* Allocate the object */
    ut_t *ut = ut_alloc_4k_untyped(NULL);
    if (ut == NULL) {
        ZF_LOGE("No untyped pages");
        return NULL;
    }

    /* allocate a slot to retype the memory for object into */
    *cptr = cspace_alloc_slot(&cspace);
    if (*cptr == seL4_CapNull) {
        ut_free(ut, seL4_PageBits);
        ZF_LOGE("Failed to allocate slot");
        return NULL;
    }

    /* now do the retype */
    seL4_Error err = cspace_untyped_retype(&cspace, ut->cap, *cptr, type, seL4_PageBits);
    ZF_LOGE_IFERR(err, "Failed retype untyped");
    if (err != seL4_NoError) {
        ut_free(ut, size_bits);
        cspace_free_slot(&cspace, *cptr);
        return NULLseL4_PageBits;
    }
    return ut;
}

void initialize_frame_table(cspace_t *cspace, seL4_CPtr vspace, seL4_Word vaddr, seL4_CapRights_t rights,
                     seL4_ARM_VMAttributes attr)
{
    ut_t *ut;
    seL4_Word vaddr = FRAME_BASE;
    frame_table = (frame_table_obj *) FRAME_BASE;
    size_t size = ut_size() / PAGE_SIZE_4K;
    for(int i = 0; i < size; ++i) {
        ut = alloc_retype(cspace, seL4_ARM_SmallPageObject, sel4_)
        if (ut == NULL) {
            return;
        }
        ZF_LOGF_IF(frame == NULL, "Failed to allocate frame table page");
        seL4_Error err = map_frame(&cspace, frame_cap, seL4_CapInitThreadVSpace,
                                   vaddr, seL4_AllRights, seL4_ARM_Default_VMAttributes);
        ZF_LOGF_IFERR(err, "Failed to map frame table pages");
        vaddr += PAGE_SIZE_4K;
    }

}