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
#include <nfsc/libnfs.h>

struct nfs_vnode *nfs_vn = NULL;
/*
 * Common file open routine (for both VOP_LOOKUP and VOP_CREATE).  Not
 * for VOP_EACHOPEN. At the hardware level, we need to "open" files in
 * order to look at them, so by the time VOP_EACHOPEN is called the
 * files are already open.
 */
static
int
nfs_open(struct emu_softc *sc, uint32_t handle, const char *name,
	 bool create, bool excl, mode_t mode,
	 uint32_t *newhandle, int *newisdir)
{
	uint32_t op;
	int result;

	if (strlen(name)+1 > EMU_MAXIO) {
		return ENAMETOOLONG;
	}

	if (create && excl) {
		op = EMU_OP_EXCLCREATE;
	}
	else if (create) {
		op = EMU_OP_CREATE;
	}
	else {
		op = EMU_OP_OPEN;
	}

	/* mode isn't supported (yet?) */
	(void)mode;

	lock_acquire(sc->e_lock);

	strcpy(sc->e_iobuf, name);
	membar_store_store();
	emu_wreg(sc, REG_IOLEN, strlen(name));
	emu_wreg(sc, REG_HANDLE, handle);
	emu_wreg(sc, REG_OPER, op);
	result = emu_waitdone(sc);

	if (result==0) {
		*newhandle = emu_rreg(sc, REG_HANDLE);
		*newisdir = emu_rreg(sc, REG_IOLEN)>0;
	}

	lock_release(sc->e_lock);
	return result;
}

/*
 * Routine for closing a file we opened at the hardware level.
 * This is not necessarily called at VOP_LASTCLOSE time; it's called
 * at VOP_RECLAIM time.
 */
static
int
nfs_close(struct nfsfh *handle)
{
	int result;

	result = nfs_close_async(nfs->context, handle, NULL, NULL);

	return result;
}

/*
 * Common code for read and readdir.
 */
static
int
emu_doread(struct emu_softc *sc, uint32_t handle, uint32_t len,
	   uint32_t op, struct uio *uio)
{
	int result;

	assert(uio->uio_rw == UIO_READ);

	if (uio->uio_offset > (off_t)0xffffffff) {
		/* beyond the largest size the file can have; generate EOF */
		return 0;
	}

	lock_acquire(sc->e_lock);

	emu_wreg(sc, REG_HANDLE, handle);
	emu_wreg(sc, REG_IOLEN, len);
	emu_wreg(sc, REG_OFFSET, uio->uio_offset);
	emu_wreg(sc, REG_OPER, op);
	result = emu_waitdone(sc);
	if (result) {
		goto out;
	}

	membar_load_load();
	result = uiomove(sc->e_iobuf, emu_rreg(sc, REG_IOLEN), uio);

	uio->uio_offset = emu_rreg(sc, REG_OFFSET);

 out:
	lock_release(sc->e_lock);
	return result;
}

/*
 * Read from a hardware-level file handle.
 */
static
int
nfs_read(struct nfs_softc *sc, uint32_t handle, uint32_t len,
	 struct uio *uio)
{
	return emu_doread(sc, handle, len, EMU_OP_READ, uio);
}


/*
 * Write to a hardware-level file handle.
 */
static
int
emu_write(struct emu_softc *sc, uint32_t handle, uint32_t len,
	  struct uio *uio)
{
	int result;

	assert(uio->uio_rw == UIO_WRITE);

	if (uio->uio_offset > (off_t)0xffffffff) {
		return EFBIG;
	}

	lock_acquire(sc->e_lock);

	emu_wreg(sc, REG_HANDLE, handle);
	emu_wreg(sc, REG_IOLEN, len);
	emu_wreg(sc, REG_OFFSET, uio->uio_offset);

	result = uiomove(sc->e_iobuf, len, uio);
	membar_store_store();
	if (result) {
		goto out;
	}

	emu_wreg(sc, REG_OPER, EMU_OP_WRITE);
	result = emu_waitdone(sc);

 out:
	lock_release(sc->e_lock);
	return result;
}

