#pragma once

#include <sel4/sel4.h>

/*
 * 4 level shadow page talbe
 * keep track of capabilities of 
 * hardware page table 
*/

typedef struct page_table_entry {
    
} page_table_entry;


void initialize_page_table();

void destroy_page_table();