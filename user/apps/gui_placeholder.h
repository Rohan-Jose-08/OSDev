#ifndef USER_APPS_GUI_PLACEHOLDER_H
#define USER_APPS_GUI_PLACEHOLDER_H

#include <graphics.h>
#include <mouse.h>
#include <string.h>
#include <unistd.h>

static void gui_draw_frame(const char *title, const char *hint) {
	int w = graphics_get_width();
	int h = graphics_get_height();

	graphics_fill_rect(0, 0, w, h, COLOR_LIGHT_CYAN);
	graphics_fill_rect(0, h - 18, w, 18, COLOR_DARK_GRAY);

	if (title) {
		graphics_print(6, 6, title, COLOR_WHITE, COLOR_LIGHT_CYAN);
	}
	if (hint) {
		graphics_print(6, h - 14, hint, COLOR_WHITE, COLOR_DARK_GRAY);
	}
}

static void gui_draw_cursor(int x, int y) {
	graphics_draw_rect(x, y, 5, 5, COLOR_WHITE);
}

static int gui_run_placeholder(const char *title, const char *hint) {
	if (!graphics_set_mode(MODE_320x240)) {
		return 1;
	}

	graphics_enable_double_buffer();

	int w = graphics_get_width();
	int h = graphics_get_height();
	int cursor_x = w / 2;
	int cursor_y = h / 2;

	while (1) {
		mouse_state_t state;
		if (mouse_get_state(&state) < 0) {
			continue;
		}

		cursor_x += state.x;
		cursor_y -= state.y;

		if (cursor_x < 0) cursor_x = 0;
		if (cursor_y < 0) cursor_y = 0;
		if (cursor_x > w - 6) cursor_x = w - 6;
		if (cursor_y > h - 6) cursor_y = h - 6;

		gui_draw_frame(title, hint);
		gui_draw_cursor(cursor_x, cursor_y);

		graphics_flip_buffer();

		if (state.buttons & MOUSE_RIGHT_BUTTON) {
			break;
		}

		sleep_ms(16);
	}

	graphics_disable_double_buffer();
	graphics_return_to_text();
	return 0;
}

#endif
