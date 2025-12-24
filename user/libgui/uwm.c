#include <uwm.h>
#include <graphics.h>
#include <mouse.h>
#include <string.h>
#include <unistd.h>

#define UWM_MAX_WINDOWS 8
#define UWM_TITLE_HEIGHT 16
#define UWM_BORDER 2
#define UWM_CLOSE_SIZE 10

struct uwm_window {
	int x;
	int y;
	int width;
	int height;
	int client_x;
	int client_y;
	int client_w;
	int client_h;
	char title[32];
	bool open;
	bool focused;
	bool dragging;
	int drag_dx;
	int drag_dy;
	uwm_draw_fn on_draw;
	uwm_mouse_fn on_mouse_down;
	uwm_mouse_fn on_mouse_up;
	uwm_mouse_fn on_mouse_move;
	uwm_scroll_fn on_scroll;
	uwm_key_fn on_key;
	void* user_data;
};

static uwm_window_t windows[UWM_MAX_WINDOWS];
static uwm_window_t* window_order[UWM_MAX_WINDOWS];
static int window_count = 0;
static bool uwm_running = false;
static uwm_draw_fn background_draw = NULL;
static uwm_draw_fn overlay_draw = NULL;
static uwm_mouse_fn background_mouse_down = NULL;
static uwm_mouse_fn background_mouse_up = NULL;
static uwm_mouse_fn background_mouse_move = NULL;
static uwm_scroll_fn background_scroll = NULL;
static uwm_key_fn background_key = NULL;
static uwm_hit_fn background_capture = NULL;

static bool point_in_window(uwm_window_t* win, int x, int y) {
	return x >= win->x && y >= win->y &&
	       x < win->x + win->width && y < win->y + win->height;
}

static bool point_in_title(uwm_window_t* win, int x, int y) {
	return x >= win->x && x < win->x + win->width &&
	       y >= win->y && y < win->y + UWM_TITLE_HEIGHT;
}

static bool point_in_close(uwm_window_t* win, int x, int y) {
	int cx = win->x + win->width - UWM_CLOSE_SIZE - 4;
	int cy = win->y + 3;
	return x >= cx && x < cx + UWM_CLOSE_SIZE &&
	       y >= cy && y < cy + UWM_CLOSE_SIZE;
}

static void recompute_client(uwm_window_t* win) {
	win->client_x = win->x + UWM_BORDER;
	win->client_y = win->y + UWM_TITLE_HEIGHT;
	win->client_w = win->width - UWM_BORDER * 2;
	win->client_h = win->height - UWM_TITLE_HEIGHT - UWM_BORDER;
	if (win->client_w < 0) win->client_w = 0;
	if (win->client_h < 0) win->client_h = 0;
}

static void focus_window(uwm_window_t* win) {
	if (!win) return;
	for (int i = 0; i < window_count; i++) {
		window_order[i]->focused = false;
	}
	win->focused = true;
	for (int i = 0; i < window_count; i++) {
		if (window_order[i] == win) {
			for (int j = i; j < window_count - 1; j++) {
				window_order[j] = window_order[j + 1];
			}
			window_order[window_count - 1] = win;
			break;
		}
	}
}

static void draw_window(uwm_window_t* win) {
	if (!win || !win->open) return;

	uint8_t title_color = win->focused ? COLOR_LIGHT_BLUE : COLOR_DARK_GRAY;
	graphics_fill_rect(win->x, win->y, win->width, win->height, COLOR_LIGHT_GRAY);
	graphics_draw_rect(win->x, win->y, win->width, win->height, COLOR_DARK_GRAY);
	graphics_fill_rect(win->x, win->y, win->width, UWM_TITLE_HEIGHT, title_color);
	graphics_print(win->x + 4, win->y + 4, win->title, COLOR_WHITE, title_color);

	int cx = win->x + win->width - UWM_CLOSE_SIZE - 4;
	int cy = win->y + 3;
	graphics_fill_rect(cx, cy, UWM_CLOSE_SIZE, UWM_CLOSE_SIZE, COLOR_RED);
	graphics_draw_char(cx + 3, cy + 1, 'X', COLOR_WHITE, COLOR_RED);

	graphics_fill_rect(win->client_x, win->client_y, win->client_w, win->client_h, COLOR_WHITE);

	if (win->on_draw) {
		win->on_draw(win);
	}
}

