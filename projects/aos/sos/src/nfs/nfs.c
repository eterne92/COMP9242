/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* wrapper of nfs */


#include "../vfs/type.h"
#include <errno.h>
#include <fcntl.h>
#include "../vfs/stat.h"
#include "../vfs/array.h"
#include "../vfs/uio.h"
#include "../vfs/vfs.h"
#include "nfsfs.h"
#include "nfs.h"
#include <nfsc/libnfs.h>
#include "../syscall/syscall.h"
#include <nfsc/libnfs.h>
#include <picoro/picoro.h>

struct nfs_vnode *nfs_vn = NULL;

void nfs_mount_cb(int status, UNUSED struct nfs_context *nfs, void *data,
                  UNUSED void *private_data);

static int nfs_loadvnode(struct vnode *root, const char *name, bool creat,struct nfs_vnode **ret);

static void nfs_open_cb(int status, UNUSED struct nfs_context *nfs, void *data,
						void *private_data)
{
	struct nfs_cb *cb = private_data;
	if(status != 0){
		cb->status = status;
		cb->handle = NULL;
	}
	cb->handle = data;
}

static void nfs_creat_cb(int status, UNUSED struct nfs_context *nfs, void *data,
						void *private_data)
{
	struct nfs_cb *cb = private_data;
	if(status != 0){
		cb->status = status;
		cb->handle = NULL;
	}
	cb->handle = data;
}

static void nfs_open_cb(int status, UNUSED struct nfs_context *nfs, void *data,
						void *private_data)
{
	struct nfs_cb *cb = private_data;
	cb->handle = NULL;
	cb->status = status;
}

//
// vnode functions
//

/*
 * VOP_EACHOPEN on files
 */
static
int
_nfs_eachopen(struct vnode *v, int openflags)
{
	(void)v;
	(void)openflags;
	return 0;
}


static void nfs_close_cb(int status, UNUSED struct nfs_context *nfs, void *data,
						void *private_data)
{
	struct nfs_cb *cb = private_data;
	cb->handle = NULL;
}

/*
 * VOP_RECLAIM
 *
 */
static
int
_nfs_reclaim(struct vnode *v)
{
	struct nfs_vnode *nv = v->vn_data;
	struct nfs_fs *nf = v->vn_fs->fs_data;
	struct nfs_cb cb;
	unsigned ix, i, num;
	int result;

	num = vnodearray_num(nf->nfs_vnodes);
	ix = num;
	for (i=0; i<num; i++) {
		struct vnode *vx;

		vx = vnodearray_get(nf->nfs_vnodes, i);
		if (vx == v) {
			ix = i;
			break;
		}
	}
	if (ix == num) {
		/* should never hanppen */
		ZF_LOG_E("vnode not exist\n");
	}

	vnodearray_remove(nf->nfs_vnodes, ix);
	vnode_cleanup(&nv->nv_v);

	free(nv);

	cb.status = 0;
	cb.handle = nv->handle;
	/* should not got something wrong, but we still track it */
	result = nfs_close_async(nf->context, nv->handle, nfs_close_cb, &cb);
	if (result) {
		return result;
	}

	while(nv->handle){
		yield(NULL);
	}

	if(cb.status != 0){
		return cb.status;
	}


	return 0;
}

static void nfs_read_cb(int status, UNUSED struct nfs_context *nfs, void *data,
						void *private_data)
{
	struct nfs_cb *cb = private_data;
	cb->status = status;
	cb->handle = data;
}
/*
 * VOP_READ
 */
