#ifndef _USER_GUI_WINDOW_H
#define _USER_GUI_WINDOW_H

#include <stdint.h>
#include <uwm.h>

typedef uwm_window_t window_t;

typedef void (*window_draw_fn)(window_t*);
typedef void (*window_mouse_fn)(window_t*, int, int, int);
typedef void (*window_scroll_fn)(window_t*, int);
typedef void (*window_key_fn)(window_t*, int);

int window_content_width(window_t* win);
int window_content_height(window_t* win);

void window_clear_content(window_t* win, uint8_t color);
void window_putpixel(window_t* win, int x, int y, uint8_t color);
void window_draw_rect(window_t* win, int x, int y, int width, int height, uint8_t color);
void window_fill_rect(window_t* win, int x, int y, int width, int height, uint8_t color);
void window_print(window_t* win, int x, int y, const char* text, uint8_t color);
void window_blit(window_t* win, int x, int y, int width, int height,
                 const uint8_t* buffer, int stride);

static inline window_t* window_create(int x, int y, int width, int height, const char* title) {
	return uwm_window_create(x, y, width, height, title);
}

static inline void window_destroy(window_t* win) {
	uwm_window_destroy(win);
}

static inline void window_draw(window_t* win) {
	(void)win;
}

static inline void window_set_user_data(window_t* win, void* user_data) {
	uwm_window_set_user(win, user_data);
}

static inline void* window_get_user_data(window_t* win) {
	return uwm_window_get_user(win);
}

static inline void window_set_handlers(window_t* win,
                                       window_draw_fn draw_fn,
                                       window_mouse_fn down_fn,
                                       window_mouse_fn up_fn,
                                       window_mouse_fn move_fn,
                                       window_scroll_fn scroll_fn,
                                       window_key_fn key_fn,
                                       void* user_data) {
	uwm_window_set_handlers(win, draw_fn, down_fn, up_fn, move_fn, scroll_fn, key_fn, user_data);
}

#endif
