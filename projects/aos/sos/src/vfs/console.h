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

#ifndef _GENERIC_CONSOLE_H_
#define _GENERIC_CONSOLE_H_

#include <fcntl.h>
#include <sel4/sel4.h>
#include <serial/serial.h>
#include "uio.h"
/*
 * Device data for the hardware-independent system console.
 *
 * devdata, send, and sendpolled are provided by the underlying
 * device, and are to be initialized by the attach routine.
 */
#define BUFFER_SIZE (3000)

typedef struct proc proc;
struct con_softc {
    struct serial *serial;
    /* use for reading info */
    proc *proc;
    seL4_Word vaddr;
    unsigned cs_gotchars_head; /* next slot to put a char in   */
    unsigned cs_gotchars_tail; /* next slot to take a char out */
	int n;					   /* number of characters in the buffer */
	char console_buffer[BUFFER_SIZE];

    struct uio *uio;
};

int con_initialize(void);

#endif /* _GENERIC_CONSOLE_H_ */
