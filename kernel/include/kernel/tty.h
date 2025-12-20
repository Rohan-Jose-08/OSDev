#ifndef _KERNEL_TTY_H
#define _KERNEL_TTY_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

void terminal_initialize(void);
void terminal_putchar(char c);
void terminal_write(const char* data, size_t size);
void terminal_writestring(const char* data);
void terminal_setcolor(uint8_t color);
uint8_t terminal_getcolor(void);
void terminal_scroll_up(void);
void terminal_scroll_down(void);
void terminal_redraw_scrollback(void);
void terminal_set_mode_80x25(void);
void terminal_set_mode_80x50(void);
size_t terminal_get_width(void);
size_t terminal_get_height(void);
bool terminal_is_scrolled(void);
size_t terminal_get_scroll_offset(void);
void terminal_enable_cursor(void);
void terminal_disable_cursor(void);
void terminal_update_cursor(size_t x, size_t y);
size_t terminal_get_row(void);
size_t terminal_get_column(void);

#endif
