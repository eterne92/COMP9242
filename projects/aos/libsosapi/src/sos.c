/*
 * Copyright 2018, Data61
 * Commonwealth Scientific and Industrial Research Organisation (CSIRO)
 * ABN 41 687 119 230.
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(DATA61_GPL)
 */
#include <stdarg.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sos.h>

#include <sel4/sel4.h>

int sos_sys_open(const char *path, fmode_t mode)
{
    seL4_MessageInfo_t tag;
    seL4_MessageInfo_t retmsg;
    tag = seL4_MessageInfo_new(0, 0, 0, 3);
    seL4_SetMR(0, SOS_SYS_OPEN);
    seL4_SetMR(1, (seL4_Word)path);
    seL4_SetMR(2, (seL4_Word)mode);

    seL4_Call(SOS_IPC_EP_CAP, tag);
    int ret = seL4_GetMR(0);
    return ret;
}

int sos_sys_close(int file)  {
    seL4_MessageInfo_t tag;
    seL4_MessageInfo_t retmsg;
    tag = seL4_MessageInfo_new(0, 0, 0, 2);
    seL4_SetMR(0, SOS_SYS_CLOSE);
    seL4_SetMR(1, (seL4_Word)file);

    seL4_Call(SOS_IPC_EP_CAP, tag);
    int ret = seL4_GetMR(0);
    return ret;
}

int sos_sys_read(int file, char *buf, size_t nbyte)
{
    seL4_MessageInfo_t tag;
    seL4_MessageInfo_t retmsg;
    tag = seL4_MessageInfo_new(0, 0, 0, 4);
    seL4_SetMR(0, SOS_SYS_READ);
    seL4_SetMR(1, (seL4_Word)file);
    seL4_SetMR(2, (seL4_Word)buf);
    seL4_SetMR(3, (seL4_Word)nbyte);

    seL4_Call(SOS_IPC_EP_CAP, tag);
    int ret = seL4_GetMR(0);
    return ret;
}

int sos_sys_write(int file, const char *buf, size_t nbyte)
{
    seL4_MessageInfo_t tag;
    seL4_MessageInfo_t retmsg;
    tag = seL4_MessageInfo_new(0, 0, 0, 4);
    seL4_SetMR(0, SOS_SYS_WRITE);
    seL4_SetMR(1, (seL4_Word)file);
    seL4_SetMR(2, (seL4_Word)buf);
    seL4_SetMR(3, (seL4_Word)nbyte);

    seL4_Call(SOS_IPC_EP_CAP, tag);
    int ret = seL4_GetMR(0);
    return ret;
}

int sos_getdirent(int pos, char *name, size_t nbyte)
{
    assert(!"You need to implement this");
    seL4_MessageInfo_t tag;
    seL4_MessageInfo_t retmsg;
    tag = seL4_MessageInfo_new(0, 0, 0, 4);
    seL4_SetMR(0, SOS_SYS_GET_DIRDENTS);
    seL4_SetMR(1, (seL4_Word)pos);
    seL4_SetMR(2, (seL4_Word)name);
    seL4_SetMR(3, (seL4_Word)nbyte);
    seL4_Call(SOS_IPC_EP_CAP, tag);
    int ret = seL4_GetMR(0);
    return ret;
}

int sos_stat(const char *path, sos_stat_t *buf)
{
    seL4_MessageInfo_t tag;
    seL4_MessageInfo_t retmsg;
    tag = seL4_MessageInfo_new(0, 0, 0, 2);
    seL4_SetMR(0, SOS_SYS_STAT);
    seL4_SetMR(1, (seL4_Word)path);
    seL4_Call(SOS_IPC_EP_CAP, tag);
    int ret = seL4_GetMR(0);
    buf->st_type = (st_type_t) seL4_GetMR(2);
    buf->st_fmode = (fmode_t) seL4_GetMR(3);
    buf->st_size = (unsigned) seL4_GetMR(4);
    buf->st_ctime = (long) seL4_GetMR(5);
    buf->st_atime = (long) seL4_GetMR(6);
    return ret;
}

pid_t sos_process_create(const char *path)
{
    assert(!"You need to implement this");
    seL4_MessageInfo_t tag;
    seL4_MessageInfo_t retmsg;
    tag = seL4_MessageInfo_new(0, 0, 0, 2);
    seL4_SetMR(0, SOS_SYS_PROCESS_CREATE);
    seL4_SetMR(1, (seL4_Word)path);
    return -1;
}

int sos_process_delete(pid_t pid)
{
    assert(!"You need to implement this");
    seL4_MessageInfo_t tag;
    seL4_MessageInfo_t retmsg;
    tag = seL4_MessageInfo_new(0, 0, 0, 2);
    seL4_SetMR(0, SOS_SYS_PROCESS_DELETE);
    seL4_SetMR(1, (seL4_Word)pid);
    return -1;
}

pid_t sos_my_id(void)
{
    assert(!"You need to implement this");
    seL4_MessageInfo_t tag;
    seL4_MessageInfo_t retmsg;
    seL4_SetMR(0, SOS_SYS_MY_ID);
    tag = seL4_MessageInfo_new(0, 0, 0, 1);
    return -1;

}

int sos_process_status(sos_process_t *processes, unsigned max)
{
    assert(!"You need to implement this");
    seL4_MessageInfo_t tag;
    seL4_MessageInfo_t retmsg;
    tag = seL4_MessageInfo_new(0, 0, 0, 3);
    seL4_SetMR(0, SOS_SYS_PROCESS_STATUS);
    seL4_SetMR(1, (seL4_Word)processes);
    seL4_SetMR(2, (seL4_Word)max);
    return -1;
}

pid_t sos_process_wait(pid_t pid)
{
    assert(!"You need to implement this");
    seL4_MessageInfo_t tag;
    seL4_MessageInfo_t retmsg;
    tag = seL4_MessageInfo_new(0, 0, 0, 2);
    seL4_SetMR(0, SOS_SYS_PROCESS_WAIT);
    seL4_SetMR(1, (seL4_Word)pid);
    return -1;

}

void sos_sys_usleep(int msec)
{
    seL4_MessageInfo_t tag;
    seL4_MessageInfo_t retmsg;
    tag = seL4_MessageInfo_new(0, 0, 0, 2);
    seL4_SetMR(0, SOS_SYS_USLEEP);
    seL4_SetMR(1, (seL4_Word)msec);
    seL4_Call(SOS_IPC_EP_CAP, tag);
    seL4_GetMR(0);
}

int64_t sos_sys_time_stamp(void)
{
    seL4_MessageInfo_t tag;
    seL4_MessageInfo_t retmsg;
    tag = seL4_MessageInfo_new(0, 0, 0, 1);
    seL4_SetMR(0, SOS_SYS_TIMESTAMP);
    seL4_Call(SOS_IPC_EP_CAP, tag);
    int ret = seL4_GetMR(0);
    return ret;
}
