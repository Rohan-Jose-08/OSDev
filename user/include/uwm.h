#ifndef _USER_UWM_H
#define _USER_UWM_H

#include <stdbool.h>
#include <stdint.h>

typedef struct uwm_window uwm_window_t;

typedef void (*uwm_draw_fn)(uwm_window_t*);
typedef void (*uwm_mouse_fn)(uwm_window_t*, int, int, int);
typedef void (*uwm_scroll_fn)(uwm_window_t*, int);
typedef void (*uwm_key_fn)(uwm_window_t*, int);
typedef bool (*uwm_hit_fn)(int, int);

bool uwm_init(uint8_t mode);
void uwm_set_background(uwm_draw_fn draw_fn);
void uwm_set_overlay(uwm_draw_fn draw_fn);
void uwm_set_background_input(uwm_mouse_fn down_fn,
                               uwm_mouse_fn up_fn,
                               uwm_mouse_fn move_fn,
                               uwm_scroll_fn scroll_fn,
                               uwm_key_fn key_fn,
                               uwm_hit_fn capture_fn);
void uwm_run(void);
void uwm_quit(void);

uwm_window_t* uwm_window_create(int x, int y, int width, int height, const char* title);
void uwm_window_set_handlers(uwm_window_t* win,
                             uwm_draw_fn draw_fn,
                             uwm_mouse_fn down_fn,
                             uwm_mouse_fn up_fn,
                             uwm_mouse_fn move_fn,
                             uwm_scroll_fn scroll_fn,
                             uwm_key_fn key_fn,
                             void* user_data);
void uwm_window_destroy(uwm_window_t* win);
void* uwm_window_get_user(uwm_window_t* win);
void uwm_window_set_user(uwm_window_t* win, void* user_data);
bool uwm_window_is_open(uwm_window_t* win);
int uwm_window_client_width(uwm_window_t* win);
int uwm_window_client_height(uwm_window_t* win);

void uwm_window_clear(uwm_window_t* win, uint8_t color);
void uwm_window_putpixel(uwm_window_t* win, int x, int y, uint8_t color);
void uwm_window_draw_rect(uwm_window_t* win, int x, int y, int width, int height, uint8_t color);
void uwm_window_fill_rect(uwm_window_t* win, int x, int y, int width, int height, uint8_t color);
void uwm_window_draw_char(uwm_window_t* win, int x, int y, char c, uint8_t fg, uint8_t bg);
void uwm_window_print(uwm_window_t* win, int x, int y, const char* str, uint8_t fg, uint8_t bg);
void uwm_window_blit(uwm_window_t* win, int x, int y, int width, int height,
                     const uint8_t* buffer, int stride);

#endif
