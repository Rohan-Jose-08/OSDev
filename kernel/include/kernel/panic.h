#ifndef _KERNEL_PANIC_H
#define _KERNEL_PANIC_H

#include <stdint.h>

__attribute__((noreturn))
void panic(const char *msg);

__attribute__((noreturn))
void panic_isr(const char *msg,
               uint32_t int_no,
               uint32_t err_code,
               uint32_t eip,
               uint32_t ebp,
               uint32_t esp,
               uint32_t eflags,
               uint32_t cr2);

#endif