static
int
_nfs_read(struct vnode *v, struct uio *uio)
{
	struct nfs_vnode *nv = v->vn_data;
	struct nfs_fs *nf = v->vn_fs->fs_data;
	struct nfs_cb cb;
	void *ret_data;
	int result;

	assert(uio->uio_rw==UIO_READ);

	/* read frame by frame */
	seL4_Word sos_vaddr, user_vaddr = uio->vaddr;
    size_t n = PAGE_SIZE_4K - (uio->vaddr & PAGE_MASK_4K);
	int nbytes = 0, count;
    if (uio->uio_resid < n) {
        n = uio->uio_resid;
    }

	while (uio->uio_resid > 0) {
		sos_vaddr = get_sos_virtual_address(uio->proc->pt, user_vaddr);

		if (sos_vaddr == 0) {
			printf("handle vm fault\n");
			handle_page_fault(uio->proc, user_vaddr, 0);
			sos_vaddr = get_sos_virtual_address(uio->proc->pt, user_vaddr);
		}

		// read n bytes
		count = n;
		cb.status = 0;
		cb.handle = nv->handle;

		result = nfs_pread_async(nf->context,nv->handle,uio->uio_offset, 
								 count, nfs_read_cb, &cb);
		if(result){
			syscall_reply(uio->proc->reply, -1, nfs_get_error(nf->context));
			return result;
		}
		/* wait until callback done */
		while(cb.status == 0){
			yield(NULL);
		}
		/* callback got sth wrong */
		if(cb.status < 0){
			syscall_reply(uio->proc->reply, -1, nfs_get_error(nf->context));
			return 0;
		}

		nbytes = cb.status;
		/* use handle to get the return data pointer */
		ret_data = cb.handle;
		result = memcpy((void *) sos_vaddr, ret_data, nbytes);
		if(nbytes < count){
			/* it's over */
			uio->uio_resid -= nbytes;
			syscall_reply(uio->proc->reply, uio->length - uio->uio_resid, 0);
		}

		uio->uio_resid -= nbytes;
		user_vaddr += n;
		n = uio->uio_resid > PAGE_SIZE_4K ? PAGE_SIZE_4K : uio->uio_resid;
		yield(NULL);
	}
	syscall_reply(uio->proc->reply, uio->length - uio->uio_resid, 0);
	return 0;
}

/*
 * VOP_READDIR
 */
static
int
_nfs_getdirentry(struct vnode *v, struct uio *uio)
{
	struct nfs_vnode *nv = v->vn_data;
	struct nfs_fs *nf = v->vn_fs->fs_data;


	return emu_readdir(ev->ev_emu, ev->ev_handle, amt, uio);
}

static void nfs_write_cb(int status, UNUSED struct nfs_context *nfs, void *data,
						void *private_data)
{
	struct nfs_cb *cb = private_data;
	cb->status = status;
}

/*
 * VOP_WRITE
 */

static
int
_nfs_write(struct vnode *v, struct uio *uio)
{
	struct nfs_vnode *nv = v->vn_data;
	struct nfs_fs *nf = v->vn_fs->fs_data;
	struct nfs_cb cb;
	void *ret_data;
	int result;

	assert(uio->uio_rw==UIO_WRITE);

	/* read frame by frame */
	seL4_Word sos_vaddr, user_vaddr = uio->vaddr;
    size_t n = PAGE_SIZE_4K - (uio->vaddr & PAGE_MASK_4K);
	int nbytes = 0, count;
    if (uio->uio_resid < n) {
        n = uio->uio_resid;
    }

	while (uio->uio_resid > 0) {
		sos_vaddr = get_sos_virtual_address(uio->proc->pt, user_vaddr);

		if (sos_vaddr == 0) {
			printf("handle vm fault\n");
			handle_page_fault(uio->proc, user_vaddr, 0);
			sos_vaddr = get_sos_virtual_address(uio->proc->pt, user_vaddr);
		}

		// read n bytes
		count = n;
		cb.status = 0;
		cb.handle = nv->handle;

		result = nfs_pwrite_async(nf->context,nv->handle,uio->uio_offset, 
								  (void *) sos_vaddr, count, nfs_write_cb, &cb);
		if(result){
			syscall_reply(uio->proc->reply, -1, -1);
			return result;
		}
		/* wait until callback done */
		while(cb.status == 0){
			yield(NULL);
		}
		/* callback got sth wrong */
		if(cb.status < 0){
			syscall_reply(uio->proc->reply, -1, cb.status);
			return 0;
		}

		nbytes = cb.status;
		/* use handle to get the return data pointer */
		ret_data = cb.handle;
		if(nbytes < count){
			/* it's over */
			uio->uio_resid -= nbytes;
			syscall_reply(uio->proc->reply, uio->length - uio->uio_resid, 0);
		}

		uio->uio_resid -= nbytes;
		user_vaddr += n;
		n = uio->uio_resid > PAGE_SIZE_4K ? PAGE_SIZE_4K : uio->uio_resid;
		yield(NULL);
	}
	syscall_reply(uio->proc->reply, uio->length - uio->uio_resid, 0);
	return 0;
}

