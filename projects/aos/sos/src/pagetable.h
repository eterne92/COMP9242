#pragma once

#include <sel4/sel4.h>
#include "ut.h"
#include "proc.h"

/*
 * 4 level shadow page talbe
 * keep track of capabilities of 
 * hardware page table 
*/

#define PAGE_TABLE_SIZE 512

typedef struct page_table
{
    seL4_Word page_obj_addr[PAGE_TABLE_SIZE];
} page_table_t;

typedef struct page_table_ut
{
    ut_t *ut[PAGE_TABLE_SIZE];
} page_table_ut;

typedef struct page_table_cap
{
    seL4_Word cap[PAGE_TABLE_SIZE];
} page_table_cap;

page_table_t *initialize_page_table(void);

void destroy_page_table();

void handle_page_fault(proc *cur_proc, seL4_Word vaddr, seL4_Word fault_info);

page_table_cap *get_page_table_cap(seL4_Word page_table);
page_table_ut *get_page_table_ut(seL4_Word page_table);
int get_offset(seL4_Word vaddr, int n);
seL4_Word get_n_level_table(seL4_Word page_table, seL4_Word vaddr, int n);