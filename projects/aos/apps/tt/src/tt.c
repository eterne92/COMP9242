#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <stdbool.h>
#include <sys/time.h>
#include <utils/time.h>
#include <syscalls.h>
/* Your OS header file */
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

static int threshold = 3500;

void recursive_test(int i)
{
    if (i >= threshold) return;
    char *p = (char *)malloc(sizeof(char) * 8192);
   // assert(p);
    memset(p, 'H', sizeof(char) * 8192);
    recursive_test(i + 1);
    //assert(p);
    for(int i = 0;i < 8192;i++){
        assert(p[i] == 'H');
    }
    free(p);
    return;
}

int recursive_test2(int i)
{
    if (i < 2) return 1;
    int a = recursive_test2(i - 1);
    int b = recursive_test2(i - 2);
    int count = a + b;
    return count;
}

void loop_leak_test(int count)
{
    for (int i = 0; i < count; ++i) {
        void *p = malloc(8192);
        //printf("%p\n", p);
        memset(p, 'H', 8192);
    }
}

int main(int argc, char const *argv[])
{
    sosapi_init_syscall_table();

    int pid = sos_my_id();
    printf("I am %d\n", pid);
    // int child = -1;
    // child = sos_process_create("helloworld");
    // printf("%d child created %d\n", pid, child);
    // if(child != -1){
    //     sos_process_wait(child);
    // }
    // else{
    //     sos_process_wait(0);
    // }
    // int *p = (int *)0x400000 + 512;
    // printf("%d\n", *p);
    // *p = 5;
    //printf("hello world!\n");
    // printf("bye world!\n");
    // printf("start calling recursive test\n");
    recursive_test(1);
    printf("test pass %d\n", pid);
    // // int v = recursive_test2(35);
    //loop_leak_test(2500);
    // int count = 2000;
    // for (int i = 0; i < count; ++i) {
    //     void *p = malloc(8192);
    //     assert(p);
    //     memset(p, 'B', 8192);
    // }
    // printf("recursive function call finished!\n enter infinite loop\n");
    // printf("%d\n", v);
    // while(true);
    return 0;
}