/*
 * VOP_IOCTL
 */
static
int
_nfs_ioctl(struct vnode *v, int op, void *data)
{
	/*
	 * No ioctls.
	 */

	(void)v;
	(void)op;
	(void)data;

	return EINVAL;
}

static void nfs_stat_cb(int status, UNUSED struct nfs_context *nfs, void *data,
						void *private_data)
{
	struct nfs_cb *cb = private_data;
	cb->status = status;
	cb->handle = data;
}
/*
 * VOP_STAT
 */
static
int
_nfs_stat(struct vnode *v, struct stat *statbuf)
{
	struct nfs_vnode *nv = v->vn_data;
	struct nfs_fs *nf = v->vn_fs->fs_data;
	struct nfs_cb cb;
	int result;

	cb.status = 0;
	cb.handle = NULL;

	result = nfs_fstat64_async(nf->context, nv->handle, nfs_stat_cb, &cb);
	if (result) {
		return result;
	}

	while(cb.status == 0 && cb.handle == NULL){
		yield(NULL);
	}


	if(cb.status != 0){
		return cb.status;
	}

	struct nfs_stat_64 *retstat = (struct nfs_stat_64 *)cb.handle;

	statbuf->st_atime = retstat->nfs_atime;
	statbuf->st_ctime = retstat->nfs_ctime;
	statbuf->st_size = retstat->nfs_size;
	statbuf->st_mode = retstat->nfs_mode;

	return 0;
}

/*
 * VOP_GETTYPE for files
 */
static
int
_nfs_file_gettype(struct vnode *v, uint32_t *result)
{
	(void)v;
	*result = 1; /* plain file */
	return 0;
}

/*
 * VOP_GETTYPE for directories
 */
static
int
_nfs_dir_gettype(struct vnode *v, uint32_t *result)
{
	(void)v;
	(void)result;
	// *result = S_IFDIR;
	/* there is no dir */
	return -1;
}

/*
 * VOP_ISSEEKABLE
 */
static
bool
_nfs_isseekable(struct vnode *v)
{
	(void)v;
	return true;
}

/*
 * VOP_FSYNC
 */
static
int
_nfs_fsync(struct vnode *v)
{
	(void)v;
	return 0;
}

/*
 * VOP_TRUNCATE
 */
static
int
_nfs_truncate(struct vnode *v, off_t len)
{
	// struct nfs_vnode *ev = v->vn_data;
	// return emu_trunc(ev->ev_emu, ev->ev_handle, len);
	(void) v;
	(void) len;
	/* trunc not support */
	return -1;
}

/*
 * VOP_CREAT
 */
/* nfs have a flat filesystem, so no dir needed, dir vnode is always root */
static
int
_nfs_creat(struct vnode *root, const char *name, bool excl, mode_t mode,
	    struct vnode **ret)
{
	struct nfs_fs *nf = root->vn_fs->fs_data;
	struct nfs_vnode *newguy;
	int result;

	result = nfs_loadvnode(root, name, true, &newguy);
	if(result != 0){
		return result;
	}

	*ret = &newguy->nv_v;
	return 0;
}

/*
 * VOP_LOOKUP
 */
static
int
_nfs_lookup(struct vnode *root, char *pathname, struct vnode **ret)
{
	struct nfs_fs *nf = root->vn_fs->fs_data;
	struct nfs_vnode *newguy;
	int result;

	result = nfs_loadvnode(root, pathname, false, &newguy);
	if(result != 0){
		return result;
	}

	*ret = &newguy->nv_v;
	return 0;
}

/*
 * VOP_LOOKPARENT
 */
