/*
 * Copyright 2018, Data61
 * Commonwealth Scientific and Industrial Research Organisation (CSIRO)
 * ABN 41 687 119 230.
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(DATA61_GPL)
 */
#include <autoconf.h>
#include <utils/util.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <sys/mman.h>
#include <errno.h>
#include <assert.h>

#include "sos.h"

#define SOS_SYSCALLBRK 101
#define SOS_SYSCALL_MMAP 102
#define SOS_SYSCALL_MUNMAP 200
/*
 * Statically allocated morecore area.
 *
 * This is rather terrible, but is the simplest option without a
 * huge amount of infrastructure.
 */
#define MORECORE_AREA_BYTE_SIZE 0x100000
char morecore_area[MORECORE_AREA_BYTE_SIZE];

/* Pointer to free space in the morecore area. */
static uintptr_t morecore_base = (uintptr_t) &morecore_area;
static uintptr_t morecore_top = (uintptr_t) &morecore_area[MORECORE_AREA_BYTE_SIZE];

/* Actual morecore implementation
   returns 0 if failure, returns newbrk if success.
*/

long sys_brk(va_list ap)
{

    uintptr_t ret;
    uintptr_t newbrk = va_arg(ap, uintptr_t);

    /*if the newbrk is 0, return the bottom of the heap*/
    seL4_MessageInfo_t tag;
    seL4_MessageInfo_t retmsg;
    tag = seL4_MessageInfo_new(0, 0, 0, 2);
    seL4_SetMR(0, SOS_SYSCALLBRK);
    seL4_SetMR(1, newbrk);
    seL4_Call(SOS_IPC_EP_CAP, tag);
    ret = seL4_GetMR(0);

    // if (!newbrk) {
    //     ret = morecore_base;
    // } else if (newbrk < morecore_top && newbrk > (uintptr_t)&morecore_area[0]) {
    //     ret = morecore_base = newbrk;
    // } else {
    //     ret = 0;
    // }

    return ret;
}

/* Large mallocs will result in muslc calling mmap, so we do a minimal implementation
   here to support that. We make a bunch of assumptions in the process */
long sys_mmap(va_list ap)
{
    void *addr = va_arg(ap, void*);
    size_t length = va_arg(ap, size_t);
    int prot = va_arg(ap, int);
    int flags = va_arg(ap, int);
    int fd = va_arg(ap, int);
    off_t offset = va_arg(ap, off_t);


    seL4_MessageInfo_t tag;
    seL4_MessageInfo_t retmsg;
    tag = seL4_MessageInfo_new(0, 0, 0, 7);
    seL4_SetMR(0, SOS_SYSCALL_MMAP);
    seL4_SetMR(1, addr);
    seL4_SetMR(2, length);
    seL4_SetMR(3, prot);
    seL4_SetMR(4, flags);
    seL4_SetMR(5, fd);
    seL4_SetMR(6, offset);
    seL4_Call(SOS_IPC_EP_CAP, tag);

    seL4_Word ret = seL4_GetMR(0);
    if(ret != 0){
        return ret;
    }
    else{
        return -ENOMEM;
    }

}


long sys_munmap(va_list ap){
    char *base = va_arg(ap, char*);
    size_t length = va_arg(ap, size_t);

    seL4_MessageInfo_t tag;
    seL4_MessageInfo_t retmsg;
    tag = seL4_MessageInfo_new(0, 0, 0, 3);
    seL4_SetMR(0, SOS_SYSCALL_MUNMAP);
    seL4_SetMR(1, base);
    seL4_SetMR(2, length);
    seL4_Call(SOS_IPC_EP_CAP, tag);
    seL4_Word ret = seL4_GetMR(0);
    if(ret == 0){
        return ret;
    }
    else{
        return -1;
    }
}