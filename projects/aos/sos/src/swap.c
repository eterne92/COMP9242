#include "frametable.h"
#include "pagetable.h"
#include "proc.h"
#include "vfs/vfs.h"
#include "vfs/vnode.h"
#include "vfs/uio.h"
#include <fcntl.h>
#include <sel4/sel4.h>


static struct vnode *swap_file;
static unsigned header = 0;
static unsigned tail = 0;


void initialize_swapping_file(void)
{
    vfs_open("swapping", O_RDWR, 0666, &swap_file);
}

seL4_Error load_page(seL4_Word offset, seL4_Word vaddr)
{
    int result = 0;
    struct uio u_uio;
    struct uio k_uio;
    struct proc *cur_proc = get_cur_proc();
    unsigned tmp = 0;
    uio_uinit(&u_uio, vaddr, PAGE_SIZE_4K, offset, UIO_READ, cur_proc);
    result = VOP_READ(swap_file, &u_uio);
    if (!result) {
        // update free list
        tmp = header;
        header = offset / PAGE_SIZE_4K;
        uio_kinit(&k_uio, &tmp, sizeof(unsigned), offset, UIO_WRITE);
        VOP_WRITE(swap_file, &k_uio);

    }
    return result;
}

seL4_Error try_swap_out(void)
{
    int clock_bit, pin_bit;
    seL4_Error err = seL4_NotEnoughMemory;
    struct proc *cur_proc = get_cur_proc();
    seL4_Word addr = 0;
    page_table_entry entry;
    struct uio u_uio;
    struct uio k_uio;
    unsigned tmp = 0;
    // still need to figure out the actual size of all the frames
    // no need to go all the way down to the length since many of them
    // have already been retyped into page table object or thread control block
    unsigned size = frame_table.length;
    // go through the frame table to find the victim
    for (unsigned i = first_available_frame; i < size; ++i) {
        pin_bit = FRAME_GET_BIT(i, PIN);
        if (!pin_bit) {
            clock_bit = FRAME_GET_BIT(i, CLOCK);
            if (clock_bit) {
                // unmap the page and set the clock bit to 0
                FRAME_CLEAR_BIT(i, CLOCK);
                seL4_ARM_Page_Unmap(get_cap_from_vaddr(cur_proc->pt,
                                                       frame_table.frames[i].vaddr));
            } else {
                // victim found
                // read the free list header from the swapping file
                uio_kinit(&k_uio, &tmp, sizeof(unsigned), header * PAGE_SIZE_4K, UIO_READ);
                VOP_READ(swap_file, &k_uio);
                // update the present bit & offset

                // write out the page into disk
                uio_uinit(&u_uio, addr, PAGE_SIZE_4K, header * PAGE_SIZE_4K, UIO_WRITE,
                          cur_proc);
                VOP_WRITE(swap_file, &u_uio);

                header = tmp;
                err = seL4_NoError;
            }
        }
    }
    return err;
}