/*
 * Get the file size associated with a hardware-level file handle.
 */
static
int
emu_getsize(struct emu_softc *sc, uint32_t handle, off_t *retval)
{
	int result;

	lock_acquire(sc->e_lock);

	emu_wreg(sc, REG_HANDLE, handle);
	emu_wreg(sc, REG_OPER, EMU_OP_GETSIZE);
	result = emu_waitdone(sc);
	if (result==0) {
		*retval = emu_rreg(sc, REG_IOLEN);
	}

	lock_release(sc->e_lock);
	return result;
}

/*
 * Truncate a hardware-level file handle.
 */
static
int
emu_trunc(struct emu_softc *sc, uint32_t handle, off_t len)
{
	int result;

	assert(len >= 0);

	lock_acquire(sc->e_lock);

	emu_wreg(sc, REG_HANDLE, handle);
	emu_wreg(sc, REG_IOLEN, len);
	emu_wreg(sc, REG_OPER, EMU_OP_TRUNC);
	result = emu_waitdone(sc);

	lock_release(sc->e_lock);
	return result;
}

//
// vnode functions
//

// at bottom of this section

static int emufs_loadvnode(struct emufs_fs *ef, uint32_t handle, int isdir,
			   struct emufs_vnode **ret);

/*
 * VOP_EACHOPEN on files
 */
static
int
nfs_eachopen(struct vnode *v, int openflags)
{
	(void)v;
	(void)openflags;
	return 0;
}

/*
 * VOP_RECLAIM
 *
 */
static
int
emufs_reclaim(struct vnode *v)
{
	struct nfs_vnode *nv = v->vn_data;
	struct nfs_fs *nf = v->vn_fs->fs_data;
	unsigned ix, i, num;
	int result;

	/* emu_close retries on I/O error */
	result = nfs_close(ev->ev_emu, ev->ev_handle);
	if (result) {
		lock_release(ef->ef_emu->e_lock);
		vfs_biglock_release();
		return result;
	}

	num = vnodearray_num(nf->nf_vnodes);
	ix = num;
	for (i=0; i<num; i++) {
		struct vnode *vx;

		vx = vnodearray_get(nf->nf_vnodes, i);
		if (vx == v) {
			ix = i;
			break;
		}
	}
	if (ix == num) {
		ZF_LOG_E("vnode not exist\n");
	}

	vnodearray_remove(nf->nf_vnodes, ix);
	vnode_cleanup(&nv->nv_v);

	kfree(nv);
	return 0;
}

/*
 * VOP_READ
 */
static
int
emufs_read(struct vnode *v, struct uio *uio)
{
	struct emufs_vnode *ev = v->vn_data;
	uint32_t amt;
	size_t oldresid;
	int result;

	assert(uio->uio_rw==UIO_READ);

	while (uio->uio_resid > 0) {
		amt = uio->uio_resid;
		if (amt > EMU_MAXIO) {
			amt = EMU_MAXIO;
		}

		oldresid = uio->uio_resid;

		result = emu_read(ev->ev_emu, ev->ev_handle, amt, uio);
		if (result) {
			return result;
		}

		if (uio->uio_resid == oldresid) {
			/* nothing read - EOF */
			break;
		}
	}

	return 0;
}

/*
 * VOP_READDIR
 */
static
int
emufs_getdirentry(struct vnode *v, struct uio *uio)
{
	struct emufs_vnode *ev = v->vn_data;
	uint32_t amt;

	assert(uio->uio_rw==UIO_READ);

	amt = uio->uio_resid;
	if (amt > EMU_MAXIO) {
		amt = EMU_MAXIO;
	}

	return emu_readdir(ev->ev_emu, ev->ev_handle, amt, uio);
}

/*
 * VOP_WRITE
 */
static
int
emufs_write(struct vnode *v, struct uio *uio)
{
	struct emufs_vnode *ev = v->vn_data;
	uint32_t amt;
	size_t oldresid;
	int result;

	assert(uio->uio_rw==UIO_WRITE);

	while (uio->uio_resid > 0) {
		amt = uio->uio_resid;
		if (amt > EMU_MAXIO) {
			amt = EMU_MAXIO;
		}

		oldresid = uio->uio_resid;

		result = emu_write(ev->ev_emu, ev->ev_handle, amt, uio);
		if (result) {
			return result;
		}

		if (uio->uio_resid == oldresid) {
			/* nothing written...? */
			break;
		}
	}

	return 0;
}

