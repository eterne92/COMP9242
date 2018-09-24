#include "../addrspace.h"
#include "../pagetable.h"
#include "../proc.h"
#include "../vfs/uio.h"
#include "../vfs/vfs.h"
#include "../vfs/vnode.h"
#include "filetable.h"
#include "openfile.h"
#include "syscall.h"
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <limits.h>


/*
 * Common logic for read and write.
 *
 * Look up the fd, then use VOP_READ or VOP_WRITE.
 */
static int _sys_readwrite(proc *cur_proc, int fd, void *buf, size_t size,
                          enum uio_rw rw,
                          int badaccmode, size_t *retval)
{
    struct openfile *file;
    bool seekable;
    off_t pos;
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
    uio_uinit(&my_uio, (seL4_Word)buf, size, pos, rw, cur_proc);

    /* do the read or write */
    result = (rw == UIO_READ) ? VOP_READ(file->of_vnode,
                                         &my_uio) : VOP_WRITE(file->of_vnode, &my_uio);
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
    *retval = size - my_uio.uio_resid;

    return 0;

fail:
    filetable_put(cur_proc->openfile_table, fd, file);
    return result;
}

int _sys_do_open(proc *cur_proc, char *path, seL4_Word openflags, int at)
{
    int fd;
    int ret;
    struct openfile *file;
    printf("got name as %s\n", path);
    ret = openfile_open(path, openflags, 0, &file);
    if (ret) {
        return -1;
    }
    if(at == -1){
        ret = filetable_place(cur_proc->openfile_table, file, &fd);
    }
    else{
        struct openfile *tmp;
        filetable_placeat(cur_proc->openfile_table, file, at, &tmp);
    }
    printf("got fd is %d\n", fd);
    if (ret) {
        openfile_decref(file);
        return -1;
    }
    return fd;
}

/*
 * open() - get the path with copyinstr, then use openfile_open and
 * filetable_place to do the real work.
 */
void *_sys_open(proc *cur_proc)
{
    const int allflags = O_ACCMODE | O_CREAT | O_EXCL | O_TRUNC | O_APPEND |
                         O_NOCTTY;
    int ret = -1;
    seL4_Word path = seL4_GetMR(1);
    seL4_Word openflags = seL4_GetMR(2);
    // seL4_Word mode = seL4_GetMR(3);
    if ((openflags & allflags) != openflags) {
        /* unknown flags were set */
        syscall_reply(cur_proc->reply, ret, -1);
        return NULL;
    }
    char str[NAME_MAX + 1];
    int path_length = copystr(cur_proc, (char *)path, str, NAME_MAX + 1, COPYIN);
    if (path_length == -1) {
        syscall_reply(cur_proc->reply, ret, -1);
        return NULL;
    }

    ret = _sys_do_open(cur_proc, str, openflags, -1);

    if (ret < 0) {
        syscall_reply(cur_proc->reply, ret, -1);
        return NULL;
    }

    syscall_reply(cur_proc->reply, ret, 0);
    return NULL;
}

void *_sys_read(proc *cur_proc)
{
    seL4_Word fd = seL4_GetMR(1);
    seL4_Word vaddr = seL4_GetMR(2);
    seL4_Word length = seL4_GetMR(3);
    size_t ret;
    int result;

    bool valid = validate_virtual_address(cur_proc->as, vaddr, length, READ);
    if (valid) {
        result = _sys_readwrite(cur_proc, (int)fd, (void *)vaddr, length, UIO_READ,
                                O_WRONLY, &ret);
        if (result) {
            syscall_reply(cur_proc->reply, 0, EFAULT);
        } else {
            syscall_reply(cur_proc->reply, ret, 0);
        }
    } else {
        syscall_reply(cur_proc->reply, 0, EFAULT);
    }
    return NULL;
}

