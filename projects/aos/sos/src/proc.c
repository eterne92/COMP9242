#include "proc.h"

void set_cur_proc(proc *p)
{
    cur_proc = p;
}

proc *get_cur_proc(void)
{
    return cur_proc;
}


/*
 *
*/
proc *get_process(unsigned pid)
{
    return cur_proc;
}