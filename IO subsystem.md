## I/O subsystem

### VFS 

To provide a uniform way to access both console and the file system, we incorporate this abstraction layer into our SOS. The VFS layer defines a bunch of operations through which the client manipulate the underlying components. We borrow most of the VFS code from OS161.

```c
struct uio;    /* kernel or userspace I/O buffer (uio.h) */
struct device; /* abstract structure for a device (dev.h) */
struct fs;     /* abstract structure for a filesystem (fs.h) */
struct vnode;  /* abstract structure for an on-disk file (vnode.h) */
```

```c
enum uio_rw {
    UIO_READ,    /* From kernel to uio_seg */
    UIO_WRITE,   /* From uio_seg to kernel */
};

/* Source/destination. */
enum uio_seg {
    UIO_USERISPACE,    /* User process code. */
    UIO_USERSPACE,     /* User process data. */
    UIO_SYSSPACE,      /* Kernel. */
};

struct uio {
    seL4_Word vaddr;
    size_t length;              /* number of bytes to transfer   */
    size_t uio_offset;          /* Desired offset into object    */
    size_t uio_resid;           /* Remaining amt of data to xfer */
    enum uio_rw uio_rw;         /* Whether op is a read or write */
    enum uio_seg uio_segflg;
    proc *proc;
};
```

When dealing with I/O, it is inevitable to move from user level memory to kernel level memory and vice versa. To facilliate such chore, we borrow the idea of UIO from OS161. Whenever there is a syscall involved memory buffer, we use UIO to safely transfer data.  

### Console

The console is a multiple writer, single reader device and thus we have to keep track of the current process that opens the console to read. Before a process opens the console, there might be some characters coming from the serial port, so  we add a buffer to store those characters and when there is a process open the console in read mode, these character will be sent to that process. 

```c
struct con_softc {
    struct serial *serial;
    /* use for reading info */
    proc *proc;
    seL4_Word vaddr;
    unsigned cs_gotchars_head; /* next slot to put a char in   */
    unsigned cs_gotchars_tail; /* next slot to take a char out */
    size_t n;                      /* number of characters in the buffer */
    char console_buffer[BUFFER_SIZE];
    struct uio *uio;
};
```

When dealing with writing huge amounts of data to console, we use coroutine to make sure that after writing one page, it will yield back to the syscall loop to accept new incoming messages. By doing so, SOS will be reponsive even when console I/O is very busy.

### File System

The file system is actually a wrapper of the underlying NFS API. The only problem here is that the network library only supports asynchronous NFS function calls. To deal with that, we choose to use coroutine as our execution model and wrap most of the file syscalls in coroutine since using coroutine will eliminate many annoying concurrency issue and is simpler to implement. 