/*
 * VOP_IOCTL
 */
static
int
emufs_ioctl(struct vnode *v, int op, userptr_t data)
{
	/*
	 * No ioctls.
	 */

	(void)v;
	(void)op;
	(void)data;

	return EINVAL;
}

/*
 * VOP_STAT
 */
static
int
emufs_stat(struct vnode *v, struct stat *statbuf)
{
	struct emufs_vnode *ev = v->vn_data;
	int result;

	bzero(statbuf, sizeof(struct stat));

	result = emu_getsize(ev->ev_emu, ev->ev_handle, &statbuf->st_size);
	if (result) {
		return result;
	}

	result = VOP_GETTYPE(v, &statbuf->st_mode);
	if (result) {
		return result;
	}
	statbuf->st_mode |= 0644; /* possibly a lie */
	statbuf->st_nlink = 1;    /* might be a lie, but doesn't matter much */
	statbuf->st_blocks = 0;   /* almost certainly a lie */

	return 0;
}

/*
 * VOP_GETTYPE for files
 */
static
int
emufs_file_gettype(struct vnode *v, uint32_t *result)
{
	(void)v;
	*result = S_IFREG;
	return 0;
}

/*
 * VOP_GETTYPE for directories
 */
static
int
emufs_dir_gettype(struct vnode *v, uint32_t *result)
{
	(void)v;
	*result = S_IFDIR;
	return 0;
}

/*
 * VOP_ISSEEKABLE
 */
static
bool
emufs_isseekable(struct vnode *v)
{
	(void)v;
	return true;
}

/*
 * VOP_FSYNC
 */
static
int
emufs_fsync(struct vnode *v)
{
	(void)v;
	return 0;
}

/*
 * VOP_TRUNCATE
 */
static
int
emufs_truncate(struct vnode *v, off_t len)
{
	struct emufs_vnode *ev = v->vn_data;
	return emu_trunc(ev->ev_emu, ev->ev_handle, len);
}

/*
 * VOP_CREAT
 */
/* nfs have a flat filesystem, so no dir needed */
static
int
nfs_creat(struct vnode *dir, const char *name, bool excl, mode_t mode,
	    struct vnode **ret)
{
	(void) dir;
	struct nfs_fs *nf = dir->vn_fs->fs_data;
	struct emufs_vnode *newguy;
	uint32_t handle;
	int result;
	int isdir;

	result = emu_open(ev->ev_emu, ev->ev_handle, name, true, excl, mode,
			  &handle, &isdir);

	result = emufs_loadvnode(ef, handle, isdir, &newguy);

	*ret = &newguy->ev_v;
	return 0;
}

void nfs_open_cb(int status, struct nfs_context *nfs, void *data, 
				 void *private_data)
{

}

/*
 * VOP_LOOKUP
 */
static
int
nfs_lookup(struct vnode *root, char *pathname, struct vnode **ret)
{
	struct nfs_fs *nf = root->vn_fs->fs_data;
	struct nfs_vnode *newguy;
	uint32_t handle;
	int result;
	int isdir;

	// result = emu_open(ev->ev_emu, ev->ev_handle, pathname, false, false, 0,
	// 		  &handle, &isdir);
	
	if (result) {
		vfs_biglock_release();
		return result;
	}

	result = emufs_loadvnode(ef, handle, isdir, &newguy);
	vfs_biglock_release();
	if (result) {
		emu_close(ev->ev_emu, handle);
		return result;
	}

	/* we are always async */
	*ret = NULL;
	return 0;
}

/*
 * VOP_LOOKPARENT
 */
