#include <kernel/io.h>
#include <kernel/interrupt.h>
#include <kernel/task.h>
#include <kernel/process.h>
#include <stdint.h>

// PIT (Programmable Interval Timer) ports
#define PIT_CHANNEL0    0x40
#define PIT_CHANNEL1    0x41
#define PIT_CHANNEL2    0x42
#define PIT_COMMAND     0x43

// PIT command bits
#define PIT_BINARY      0x00    // Use binary mode
#define PIT_BCD         0x01    // Use BCD mode
#define PIT_MODE0       0x00    // Interrupt on terminal count
#define PIT_MODE1       0x02    // Hardware re-triggerable one-shot
#define PIT_MODE2       0x04    // Rate generator
#define PIT_MODE3       0x06    // Square wave generator
#define PIT_MODE4       0x08    // Software triggered strobe
#define PIT_MODE5       0x0A    // Hardware triggered strobe
#define PIT_LATCH       0x00    // Latch count value command
#define PIT_LSB         0x10    // Access mode: lobyte only
#define PIT_MSB         0x20    // Access mode: hibyte only
#define PIT_BOTH        0x30    // Access mode: lobyte/hibyte

// PIT frequency (Hz)
#define PIT_FREQUENCY   1193182

// Desired timer frequency (Hz) - 100 Hz = 10ms per tick
#define TIMER_FREQUENCY 100

static volatile uint32_t timer_ticks = 0;

// Timer interrupt handler
void timer_handler(trap_frame_t *frame) {
    timer_ticks++;
    
    // Call the scheduler tick
    scheduler_tick();
    process_tick(timer_ticks);
    process_schedule(frame);
}

// Initialize the PIT
void timer_init(uint32_t frequency) {
    // Calculate divisor
    uint32_t divisor = PIT_FREQUENCY / frequency;
    
    // Send command byte
    outb(PIT_COMMAND, PIT_CHANNEL0 | PIT_BOTH | PIT_MODE2 | PIT_BINARY);
    
    // Send frequency divisor
    outb(PIT_CHANNEL0, divisor & 0xFF);         // Low byte
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);  // High byte
    
}

// Get current tick count
uint32_t timer_get_ticks(void) {
    return timer_ticks;
}

// Sleep for specified milliseconds (busy wait for now)
void timer_sleep_ms(uint32_t ms) {
    if (ms == 0) {
        return;
    }

    if (task_current()) {
        uint32_t ticks = (ms * TIMER_FREQUENCY + 999) / 1000;
        if (ticks == 0) {
            ticks = 1;
        }
        task_sleep(ticks);
        return;
    }

    uint32_t target = timer_ticks + (ms * TIMER_FREQUENCY) / 1000;
    if (target == timer_ticks) {
        target++;
    }
    while (timer_ticks < target) {
        __asm__ volatile ("hlt");
    }
}
