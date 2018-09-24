/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009, 2014
 *  The President and Fellows of Harvard College.
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
 * File handles.
 */

#include "openfile.h"
#include "../vfs/vfs.h"
#include "../vfs/vnode.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>

/*
 * Constructor for struct openfile.
 */
static struct openfile *openfile_create(struct vnode *vn, int accmode)
{
    struct openfile *file;

    /* this should already have been checked (e.g. by vfs_open) */
    assert(accmode == O_RDONLY || accmode == O_WRONLY || accmode == O_RDWR);

    file = malloc(sizeof(struct openfile));
    if (file == NULL) {
        return NULL;
    }
    file->of_vnode = vn;
    file->of_accmode = accmode;
    file->of_offset = 0;
    file->of_refcount = 1;

    return file;
}

/*
 * Destructor for struct openfile. Private; should only be used via
 * openfile_decref().
 */
static void openfile_destroy(struct openfile *file)
{
    /* balance vfs_open with vfs_close (not VOP_DECREF) */
    struct vnode *v = file->of_vnode;
    if(file->of_accmode == O_RDONLY || file->of_accmode == O_RDWR){
        v->closing_op = 1;
    }
    else{
        v->closing_op = 0;
    }
    vfs_close(file->of_vnode);
    free(file);
}

/*
 * Open a file (with vfs_open) and wrap it in an openfile object.
 */
int openfile_open(char *filename, int openflags, mode_t mode,
                  struct openfile **ret)
{
    struct vnode *vn;
    struct openfile *file;
    int result;

    result = vfs_open(filename, openflags, mode, &vn);
    if (result) {
        return result;
    }

    file = openfile_create(vn, openflags & O_ACCMODE);
    if (file == NULL) {
        vfs_close(vn);
        return ENOMEM;
    }

    *ret = file;
    return 0;
}

/*
 * Increment the reference count on an openfile.
 */
void openfile_incref(struct openfile *file)
{
    file->of_refcount++;
}

/*
 * Decrement the reference count on an openfile. Destroys it when the
 * reference count reaches zero.
 */
void openfile_decref(struct openfile *file)
{
    /* if this is the last close of this file, free it up */
    if (file->of_refcount == 1) {
        openfile_destroy(file);
    } else {
        assert(file->of_refcount > 1);
        file->of_refcount--;
    }
}
