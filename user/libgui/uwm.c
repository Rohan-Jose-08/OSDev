#include <uwm.h>
#include <graphics.h>
#include <mouse.h>
#include <string.h>
#include <unistd.h>

#define UWM_MAX_WINDOWS 8
#define UWM_TITLE_HEIGHT 16
#define UWM_BORDER 2
#define UWM_CLOSE_SIZE 10
#define UWM_SNAP_THRESHOLD 8
#define UWM_RESIZE_GRIP 10
#define UWM_MIN_WIDTH 80
#define UWM_MIN_HEIGHT (UWM_TITLE_HEIGHT + UWM_BORDER + 40)
#define UWM_DBLCLICK_TICKS 12
#define UWM_SWITCHER_TICKS 20
#define UWM_KEY_ALT_DOWN 0x90
#define UWM_KEY_ALT_UP 0x91
#define UWM_KEY_F4 0x92
#define UWM_KEY_CTRL_DOWN 0x93
#define UWM_KEY_CTRL_UP 0x94

#define UWM_SNAP_NONE 0
#define UWM_SNAP_MAX 1
#define UWM_SNAP_LEFT 2
#define UWM_SNAP_RIGHT 3

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
	bool minimized;
	bool dragging;
	bool resizing;
	int snap_mode;
	int drag_dx;
	int drag_dy;
	int drag_start_x;
	int drag_start_y;
	int resize_start_w;
	int resize_start_h;
	int restore_x;
	int restore_y;
	int restore_w;
	int restore_h;
	uwm_draw_fn on_draw;
	uwm_mouse_fn on_mouse_down;
	uwm_mouse_fn on_mouse_up;
	uwm_mouse_fn on_mouse_move;
	uwm_scroll_fn on_scroll;
	uwm_key_fn on_key;
	uwm_tick_fn on_tick;
	uwm_close_fn on_close;
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
static int uwm_cursor_x = 0;
static int uwm_cursor_y = 0;
static uwm_window_t* last_title_click = NULL;
static uint32_t last_title_click_ticks = 0;
static uint32_t switcher_until = 0;
static char uwm_clipboard[256];
static bool uwm_force_redraw = false;

static void focus_window(uwm_window_t* win);
static void recompute_client(uwm_window_t* win);

static bool point_in_window(uwm_window_t* win, int x, int y) {
	if (!win || win->minimized) {
		return false;
	}
	return x >= win->x && y >= win->y &&
	       x < win->x + win->width && y < win->y + win->height;
}

static bool point_in_title(uwm_window_t* win, int x, int y) {
	if (!win || win->minimized) {
		return false;
	}
	return x >= win->x && x < win->x + win->width &&
	       y >= win->y && y < win->y + UWM_TITLE_HEIGHT;
}

static bool point_in_close(uwm_window_t* win, int x, int y) {
	if (!win || win->minimized) {
		return false;
	}
	int cx = win->x + win->width - UWM_CLOSE_SIZE - 4;
	int cy = win->y + 3;
	return x >= cx && x < cx + UWM_CLOSE_SIZE &&
	       y >= cy && y < cy + UWM_CLOSE_SIZE;
}

static bool point_in_resize_grip(uwm_window_t* win, int x, int y) {
	if (!win || win->minimized) {
		return false;
	}
	if (win->snap_mode != UWM_SNAP_NONE) {
		return false;
	}
	int gx = win->x + win->width - UWM_RESIZE_GRIP;
	int gy = win->y + win->height - UWM_RESIZE_GRIP;
	return x >= gx && y >= gy && x < win->x + win->width && y < win->y + win->height;
}

static void apply_window_min_size(uwm_window_t* win) {
	if (!win) {
		return;
	}
	if (win->width < UWM_MIN_WIDTH) win->width = UWM_MIN_WIDTH;
	if (win->height < UWM_MIN_HEIGHT) win->height = UWM_MIN_HEIGHT;
	recompute_client(win);
}

static void save_restore_bounds(uwm_window_t* win) {
	if (!win || win->snap_mode != UWM_SNAP_NONE) {
		return;
	}
	win->restore_x = win->x;
	win->restore_y = win->y;
	win->restore_w = win->width;
	win->restore_h = win->height;
}

