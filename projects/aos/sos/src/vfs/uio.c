/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
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

#include <stdbool.h>
#include <string.h>
#include "uio.h"
#include "../addrspace.h"
#include "../pagetable.h"
/*
 * Convenience function to initialize an iovec and uio for kernel I/O.
 */

void uio_uinit(struct uio *u, seL4_Word vaddr, size_t len, size_t pos,
               enum uio_rw rw, proc *proc)
{
    u->vaddr = vaddr;
    u->length = len;
    u->uio_offset = pos;
    u->uio_resid = len;
    u->uio_rw = rw;
    u->uio_segflg = UIO_USERSPACE;
    u->proc = proc;
}

void uio_kinit(struct uio *u, seL4_Word vaddr, size_t len, size_t pos,
               enum uio_rw rw)
{
    u->vaddr = vaddr;
    u->length = len;
    u->uio_offset = pos;
    u->uio_resid = len;
    u->uio_rw = rw;
    u->uio_segflg = UIO_SYSSPACE;
    u->proc = NULL;
}

int mem_move(proc *proc, seL4_Word u_vaddr, seL4_Word k_vaddr, size_t len,
             enum uio_rw rw)
{
    enum OPERATION oper = rw == UIO_READ ? READ : WRITE;
    seL4_Error err;
    bool valid = validate_virtual_address(proc->as, u_vaddr, len, oper);
    /* not valid */
    if (!valid) {
        return -1;
    }
    size_t n = PAGE_SIZE_4K - (u_vaddr & PAGE_MASK_4K);
    if (len < n) {
        n = len;
    }
    while (len > 0) {
        seL4_Word vaddr = get_sos_virtual_address(proc->pt, u_vaddr);
        if (!vaddr) {
            err = handle_page_fault(proc, u_vaddr, 0);
            if (err != seL4_NoError) {
                return -1;
            }
            vaddr = get_sos_virtual_address(proc->pt, u_vaddr);
        }
        if (rw == UIO_READ) {
            memcpy((void *)vaddr, (void *)k_vaddr, n);
        } else {
            memcpy((void *)k_vaddr, (void *)vaddr, n);
        }
        len -= n;
        u_vaddr += n;
        n = len > PAGE_SIZE_4K ? PAGE_SIZE_4K : len;
    }
    return 0;
}

/* only used to copy string size less then a frame */
/* so we won't go through too many frame */
int copystr(proc *proc, char *user, char *sos, size_t length, enum uio_rw rw)
{
    /* get region */
    as_region *region = vaddr_get_region(proc->as, (seL4_Word)user);
    /* not valid */
    if (region == NULL) {
        return -1;
    }
    seL4_Error err;
    seL4_Word left_size = region->vaddr + region->size - (seL4_Word)region->vaddr;
    seL4_Word vaddr = get_sos_virtual_address(proc->pt, (seL4_Word)user);
    if (!vaddr) {
        err = handle_page_fault(proc, (seL4_Word)user, 0);
        if (err != seL4_NoError) return -1;
        vaddr = get_sos_virtual_address(proc->pt, (seL4_Word)user);
    }
    seL4_Word top = (vaddr & PAGE_FRAME) + PAGE_SIZE_4K;
    size_t i = 0;
    size_t j = 0;
    char c = 0;
    while (i < left_size && i < length) {
        if (rw == COPYIN) {
            c = *(char *)(vaddr + j);
            sos[i] = c;
        } else {
            c = sos[i];
            *(char *)(vaddr + j) = c;
        }
        /* copy end */
        if (c == 0) {
            return i;
        }
        i++;
        j++;
        if ((vaddr + j) >= top) {
            vaddr = get_sos_virtual_address(proc->pt, (seL4_Word)user + i);
            j = 0;
        }
    }
    if (c != 0) {
        return -1;
    }
    return i;
}