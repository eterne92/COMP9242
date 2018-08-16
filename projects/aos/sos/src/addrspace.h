
#pragma once

#include <sel4/sel4.h>

typedef struct as_region {
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
    unsigned flags;
} as_region;


typedef struct addrspace {
    as_region *regions;
} addrspace;