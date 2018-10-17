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
#include "../syscall/syscall.h"
#include "device.h"
#include "uio.h"
#include "vfs.h"
#include <picoro/picoro.h>
#include <stdlib.h>

typedef  void *(*coro_t)(void *);
/*
 * The console device.
 */
static struct con_softc console;
static struct con_softc *the_console = &console;
static int console_lock = 0;

/*
 * VFS interface functions
 */

static int con_eachopen(struct device *dev, int openflags)
{
    (void)dev;
    int how = openflags & O_ACCMODE;
    printf("how : %d\n", how);
    printf("proc is %p\n", console.proc);
    // only one process could open console in read mode
    if (how == O_RDONLY || how == O_RDWR) {
        if (console.proc == NULL) {
            return 0;
        } else {
            return -1;
        }
    }
    return 0;
}

static void *putchar_to_user(void)
{
    struct uio *uio = the_console->uio;
    if (uio->vaddr == 0) {
        /* shouldn't handle this call */
        return NULL;
    }
    while(console_lock == 1){
        yield(NULL);
    }
    console_lock = 1;

    int idx = uio->length - uio->uio_resid;
    char c;
    seL4_Word sos_vaddr = 0;
    seL4_Error err;
    while (the_console->n > 0) {
        if (uio->uio_segflg == UIO_USERSPACE) {
            sos_vaddr = get_sos_virtual_address(the_console->proc->pt, uio->vaddr + idx);
            if (sos_vaddr == 0) {
                err = handle_page_fault(the_console->proc, uio->vaddr + idx, 0);
                if (err) {
                    // not enough memory
                    assert(0);
                    console_lock = 0;
                    return NULL;
                }
                sos_vaddr = get_sos_virtual_address(the_console->proc->pt, uio->vaddr + idx);
            }
        } else {
            sos_vaddr = uio->vaddr;
        }
        *(char *)sos_vaddr = c =
                                 the_console->console_buffer[the_console->cs_gotchars_head];
        the_console->cs_gotchars_head = (the_console->cs_gotchars_head + 1) %
                                        BUFFER_SIZE;
        --the_console->n;
        --uio->uio_resid;
        if (uio->uio_resid == 0) {
            // finish reading
            the_console->uio = NULL;
            console_lock = 0;
            return NULL;
        } else if (c == '\n') {
            the_console->uio = NULL;
            console_lock = 0;
            return NULL;
        }
        ++idx;
    }
    console_lock = 0;
    return NULL;
}

static void read_handler(struct serial *serial, char c)
{
    (void)serial;
    if (the_console->n < BUFFER_SIZE) {
        the_console->console_buffer[the_console->cs_gotchars_tail] = c;
        the_console->cs_gotchars_tail = (the_console->cs_gotchars_tail + 1) %
                                        BUFFER_SIZE;
        ++the_console->n;
        if (the_console->uio) {
            coro c = coroutine((coro_t) putchar_to_user);
            resume(c, NULL);
            create_coroutine(c);
        }
    }
}

static int con_io(struct device *dev, struct uio *uio)
{
    int nbytes = 0, count;

    size_t n = PAGE_SIZE_4K - (uio->vaddr & PAGE_MASK_4K);
    if (uio->uio_resid < n) {
        n = uio->uio_resid;
    }
    seL4_Word sos_vaddr, user_vaddr = uio->vaddr;
    (void)dev; // unused
    seL4_Error err;
    if (uio->uio_rw == UIO_READ) {
        // the_console->proc = uio->proc;
        // the_console->vaddr = uio->vaddr;
        // putchar_to_user(uio);
        the_console->uio = uio;
        the_console->proc = uio->proc;
        printf("read came\n");
        putchar_to_user();
        while (the_console->uio != NULL) {
            yield(NULL);
        }
    } else {
        while (uio->uio_resid > 0) {
            if (uio->uio_segflg == UIO_USERSPACE) {
                sos_vaddr = get_sos_virtual_address(uio->proc->pt, user_vaddr);
                if (sos_vaddr == 0) {
                    err = handle_page_fault(uio->proc, user_vaddr, 0);
                    if (err) {
                        return err;
                    }
                    sos_vaddr = get_sos_virtual_address(uio->proc->pt, user_vaddr);
                }
            } else {
                sos_vaddr = uio->vaddr;
            }

            // send n bytes
            count = n;
            // for (int i = 0; i < 75000; i++)
            // ;
            nbytes = serial_send(the_console->serial, (char *)sos_vaddr, n);
            while (nbytes < count) {
                count -= nbytes;
                // for (int i = 0; i < 75000; i++)
                // ;
                nbytes = serial_send(the_console->serial, (char *)(sos_vaddr + (n - count)),
                                     count);
            }
            // if(nbytes < count){
            //     printf("should never happen if serial is there\n");
            //     return;
            // }
            uio->uio_resid -= n;
            user_vaddr += n;
            n = uio->uio_resid > PAGE_SIZE_4K ? PAGE_SIZE_4K : uio->uio_resid;
            if (uio->uio_resid != 0) {
                yield(NULL);
            }
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

static int con_reclaim(struct device *dev)
{
    struct con_softc *cs = (struct con_softc *)dev->d_data;
    // if (proc == cs->proc) {
    cs->vaddr = cs->n = cs->cs_gotchars_head = cs->cs_gotchars_tail = 0;
    cs->uio = NULL;
    cs->proc = NULL;
    // }
    return 0;
}

static const struct device_ops console_devops = {
    .devop_eachopen = con_eachopen,
    .devop_io = con_io,
    .devop_ioctl = con_ioctl,
    .devop_reclaim = con_reclaim,
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
    console.cs_gotchars_head = 0;
    console.cs_gotchars_tail = 0;
    console.proc = NULL;
    console.n = 0;
    console.uio = NULL;
    serial_register_handler(the_console->serial, &read_handler);
    return attach_console_to_vfs(&console);
}
