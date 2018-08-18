#pragma once

#include <sel4/sel4.h>
#include "ut.h"
/*
 * 4 level shadow page talbe
 * keep track of capabilities of 
 * hardware page table 
*/

#define PAGE_TABLE_SIZE 512

typedef struct page_table {
    seL4_Word page_obj_addr[PAGE_TABLE_SIZE];
} page_table_t;

typedef struct page_table_ut {
    ut_t *ut[PAGE_TABLE_SIZE];
} page_table_ut;

typedef struct page_table_cap {
    seL4_Word cap[PAGE_TABLE_SIZE];
}page_table_cap;

void initialize_page_table();

void destroy_page_table();

int page_fault_handler(seL4_Word vaddr);