#pragma once

#include <stdint.h>
#include <sel4/sel4.h>
#include <cspace/cspace.h>

void initialize_frame_table(cspace_t *cspace);

int frame_alloc(seL4_Word *vaddr);

void frame_free(int frame);