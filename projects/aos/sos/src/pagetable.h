#pragma once

#include "proc.h"
#include "ut.h"
#include <sel4/sel4.h>
#include <stdbool.h>

/*
 * 4 level shadow page talbe
 * keep track of capabilities of
 * hardware page table
*/

#define PAGE_TABLE_SIZE 512
#define PAGE_TABLE_FRAME_SIZE 3
#define PAGE_FRAME 0xfffffffffffff000

typedef struct page_table page_table_t;

typedef struct page_table_entry {
    seL4_CPtr slot;
    seL4_Word table_addr;     /* virtual address of the shadow page table */
    ut_t *ut;                 /* capability of hardware page table object */
    seL4_Word frame;          /* idx of the underlying frame table */
} page_table_entry;

/*
 * initialize a top level shadow page table
 *
 * return the address of the page table NULL if out of memory
 */
page_table_t *initialize_page_table(void);

void destroy_page_table(page_table_t *table);

/*
 * page fault handler
 * @param cur_proc     current process that triggered the page fault
 * @param vaddr        virtual address that triggered page fault
 * @param fault_info   fault information
 *
 */
// seL4_Error handle_page_fault(proc *cur_proc, seL4_Word vaddr, seL4_Word fault_info);
seL4_Error handle_page_fault(proc *cur_proc, seL4_Word vaddr,
                             seL4_Word fault_info);

/*
 * insert an entry into shadow page table
 * @param table        top level shadow page table
 * @param entry        an page table entry to be inserted
 * @param level        level of the page table (2, 3, 4 are the only valid values)
 * @param vaddr        virtual address that triggered page fault
 *
 * return 0 on success
 */
seL4_Error insert_page_table_entry(page_table_t *table, page_table_entry *entry,
                                   int level, seL4_Word vaddr);

/*
 * update the 4th level page table entry
 * when there is no error happened after calling seL4_ARM_Page_Map
 * only need to save the frame into 4th level page table entry
 *
 * @param table        top level shadow page table
 * @param entry        an page table entry to be inserted
 * @param vaddr        virtual address that triggered page fault
 *
 */
void update_level_4_page_table_entry(page_table_t *table,
                                     page_table_entry *entry, seL4_Word vaddr);

/* some help functions to get slot / frame from vaddr */
seL4_CPtr get_cap_from_vaddr(page_table_t *table, seL4_Word vaddr);
seL4_Word get_frame_from_vaddr(page_table_t *table, seL4_Word vaddr);
seL4_Word _get_frame_from_vaddr(page_table_t *table, seL4_Word vaddr);

/*
 * convert a user-level virtual address to SOS's virtual address
 * @param table        user-level page table
 * @param vaddr        user-level virtual address
 *
 * return SOS's virtual address
 */
seL4_Word get_sos_virtual_address(page_table_t *table, seL4_Word vaddr);


/*
 * load page from swapping file
 * @param offset       the offset of the swapping file where page resides
 * @param vaddr        virtual address that triggered page fault (user level)
 *
 * return 0 on success
 */
seL4_Error load_page(seL4_Word offset, seL4_Word vaddr, proc *cur_proc);

void update_page_status(page_table_t *table, seL4_Word vaddr, bool present,
                        seL4_Word file_offset);

void initialize_swapping_file(void);
seL4_Error try_swap_out(void);

void page_table_destroy(page_table_t *table);

void clean_up_swapping(unsigned offset);