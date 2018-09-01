#include "syscall.h"
#include "pagetable.h"
#include "proc.h"
#include "addrspace.h"
#include "vfs/stat.h"
#include "vfs/vnode.h"
#include "vfs/vfs.h"
#include "vfs/uio.h"
#include <fcntl.h>
#include <errno.h>


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
 * Common logic for read and write.
 *
 * Look up the fd, then use VOP_READ or VOP_WRITE.
 */
static int _sys_readwrite(proc *cur_proc, int fd, void *buf, size_t size, enum uio_rw rw,
                 		 int badaccmode, ssize_t *retval)
{
    struct openfile *file;
    bool seekable;
    off_t pos;
    struct iovec iov;
    struct uio useruio;
    int result;

    /* better be a valid file descriptor */
    result = filetable_get(cur_proc->openfile_table, fd, &file);
    if (result) {
        return result;
    }

    seekable = VOP_ISSEEKABLE(file->of_vnode);
	pos = seekable ? file->of_offset : 0;

    if (file->of_accmode == badaccmode) {
        result = EBADF;
        goto fail;
    }

    /* set up a uio with the buffer, its size, and the current offset */

	struct uio my_uio;
	uio_init(&my_uio, (seL4_Word) buf, size, pos, rw, cur_proc);

    /* do the read or write */
    result = (rw == UIO_READ) ?
             VOP_READ(file->of_vnode, &my_uio) :
             VOP_WRITE(file->of_vnode, &my_uio);
    if (result) {
        goto fail;
    }

    if (seekable) {
        /* set the offset to the updated offset in the uio */
        file->of_offset = my_uio.uio_offset;
    }

    filetable_put(cur_proc->openfile_table, fd, file);

    /*
     * The amount read (or written) is the original buffer size,
     * minus how much is left in it.
     */
    *retval = size - useruio.uio_resid;

    return 0;

fail:
    filetable_put(cur_proc->openfile_table, fd, file);
    return result;
}

/*
 * open() - get the path with copyinstr, then use openfile_open and
 * filetable_place to do the real work.
 */
void *_sys_open(proc *cur_proc) {
	printf("in open\n");
	const int allflags = O_ACCMODE | O_CREAT | O_EXCL | O_TRUNC | O_APPEND | O_NOCTTY;
	int ret = -1;
	seL4_Word path = seL4_GetMR(1);
	seL4_Word openflags = seL4_GetMR(2);
	seL4_Word mode = seL4_GetMR(3);
	struct openfile *file;
	if ((openflags & allflags) != flags) {
        /* unknown flags were set */
        syscall_reply(cur_proc->reply, ret, -1);
    }
	char str[257];
	int path_length = copyinstr(cur_proc, (char *) path, str, 256);
	if(path_length == -1) {
		syscall_reply(cur_proc->reply, ret, -1);
	}

	ret = openfile_open(str, openflags, (mode_t) mode, &file);
    if (ret) {
		syscall_reply(cur_proc->reply, ret, -1);
    }

    /*
     * Place the file in our process's file table, which gives us
     * the result file descriptor.
     */
    ret = filetable_place(curproc->p_filetable, file, retval);
    if (ret) {
        openfile_decref(file);
		syscall_reply(cur_proc->reply, -1, -1);
    }

	syscall_reply(cur_proc->reply, ret, 0);
	return NULL;
}


void *_sys_read(proc *cur_proc) {
	printf("in read\n");
	seL4_Word fd = seL4_GetMR(1);
	seL4_Word vaddr = seL4_GetMR(2);
	seL4_Word length = seL4_GetMR(3);
	int ret;

	if(validate_virtual_address(cur_proc->as, vaddr, length, READ)) {
		_sys_readwrite(cur_proc, (int)fd, (void *)vaddr, length, UIO_READ, O_WRONLY, &ret);
	} else {
		syscall_reply(cur_proc->reply, -1, EFAULT);
	}
	return NULL;
}

void *_sys_write(proc *cur_proc) {
	printf("in write\n");
	seL4_Word fd = seL4_GetMR(1);
	seL4_Word vaddr = seL4_GetMR(2);
	seL4_Word length = seL4_GetMR(3);

	if(validate_virtual_address(cur_proc->as, vaddr, length, WRITE)) {
		_sys_readwrite(cur_proc, (int)fd, (void *)vaddr, length, UIO_WRITE, O_RDONLY, &ret);
	} else {
		syscall_reply(cur_proc->reply, -1, EFAULT);
	}
	return NULL;
}

void *_sys_stat(proc *cur_proc)
{
	printf("in stat\n");
	struct stat;
	mode_t result;
	int ret;
	seL4_Word path = seL4_GetMR(1);
	char str[257];
	int path_length = copyinstr(cur_proc, (char *) path, str, 256);
	if(path_length == -1) {
		ret = -1;
		syscall_reply(cur_proc->reply, ret, 0);
	}
	VOP_GETTYPE(vn, &result);
	ret = VOP_STAT(vn, &stat);
	seL4_MessageInfo_t reply_msg = seL4_MessageInfo_new(0, 0, 0, 7);
    /* Set the first (and only) word in the message to 0 */
    seL4_SetMR(0, ret);
    seL4_SetMR(1, errno);
	seL4_SetMR(2, result);
	seL4_SetMR(3, stat.st_mode);
	seL4_SetMR(4, stat.st_size);
	seL4_SetMR(5, stat.st_ctime);
	seL4_SetMR(6, stat.st_atime);
    /* Send the reply to the saved reply capability. */
    seL4_Send(reply, reply_msg);
    /* Free the slot we allocated for the reply - it is now empty, as the reply
         * capability was consumed by the send. */
    cspace_free_slot(global_cspace, cur_proc->reply);
	return NULL;
}

void *_sys_close(proc *cur_proc) 
{
	printf("in close\n");

	seL4_Word file = seL4_GetMR(1);

	VOP_DECREF(vn);
	syscall_reply(cur_proc->reply, 0, 0);
	return NULL;
}