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

/*
 * Machine (and hardware) independent console driver.
 *
 * We expose a simple interface to the rest of the kernel: "putch" to
 * print a character, "getch" to read one.
 *
 * As long as the device we're connected to does, we allow printing in
 * an interrupt handler or with interrupts off (by polling),
 * transparently to the caller. Note that getch by polling is not
 * supported, although such support could be added without undue
 * difficulty.
 *
 * Note that nothing happens until we have a device to write to. A
 * buffer of size DELAYBUFSIZE is used to hold output that is
 * generated before this point. This means that (1) using kprintf for
 * debugging problems that occur early in initialization is awkward,
 * and (2) if the system crashes before we find a console, no output
 * at all may appear.
 */
#include "console.h"
#include "../pagetable.h"
#include "../syscall.h"
#include "device.h"
#include "uio.h"
#include "vfs.h"
#include <stdlib.h>

/*
 * The console device.
 */
static struct con_softc console;
static struct con_softc *the_console = &console;

/*
 * VFS interface functions
 */

static int con_eachopen(struct device *dev, int openflags)
{
    (void)dev;
    (void)openflags;
    return 0;
}

static void read_handler(struct serial *serial, char c)
{
    if (the_console->vaddr == 0) {
        /* shouldn't handle this call */
        return;
    }
    seL4_Word vaddr = get_sos_virtual_address(the_console->proc->pt, the_console->vaddr + the_console->index);
    /* there is a page fault */
    if (vaddr == 0) {
        handle_page_fault(the_console->proc, vaddr, 0);
    }
    *(char *)vaddr = c;
    /* reach buffsize */
    if (the_console->index == the_console->buffsize) {
        syscall_reply(the_console->proc->reply, the_console->index, 0);
        the_console->vaddr = 0;
    }
    /* reach endline */
    else if (c == '\n') {
        syscall_reply(the_console->proc->reply, the_console->index, 0);
        the_console->vaddr = 0;
    }
    the_console->index++;
}

static int con_io(struct device *dev, struct uio *uio)
{
    int result;
    int nbytes = 0, count;
    int n = PAGE_SIZE_4K - (uio->vaddr & PAGE_MASK_4K);
    seL4_Word sos_vaddr, user_vaddr = uio->vaddr;
    (void)dev; // unused
    if (uio->uio_rw == UIO_READ) {
        the_console->proc = uio->proc;
        the_console->vaddr = uio->vaddr;
        the_console->buffsize = uio->length;
        the_console->index = 0;
        serial_register_handler(the_console->serial, &read_handler);
    } else {
        while (uio->uio_resid > 0) {
            sos_vaddr = get_sos_virtual_address(the_console->proc->pt, user_vaddr);
            // send n bytes
            count = n;
            nbytes = serial_send(console.serial, sos_vaddr, n);
            while (nbytes < count) {
                count -= nbytes;
                nbytes = serial_send(console.serial, sos_vaddr + (n - count), count);
            }
            yield(NULL);
            uio->uio_resid -= nbytes;
            n = uio->uio_resid > PAGE_SIZE_4K ? PAGE_SIZE_4K : uio->uio_resid;
            user_vaddr += n;
        }
    }
    return 0;
}

static int con_ioctl(struct device *dev, int op, const void *data)
{
    /* No ioctls. */
    (void)dev;
    (void)op;
    (void)data;
    return -1;
}

static const struct device_ops console_devops = {
    .devop_eachopen = con_eachopen,
    .devop_io = con_io,
    .devop_ioctl = con_ioctl,
};

static int attach_console_to_vfs(struct con_softc *cs)
{
    struct device *dev;
    int result;

    dev = malloc(sizeof(*dev));
    if (dev == NULL) {
        return -1;
    }

    dev->d_ops = &console_devops;
    dev->d_blocks = 0;
    dev->d_blocksize = 1;
    dev->d_data = cs;

    result = vfs_adddev("console", dev, 0);
    if (result) {
        free(dev);
        return result;
    }

    return 0;
}

int con_initialize(void)
{
    console.serial = serial_init();
    console.vaddr = 0;
    console.buffsize = 0;
    console.index = 0;
    console.proc = NULL;
    return attach_console_to_vfs(&console);
}
