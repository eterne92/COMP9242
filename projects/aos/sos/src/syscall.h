#pragma once

#include <sel4/sel4.h>
/* 
 * our new syscall
 */

void handle_syscall(seL4_Word badge, int num_args);