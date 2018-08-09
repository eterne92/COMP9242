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
#include <clock/clock.h>
#include <cspace/cspace.h>
#include <utils/io.h>
#include <utils/util.h>

#define MAX_TIMER_HANDLER 32
#define MAX_HANDLER_SIZE 32
#define TOLERANCE 5000 // tolerance is 5ms

static seL4_CPtr irq_handler_timer;

typedef struct {
    uint64_t id;
    timer_callback_t callback;
    void * data;
    uint64_t delay;
    uint64_t registered_time;
} timer_handler_obj;

typedef struct {
    int n;
    timer_handler_obj pq[MAX_HANDLER_SIZE + 1];
} timer_handler_heap;

typedef volatile struct {
    uint32_t timer_mux;
    uint32_t timer_f;
    uint32_t timer_g;
    uint32_t timer_h;
    uint32_t timer_i;
    /*TODO add dynamically allocated timer handler heap for each timer  */
} timer_t;

static volatile void *timer_base;
static timer_handler_heap handlers;
/*
static volatile void *timer_base; // virtual address of the TIMER_MUX 
static timer_handler_obj pq[MAX_HANDLER_SIZE + 1];
int n; // number of items on pq 
*/
static inline void update_timer_mux(uint32_t value)
{
    uint32_t timer_mux_val = RAW_READ32(timer_base);
    value = timer_mux_val | value;
    RAW_WRITE32(value, timer_base);
    COMPILER_MEMORY_FENCE();
}

static inline void update_timer_count(uint64_t count, int offset)
{
    uint16_t timer_count = (count / 1000 >= TIMER_MAX) ? TIMER_MAX : count / 1000;
    RAW_WRITE16(timer_count, timer_base + offset);
    COMPILER_MEMORY_FENCE();
}

static inline int cmp(timer_handler_heap *handlers, int index1, int index2)
{
    return handlers->pq[index1].id > handlers->pq[index2].id;
}

static inline void swap(timer_handler_heap *handlers, int index1, int index2)
{
    timer_handler_obj tmp = handlers->pq[index1];
    handlers->pq[index1] = handlers->pq[index2];
    handlers->pq[index2] = tmp;
}

static inline void swim(timer_handler_heap *handlers, int k)
{
    while (k > 0 && cmp(handlers, k / 2, k))
    {
        swap(handlers, k, k / 2);
        k = k / 2;
    }
}

static inline void sink(timer_handler_heap *handlers, int k)
{
    while (2 * k <= handlers->n)
    {
        int j = 2 * k;
        if (j < handlers->n && cmp(handlers, j, j + 1))
            j++;
        if (!cmp(handlers, k, j))
            break;
        swap(handlers, k, j);
        k = j;
    }
}

static inline int is_handlers_empty(timer_handler_heap *handlers)
{
    return handlers->n == 0;
}

static int insert_handler(timer_handler_heap *handlers, uint64_t id, timer_callback_t callback, void *data, uint64_t delay, uint64_t registered_time)
{
    if (handlers->n > MAX_HANDLER_SIZE)
    {
        return -1;
    }
    handlers->n++;
    int n = handlers->n;
    handlers->pq[n].id = id;
    handlers->pq[n].callback = callback;
    handlers->pq[n].data = data;
    handlers->pq[n].delay = delay;
    handlers->pq[n].registered_time = registered_time;
    swim(handlers, handlers->n);
    return 0;
}

static timer_handler_obj *delete_min(timer_handler_heap *handlers) {
    if(handlers->n == 0) {
        return NULL;
    }
    swap(handlers, 1, handlers->n--);
    sink(handlers, 1);
    return &handlers->pq[handlers->n + 1];
}

static timer_handler_obj *peep_min(timer_handler_heap *handlers) {
    if (handlers->n == 0) {
        return NULL;
    }
    return &handlers->pq[1];
}