void *_sys_write(proc *cur_proc)
{
    seL4_Word fd = seL4_GetMR(1);
    seL4_Word vaddr = seL4_GetMR(2);
    seL4_Word length = seL4_GetMR(3);
    size_t ret;
    int result;
    bool valid = validate_virtual_address(cur_proc->as, vaddr, length, WRITE);

    if (valid) {
        result = _sys_readwrite(cur_proc, (int)fd, (void *)vaddr, length, UIO_WRITE,
                                O_RDONLY, &ret);
        if (result) {
            syscall_reply(cur_proc->reply, 0, EFAULT);
        } else {
            syscall_reply(cur_proc->reply, ret, 0);
        }
    } else {
        syscall_reply(cur_proc->reply, 0, EFAULT);
    }
    return NULL;
}

void *_sys_stat(proc *cur_proc)
{
    printf("in stat\n");
    struct stat st;
    mode_t result;
    int ret;
    seL4_Word path = seL4_GetMR(1);
    char str[NAME_MAX + 1];
    int path_length = copystr(cur_proc, (char *)path, str, NAME_MAX + 1, COPYIN);
    if (path_length == -1) {
        ret = -1;
        syscall_reply(cur_proc->reply, ret, 0);
        return NULL;
    }
    struct vnode *vn;
    printf("got str is %s\n", str);
    if (strcmp("..", str) == 0) {
        str[1] = 0;
    }
    result = vfs_lookup(str, &vn);
    if (result) {
        ret = -1;
        syscall_reply(cur_proc->reply, ret, 0);
        return NULL;
    }
    VOP_GETTYPE(vn, &result);
    ret = VOP_STAT(vn, &st);
    seL4_MessageInfo_t reply_msg = seL4_MessageInfo_new(0, 0, 0, 7);
    /* Set the first (and only) word in the message to 0 */
    printf("stat ret is %ld\n", st.st_size);
    seL4_SetMR(0, ret);
    seL4_SetMR(1, errno);
    seL4_SetMR(2, result);
    seL4_SetMR(3, st.st_mode);
    seL4_SetMR(4, st.st_size);
    seL4_SetMR(5, st.st_ctime);
    seL4_SetMR(6, st.st_atime);
    /* Send the reply to the saved reply capability. */
    seL4_Send(cur_proc->reply, reply_msg);
    /* Free the slot we allocated for the reply - it is now empty, as the reply
         * capability was consumed by the send. */
    cspace_free_slot(global_cspace, cur_proc->reply);
    return NULL;
}

void *_sys_close(proc *cur_proc)
{
    printf("in close\n");
    seL4_Word fd = seL4_GetMR(1);
    struct openfile *file;

    /* check if the file's in range before calling placeat */
    if (!filetable_okfd(cur_proc->openfile_table, fd)) {
        syscall_reply(cur_proc->reply, -1, EBADF);
        return NULL;
    }
    /* place null in the filetable and get the file previously there */
    filetable_placeat(cur_proc->openfile_table, NULL, fd, &file);

    if (file == NULL) {
        /* oops, it wasn't open, that's an error */
        syscall_reply(cur_proc->reply, -1, EBADF);
        return NULL;
    }
    /* drop the reference */
    openfile_decref(file);
    syscall_reply(cur_proc->reply, 0, 0);
    return NULL;
}

struct vnode *get_bootfs_vnode(void);

void *_sys_getdirent(proc *cur_proc)
{
    int pos = (int)seL4_GetMR(1);
    seL4_Word path = seL4_GetMR(2);
    size_t nbytes = (size_t)seL4_GetMR(3);
    if (validate_virtual_address(cur_proc->as, path, nbytes, READ)) {
        struct vnode *vn;

        int ret, err = 0;
        vn = get_bootfs_vnode();
        struct uio my_uio;
        uio_uinit(&my_uio, path, nbytes, pos, UIO_READ, cur_proc);
        ret = VOP_GETDIRENTRY(vn, &my_uio);
        err = !ret ? 0 : EFAULT;
        syscall_reply(cur_proc->reply, ret, err);
    } else {
        syscall_reply(cur_proc->reply, -1, EFAULT);
    }
    return NULL;
}