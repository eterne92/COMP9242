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
#pragma once

#include <cspace/cspace.h>
#include <elf.h>
#include <elf/elf.h>
#include <sel4/sel4.h>

struct vnode;
typedef struct proc proc;

int elf_load(cspace_t *cspace, seL4_CPtr loader_vspace, proc *cur_proc,
             char *elf_file, struct vnode *elf_vn);

int cpio_elf_load(cspace_t *cspace, seL4_CPtr loader_vspace, proc *cur_proc,
                  char *elf_file, struct vnode *elf_vn);
