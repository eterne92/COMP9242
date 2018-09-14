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

/*
 * VFS operations relating to pathname translation
 */

#include "fs.h"
#include "vfs.h"
#include "vnode.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <picoro/picoro.h>

static struct vnode *bootfs_vnode = NULL;

struct vnode *get_bootfs_vnode(void)
{
    return bootfs_vnode;
}

/*
 * Helper function for actually changing bootfs_vnode.
 */
void change_bootfs(struct vnode *newvn)
{
    struct vnode *oldvn;

    oldvn = bootfs_vnode;
    bootfs_vnode = newvn;
    printf("boot_vnode is %p\n", bootfs_vnode);

    if (oldvn != NULL) {
        VOP_DECREF(oldvn);
    }
}

/*
 * Set bootfs_vnode.
 *
 * Bootfs_vnode is the vnode used for beginning path translation of
 * pathnames starting with /.
 *
 * It is also incidentally the system's first current directory.
 */
int vfs_setbootfs(const char *fsname)
{
    char tmp[NAME_MAX + 1];
    char *s;
    int result;
    struct vnode *newguy;

    snprintf(tmp, sizeof(tmp) - 1, "%s", fsname);
    s = strchr(tmp, ':');
    if (s) {
        /* If there's a colon, it must be at the end */
        if (strlen(s) > 0) {
            return EINVAL;
        }
    } else {
        strcat(tmp, ":");
    }

    result = vfs_chdir(tmp);
    if (result) {
        return result;
    }

    result = vfs_getcurdir(&newguy);
    if (result) {
        return result;
    }

    change_bootfs(newguy);

    return 0;
}

/*
 * Clear the bootfs vnode (preparatory to system shutdown).
 */
void vfs_clearbootfs(void)
{
    vfs_biglock_acquire();
    change_bootfs(NULL);
    vfs_biglock_release();
}

/*
 * Common code to pull the device name, if any, off the front of a
 * path and choose the vnode to begin the name lookup relative to.
 */

static int
getdevice(char *path, char **subpath, struct vnode **startvn)
{
    struct vnode *vn;
    int result;
    int length;

    /*
	 * Entirely empty filenames aren't legal.
	 */
    if (path[0] == 0) {
        return EINVAL;
    }

    length = strlen(path);
	result = vfs_getroot(path, &vn);
	if (result == ENODEV) {
		/* it's our nfs fs system */
        while(bootfs_vnode == NULL){
            yield(NULL);
        }
        printf("we are nfs yeha!\n");
		*startvn = bootfs_vnode;
		*subpath = path;
        printf("got startvn as %p\n", *startvn);
		return 0;
	}
	else if(result != 0){
		return result;
	}
    *startvn = vn;
	*subpath = &path[length];
    return 0;
}

/*
 * Name-to-vnode translation.
 * (In BSD, both of these are subsumed by namei().)
 */

int vfs_lookparent(char *path, struct vnode **retval,
    char *buf, size_t buflen)
{
    (void) buf;
    (void) buflen;
    struct vnode *startvn;
    int result;

    result = getdevice(path, &path, &startvn);
    if (result) {
        return result;
    }


    *retval = startvn;
    printf("start vn in lookparent %p\n", startvn);

    return result;
}

int vfs_lookup(char *path, struct vnode **retval)
{
    struct vnode *startvn;
    int result;

    result = getdevice(path, &path, &startvn);
    if (result) {
        return result;
    }

    result = VOP_LOOKUP(startvn, path, retval);

    return result;
}
