#pragma once

#include <stdint.h>
#include <sel4/sel4.h>

void initialize_frame_table(void);

uint32_t frame_alloc(seL4_Word *vaddr);

void frame_free(uint32_t frame);