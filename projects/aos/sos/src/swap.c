#include "frametable.h"
#include "pagetable.h"
#include "proc.h"
#include "vfs/vfs.h"
#include "vfs/vnode.h"
#include "vfs/uio.h"
#include <fcntl.h>
#include <sel4/sel4.h>


static struct vnode *swap_file = NULL;
static unsigned header = 0;
static unsigned tail = 0;
static unsigned clock_hand;


void initialize_swapping_file(void)
{
    clock_hand = first_available_frame;
}

seL4_Error load_page(seL4_Word offset, seL4_Word vaddr)
{
    int result = 0;
    struct uio u_uio;
    struct uio k_uio;
    struct proc *cur_proc = get_cur_proc();
    unsigned tmp = 0;
    // printf("load page start\n");
    // printf("%d, vaddr %p\n", offset, vaddr);
    offset = offset - 1;
    uio_uinit(&u_uio, vaddr, PAGE_SIZE_4K, offset, UIO_READ, cur_proc);
    result = VOP_READ(swap_file, &u_uio);
    assert(result == 0);
    // printf("read finish\n");
    if (!result) {
        // update free list
        tmp = header;
        header = offset / PAGE_SIZE_4K;
        uio_kinit(&k_uio, (seL4_Word)&tmp, sizeof(unsigned), offset, UIO_WRITE);
        VOP_WRITE(swap_file, &k_uio);
    }
    return result;
}

seL4_Error try_swap_out(void)
{
    int clock_bit, pin_bit;
    seL4_Error err = seL4_NotEnoughMemory;
    struct proc *process;
    seL4_Word file_offset = 0;
    page_table_entry entry;
    struct uio u_uio;
    struct uio k_uio;
    unsigned tmp = 0;
    // still need to figure out the actual size of all the frames
    // no need to go all the way down to the length since many of them
    // have already been retyped into page table object or thread control block
    unsigned size = frame_table.max;
    // go through the frame table to find the victim
    for (unsigned j = first_available_frame; j < size * 2; ++j) {
        if (clock_hand == frame_table.max - 1u) {
            clock_hand = first_available_frame;
        }
        pin_bit = FRAME_GET_BIT(clock_hand, PIN);
        if (!pin_bit) {
            clock_bit = FRAME_GET_BIT(clock_hand, CLOCK);
            process = get_process(GET_PID(frame_table.frames[clock_hand].flag));
            if (clock_bit) {
                // unmap the page and set the clock bit to 0
                FRAME_CLEAR_BIT(clock_hand, CLOCK);
                seL4_CPtr cap = get_cap_from_vaddr(process->pt,
                                                       frame_table.frames[clock_hand].vaddr);
                seL4_ARM_Page_Unmap(cap);
                
                cspace_delete(global_cspace, cap);
                cspace_free_slot(global_cspace, cap);
            } else {
                // victim found
                // printf("victim's vaddr is %p\n", frame_table.frames[clock_hand].vaddr);
                // printf("victim's cap is %d\n", frame_table.frames[clock_hand].ut->cap);
                file_offset = header * PAGE_SIZE_4K;
                if (swap_file == NULL) {
                    seL4_Word tmp = 'c';
                    vfs_open("swapping", O_RDWR | O_CREAT, 0666, &swap_file);

                    uio_kinit(&k_uio, (seL4_Word)&tmp, sizeof(unsigned), 0, UIO_WRITE);
                    VOP_WRITE(swap_file, &k_uio);
                }


                if (header == tail) {
                    tail++;
                    header++;
                } else {
                    // read the free list header from the swapping file
                    uio_kinit(&k_uio, (seL4_Word)&tmp, sizeof(unsigned), file_offset, UIO_READ);
                    int ret = VOP_READ(swap_file, &k_uio);
                    header = tmp;
                }

                // update the present bit & offset
                update_page_status(process->pt, frame_table.frames[clock_hand].vaddr, false,
                                   file_offset + 1);
                // write out the page into disk
                uio_kinit(&k_uio, FRAME_BASE + PAGE_SIZE_4K * clock_hand, PAGE_SIZE_4K,
                          file_offset, UIO_WRITE);
                // printf("uio: %p, sos_vaddr %p,offset %ld, swap_file %p\n", 
                //             k_uio, k_uio.vaddr, k_uio.uio_offset, swap_file);
                VOP_WRITE(swap_file, &k_uio);
                // printf("WRITE DONE\n");



                // free the frame

                frame_free(clock_hand);
                err = seL4_NoError;
                clock_hand++;
                break;
            }
        }
        clock_hand++;
    }
    return err;
}