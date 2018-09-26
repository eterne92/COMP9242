#pragma once

#include <cspace/cspace.h>
#include <stdbool.h>

#include "ut.h"

#define N_NAME 32
#define PROCESS_ARRAY_SIZE 32

#define GET_BIT(number, bit) (((number) >> (bit)) & 1)
#define SET_BIT(number, bit) ((number) |= (1 << (bit)))
#define RST_BIT(number, bit) ((number) &= ~(1 << (bit)))

typedef struct page_table page_table_t;
typedef struct addrspace addrspace;
typedef struct filetable filetable;

enum process_state {
    DEAD, ACTIVE, INACTIVE
};

typedef struct {
    int     pid;
    unsigned  size;            /* in pages */
    unsigned  stime;           /* start time in msec since booting */
    char      command[N_NAME]; /* Name of exectuable */
} sos_process_t;

typedef struct proc {
    ut_t *tcb_ut;
    seL4_CPtr tcb;
    ut_t *vspace_ut;
    seL4_CPtr vspace;
    cspace_t cspace;
    page_table_t *pt;
    addrspace *as;
    seL4_CPtr reply;
    filetable *openfile_table;
    seL4_CPtr user_endpoint;
    sos_process_t status;
    unsigned waiting_list;
    enum process_state state;
} proc;

extern cspace_t *global_cspace;


extern seL4_CPtr ipc_ep;

/* The linker will link this symbol to the start address  *
 * of an archive of attached applications.                */
extern char _cpio_archive[];

extern proc process_array[];

void set_cur_proc(proc *p);

proc *get_cur_proc(void);

proc *get_process(int pid);

bool start_process(char *app_name, seL4_CPtr ep, int *ret_pid);
void kill_process(int pid);