#pragma once

#include "ut.h"
#include "pagetable.h"
#include "addrspace.h"

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
} proc;