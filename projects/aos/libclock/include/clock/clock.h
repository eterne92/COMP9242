/*
 * Copyright 2018, Data61
 * Commonwealth Scientific and Industrial Research Organisation (CSIRO)
 * ABN 41 687 119 230.
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(DATA61_GPL)
 */
#pragma once

#include <stdint.h>
#include <sel4/sel4.h>
#include <clock/device.h>
#include <clock/timestamp.h>
#include <cspace/cspace.h>

/*
 * Return codes for driver functions
 */
#define CLOCK_R_OK     0        /* success */
#define CLOCK_R_UINT (-1)       /* driver not initialised */
#define CLOCK_R_CNCL (-2)       /* operation cancelled (driver stopped) */
#define CLOCK_R_FAIL (-3)       /* operation failed for other reason */

typedef uint64_t timestamp_t;
typedef void (*timer_callback_t)(uint64_t id, void *data);
enum TIMER_TYPE { ONE_SHOT, PERIODIC };
enum TIMER_TAG { F, G, H, I};

/*
 * Initialise driver. Performs implicit stop_timer() if already initialised.
 *    interrupt_ep:       A (possibly badged) async endpoint that the driver
                          should use for deliverying interrupts to
 *
 * Returns CLOCK_R_OK iff successful.
 */
int start_timer(cspace_t *cspace, seL4_CPtr ntfn, void *timer_vaddr, enum TIMER_TAG tag);

/*
 * Register a callback to be called after a given delay
 *    delay:  Delay time in microseconds before callback is invoked
 *    callback: Function to be called
 *    data: Custom data to be passed to callback function
 *    type: periodic or one-shot
 *
 * Returns 0 on failure, otherwise an unique ID for this timeout
 */
uint64_t register_timer(uint64_t delay, timer_callback_t callback, void *data, enum TIMER_TAG tag, enum TIMER_TYPE type);

/*
 * Remove a previously registered callback by its ID
 *    id: Unique ID returned by register_time
 * Returns CLOCK_R_OK iff successful.
 */
int remove_timer(enum TIMER_TAG tag, uint64_t id);

/*
 * Handle an interrupt message sent to 'interrupt_ep' from start_timer
 *
 * Returns CLOCK_R_OK iff successful
 */
int timer_interrupt(enum TIMER_TAG tag);

/*
 * Stop clock driver operation.
 *
 * Returns CLOCK_R_OK iff successful.
 */
int stop_timer(enum TIMER_TAG tag);