static
int
_nfs_lookparent(struct vnode *dir, char *pathname, struct vnode **ret,
		 char *buf, size_t len)
{
	(void) dir;
	(void) pathname;
	(void) ret;
	(void) buf;
	(void) len;
	/* should never call lookparent now */
	return -1;
}

/*
 * VOP_NAMEFILE
 */
static
int
_nfs_namefile(struct vnode *v, struct uio *uio)
{
	(void) v;
	(void) uio;
	return ENOSYS;
}

/*
 * VOP_MMAP
 */
/* TODO: done it later */
static
int
_nfs_mmap(struct vnode *v)
{
	(void)v;
	return ENOSYS;
}

//////////////////////////////

/*
 * Bits not implemented at all on nfs
 */

static
int
_nfs_symlink(struct vnode *v, const char *contents, const char *name)
{
	(void)v;
	(void)contents;
	(void)name;
	return ENOSYS;
}

static
int
_nfs_mkdir(struct vnode *v, const char *name, mode_t mode)
{
	(void)v;
	(void)name;
	(void)mode;
	return ENOSYS;
}

static
int
_nfs_link(struct vnode *v, const char *name, struct vnode *target)
{
	(void)v;
	(void)name;
	(void)target;
	return ENOSYS;
}

static
int
_nfs_remove(struct vnode *v, const char *name)
{
	(void)v;
	(void)name;
	return ENOSYS;
}

static
int
_nfs_rmdir(struct vnode *v, const char *name)
{
	(void)v;
	(void)name;
	return ENOSYS;
}

static
int
_nfs_rename(struct vnode *v1, const char *n1,
	     struct vnode *v2, const char *n2)
{
	(void)v1;
	(void)n1;
	(void)v2;
	(void)n2;
	return ENOSYS;
}

//////////////////////////////

/*
 * Routines that fail
 *
 * It is kind of silly to write these out each with their particular
 * arguments; however, portable C doesn't let you cast function
 * pointers with different argument signatures even if the arguments
 * are never used.
 *
 * The BSD approach (all vnode ops take a vnode pointer and a void
 * pointer that's cast to a op-specific args structure) avoids this
 * problem but is otherwise not very appealing.
 */

static
int
_nfs_void_op_isdir(struct vnode *v)
{
	(void)v;
	return EISDIR;
}

static
int
_nfs_uio_op_isdir(struct vnode *v, struct uio *uio)
{
	(void)v;
	(void)uio;
	return EISDIR;
}

static
int
_nfs_uio_op_notdir(struct vnode *v, struct uio *uio)
{
	(void)v;
	(void)uio;
	return ENOTDIR;
}

static
int
_nfs_name_op_notdir(struct vnode *v, const char *name)
{
	(void)v;
	(void)name;
	return ENOTDIR;
}

static
int
_nfs_readlink_notlink(struct vnode *v, struct uio *uio)
{
	(void)v;
	(void)uio;
	return EINVAL;
}

static
int
_nfs_creat_notdir(struct vnode *v, const char *name, bool excl, mode_t mode,
		   struct vnode **retval)
{
	(void)v;
	(void)name;
	(void)excl;
	(void)mode;
	(void)retval;
	return ENOTDIR;
}

static
int
_nfs_symlink_notdir(struct vnode *v, const char *contents, const char *name)
{
	(void)v;
	(void)contents;
	(void)name;
	return ENOTDIR;
}

static
int
_nfs_mkdir_notdir(struct vnode *v, const char *name, mode_t mode)
{
	(void)v;
	(void)name;
	(void)mode;
	return ENOTDIR;
}

static
int
_nfs_link_notdir(struct vnode *v, const char *name, struct vnode *target)
{
	(void)v;
	(void)name;
	(void)target;
	return ENOTDIR;
}

static
int
_nfs_rename_notdir(struct vnode *v1, const char *n1,
		    struct vnode *v2, const char *n2)
{
	(void)v1;
	(void)n1;
	(void)v2;
	(void)n2;
	return ENOTDIR;
}

static
int
_nfs_lookup_notdir(struct vnode *v, char *pathname, struct vnode **result)
{
	(void)v;
	(void)pathname;
	(void)result;
	return ENOTDIR;
}

