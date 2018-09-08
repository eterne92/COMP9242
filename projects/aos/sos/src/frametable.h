#pragma once

#include "ut.h"
#include <cspace/cspace.h>
#include <sel4/sel4.h>
#include <stdint.h>

#define FRAME_BASE 0xA000000000

#define PIN 3
#define CLOCK 4
#define FRAME_SET_BIT(x, bit) (frame_table.frames[x].flag |= (1 << bit))
#define FRAME_CLEAR_BIT(x, bit) (frame_table.frames[x].flag &= ~(1 << bit))
#define PID 0xFF0
#define GET_PID(flag) ((flag & PID) >> 4)
#define SET_PID(x, pid) (frame_table.frames[x].flag |= (pid << 4))
typedef struct frame_table_obj {
    ut_t *ut;
    int next;
    seL4_CPtr frame_cap;
    uint16_t flag;
    seL4_Word vaddr;
} frame_table_obj;

typedef struct frame_table {
    int free;
    int untyped;
    int num_frees;
    frame_table_obj *frames;
    int length;
} frame_table_t;

// declaration of frame table
extern frame_table_t frame_table;

void initialize_frame_table(cspace_t *cspace);

int frame_alloc(seL4_Word *vaddr);

/* cannot use with frame_free */
int frame_n_alloc(seL4_Word *vaddr, int nframes);

/* could only accept frame returned by frame_n_alloc unless n == 1 */
void frame_n_free(int frames);

void frame_free(int frame);