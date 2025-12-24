#ifndef _USER_GRAPHICS_H
#define _USER_GRAPHICS_H

#include <stdint.h>
#include <stdbool.h>

#define MODE_TEXT       0
#define MODE_13H        1
#define MODE_320x240    2
#define MODE_640x480    3

#define COLOR_BLACK         0
#define COLOR_BLUE          1
#define COLOR_GREEN         2
#define COLOR_CYAN          3
#define COLOR_RED           4
#define COLOR_MAGENTA       5
#define COLOR_BROWN         6
#define COLOR_LIGHT_GRAY    7
#define COLOR_DARK_GRAY     8
#define COLOR_LIGHT_BLUE    9
#define COLOR_LIGHT_GREEN   10
#define COLOR_LIGHT_CYAN    11
#define COLOR_LIGHT_RED     12
#define COLOR_LIGHT_MAGENTA 13
#define COLOR_YELLOW        14
#define COLOR_WHITE         15

bool graphics_set_mode(uint8_t mode);
uint8_t graphics_get_mode(void);
void graphics_return_to_text(void);

void graphics_putpixel(int x, int y, uint8_t color);
void graphics_clear(uint8_t color);
void graphics_draw_line(int x1, int y1, int x2, int y2, uint8_t color);
void graphics_draw_rect(int x, int y, int width, int height, uint8_t color);
void graphics_fill_rect(int x, int y, int width, int height, uint8_t color);
void graphics_draw_char(int x, int y, char c, uint8_t fg, uint8_t bg);
void graphics_print(int x, int y, const char* str, uint8_t fg, uint8_t bg);

void graphics_enable_double_buffer(void);
void graphics_disable_double_buffer(void);
void graphics_flip_buffer(void);

int graphics_get_width(void);
int graphics_get_height(void);

#endif
