#ifndef _KERNEL_TIMER_H
#define _KERNEL_TIMER_H

#include <stdint.h>
#include <kernel/trap_frame.h>

// Initialize the timer
void timer_init(uint32_t frequency);

// Get current tick count
uint32_t timer_get_ticks(void);

// Sleep for specified milliseconds
void timer_sleep_ms(uint32_t ms);

// Timer interrupt handler
void timer_handler(trap_frame_t *frame);

#endif