static int delete_id(timer_handler_heap *handlers, uint64_t id){
    int i;
    for(i = 1; i <= handlers->n; i++){
        if(id == handlers->pq[i].id){
            break;
        }
    }
    if(i > handlers->n){
        /* no such id */
        return -1;
    }
    swap(handlers, i, handlers->n--);
    sink(handlers, i);
    return 0;
}

static seL4_CPtr init_irq(cspace_t *cspace, int irq_number, int edge_triggered,
                          seL4_CPtr ntfn) 
{
    seL4_CPtr irq_handler = cspace_alloc_slot(cspace);
    ZF_LOGF_IF(irq_handler == seL4_CapNull, "Failed to alloc slot for irq handler!");
    seL4_Error error = cspace_irq_control_get(cspace, irq_handler, seL4_CapIRQControl, irq_number, edge_triggered);
    ZF_LOGF_IF(error, "Failed to get irq handler for irq %d", irq_number);
    error = seL4_IRQHandler_SetNotification(irq_handler, ntfn);
    ZF_LOGF_IF(error, "Failed to set irq handler ntfn");
    seL4_IRQHandler_Ack(irq_handler);
    return irq_handler;
}

int start_timer(cspace_t *cspace, seL4_CPtr ntfn, void *timer_vaddr, enum TIMER_TAG tag)
{
    int input_clock, offset, irq_number;
    input_clock = offset = irq_number = -1;
    uint32_t timer_mux_val = 0;
    if (timer_base) {
        /* timer is already initialized 
           stop timer */
           ZF_LOGI("Timer is already initialized!");
           stop_timer(tag);
           return CLOCK_R_OK;
    }
    timer_base = timer_vaddr + (TIMER_MUX & MASK((size_t) seL4_PageBits));
    switch(tag) {
        case F:
            irq_number = TIMER_F_IRQ;
            offset = TIMER_F_PADDR - TIMER_MUX;
            input_clock = TIMER_F_INPUT_CLK;
            break;
        case G:
            irq_number = TIMER_G_IRQ;
            offset = TIMER_G_PADDR - TIMER_MUX;
            input_clock = TIMER_G_INPUT_CLK;
            break;
        case H:
            irq_number = TIMER_H_IRQ;
            offset = TIMER_H_PADDR - TIMER_MUX;
            input_clock = TIMER_H_INPUT_CLK;
            break;
        case I:
            irq_number = TIMER_I_IRQ;
            offset = TIMER_I_PADDR - TIMER_MUX;
            input_clock = TIMER_I_INPUT_CLK;
            break;
        default:
            /* should never come here */
            return CLOCK_R_FAIL;
    }
    irq_handler_timer = init_irq(cspace, irq_number, 1, ntfn);
    /* initialize timer mux */
    update_timer_mux(TIMEBASE_1000_US << input_clock);
}

uint64_t register_timer(uint64_t delay, timer_callback_t callback, void *data, enum TIMER_TAG tag, enum TIMER_TYPE type)
{
    uint32_t timer_mux_val = 0, value = 0;
    int offset = 0;
    uint16_t timer_count = 0;
    uint64_t current_time = timestamp_us(timestamp_get_freq());
    uint64_t id = delay + current_time;
    printf("delay is %ld, time is %ld\n", delay, current_time);
    if(insert_handler(&handlers, id, callback, data, delay, current_time) == -1) {
        /* failed to register timer */
        ZF_LOGE("Failed to register timer!");
        return 0;
    }
   
    switch(tag) {
        case F:
            value = TIMER_F_EN;
            offset = TIMER_F_PADDR - TIMER_MUX;
            break;
        case G:
            value = TIMER_G_EN;
            offset = TIMER_G_PADDR - TIMER_MUX;
            break;
        case H:
            value = TIMER_H_EN;
            offset = TIMER_H_PADDR - TIMER_MUX;
            break;
        case I:
            value = TIMER_I_EN;
            offset = TIMER_I_PADDR - TIMER_MUX;
            break;
        default:
            /* should never come here */
            return CLOCK_R_FAIL;
    }
    /* update the timer registers  */
    update_timer_mux(value);
    timer_handler_obj *obj = peep_min(&handlers);
    update_timer_count(obj->id - current_time, offset);
    return id;
}

