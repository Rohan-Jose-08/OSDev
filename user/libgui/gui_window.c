#include <gui_window.h>
#include <stddef.h>

#define WINDOW_TEXT_TRANSPARENT 0xFF

int window_content_width(window_t* win) {
	return uwm_window_client_width(win);
}

int window_content_height(window_t* win) {
	return uwm_window_client_height(win);
}

void window_clear_content(window_t* win, uint8_t color) {
	uwm_window_clear(win, color);
}

void window_putpixel(window_t* win, int x, int y, uint8_t color) {
	int w = window_content_width(win);
	int h = window_content_height(win);
	if (x < 0 || y < 0 || x >= w || y >= h) {
		return;
	}
	uwm_window_putpixel(win, x, y, color);
}

void window_draw_rect(window_t* win, int x, int y, int width, int height, uint8_t color) {
	uwm_window_draw_rect(win, x, y, width, height, color);
}

void window_fill_rect(window_t* win, int x, int y, int width, int height, uint8_t color) {
	uwm_window_fill_rect(win, x, y, width, height, color);
}

void window_print(window_t* win, int x, int y, const char* text, uint8_t color) {
	if (!win || !text) {
		return;
	}

	int cx = x;
	int cy = y;
	int max_width = window_content_width(win);
	int max_height = window_content_height(win);

	while (*text) {
		if (*text == '\n') {
			cx = x;
			cy += 8;
		} else {
			if (cx + 8 > max_width) {
				cx = x;
				cy += 8;
			}
			if (cy + 8 > max_height) {
				break;
			}
			uwm_window_draw_char(win, cx, cy, *text, color, WINDOW_TEXT_TRANSPARENT);
			cx += 8;
		}

		text++;
		if (cx + 8 > max_width) {
			cx = x;
			cy += 8;
		}
	}
}

void window_blit(window_t* win, int x, int y, int width, int height,
                 const uint8_t* buffer, int stride) {
	if (!win || !buffer || width <= 0 || height <= 0) {
		return;
	}
	uwm_window_blit(win, x, y, width, height, buffer, stride);
}
