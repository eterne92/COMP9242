
#pragma once

#include <sel4/sel4.h>
#include <stdint.h>
#include <stdlib.h>
#include "frametable.h"

#define USERSPACETOP 0x800000000000
#define USERIPCBUFFER (USERSPACETOP - 1024 * PAGE_SIZE_4K)
#define USERSTACKTOP (USERSPACETOP - 1024 * PAGE_SIZE_4K)
#define USERSTACKSIZE (4096 * PAGE_SIZE_4K)
#define USERHEAPBASE 0x700000000000
#define USERHEAPSIZE (4096 * 2 * PAGE_SIZE_4K)

enum OPERATION {
    READ, 
    WRITE,
};

#define RG_R (1 << 2)
#define RG_W (1 << 1)
#define RG_X (1 << 0)
#define RG_OLD (1 << 4)

typedef struct as_region
{
    struct as_region *next;
    /* starting virtual address of the segment. Note: 
       This address does not necessarily be 4k aligned 
       and one should check the address triggered page 
       fault to make sure the address is within the range 
       of this segment           
    */
    seL4_Word vaddr;
    size_t size;
    size_t npages;
    unsigned char flags;
} as_region;

typedef struct addrspace
{
    as_region *regions;
    as_region *stack;
    as_region *heap;
    as_region *ipcbuffer;
    seL4_Word used_top;
} addrspace;

addrspace *addrspace_init(void);
void addrspace_destroy(addrspace *as);
as_region *as_define_region(addrspace *as, seL4_Word vaddr, size_t memsize,
                     unsigned char flag);
void as_destroy_region(addrspace *as, as_region *region);
int as_define_stack(addrspace *as);
int as_define_heap(addrspace *as);
int as_define_ipcbuffer(addrspace *as);


/*
 * validate the virtual address
 * @param cur_proc     current process
 * @param vaddr        virtual address to be validated
 * @param size         size (in bytes) of the memory to access
 * @param operation    read or write 
 * 
 * return true when valid false otherwise
 */
bool validate_virtual_address(addrspace *as, seL4_Word vaddr, size_t size, enum OPERATION operation);