static
int
_nfs_lookparent_notdir(struct vnode *v, char *pathname, struct vnode **result,
			char *buf, size_t len)
{
	(void)v;
	(void)pathname;
	(void)result;
	(void)buf;
	(void)len;
	return ENOTDIR;
}


static
int
_nfs_truncate_isdir(struct vnode *v, off_t len)
{
	(void)v;
	(void)len;
	return ENOTDIR;
}

//////////////////////////////

/*
 * Function table for nfs files.
 */
static const struct vnode_ops nfs_fileops = {
	.vop_magic = VOP_MAGIC,	/* mark this a valid vnode ops table */

	.vop_eachopen = _nfs_eachopen,
	.vop_reclaim = _nfs_reclaim,

	.vop_read = _nfs_read,
	.vop_readlink = _nfs_readlink_notlink,
	.vop_getdirentry = _nfs_uio_op_notdir,
	.vop_write = _nfs_write,
	.vop_ioctl = _nfs_ioctl,
	.vop_stat = _nfs_stat,
	.vop_gettype = _nfs_file_gettype,
	.vop_isseekable = _nfs_isseekable,
	.vop_fsync = _nfs_fsync,
	.vop_mmap = _nfs_mmap,
	.vop_truncate = _nfs_truncate,
	.vop_namefile = _nfs_uio_op_notdir,

	.vop_creat = _nfs_creat_notdir,
	.vop_symlink = _nfs_symlink_notdir,
	.vop_mkdir = _nfs_mkdir_notdir,
	.vop_link = _nfs_link_notdir,
	.vop_remove = _nfs_name_op_notdir,
	.vop_rmdir = _nfs_name_op_notdir,
	.vop_rename = _nfs_rename_notdir,

	.vop_lookup = _nfs_lookup_notdir,
	.vop_lookparent = _nfs_lookparent_notdir,
};

/*
 * Function table for nfs directories.
 */
static const struct vnode_ops nfs_dirops = {
	.vop_magic = VOP_MAGIC,	/* mark this a valid vnode ops table */

	.vop_eachopen = _nfs_eachopen,
	.vop_reclaim = _nfs_reclaim,

	.vop_read = _nfs_uio_op_isdir,
	.vop_readlink = _nfs_uio_op_isdir,
	.vop_getdirentry = _nfs_getdirentry,
	.vop_write = _nfs_uio_op_isdir,
	.vop_ioctl = _nfs_ioctl,
	.vop_stat = _nfs_stat,
	.vop_gettype = _nfs_dir_gettype,
	.vop_isseekable = _nfs_isseekable,
	.vop_fsync = _nfs_void_op_isdir,
	.vop_mmap = _nfs_void_op_isdir,
	.vop_truncate = _nfs_truncate_isdir,
	.vop_namefile = _nfs_namefile,

	.vop_creat = _nfs_creat,
	.vop_symlink = _nfs_symlink,
	.vop_mkdir = _nfs_mkdir,
	.vop_link = _nfs_link,
	.vop_remove = _nfs_remove,
	.vop_rmdir = _nfs_rmdir,
	.vop_rename = _nfs_rename,

	.vop_lookup = _nfs_lookup,
	.vop_lookparent = _nfs_lookparent,
};

/*
 * Function to load a vnode into memory.
 */
