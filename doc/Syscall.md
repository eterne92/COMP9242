## Syscall 

All the syscall implemeted in SOS

```C
int sos_sys_open(const char *path, fmode_t mode);

int sos_sys_close(int file);

int sos_sys_read(int file, char *buf, size_t nbyte);

int sos_sys_write(int file, const char *buf, size_t nbyte);

int sos_getdirent(int pos, char *name, size_t nbyte);

int sos_stat(const char *path, sos_stat_t *buf);

pid_t sos_process_create(const char *path);

int sos_process_delete(pid_t pid);

pid_t sos_my_id(void);

int sos_process_status(sos_process_t *processes, unsigned max);

pid_t sos_process_wait(pid_t pid);

int64_t sos_sys_time_stamp(void);

void sos_sys_usleep(int msec);

```

Basically, most time-consuming syscalls like read, write, process_create, etc are wrapped in coroutine. When SOS receives such syscalls, it will create a coroutine resume immediately and put the coroutine into a queue. In the syscall loop, a coroutine will be poped and get resumed and if it is still resumable, it will be added to the end of the queue. By doing so, we ensure no process could monopolize the processor by issuing syscall in a high frequency.  As for those syscalls with short execution time like sos_my_id, no coroutine is used since it will not block the system.

```c
typedef struct coroutines {
    coro data;
    struct coroutines *next;
} coroutines;
```

For passing argument in syscall, we take two approaches. One is passing arguments through IPC messages. The other is through UIO. For those arguemnts having reference semantics, we use UIO to transfer between user level and kernel level. For those having value semantics, IPC is sufficient. 