static
int
emufs_lookparent(struct vnode *dir, char *pathname, struct vnode **ret,
		 char *buf, size_t len)
{
	char *s;

	s = strrchr(pathname, '/');
	if (s==NULL) {
		/* just a last component, no directory part */
		if (strlen(pathname)+1 > len) {
			return ENAMETOOLONG;
		}
		VOP_INCREF(dir);
		*ret = dir;
		strcpy(buf, pathname);
		return 0;
	}

	*s = 0;
	s++;
	if (strlen(s)+1 > len) {
		return ENAMETOOLONG;
	}
	strcpy(buf, s);

	return emufs_lookup(dir, pathname, ret);
}

/*
 * VOP_NAMEFILE
 */
static
int
emufs_namefile(struct vnode *v, struct uio *uio)
{
	struct emufs_vnode *ev = v->vn_data;
	struct emufs_fs *ef = v->vn_fs->fs_data;

	if (ev == ef->ef_root) {
		/*
		 * Root directory - name is empty string
		 */
		return 0;
	}

	(void)uio;

	return ENOSYS;
}

/*
 * VOP_MMAP
 */
static
int
emufs_mmap(struct vnode *v)
{
	(void)v;
	return ENOSYS;
}

//////////////////////////////

/*
 * Bits not implemented at all on emufs
 */

static
int
emufs_symlink(struct vnode *v, const char *contents, const char *name)
{
	(void)v;
	(void)contents;
	(void)name;
	return ENOSYS;
}

static
int
emufs_mkdir(struct vnode *v, const char *name, mode_t mode)
{
	(void)v;
	(void)name;
	(void)mode;
	return ENOSYS;
}

static
int
emufs_link(struct vnode *v, const char *name, struct vnode *target)
{
	(void)v;
	(void)name;
	(void)target;
	return ENOSYS;
}

static
int
emufs_remove(struct vnode *v, const char *name)
{
	(void)v;
	(void)name;
	return ENOSYS;
}

static
int
emufs_rmdir(struct vnode *v, const char *name)
{
	(void)v;
	(void)name;
	return ENOSYS;
}

