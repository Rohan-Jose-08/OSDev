#ifndef _KERNEL_KEYBOARD_H
#define _KERNEL_KEYBOARD_H

#include <stdbool.h>

void keyboard_init(void);
void keyboard_handler(void);
bool keyboard_has_input(void);
char keyboard_getchar(void);
void keyboard_clear_buffer(void);

#endif
