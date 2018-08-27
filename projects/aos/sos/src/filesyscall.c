#include "syscall.h"
#include "pagetable.h"
#include "proc.h"
#include "addrspace.h"
#include "vfs/vnode.h"
#include "vfs/vfs.h"
#include "vfs/uio.h"
#include <fcntl.h>

static struct vnode *vn;
/* only used to copy string size less then 256 */
/* so we won't go through too many frame */
static int copyinstr(proc *proc, const char *src, char *dest, size_t length){
    /* get region */
    as_region *region = vaddr_get_region(cur_proc->as, (seL4_Word)src);
    /* not valid */
    if(region == NULL){
        return -1;
    }

    seL4_Word left_size = region->vaddr + region->size - (seL4_Word)region->vaddr;
    seL4_Word vaddr = get_sos_virtual_address(proc->pt, (seL4_Word)src);
    seL4_Word top = (vaddr & PAGE_FRAME) + PAGE_SIZE_4K;
    size_t i = 0;
    size_t j = 0;
    while(i < left_size && i < length) {
        char c = *(char *)(vaddr + j);
        dest[i] = c;
        /* copy end */
        if(c == 0){
            return i;
        }
        i++;
        j++;
        if((vaddr + j) >= top){
            vaddr = get_sos_virtual_address(proc->pt, (seL4_Word)src + i);
            j = 0;
        }
    }
    if(dest[i - 1] != 0){
        return -1;
    }
    return i;
}

/*
 * open() - get the path with copyinstr, then use openfile_open and
 * filetable_place to do the real work.
 */
void _sys_open(proc *cur_proc) {
	printf("in open\n");
	int ret;
	seL4_Word path = seL4_GetMR(1);
	seL4_Word mode = seL4_GetMR(2);
	char str[256];
	int path_length = copyinstr(cur_proc, (char *) path, str, 256);
	if(path_length == -1){
		ret = -1;
		syscall_reply(cur_proc->reply, ret, 0);
	}

	ret = vfs_open(str, mode, 0, &vn);
	syscall_reply(cur_proc->reply, 4, 0);
}


void *_sys_read(proc *cur_proc) {
	printf("in read\n");

	seL4_Word file = seL4_GetMR(1);
	(void) file;
	seL4_Word vaddr = seL4_GetMR(2);
	seL4_Word length = seL4_GetMR(3);

	validate_virtual_address(cur_proc->as, vaddr, length, READ);

	struct uio my_uio;
	uio_init(&my_uio, vaddr, length, 0, UIO_READ, cur_proc);
	VOP_READ(vn, &my_uio);
	return NULL;
}

void *_sys_write(proc *cur_proc) {
	printf("in write\n");

	seL4_Word file = seL4_GetMR(1);
	(void) file;
	seL4_Word vaddr = seL4_GetMR(2);
	seL4_Word length = seL4_GetMR(3);

	validate_virtual_address(cur_proc->as, vaddr, length, WRITE);

	struct uio my_uio;
	uio_init(&my_uio, vaddr, length, 0, UIO_WRITE, cur_proc);
	VOP_READ(vn, &my_uio);
	return NULL;
}

void _sys_close(proc *cur_proc) {
	printf("in close\n");

	seL4_Word file = seL4_GetMR(1);

	VOP_DECREF(vn);
}