static
int
emufs_rename(struct vnode *v1, const char *n1,
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
emufs_void_op_isdir(struct vnode *v)
{
	(void)v;
	return EISDIR;
}

static
int
emufs_uio_op_isdir(struct vnode *v, struct uio *uio)
{
	(void)v;
	(void)uio;
	return EISDIR;
}

static
int
emufs_uio_op_notdir(struct vnode *v, struct uio *uio)
{
	(void)v;
	(void)uio;
	return ENOTDIR;
}

static
int
emufs_name_op_notdir(struct vnode *v, const char *name)
{
	(void)v;
	(void)name;
	return ENOTDIR;
}

static
int
emufs_readlink_notlink(struct vnode *v, struct uio *uio)
{
	(void)v;
	(void)uio;
	return EINVAL;
}

static
int
emufs_creat_notdir(struct vnode *v, const char *name, bool excl, mode_t mode,
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
emufs_symlink_notdir(struct vnode *v, const char *contents, const char *name)
{
	(void)v;
	(void)contents;
	(void)name;
	return ENOTDIR;
}

static
int
emufs_mkdir_notdir(struct vnode *v, const char *name, mode_t mode)
{
	(void)v;
	(void)name;
	(void)mode;
	return ENOTDIR;
}

static
int
emufs_link_notdir(struct vnode *v, const char *name, struct vnode *target)
{
	(void)v;
	(void)name;
	(void)target;
	return ENOTDIR;
}

static
int
emufs_rename_notdir(struct vnode *v1, const char *n1,
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
emufs_lookup_notdir(struct vnode *v, char *pathname, struct vnode **result)
{
	(void)v;
	(void)pathname;
	(void)result;
	return ENOTDIR;
}

static
int
emufs_lookparent_notdir(struct vnode *v, char *pathname, struct vnode **result,
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
emufs_truncate_isdir(struct vnode *v, off_t len)
{
	(void)v;
	(void)len;
	return ENOTDIR;
}

//////////////////////////////

/*
 * Function table for emufs files.
 */
static const struct vnode_ops emufs_fileops = {
	.vop_magic = VOP_MAGIC,	/* mark this a valid vnode ops table */

	.vop_eachopen = emufs_eachopen,
	.vop_reclaim = emufs_reclaim,

	.vop_read = emufs_read,
	.vop_readlink = emufs_readlink_notlink,
	.vop_getdirentry = emufs_uio_op_notdir,
	.vop_write = emufs_write,
	.vop_ioctl = emufs_ioctl,
	.vop_stat = emufs_stat,
	.vop_gettype = emufs_file_gettype,
	.vop_isseekable = emufs_isseekable,
	.vop_fsync = emufs_fsync,
	.vop_mmap = emufs_mmap,
	.vop_truncate = emufs_truncate,
	.vop_namefile = emufs_uio_op_notdir,

	.vop_creat = emufs_creat_notdir,
	.vop_symlink = emufs_symlink_notdir,
	.vop_mkdir = emufs_mkdir_notdir,
	.vop_link = emufs_link_notdir,
	.vop_remove = emufs_name_op_notdir,
	.vop_rmdir = emufs_name_op_notdir,
	.vop_rename = emufs_rename_notdir,

	.vop_lookup = emufs_lookup_notdir,
	.vop_lookparent = emufs_lookparent_notdir,
};

/*
 * Function table for emufs directories.
 */
static const struct vnode_ops emufs_dirops = {
	.vop_magic = VOP_MAGIC,	/* mark this a valid vnode ops table */

	.vop_eachopen = emufs_eachopendir,
	.vop_reclaim = emufs_reclaim,

	.vop_read = emufs_uio_op_isdir,
	.vop_readlink = emufs_uio_op_isdir,
	.vop_getdirentry = emufs_getdirentry,
	.vop_write = emufs_uio_op_isdir,
	.vop_ioctl = emufs_ioctl,
	.vop_stat = emufs_stat,
	.vop_gettype = emufs_dir_gettype,
	.vop_isseekable = emufs_isseekable,
	.vop_fsync = emufs_void_op_isdir,
	.vop_mmap = emufs_void_op_isdir,
	.vop_truncate = emufs_truncate_isdir,
	.vop_namefile = emufs_namefile,

	.vop_creat = emufs_creat,
	.vop_symlink = emufs_symlink,
	.vop_mkdir = emufs_mkdir,
	.vop_link = emufs_link,
	.vop_remove = emufs_remove,
	.vop_rmdir = emufs_rmdir,
	.vop_rename = emufs_rename,

	.vop_lookup = emufs_lookup,
	.vop_lookparent = emufs_lookparent,
};

/*
 * Function to load a vnode into memory.
 */
static
int
emufs_loadvnode(struct emufs_fs *ef, uint32_t handle, int isdir,
		struct emufs_vnode **ret)
{
	struct vnode *v;
	struct emufs_vnode *ev;
	unsigned i, num;
	int result;

	vfs_biglock_acquire();
	lock_acquire(ef->ef_emu->e_lock);

	num = vnodearray_num(ef->ef_vnodes);
	for (i=0; i<num; i++) {
		v = vnodearray_get(ef->ef_vnodes, i);
		ev = v->vn_data;
		if (ev->ev_handle == handle) {
			/* Found */

			VOP_INCREF(&ev->ev_v);

			lock_release(ef->ef_emu->e_lock);
			vfs_biglock_release();
			*ret = ev;
			return 0;
		}
	}

	/* Didn't have one; create it */

	ev = kmalloc(sizeof(struct emufs_vnode));
	if (ev==NULL) {
		lock_release(ef->ef_emu->e_lock);
		return ENOMEM;
	}

	ev->ev_emu = ef->ef_emu;
	ev->ev_handle = handle;

	result = vnode_init(&ev->ev_v, isdir ? &emufs_dirops : &emufs_fileops,
			    &ef->ef_fs, ev);
	if (result) {
		lock_release(ef->ef_emu->e_lock);
		vfs_biglock_release();
		kfree(ev);
		return result;
	}

	result = vnodearray_add(ef->ef_vnodes, &ev->ev_v, NULL);
	if (result) {
		/* note: vnode_cleanup undoes vnode_init - it does not kfree */
		vnode_cleanup(&ev->ev_v);
		lock_release(ef->ef_emu->e_lock);
		vfs_biglock_release();
		kfree(ev);
		return result;
	}

	lock_release(ef->ef_emu->e_lock);
	vfs_biglock_release();

	*ret = ev;
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
 * Function table for the emufs file system.
 */
static const struct fs_ops nfs_fsops = {
	.fsop_sync = nfs_sync,
	.fsop_getvolname = nfs_getvolname,
	.fsop_getroot = nfs_getroot,
	.fsop_unmount =nfs_unmount,
};

/*
 * Routine for "mounting" an emufs - we're not really mounted in the
 * sense that the VFS understands that term, because we're not
 * connected to a block device.
 *
 * Basically, we just add ourselves to the name list in the VFS layer.
 */
/*
static
int
emufs_addtovfs(struct emu_softc *sc, const char *devname)
{
	struct emufs_fs *ef;
	int result;

	ef = kmalloc(sizeof(struct emufs_fs));
	if (ef==NULL) {
		return ENOMEM;
	}

	ef->ef_fs.fs_data = ef;
	ef->ef_fs.fs_ops = &emufs_fsops;

	ef->ef_emu = sc;
	ef->ef_root = NULL;
	ef->ef_vnodes = vnodearray_create();
	if (ef->ef_vnodes == NULL) {
		kfree(ef);
		return ENOMEM;
	}

	result = emufs_loadvnode(ef, EMU_ROOTHANDLE, 1, &ef->ef_root);
	if (result) {
		kfree(ef);
		return result;
	}

	assert(ef->ef_root!=NULL);

	result = vfs_addfs(devname, &ef->ef_fs);
	if (result) {
		VOP_DECREF(&ef->ef_root->ev_v);
		kfree(ef);
	}
	return result;*.h
}

//
////////////////////////////////////////////////////////////
*/
/*
 * Config routine called by autoconf stuff.
 *
 * Initialize our data, then add ourselves to the VFS layer.
 */
/*
int
config_emu(struct emu_softc *sc, int emuno)
{
	char name[32];

	sc->e_lock = lock_create("emufs-lock");
	if (sc->e_lock == NULL) {
		return ENOMEM;
	}
	sc->e_sem = sem_create("emufs-sem", 0);
	if (sc->e_sem == NULL) {
		lock_destroy(sc->e_lock);
		sc->e_lock = NULL;
		return ENOMEM;
	}
	sc->e_iobuf = bus_map_area(sc->e_busdata, sc->e_buspos, EMU_BUFFER);

	snprintf(name, sizeof(name), "emu%d", emuno);

	return emufs_addtovfs(sc, name);
}

*/

void change_bootfs(struct vnode *newvn);

void nfs_mount_cb(int status, struct nfs_context *nfs, void *data, void *private_data)
{
	if (status < 0) {
        ZF_LOGF("mount/mnt call failed with \"%s\"\n", (char *)data);
    }

    printf("Mounted nfs dir %s\n", SOS_NFS_DIR);
	struct vnode *vn;
	vn = nfs_bootstrap(nfs, &vn);
	change_bootfs(vn);
	
}

struct vnode *nfs_bootstrap(struct nfs_context *context)
{
	nfs_vn = (struct nfs_vnode *)malloc(sizeof(struct nfs_vnode));
	if (!nfs_vn) {
		ZF_LOG_E("Out of Memory!");
		return;
	}
	nfs_fs *fs = (nfs_fs *)malloc(sizeof(fs));
	if (!fs) {
		free(nfs_vn);
		ZF_LOG_E("Out of Memory!");
		return;
	}
	fs->nfs_vnodes = vnodearray_create();
	if (!fs->nfs_vnodes) {
		free(nfs_vn);
		free(fs);
		ZF_LOG_E("Out of Memory!");
		return;
	}
	fs->nfs_fsdata.fs_ops = nfs_fsops;
	fs->nfs_fsdata.fs_data = fs;
	fs->context = context;
	vnode_init(&nfs_vn->nv_v, vnode_ops, &fs->nfs_fsdata, nfs_vn);
	return &nfs_vn->nv_v;
}
