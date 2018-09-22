#pragma once

#include <cspace/cspace.h>
#include <stdbool.h>

#include "ut.h"

#define N_NAME 32


typedef struct page_table page_table_t;
typedef struct addrspace addrspace;
typedef struct filetable filetable;

enum process_state {
    DEAD, ACTIVE, INACTIVE
};

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
    int     pid;
    unsigned  size;            /* in pages */
    unsigned  stime;           /* start time in msec since booting */
    char      command[N_NAME]; /* Name of exectuable */
    unsigned waiting_list;
    enum process_state state;
} proc;

extern cspace_t *global_cspace;

extern proc *cur_proc;

/* The linker will link this symbol to the start address  *
 * of an archive of attached applications.                */
extern char _cpio_archive[];

extern proc process_array[];

void set_cur_proc(proc *p);

proc *get_cur_proc(void);

proc *get_process(int pid);

bool start_process(char *app_name, seL4_CPtr ep, int *ret_pid);