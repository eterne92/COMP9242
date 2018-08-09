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
/****************************************************************************
 *
 *      $Id:  $
 *
 *      Description: Simple milestone 0 code.
 *      		     Libc will need sos_write & sos_read implemented.
 *
 *      Author:      Ben Leslie
 *
 ****************************************************************************/

#include <stdarg.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "ttyout.h"

#include <sel4/sel4.h>

#define SOS_SYSCALLX 99

#define BUFFER_SIZE 64


void ttyout_init(void)
{
    /* Perform any initialisation you require here */
}

static size_t sos_debug_print(const void *vData, size_t count)
{
    size_t i;
    const char *realdata = vData;
    for (i = 0; i < count; i++) {
        seL4_DebugPutChar(realdata[i]);
    }
    return count;
}

size_t sos_write(void *vData, size_t count)
{

    //return sos_debug_print(vData, count);
    size_t remaining_pos = 0;
    const char *characters = vData;
    seL4_Word val;
    /* construct some info about the IPC message tty_test will send
     * to sos -- it's 1 word long */
    seL4_MessageInfo_t tag;
    //seL4_SetTag(tag);
    for(size_t i = 0; i < count / BUFFER_SIZE; ++i) {
        tag = seL4_MessageInfo_new(0, 0, 0, 2 + BUFFER_SIZE);
        seL4_SetMR(0, SOS_SYSCALLX);
        seL4_SetMR(1, BUFFER_SIZE);
        for(int j = 0, k = remaining_pos; j < BUFFER_SIZE; ++j, ++k) {
            seL4_SetMR(2 + j, characters[k]);
        }
        
        /* Now send the ipc -- call will send the ipc, then block until a reply
         * message is received */
        remaining_pos += BUFFER_SIZE;
        seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);
    }
    if ((count - remaining_pos) != 0) {
        tag = seL4_MessageInfo_new(0, 0, 0, 2 + count);
        seL4_SetMR(0, SOS_SYSCALLX);
        seL4_SetMR(1, count - remaining_pos);
        for(int i = 0, j = remaining_pos; i < count; ++i, ++j) {
            seL4_SetMR(2 + i, characters[j]);
        }
        seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);
    }
    return count;
}

size_t sos_read(void *vData, size_t count)
{
    //implement this to use your syscall
    return 0;
}

