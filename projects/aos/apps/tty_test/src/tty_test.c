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
 *      Description: Simple milestone 0 test.
 *
 *      Author:			Godfrey van der Linden
 *      Original Author:	Ben Leslie
 *
 ****************************************************************************/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sel4/sel4.h>
#include <syscalls.h>

#include "ttyout.h"

#include <utils/page.h>

#define NPAGES 256
#define TEST_ADDRESS 0x8000000000

/* called from pt_test */
static void
do_pt_test(char *buf)
{
    int i;

    /* set */
    for (int i = 0; i < NPAGES; i++) {
	    buf[i * PAGE_SIZE_4K] = i;
    }

    /* check */
    for (int i = 0; i < NPAGES; i++) {
	    assert(buf[i * PAGE_SIZE_4K] == i);
    }
}

static void
pt_test( void )
{
    /* need a decent sized stack */
    char buf1[NPAGES * PAGE_SIZE_4K];
    char *buf2 = NULL;

    /* check the stack is above phys mem */
    assert((void *) buf1 > (void *) TEST_ADDRESS);

    /* stack test */
    do_pt_test(buf1);
    printf("stack test pass\n");

    /* heap test */
    buf2 = malloc(PAGE_SIZE_4K * NPAGES);
    assert(buf2);
    do_pt_test(buf2);
    free(buf2);
    printf("heap test pass\n");
}

// Block a thread forever
// we do this by making an unimplemented system call.
static void
thread_block(void)
{
    /* construct some info about the IPC message tty_test will send
     * to sos -- it's 1 word long */
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 1);
    /* Set the first word in the message to 0 */
    seL4_SetMR(0, 1);
    /* Now send the ipc -- call will send the ipc, then block until a reply
     * message is received */
    seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);
    /* Currently SOS does not reply -- so we never come back here */
}

int main(void)
{
    sosapi_init_syscall_table();

    /* initialise communication */
    ttyout_init();


    do {
        // printf("%d\n", *p);
        printf("task:\tHello world, I'm\ttty_test!\n");
        printf("task:\tHello world, I'm\ttty_test!\n");
        printf("task:\tHello world, I'm\ttty_test!\n");
        printf("task:\tHello world, I'm\ttty_test!\n");
        // printf("%d\n", sizeof(char) * 256);
        pt_test();
        thread_block();
        // sleep(1);	// Implement this as a syscall
    } while (1);

    return 0;
}
