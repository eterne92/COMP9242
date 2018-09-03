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

#ifndef _UIO_H_
#define _UIO_H_

/*
 * A uio is an abstraction encapsulating a memory block, some metadata
 * about it, and also a cursor position associated with working
 * through it. The uio structure is used to manage blocks of data
 * moved around by the kernel.
 *
 * Note: struct iovec is in <kern/iovec.h>.
 *
 * The structure here is essentially the same as BSD uio. The
 * position is maintained by incrementing the block pointer,
 * decrementing the block size, decrementing the residue count, and
 * also incrementing the seek offset in uio_offset. The last is
 * intended to provide management for seek pointers.
 *
 * Callers of file system operations that take uios should honor the
 * uio_offset values returned by these operations, as for directories
 * they may not necessarily be byte counts and attempting to compute
 * seek positions based on byte counts can produce wrong behavior.
 *
 * File system operations calling uiomove for directory data and not
 * intending to use byte counts should update uio_offset to the
 * desired value explicitly after calling uiomove, as uiomove always
 * increments uio_offset by the number of bytes transferred.
 */

#include <stdint.h>
#include <sel4/sel4.h>
#include "../proc.h"

#define COPYIN UIO_WRITE
#define COPYOUT UIO_READ

/* Direction. */
enum uio_rw {
    UIO_READ,  /* From kernel to uio_seg */
    UIO_WRITE, /* From uio_seg to kernel */
};

struct uio {
    seL4_Word vaddr;
    size_t length;       /* number of bytes to transfer   */
    size_t uio_offset;  /* Desired offset into object    */
    size_t uio_resid;   /* Remaining amt of data to xfer */
    enum uio_rw uio_rw; /* Whether op is a read or write */
    proc *proc;
};

void uio_init(struct uio *u, seL4_Word vaddr, size_t len, size_t pos, enum uio_rw rw, proc *proc);

int copystr(proc *proc, char * user, char *sos, size_t length, enum uio_rw rw);

#endif /* _UIO_H_ */