int remove_timer(enum TIMER_TAG tag, uint64_t id)
{
    int offset = -1;
    int error = delete_id(&handlers, id);
    if (error != 0) {
        return CLOCK_R_FAIL;
    }
    switch(tag) {
        case F:
            offset = TIMER_F_PADDR - TIMER_MUX;
            break;
        case G:
            offset = TIMER_G_PADDR - TIMER_MUX;
            break;
        case H:
            offset = TIMER_H_PADDR - TIMER_MUX;
            break;
        case I:
            offset = TIMER_I_PADDR - TIMER_MUX;
            break;
        default:
            /* should never come here */
            return CLOCK_R_FAIL;
    }
    if (is_handlers_empty(&handlers)) {
        stop_timer(tag);
    } else {
        /* update the timer registers  */
        timer_handler_obj *obj = peep_min(&handlers);
        update_timer_count(obj->id - timestamp_us(timestamp_get_freq()), offset);
    }
    return CLOCK_R_OK;
}

int timer_interrupt(enum TIMER_TAG tag)
{
    int value, offset, difference;
    uint16_t timer_count = 0;
    uint64_t now = timestamp_us(timestamp_get_freq());
    switch(tag) {
        case F:
            value = TIMER_F_EN;
            offset = TIMER_F_PADDR - TIMER_MUX;
            break;
        case G:
            value = TIMER_G_EN;
            offset = TIMER_G_PADDR - TIMER_MUX;
            break;
        case H:
            value = TIMER_H_EN;
            offset = TIMER_H_PADDR - TIMER_MUX;
            break;
        case I:
            value = TIMER_I_EN;
            offset = TIMER_I_PADDR - TIMER_MUX;
            break;
        default:
            /* should never come here */
            return CLOCK_R_FAIL;
    }
    timer_handler_obj obj = *peep_min(&handlers);
    difference = obj.id - now;

    if (difference < 0 || difference <= TOLERANCE) {
        printf("INTERRUPT CALLED TIMER TAG %d, at %ld, diff is %ld\n", tag, now, now - obj.id);
        delete_min(&handlers);
        /* is it the last one */
        if(is_handlers_empty(&handlers)){
            stop_timer(tag);
        }
        else{
            update_timer_count(peep_min(&handlers)->id - now, offset);
        }

        seL4_IRQHandler_Ack(irq_handler_timer);
        if (obj.callback) {
            obj.callback(obj.id, obj.data);
        }
    } else {
        // timer interrupt triggerd prematurely 
        // register again
        update_timer_count(obj.id - now, offset);
        printf("setup time %ld\n", (obj.id - now));
        seL4_IRQHandler_Ack(irq_handler_timer);
    }
    return CLOCK_R_OK;
}

int stop_timer(enum TIMER_TAG tag)
{
    uint32_t timer_mux_val = 0, value = 0;
    if (!timer_base) {
        ZF_LOGE("Timer has not been initialized!");
        return CLOCK_R_UINT;
    }
    switch(tag) {
        case F:
            value = ~((uint32_t) TIMER_F_EN);
            break;
        case G:
            value = ~((uint32_t) TIMER_G_EN);
            break;
        case H:
            value = ~((uint32_t) TIMER_H_EN);
            break;
        case I:
            value = ~((uint32_t) TIMER_I_EN);
            break;
        default:
            /* should never come here */
            return CLOCK_R_FAIL;
    }
    /* disable timer */
    timer_mux_val = RAW_READ32(timer_base);
    RAW_WRITE32(timer_mux_val & value, timer_base); 
    COMPILER_MEMORY_FENCE();
    return CLOCK_R_OK;
}
