#pragma once

#include <cspace/cspace.h>

#include "ut.h"

typedef struct page_table page_table_t;
typedef struct addrspace addrspace;
typedef struct filetable filetable;

typedef struct proc {
    ut_t *tcb_ut;
    seL4_CPtr tcb;
    ut_t *vspace_ut;
    seL4_CPtr vspace;
    cspace_t cspace;
    page_table_t *pt;
    addrspace *as;
    ut_t *ipc_buffer_ut;
    seL4_CPtr ipc_buffer;
    ut_t *stack_ut;
    seL4_CPtr stack;
    seL4_CPtr reply;
    filetable *openfile_table;
} proc;

extern cspace_t *global_cspace;
extern proc *cur_proc;

void set_cur_proc(proc *p);
proc *get_cur_proc(void);
