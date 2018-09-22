#include "proc.h"

#define SIZE 32

#define GET_BIT(X,N) ( ( (X) >> (N) ) & 1 )
#define SET_BIT(X,N) ( (X) |  (1 << (N) ) )
#define RST_BIT(X,N) ( (X) & ~(1 << (N) ) )

proc process_array[SIZE];

void set_cur_proc(proc *p)
{
    cur_proc = p;
}

proc *get_cur_proc(void)
{
    return cur_proc;
}

proc *get_process(unsigned pid)
{
    if (pid < 0 || pid > 32)
        return NULL;
    return &process_array[pid - 1];
}