static void restore_window(uwm_window_t* win) {
	if (!win || win->snap_mode == UWM_SNAP_NONE) {
		return;
	}
	win->x = win->restore_x;
	win->y = win->restore_y;
	win->width = win->restore_w;
	win->height = win->restore_h;
	win->snap_mode = UWM_SNAP_NONE;
	recompute_client(win);
}

static void snap_window_to(uwm_window_t* win, int mode) {
	int screen_w = graphics_get_width();
	int screen_h = graphics_get_height();
	if (!win || mode == UWM_SNAP_NONE || screen_w <= 0 || screen_h <= 0) {
		return;
	}

	save_restore_bounds(win);
	win->snap_mode = mode;
	if (mode == UWM_SNAP_MAX) {
		win->x = 0;
		win->y = 0;
		win->width = screen_w;
		win->height = screen_h;
	} else if (mode == UWM_SNAP_LEFT) {
		win->x = 0;
		win->y = 0;
		win->width = screen_w / 2;
		win->height = screen_h;
	} else if (mode == UWM_SNAP_RIGHT) {
		win->width = screen_w / 2;
		win->height = screen_h;
		win->x = screen_w - win->width;
		win->y = 0;
	}
	apply_window_min_size(win);
}

static void snap_window_on_release(uwm_window_t* win, int cursor_x, int cursor_y) {
	int screen_w = graphics_get_width();
	int screen_h = graphics_get_height();
	if (!win || screen_w <= 0 || screen_h <= 0) {
		return;
	}
	if (cursor_x < UWM_SNAP_THRESHOLD) {
		snap_window_to(win, UWM_SNAP_LEFT);
		return;
	}
	if (cursor_x > screen_w - UWM_SNAP_THRESHOLD) {
		snap_window_to(win, UWM_SNAP_RIGHT);
		return;
	}
	if (cursor_y < UWM_SNAP_THRESHOLD) {
		snap_window_to(win, UWM_SNAP_MAX);
		return;
	}
}

static bool cancel_active_interactions(void) {
	bool canceled = false;
	for (int i = 0; i < window_count; i++) {
		uwm_window_t* win = window_order[i];
		if (!win || !win->open) {
			continue;
		}
		if (win->resizing) {
			win->resizing = false;
			win->width = win->resize_start_w;
			win->height = win->resize_start_h;
			apply_window_min_size(win);
			canceled = true;
		}
		if (win->dragging) {
			win->dragging = false;
			win->x = win->drag_start_x;
			win->y = win->drag_start_y;
			recompute_client(win);
			canceled = true;
		}
	}
	return canceled;
}

static void focus_prev_window(void) {
	if (window_count <= 0) {
		return;
	}
	int focused_index = -1;
	for (int i = 0; i < window_count; i++) {
		if (window_order[i] && window_order[i]->focused && !window_order[i]->minimized) {
			focused_index = i;
			break;
		}
	}

	for (int offset = 1; offset <= window_count; offset++) {
		int idx = (focused_index >= 0) ? (focused_index - offset) : (window_count - offset);
		if (idx < 0) {
			idx += window_count;
		}
		uwm_window_t* candidate = window_order[idx];
		if (candidate && candidate->open && !candidate->minimized) {
			focus_window(candidate);
			return;
		}
	}
}

static int collect_switcher_windows(uwm_window_t** list, int max) {
	int count = 0;
	for (int i = window_count - 1; i >= 0; i--) {
		uwm_window_t* win = window_order[i];
		if (!win || !win->open || win->minimized) {
			continue;
		}
		list[count++] = win;
		if (count >= max) {
			break;
		}
	}
	return count;
}

