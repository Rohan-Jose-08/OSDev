#include <gui_window.h>
#include <stddef.h>

extern const uint8_t font_8x8[256][8];

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

	while (*text) {
		if (*text == '\n') {
			cx = x;
			cy += 8;
		} else {
			const uint8_t* glyph = font_8x8[(uint8_t)*text];
			for (int row = 0; row < 8; row++) {
				uint8_t bits = glyph[row];
				for (int col = 0; col < 8; col++) {
					if (bits & (1u << (7 - col))) {
						window_putpixel(win, cx + col, cy + row, color);
					}
				}
			}
			cx += 8;
		}

		text++;
		if (cx + 8 > max_width) {
			cx = x;
			cy += 8;
		}
	}
}