bool uwm_init(uint8_t mode) {
	if (!graphics_set_mode(mode)) {
		return false;
	}
	graphics_enable_double_buffer();
	window_count = 0;
	overlay_draw = NULL;
	background_draw = NULL;
	background_mouse_down = NULL;
	background_mouse_up = NULL;
	background_mouse_move = NULL;
	background_scroll = NULL;
	background_key = NULL;
	background_capture = NULL;
	for (int i = 0; i < UWM_MAX_WINDOWS; i++) {
		windows[i].open = false;
		window_order[i] = NULL;
	}
	return true;
}

void uwm_set_background(uwm_draw_fn draw_fn) {
	background_draw = draw_fn;
}

void uwm_set_overlay(uwm_draw_fn draw_fn) {
	overlay_draw = draw_fn;
}

void uwm_set_background_input(uwm_mouse_fn down_fn,
                              uwm_mouse_fn up_fn,
                              uwm_mouse_fn move_fn,
                              uwm_scroll_fn scroll_fn,
                              uwm_key_fn key_fn,
                              uwm_hit_fn capture_fn) {
	background_mouse_down = down_fn;
	background_mouse_up = up_fn;
	background_mouse_move = move_fn;
	background_scroll = scroll_fn;
	background_key = key_fn;
	background_capture = capture_fn;
}

uwm_window_t* uwm_window_create(int x, int y, int width, int height, const char* title) {
	for (int i = 0; i < UWM_MAX_WINDOWS; i++) {
		if (!windows[i].open) {
			uwm_window_t* win = &windows[i];
			memset(win, 0, sizeof(*win));
			win->open = true;
			win->x = x;
			win->y = y;
			win->width = width;
			win->height = height;
			recompute_client(win);
			if (title) {
				strncpy(win->title, title, sizeof(win->title) - 1);
				win->title[sizeof(win->title) - 1] = '\0';
			}
			window_order[window_count++] = win;
			focus_window(win);
			return win;
		}
	}
	return NULL;
}

void uwm_window_set_handlers(uwm_window_t* win,
                             uwm_draw_fn draw_fn,
                             uwm_mouse_fn down_fn,
                             uwm_mouse_fn up_fn,
                             uwm_mouse_fn move_fn,
                             uwm_scroll_fn scroll_fn,
                             uwm_key_fn key_fn,
                             void* user_data) {
	if (!win) return;
	win->on_draw = draw_fn;
	win->on_mouse_down = down_fn;
	win->on_mouse_up = up_fn;
	win->on_mouse_move = move_fn;
	win->on_scroll = scroll_fn;
	win->on_key = key_fn;
	win->user_data = user_data;
}

void uwm_window_destroy(uwm_window_t* win) {
	if (!win || !win->open) return;
	for (int i = 0; i < window_count; i++) {
		if (window_order[i] == win) {
			for (int j = i; j < window_count - 1; j++) {
				window_order[j] = window_order[j + 1];
			}
			window_order[window_count - 1] = NULL;
			window_count--;
			break;
		}
	}
	win->open = false;
}

void* uwm_window_get_user(uwm_window_t* win) {
	return win ? win->user_data : NULL;
}

void uwm_window_set_user(uwm_window_t* win, void* user_data) {
	if (win) win->user_data = user_data;
}

bool uwm_window_is_open(uwm_window_t* win) {
	return win && win->open;
}

int uwm_window_client_width(uwm_window_t* win) {
	return win ? win->client_w : 0;
}

int uwm_window_client_height(uwm_window_t* win) {
	return win ? win->client_h : 0;
}

void uwm_window_clear(uwm_window_t* win, uint8_t color) {
	if (!win) return;
	graphics_fill_rect(win->client_x, win->client_y, win->client_w, win->client_h, color);
}

void uwm_window_putpixel(uwm_window_t* win, int x, int y, uint8_t color) {
	if (!win) return;
	graphics_putpixel(win->client_x + x, win->client_y + y, color);
}

void uwm_window_draw_rect(uwm_window_t* win, int x, int y, int width, int height, uint8_t color) {
	if (!win) return;
	graphics_draw_rect(win->client_x + x, win->client_y + y, width, height, color);
}

void uwm_window_fill_rect(uwm_window_t* win, int x, int y, int width, int height, uint8_t color) {
	if (!win) return;
	graphics_fill_rect(win->client_x + x, win->client_y + y, width, height, color);
}

void uwm_window_draw_char(uwm_window_t* win, int x, int y, char c, uint8_t fg, uint8_t bg) {
	if (!win) return;
	graphics_draw_char(win->client_x + x, win->client_y + y, c, fg, bg);
}

void uwm_window_print(uwm_window_t* win, int x, int y, const char* str, uint8_t fg, uint8_t bg) {
	if (!win || !str) return;
	graphics_print(win->client_x + x, win->client_y + y, str, fg, bg);
}

