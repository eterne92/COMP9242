#pragma once

#include <stdint.h>
#include <sel4/sel4.h>
#include <cspace/cspace.h>

void initialize_frame_table(cspace_t *cspace);

int frame_alloc(seL4_Word *vaddr);

/* cannot use with frame_free */
int frame_n_alloc(seL4_Word *vaddr, int nframes);

/* could only accept frame returned by frame_n_alloc unless n == 1 */
void frame_n_free(int frames);

void frame_free(int frame);