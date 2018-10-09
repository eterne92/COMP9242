#include <autoconf.h>
#include <assert.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/time.h>
#include <syscalls.h>
#include <utils/util.h>

#include <sos.h>

size_t sos_write(void *vData, size_t count)
{
    // use the content of tty test for this
    return sos_sys_write(STDOUT_FILENO ,vData, count);
    // return sos_debug_print(vData, count);
}

size_t sos_read(void *vData, size_t count)
{
    // use the content of tty test
    return 0;
}

int main(int argc, char const *argv[])
{
    sosapi_init_syscall_table();

    int pid = sos_my_id();
    printf("I am %d\n", pid);
    int child = -1;
    child = sos_process_create("helloworld");
    printf("%d child created %d\n", pid, child);
    if(child != -1){
        sos_process_wait(child);
    }
    else{
        sos_process_wait(0);
    }
    // int *p = (int *)0x400000 + 512;
    // printf("%d\n", *p);
    // *p = 5;
    return 0;
}