int
nfs_loadvnode(struct vnode *root, const char *name, bool creat, struct nfs_vnode **ret)
{
	struct vnode *v;
	struct nfs_vnode *nv;
	unsigned i, num;
	int result;

	struct nfs_fs *nf = root->vn_fs->fs_data;

	num = vnodearray_num(nf->nfs_vnodes);
	for (i=0; i<num; i++) {
		v = vnodearray_get(nf->nfs_vnodes, i);
		nv = v->vn_data;
		if (strcmp(name, nv->filename) == 0) {
			/* Found */
			if(creat){
				/* shouldn't create sth already exist */
				return EINVAL;
			}
			VOP_INCREF(&nv->nv_v);

			*ret = nv;
			return 0;
		}
	}

	/* Didn't have one; create it */

	/* async open file */
	struct nfs_cb cb;
	cb.handle = NULL;
	cb.status = 0;

	if(creat){
		result = nfs_creat_async(nf->context, name, O_RDWR, nfs_creat_cb, &cb);
	}
	else{
		result = nfs_open_async(nf->context, name, O_RDWR, nfs_open_cb, &cb);
	}

	if (result)
	{
		return result;
	}
	/* wait until callback is done */
	while (cb.handle == NULL && cb.status == 0)
	{
		yield(NULL);
	}

	/* something wrong with open callback */
	if(cb.status != 0){
		*ret = NULL;
		return cb.status;
	}
	/* init a nfs_vnode */
	nv = malloc(sizeof(struct nfs_vnode));
	if (nv==NULL) {
		return ENOMEM;
	}

	nv->handle = cb.handle;
	strcpy(nv->filename, name);

	/* since we do root node seperately, this node is always a file node */
	result = vnode_init(&nv->nv_v, &nfs_fileops, &nf->nfs_fsdata, nv);
	if (result) {
		free(nv);
		return result;
	}

	result = vnodearray_add(nf->nfs_vnodes, &nv->nv_v, NULL);
	if (result) {
		/* note: vnode_cleanup undoes vnode_init - it does not kfree */
		vnode_cleanup(&nv->nv_v);
		free(nv);
		return result;
	}

	*ret = nv;
	return 0;
}


////////////////////////////////////////////////////////////
//
// Whole-filesystem functions
//

/*
 * FSOP_SYNC
 */
static
int
nfs_sync(struct fs *fs)
{
	(void)fs;
	return 0;
}

/*
 * FSOP_GETVOLNAME
 */
static
const char *
nfs_getvolname(struct fs *fs)
{
	/* We don't have a volume name beyond the device name */
	(void)fs;
	return NULL;
}

/*
 * FSOP_GETROOT
 */
static
int
nfs_getroot(struct fs *fs, struct vnode **ret)
{
	assert(fs != NULL);
	assert(nfs_vn != NULL);
	VOP_INCREF(&nfs_vn->nv_v);
	*ret = &nfs_vn->nv_v;
	return 0;
}

/*
 * FSOP_UNMOUNT
 */
static
int
nfs_unmount(struct fs *fs)
{
	/* Always prohibit unmount, as we're not really "mounted" */
	(void)fs;
	return EBUSY;
}

/*
 * Function table for the nfs file system.
 */
static const struct fs_ops nfs_fsops = {
	.fsop_sync = nfs_sync,
	.fsop_getvolname = nfs_getvolname,
	.fsop_getroot = nfs_getroot,
	.fsop_unmount =nfs_unmount,
};


void change_bootfs(struct vnode *newvn);

void nfs_mount_cb(int status, struct nfs_context *nfs, void *data, UNUSED void *private_data)
{
	if (status < 0) {
        ZF_LOGF("mount/mnt call failed with \"%s\"\n", (char *)data);
    }

    printf("Mounted nfs dir %s\n", SOS_NFS_DIR);
	struct vnode *vn;
	vn = nfs_bootstrap(nfs);
	change_bootfs(vn);
	
}

struct vnode *nfs_bootstrap(struct nfs_context *context)
{
	nfs_vn = (struct nfs_vnode *)malloc(sizeof(struct nfs_vnode));
	if (!nfs_vn) {
		ZF_LOG_E("Out of Memory!");
		return NULL;
	}
	struct nfs_fs *fs = (struct nfs_fs *)malloc(sizeof(fs));
	if (!fs) {
		free(nfs_vn);
		ZF_LOG_E("Out of Memory!");
		return NULL;
	}
	fs->nfs_vnodes = vnodearray_create();
	if (!fs->nfs_vnodes) {
		free(nfs_vn);
		free(fs);
		ZF_LOG_E("Out of Memory!");
		return NULL;
	}
	fs->nfs_fsdata.fs_ops = &nfs_fsops;
	fs->nfs_fsdata.fs_data = fs;
	fs->context = context;
	vnode_init(&nfs_vn->nv_v, &nfs_dirops, &fs->nfs_fsdata, nfs_vn);
	return &nfs_vn->nv_v;
}