static void draw_switcher_overlay(void) {
	if (!switcher_until) {
		return;
	}
	uint32_t now = get_ticks();
	if ((int32_t)(switcher_until - now) <= 0) {
		switcher_until = 0;
		return;
	}

	uwm_window_t* list[UWM_MAX_WINDOWS];
	int count = collect_switcher_windows(list, UWM_MAX_WINDOWS);
	if (count <= 0) {
		return;
	}

	int screen_w = graphics_get_width();
	int screen_h = graphics_get_height();
	int max_chars = 0;
	for (int i = 0; i < count; i++) {
		int len = (int)strlen(list[i]->title);
		if (len > max_chars) {
			max_chars = len;
		}
	}
	if (max_chars < 6) max_chars = 6;
	int width = max_chars * 8 + 24;
	if (width < 120) width = 120;
	if (width > screen_w - 20) width = screen_w - 20;
	int item_h = 14;
	int height = count * item_h + 8;
	int x = (screen_w - width) / 2;
	int y = 10;
	if (x < 2) x = 2;
	if (y + height > screen_h - 2) {
		y = screen_h - height - 2;
		if (y < 2) y = 2;
	}

	graphics_fill_rect(x, y, width, height, COLOR_DARK_GRAY);
	graphics_draw_rect(x, y, width, height, COLOR_WHITE);

	for (int i = 0; i < count; i++) {
		uwm_window_t* win = list[i];
		int row_y = y + 4 + i * item_h;
		bool focused = win->focused;
		if (focused) {
			graphics_fill_rect(x + 2, row_y - 2, width - 4, item_h, COLOR_LIGHT_BLUE);
		}
		graphics_print(x + 8, row_y, win->title,
		               focused ? COLOR_WHITE : COLOR_LIGHT_GRAY,
		               focused ? COLOR_LIGHT_BLUE : COLOR_DARK_GRAY);
	}
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
	if (!win || win->minimized) return;
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

static void focus_top_window(void) {
	for (int i = window_count - 1; i >= 0; i--) {
		uwm_window_t* win = window_order[i];
		if (win && win->open && !win->minimized) {
			focus_window(win);
			return;
		}
	}
}

static void draw_window(uwm_window_t* win) {
	if (!win || !win->open || win->minimized) return;

	bool close_hover = win->focused && point_in_close(win, uwm_cursor_x, uwm_cursor_y);
	uint8_t title_top = win->focused ? COLOR_LIGHT_BLUE : COLOR_LIGHT_GRAY;
	uint8_t title_bottom = win->focused ? COLOR_BLUE : COLOR_DARK_GRAY;
	uint8_t shadow_color = win->focused ? COLOR_BLACK : COLOR_DARK_GRAY;
	int shadow_offset = win->focused ? 3 : 2;

	graphics_fill_rect(win->x + shadow_offset, win->y + shadow_offset,
	                   win->width, win->height, shadow_color);
	graphics_fill_rect(win->x, win->y, win->width, win->height, COLOR_LIGHT_GRAY);
	graphics_draw_rect(win->x, win->y, win->width, win->height,
	                   win->focused ? COLOR_LIGHT_BLUE : COLOR_DARK_GRAY);
	if (win->focused && win->width > 4 && win->height > 4) {
		graphics_draw_rect(win->x + 1, win->y + 1, win->width - 2, win->height - 2,
		                   COLOR_WHITE);
	}
	graphics_fill_rect(win->x, win->y, win->width, UWM_TITLE_HEIGHT / 2, title_top);
	graphics_fill_rect(win->x, win->y + UWM_TITLE_HEIGHT / 2, win->width,
	                   UWM_TITLE_HEIGHT - (UWM_TITLE_HEIGHT / 2), title_bottom);
	graphics_print(win->x + 4, win->y + 4, win->title, COLOR_WHITE, title_top);

	int cx = win->x + win->width - UWM_CLOSE_SIZE - 4;
	int cy = win->y + 3;
	uint8_t close_bg = close_hover ? COLOR_LIGHT_RED : COLOR_RED;
	uint8_t close_border = close_hover ? COLOR_WHITE : COLOR_DARK_GRAY;
	graphics_fill_rect(cx, cy, UWM_CLOSE_SIZE, UWM_CLOSE_SIZE, close_bg);
	graphics_draw_rect(cx, cy, UWM_CLOSE_SIZE, UWM_CLOSE_SIZE, close_border);
	graphics_draw_char(cx + 3, cy + 1, 'X', COLOR_WHITE, close_bg);

	graphics_fill_rect(win->client_x, win->client_y, win->client_w, win->client_h, COLOR_WHITE);

	if (win->on_draw) {
		win->on_draw(win);
	}

	if (win->snap_mode == UWM_SNAP_NONE) {
		int gx = win->x + win->width - UWM_BORDER - 1;
		int gy = win->y + win->height - UWM_BORDER - 1;
		for (int i = 0; i < 3; i++) {
			graphics_draw_line(gx - i * 3, gy, gx, gy - i * 3, COLOR_DARK_GRAY);
		}
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
			win->minimized = false;
			win->x = x;
			win->y = y;
			win->width = width;
			win->height = height;
			apply_window_min_size(win);
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

int uwm_window_count(void) {
	return window_count;
}

uwm_window_t* uwm_window_get_at(int index) {
	if (index < 0 || index >= window_count) {
		return NULL;
	}
	return window_order[index];
}

const char* uwm_window_get_title(uwm_window_t* win) {
	return win ? win->title : "";
}

bool uwm_window_is_focused(uwm_window_t* win) {
	return win && win->focused;
}

bool uwm_window_is_minimized(uwm_window_t* win) {
	return win && win->minimized;
}

void uwm_window_set_minimized(uwm_window_t* win, bool minimized) {
	if (!win || !win->open) return;
	if (minimized) {
		if (win->minimized) return;
		win->minimized = true;
		win->dragging = false;
		if (win->focused) {
			win->focused = false;
			focus_top_window();
		}
	} else {
		if (!win->minimized) return;
		win->minimized = false;
		focus_window(win);
	}
}

void uwm_window_focus(uwm_window_t* win) {
	if (!win || !win->open || win->minimized) return;
	focus_window(win);
}

int uwm_clipboard_set(const char* text) {
	if (!text) {
		uwm_clipboard[0] = '\0';
		return 0;
	}
	strncpy(uwm_clipboard, text, sizeof(uwm_clipboard) - 1);
	uwm_clipboard[sizeof(uwm_clipboard) - 1] = '\0';
	return (int)strlen(uwm_clipboard);
}

int uwm_clipboard_get(char* out, int out_len) {
	if (!out || out_len <= 0) {
		return -1;
	}
	if (out_len == 1) {
		out[0] = '\0';
		return 0;
	}
	strncpy(out, uwm_clipboard, (size_t)out_len - 1);
	out[out_len - 1] = '\0';
	return (int)strlen(out);
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

void uwm_window_set_tick_handler(uwm_window_t* win, uwm_tick_fn tick_fn) {
	if (!win) return;
	win->on_tick = tick_fn;
}

void uwm_window_set_close_handler(uwm_window_t* win, uwm_close_fn close_fn) {
	if (!win) return;
	win->on_close = close_fn;
}

void uwm_window_destroy(uwm_window_t* win) {
	if (!win || !win->open) return;
	if (win->on_close) {
		win->on_close(win);
	}
	bool was_focused = win->focused;
	if (last_title_click == win) {
		last_title_click = NULL;
		last_title_click_ticks = 0;
	}
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
	win->focused = false;
	win->minimized = false;
	if (was_focused) {
		focus_top_window();
	}
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

void uwm_window_blit(uwm_window_t* win, int x, int y, int width, int height,
                     const uint8_t* buffer, int stride) {
	if (!win || !buffer) return;
	graphics_blit(win->client_x + x, win->client_y + y, width, height, buffer, stride);
}

void uwm_quit(void) {
	uwm_running = false;
}

void uwm_request_redraw(void) {
	uwm_force_redraw = true;
}

void uwm_run(void) {
	int cursor_x = graphics_get_width() / 2;
	int cursor_y = graphics_get_height() / 2;
	uint8_t prev_buttons = 0;
	bool needs_redraw = true;
	bool alt_pressed = false;
	bool ctrl_pressed = false;

	uwm_running = true;
	while (uwm_running) {
		uint32_t now_ticks = get_ticks();
		mouse_state_t state;
		if (mouse_get_state(&state) < 0) {
			continue;
		}

		int prev_x = cursor_x;
		int prev_y = cursor_y;
		cursor_x += state.x;
		cursor_y -= state.y;
		if (cursor_x < 0) cursor_x = 0;
		if (cursor_y < 0) cursor_y = 0;
		if (cursor_x > graphics_get_width() - 2) cursor_x = graphics_get_width() - 2;
		if (cursor_y > graphics_get_height() - 2) cursor_y = graphics_get_height() - 2;
		uwm_cursor_x = cursor_x;
		uwm_cursor_y = cursor_y;

		uint8_t buttons = state.buttons;
		int left_down = (buttons & MOUSE_LEFT_BUTTON) && !(prev_buttons & MOUSE_LEFT_BUTTON);
		int left_up = !(buttons & MOUSE_LEFT_BUTTON) && (prev_buttons & MOUSE_LEFT_BUTTON);
		int right_down = (buttons & MOUSE_RIGHT_BUTTON) && !(prev_buttons & MOUSE_RIGHT_BUTTON);
		int right_up = !(buttons & MOUSE_RIGHT_BUTTON) && (prev_buttons & MOUSE_RIGHT_BUTTON);
		if (cursor_x != prev_x || cursor_y != prev_y || buttons != prev_buttons ||
		    state.scroll != 0) {
			needs_redraw = true;
		}

		uwm_window_t* active = NULL;
		for (int i = window_count - 1; i >= 0; i--) {
			if (point_in_window(window_order[i], cursor_x, cursor_y)) {
				active = window_order[i];
				break;
			}
		}
		bool capture = background_capture && background_capture(cursor_x, cursor_y);

		if (left_down || right_down) {
			bool is_left = left_down;
			needs_redraw = true;
			if (capture) {
				if (background_mouse_down) {
					background_mouse_down(NULL, cursor_x, cursor_y, buttons);
				}
			} else if (active) {
				focus_window(active);
				if (is_left && point_in_close(active, cursor_x, cursor_y)) {
					uwm_window_destroy(active);
				} else if (is_left && point_in_resize_grip(active, cursor_x, cursor_y)) {
					active->resizing = true;
					active->resize_start_w = active->width;
					active->resize_start_h = active->height;
				} else if (is_left && point_in_title(active, cursor_x, cursor_y)) {
					uint32_t now = get_ticks();
					bool is_double = (active == last_title_click &&
					                  (now - last_title_click_ticks) <= UWM_DBLCLICK_TICKS);
					last_title_click = active;
					last_title_click_ticks = now;

					if (is_double) {
						if (active->snap_mode == UWM_SNAP_NONE) {
							snap_window_to(active, UWM_SNAP_MAX);
						} else {
							restore_window(active);
						}
					} else {
						if (active->snap_mode != UWM_SNAP_NONE) {
							restore_window(active);
						}
						active->dragging = true;
						active->drag_dx = cursor_x - active->x;
						active->drag_dy = cursor_y - active->y;
						active->drag_start_x = active->x;
						active->drag_start_y = active->y;
					}
				} else if (active->on_mouse_down) {
					int cx = cursor_x - active->client_x;
					int cy = cursor_y - active->client_y;
					active->on_mouse_down(active, cx, cy, buttons);
				}
			} else if (background_mouse_down) {
				background_mouse_down(NULL, cursor_x, cursor_y, buttons);
			}
		}

		if (left_up || right_up) {
			needs_redraw = true;
			if (capture) {
				if (background_mouse_up) {
					background_mouse_up(NULL, cursor_x, cursor_y, buttons);
				}
			} else if (active) {
				if (active->resizing) {
					active->resizing = false;
				}
				if (active->dragging) {
					active->dragging = false;
					snap_window_on_release(active, cursor_x, cursor_y);
				}
				if (active->on_mouse_up) {
					int cx = cursor_x - active->client_x;
					int cy = cursor_y - active->client_y;
					active->on_mouse_up(active, cx, cy, buttons);
				}
			} else if (background_mouse_up) {
				background_mouse_up(NULL, cursor_x, cursor_y, buttons);
			}
		}

		for (int i = 0; i < window_count; i++) {
			uwm_window_t* win = window_order[i];
			if (win->minimized) {
				continue;
			}
			if (win->resizing) {
				int new_w = cursor_x - win->x + 1;
				int new_h = cursor_y - win->y + 1;
				win->width = new_w;
				win->height = new_h;
				apply_window_min_size(win);
				needs_redraw = true;
			} else if (win->dragging) {
				win->x = cursor_x - win->drag_dx;
				win->y = cursor_y - win->drag_dy;
				recompute_client(win);
				needs_redraw = true;
			} else if (!capture && win->focused && win->on_mouse_move &&
			           point_in_window(win, cursor_x, cursor_y)) {
				int cx = cursor_x - win->client_x;
				int cy = cursor_y - win->client_y;
				win->on_mouse_move(win, cx, cy, buttons);
				needs_redraw = true;
			}

			if (!capture && state.scroll != 0 && win->focused && win->on_scroll &&
			    point_in_window(win, cursor_x, cursor_y)) {
				win->on_scroll(win, state.scroll);
				needs_redraw = true;
			}
		}

		if ((capture || !active) && background_mouse_move) {
			background_mouse_move(NULL, cursor_x, cursor_y, buttons);
			needs_redraw = true;
		}
		if ((capture || !active) && state.scroll != 0 && background_scroll) {
			background_scroll(NULL, state.scroll);
			needs_redraw = true;
		}

		if (keyboard_has_input()) {
			int key = getchar();
			if (key == 27) {
				if (cancel_active_interactions()) {
					needs_redraw = true;
					continue;
				}
				uwm_running = false;
				continue;
			}

			if (key == UWM_KEY_ALT_DOWN) {
				alt_pressed = true;
				continue;
			}
			if (key == UWM_KEY_ALT_UP) {
				alt_pressed = false;
				continue;
			}
			if (key == UWM_KEY_CTRL_DOWN) {
				ctrl_pressed = true;
				continue;
			}
			if (key == UWM_KEY_CTRL_UP) {
				ctrl_pressed = false;
				continue;
			}

			if (ctrl_pressed) {
				if (key >= 'a' && key <= 'z') {
					key = key - 'a' + 1;
				} else if (key >= 'A' && key <= 'Z') {
					key = key - 'A' + 1;
				}
			}

			if (alt_pressed && key == '\t') {
				focus_prev_window();
				if (window_count > 0) {
					switcher_until = get_ticks() + UWM_SWITCHER_TICKS;
				}
				needs_redraw = true;
				continue;
			}

			if (alt_pressed && key == UWM_KEY_F4) {
				for (int i = window_count - 1; i >= 0; i--) {
					if (window_order[i]->focused) {
						uwm_window_destroy(window_order[i]);
						needs_redraw = true;
						break;
					}
				}
				continue;
			}

			for (int i = window_count - 1; i >= 0; i--) {
				if (window_order[i]->focused && window_order[i]->on_key) {
					window_order[i]->on_key(window_order[i], key);
					break;
				}
			}
			if (background_key && (!window_count || !window_order[window_count - 1]->focused)) {
				background_key(NULL, key);
			}
			needs_redraw = true;
		}

		if (switcher_until) {
			uint32_t now = get_ticks();
			if ((int32_t)(switcher_until - now) <= 0) {
				switcher_until = 0;
				needs_redraw = true;
			} else {
				needs_redraw = true;
			}
		}

		for (int i = 0; i < window_count; i++) {
			uwm_window_t* win = window_order[i];
			if (win && win->on_tick) {
				win->on_tick(win, now_ticks);
			}
		}
		if (uwm_force_redraw) {
			needs_redraw = true;
			uwm_force_redraw = false;
		}

		if (needs_redraw && uwm_running) {
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

			draw_switcher_overlay();

			graphics_draw_rect(cursor_x, cursor_y, 5, 5, COLOR_BLACK);
			graphics_flip_buffer();
			needs_redraw = false;
		}

		prev_buttons = buttons;
		sleep_ms(16);
	}

	graphics_disable_double_buffer();
	graphics_return_to_text();
}
