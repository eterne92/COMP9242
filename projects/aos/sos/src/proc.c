#include "proc.h"

proc *set_cur_proc(proc *p)
{
    cur_proc = p;
}

proc *get_cur_proc(void)
{
    return cur_proc;
}