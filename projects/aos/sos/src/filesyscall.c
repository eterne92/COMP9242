#include "syscall.h"
#include "pagetable.h"
#include "proc.h"
#include "addrspace.h"
#include <muslc/fcntl.h>

/* only used to copy string size less then 256 */
/* so we won't go through too many frame */
static int copyinstr(proc *proc, const char *src, char *dest, size_t length){
    /* get region */
    as_region *region = vaddr_get_region(cur_proc->as, (seL4_Word)src);
    /* not valid */
    if(region == NULL){
        return -1;
    }

    seL4_Word left_size = region->vaddr + region->size - (seL4_Word)vaddr;
    seL4_Word vaddr = get_sos_virtual_address(proc->pt, (seL4_Word)src);
    seL4_Word top = (vaddr & PAGE_FRAME) + PAGE_SIZE_4K;
    size_t i = 0;
    size_t j = 0;
    while(i < left_size && i < length) {
        char c = *(char *)(vaddr + j)
        dest[i] = c;
        /* copy end */
        if(c == 0){
            return i;
        }
        i++;
        j++;
        if((vaddr + j) >= top){
            seL4_Word vaddr = get_sos_virtual_address(proc->pt, (seL4_Word)src + i);
            seL4_Word top = (vaddr & PAGE_FRAME) + PAGE_SIZE_4K;
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
void _sys_open(void) {

}

int
sys_open(const char *upath, int mode)
{
    proc *cur_proc = get_cur_proc();

	char kpath[256];
	// struct openfile *file;
	// int result;

	/* Get the pathname. */
    copyinstr(cur_proc, upath, kpath, 256);

    printf("kpath %s\n", kpath);

    struct vnode *vn;
	int result;

	result = vfs_open(filename, openflags, mode, &vn);
	if (result) {
		return result;
	} 

	/*
	 * Open the file. Code lower down (in vfs_open) checks that
	 * flags & O_ACCMODE is a valid value.
	 */
	// result = openfile_open(kpath, flags, mode, &file);
	// if (result) {
	// 	kfree(kpath);
	// 	return result;
	// }
	// kfree(kpath);

	/*
	 * Place the file in our process's file table, which gives us
	 * the result file descriptor.
	 */
	// result = filetable_place(curproc->p_filetable, file, retval);
	// if (result) {
	// 	openfile_decref(file);
	// 	return result;
	// }

}

/*
 * Common logic for read and write.
 *
 * Look up the fd, then use VOP_READ or VOP_WRITE.
 */
static
int
sys_readwrite(int fd, userptr_t buf, size_t size, enum uio_rw rw,
	      int badaccmode, ssize_t *retval)
{
	struct openfile *file;
	bool locked;
	off_t pos;
	struct iovec iov;
	struct uio useruio;
	int result;

	/* better be a valid file descriptor */
	result = filetable_get(curproc->p_filetable, fd, &file);
	if (result) {
		return result;
	}

	/* Only lock the seek position if we're really using it. */
	locked = VOP_ISSEEKABLE(file->of_vnode);
	if (locked) {
		lock_acquire(file->of_offsetlock);
		pos = file->of_offset;
	}
	else {
		pos = 0;
	}

	if (file->of_accmode == badaccmode) {
		result = EBADF;
		goto fail;
	}

	/* set up a uio with the buffer, its size, and the current offset */
	uio_uinit(&iov, &useruio, buf, size, pos, rw);

	/* do the read or write */
	result = (rw == UIO_READ) ?
		VOP_READ(file->of_vnode, &useruio) :
		VOP_WRITE(file->of_vnode, &useruio);
	if (result) {
		goto fail;
	}

	if (locked) {
		/* set the offset to the updated offset in the uio */
		file->of_offset = useruio.uio_offset;
		lock_release(file->of_offsetlock);
	}

	filetable_put(curproc->p_filetable, fd, file);

	/*
	 * The amount read (or written) is the original buffer size,
	 * minus how much is left in it.
	 */
	*retval = size - useruio.uio_resid;

	return 0;

fail:
	if (locked) {
		lock_release(file->of_offsetlock);
	}
	filetable_put(curproc->p_filetable, fd, file);
	return result;
}

/*
 * read() - use sys_readwrite
 */
int
sys_read(int fd, userptr_t buf, size_t size, int *retval)
{
	return sys_readwrite(fd, buf, size, UIO_READ, O_WRONLY, retval);
}

/*
 * write() - use sys_readwrite
 */
int
sys_write(int fd, userptr_t buf, size_t size, int *retval)
{
	return sys_readwrite(fd, buf, size, UIO_WRITE, O_RDONLY, retval);
}

/*
 * close() - remove from the file table.
 */
int
sys_close(int fd)
{
	struct filetable *ft;
	struct openfile *file;

	ft = curproc->p_filetable;

	/* check if the file's in range before calling placeat */
	if (!filetable_okfd(ft, fd)) {
		return EBADF;
	}

	/* place null in the filetable and get the file previously there */
	filetable_placeat(ft, NULL, fd, &file);

	if (file == NULL) {
		/* oops, it wasn't open, that's an error */
		return EBADF;
	}

	/* drop the reference */
	openfile_decref(file);
	return 0;
}

/*
 * lseek() - manipulate the seek position.
 */

// int
// sys_lseek(int fd, off_t offset, int whence, off_t *retval)
// {
// 	struct stat info;
// 	struct openfile *file;
// 	int result;

// 	/* Get the open file. */
// 	result = filetable_get(curproc->p_filetable, fd, &file);
// 	if (result) {
// 		return result;
// 	}

// 	/* If it's not a seekable object, forget about it. */
// 	if (!VOP_ISSEEKABLE(file->of_vnode)) {
// 		filetable_put(curproc->p_filetable, fd, file);
// 		return ESPIPE;
// 	}

// 	/* Lock the seek position. */
// 	lock_acquire(file->of_offsetlock);

// 	/* Compute the new position. */
// 	switch (whence) {
// 	    case SEEK_SET:
// 		*retval = offset;
// 		break;
// 	    case SEEK_CUR:
// 		*retval = file->of_offset + offset;
// 		break;
// 	    case SEEK_END:
// 		result = VOP_STAT(file->of_vnode, &info);
// 		if (result) {
// 			lock_release(file->of_offsetlock);
// 			filetable_put(curproc->p_filetable, fd, file);
// 			return result;
// 		}
// 		*retval = info.st_size + offset;
// 		break;
// 	    default:
// 		lock_release(file->of_offsetlock);
// 		filetable_put(curproc->p_filetable, fd, file);
// 		return EINVAL;
// 	}

// 	/* If the resulting position is negative (which is invalid) fail. */
// 	if (*retval < 0) {
// 		lock_release(file->of_offsetlock);
// 		filetable_put(curproc->p_filetable, fd, file);
// 		return EINVAL;
// 	}

// 	/* Success -- update the file structure with the new position. */
// 	file->of_offset = *retval;

// 	lock_release(file->of_offsetlock);
// 	filetable_put(curproc->p_filetable, fd, file);

// 	return 0;
// }