void uwm_quit(void) {
	uwm_running = false;
}

void uwm_run(void) {
	int cursor_x = graphics_get_width() / 2;
	int cursor_y = graphics_get_height() / 2;
	uint8_t prev_buttons = 0;

	uwm_running = true;
	while (uwm_running) {
		mouse_state_t state;
		if (mouse_get_state(&state) < 0) {
			continue;
		}

		cursor_x += state.x;
		cursor_y -= state.y;
		if (cursor_x < 0) cursor_x = 0;
		if (cursor_y < 0) cursor_y = 0;
		if (cursor_x > graphics_get_width() - 2) cursor_x = graphics_get_width() - 2;
		if (cursor_y > graphics_get_height() - 2) cursor_y = graphics_get_height() - 2;

		if (background_draw) {
			background_draw(NULL);
		} else {
			graphics_clear(COLOR_LIGHT_CYAN);
		}

		for (int i = 0; i < window_count; i++) {
			draw_window(window_order[i]);
		}

		if (overlay_draw) {
			overlay_draw(NULL);
		}

		graphics_draw_rect(cursor_x, cursor_y, 5, 5, COLOR_WHITE);
		graphics_flip_buffer();

		uint8_t buttons = state.buttons;
		int left_down = (buttons & MOUSE_LEFT_BUTTON) && !(prev_buttons & MOUSE_LEFT_BUTTON);
		int left_up = !(buttons & MOUSE_LEFT_BUTTON) && (prev_buttons & MOUSE_LEFT_BUTTON);

		uwm_window_t* active = NULL;
		for (int i = window_count - 1; i >= 0; i--) {
			if (point_in_window(window_order[i], cursor_x, cursor_y)) {
				active = window_order[i];
				break;
			}
		}
		bool capture = !active && background_capture && background_capture(cursor_x, cursor_y);

		if (left_down && active) {
			focus_window(active);
			if (point_in_close(active, cursor_x, cursor_y)) {
				uwm_window_destroy(active);
			} else if (point_in_title(active, cursor_x, cursor_y)) {
				active->dragging = true;
				active->drag_dx = cursor_x - active->x;
				active->drag_dy = cursor_y - active->y;
			} else if (active->on_mouse_down) {
				int cx = cursor_x - active->client_x;
				int cy = cursor_y - active->client_y;
				active->on_mouse_down(active, cx, cy, buttons);
			}
		} else if (left_down && background_mouse_down) {
			background_mouse_down(NULL, cursor_x, cursor_y, buttons);
		}

		if (left_up && active) {
			if (active->dragging) {
				active->dragging = false;
			}
			if (active->on_mouse_up) {
				int cx = cursor_x - active->client_x;
				int cy = cursor_y - active->client_y;
				active->on_mouse_up(active, cx, cy, buttons);
			}
		} else if (left_up && background_mouse_up) {
			background_mouse_up(NULL, cursor_x, cursor_y, buttons);
		}

		for (int i = 0; i < window_count; i++) {
			uwm_window_t* win = window_order[i];
			if (win->dragging) {
				win->x = cursor_x - win->drag_dx;
				win->y = cursor_y - win->drag_dy;
				recompute_client(win);
			} else if (!capture && win->focused && win->on_mouse_move &&
			           point_in_window(win, cursor_x, cursor_y)) {
				int cx = cursor_x - win->client_x;
				int cy = cursor_y - win->client_y;
				win->on_mouse_move(win, cx, cy, buttons);
			}

			if (!capture && state.scroll != 0 && win->focused && win->on_scroll &&
			    point_in_window(win, cursor_x, cursor_y)) {
				win->on_scroll(win, state.scroll);
			}
		}

		if ((capture || !active) && background_mouse_move) {
			background_mouse_move(NULL, cursor_x, cursor_y, buttons);
		}
		if ((capture || !active) && state.scroll != 0 && background_scroll) {
			background_scroll(NULL, state.scroll);
		}

		if (keyboard_has_input()) {
			int key = getchar();
			if (key == 27) {
				uwm_running = false;
			} else {
				for (int i = window_count - 1; i >= 0; i--) {
					if (window_order[i]->focused && window_order[i]->on_key) {
						window_order[i]->on_key(window_order[i], key);
						break;
					}
				}
				if (background_key && (!window_count || !window_order[window_count - 1]->focused)) {
					background_key(NULL, key);
				}
			}
		}

		prev_buttons = buttons;
		sleep_ms(16);
	}

	graphics_disable_double_buffer();
	graphics_return_to_text();
}
