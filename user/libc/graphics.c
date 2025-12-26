#include <graphics.h>
#include <stdint.h>
#include "syscall.h"

typedef struct {
	int32_t x;
	int32_t y;
	uint8_t color;
} gfx_pixel_t;

typedef struct {
	int32_t x;
	int32_t y;
	int32_t width;
	int32_t height;
	uint8_t color;
} gfx_rect_t;

typedef struct {
	int32_t x1;
	int32_t y1;
	int32_t x2;
	int32_t y2;
	uint8_t color;
} gfx_line_t;

typedef struct {
	int32_t x;
	int32_t y;
	char c;
	uint8_t fg;
	uint8_t bg;
} gfx_char_t;

typedef struct {
	int32_t x;
	int32_t y;
	uint8_t fg;
	uint8_t bg;
	const char *text;
} gfx_print_t;

typedef struct {
	int32_t x;
	int32_t y;
	int32_t width;
	int32_t height;
	int32_t stride;
	const uint8_t *pixels;
} gfx_blit_t;

bool graphics_set_mode(uint8_t mode) {
	return syscall3(SYSCALL_GFX_SET_MODE, mode, 0, 0) == 0;
}

uint8_t graphics_get_mode(void) {
	return (uint8_t)syscall3(SYSCALL_GFX_GET_MODE, 0, 0, 0);
}

void graphics_return_to_text(void) {
	graphics_set_mode(MODE_TEXT);
}

void graphics_putpixel(int x, int y, uint8_t color) {
	gfx_pixel_t args = {x, y, color};
	syscall3(SYSCALL_GFX_PUTPIXEL, (uint32_t)&args, 0, 0);
}

void graphics_clear(uint8_t color) {
	syscall3(SYSCALL_GFX_CLEAR, color, 0, 0);
}

void graphics_draw_line(int x1, int y1, int x2, int y2, uint8_t color) {
	gfx_line_t args = {x1, y1, x2, y2, color};
	syscall3(SYSCALL_GFX_DRAW_LINE, (uint32_t)&args, 0, 0);
}

void graphics_draw_rect(int x, int y, int width, int height, uint8_t color) {
	gfx_rect_t args = {x, y, width, height, color};
	syscall3(SYSCALL_GFX_DRAW_RECT, (uint32_t)&args, 0, 0);
}

void graphics_fill_rect(int x, int y, int width, int height, uint8_t color) {
	gfx_rect_t args = {x, y, width, height, color};
	syscall3(SYSCALL_GFX_FILL_RECT, (uint32_t)&args, 0, 0);
}

void graphics_draw_char(int x, int y, char c, uint8_t fg, uint8_t bg) {
	gfx_char_t args = {x, y, c, fg, bg};
	syscall3(SYSCALL_GFX_DRAW_CHAR, (uint32_t)&args, 0, 0);
}

void graphics_print(int x, int y, const char* str, uint8_t fg, uint8_t bg) {
	gfx_print_t args = {x, y, fg, bg, str};
	syscall3(SYSCALL_GFX_PRINT, (uint32_t)&args, 0, 0);
}

void graphics_blit(int x, int y, int width, int height, const uint8_t* buffer, int stride) {
	gfx_blit_t args = {x, y, width, height, stride, buffer};
	syscall3(SYSCALL_GFX_BLIT, (uint32_t)&args, 0, 0);
}

void graphics_enable_double_buffer(void) {
	syscall3(SYSCALL_GFX_DOUBLEBUFFER_ENABLE, 0, 0, 0);
}

void graphics_disable_double_buffer(void) {
	syscall3(SYSCALL_GFX_DOUBLEBUFFER_DISABLE, 0, 0, 0);
}

void graphics_flip_buffer(void) {
	syscall3(SYSCALL_GFX_FLIP, 0, 0, 0);
}

int graphics_get_width(void) {
	return (int)syscall3(SYSCALL_GFX_GET_WIDTH, 0, 0, 0);
}

int graphics_get_height(void) {
	return (int)syscall3(SYSCALL_GFX_GET_HEIGHT, 0, 0, 0);
}
