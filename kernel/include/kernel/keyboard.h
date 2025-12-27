#ifndef _KERNEL_KEYBOARD_H
#define _KERNEL_KEYBOARD_H

#include <stdbool.h>
#include <stdint.h>

void keyboard_init(void);
void keyboard_handler(void);
bool keyboard_has_input(void);
char keyboard_getchar(void);
void keyboard_clear_buffer(void);
void keyboard_set_typematic(uint8_t delay, uint8_t rate);

#endif
