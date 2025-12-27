#include <dirent.h>
#include <file_dialog.h>
#include <gui_window.h>
#include <graphics.h>
#include <mouse.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define IMG_MAX_W 320
#define IMG_MAX_H 240
#define IMG_FILE_MAX (96 * 1024)

#define IMG_TOOLBAR_H 18
#define IMG_STATUS_H 14
#define IMG_PADDING 4
#define IMG_MAX_ZOOM_IN 4
#define IMG_MAX_ZOOM_OUT 4

typedef enum {
	IMG_BTN_OPEN = 0,
	IMG_BTN_FIT,
	IMG_BTN_ONE,
	IMG_BTN_ZOOM_IN,
	IMG_BTN_ZOOM_OUT,
	IMG_BTN_COUNT
} img_button_t;

typedef struct {
	window_t* win;
	uint16_t img_w;
	uint16_t img_h;
	bool has_image;
	bool fit;
	int zoom;
	int view_x;
	int view_y;
	bool dragging;
	int drag_x;
	int drag_y;
	int btn_x[IMG_BTN_COUNT];
	int btn_w[IMG_BTN_COUNT];
	int hover_btn;
	char filename[64];
	char status[64];
	uint8_t pixels[IMG_MAX_W * IMG_MAX_H];
} image_state_t;

static window_t* image_window = NULL;
static image_state_t image_state;
static void image_on_draw(window_t* win);

static const char* img_button_labels[IMG_BTN_COUNT] = {
	"Open",
	"Fit",
	"1:1",
	"+",
	"-"
};

static const uint8_t img_palette[16][3] = {
	{0, 0, 0},       {0, 0, 170},   {0, 170, 0},   {0, 170, 170},
	{170, 0, 0},     {170, 0, 170}, {170, 85, 0},  {170, 170, 170},
	{85, 85, 85},    {85, 85, 255}, {85, 255, 85}, {85, 255, 255},
	{255, 85, 85},   {255, 85, 255},{255, 255, 85},{255, 255, 255}
};

static uint8_t img_rgb_to_color(uint8_t r, uint8_t g, uint8_t b) {
	uint32_t best = 0xFFFFFFFF;
	uint8_t best_idx = COLOR_WHITE;
	for (uint8_t i = 0; i < 16; i++) {
		int dr = (int)r - img_palette[i][0];
		int dg = (int)g - img_palette[i][1];
		int db = (int)b - img_palette[i][2];
		uint32_t dist = (uint32_t)(dr * dr + dg * dg + db * db);
		if (dist < best) {
			best = dist;
			best_idx = i;
		}
	}
	return best_idx;
}

static int img_read_file(const char* path, uint8_t* buffer, int max_len) {
	int fd = open(path);
	if (fd < 0) {
		return -1;
	}
	int total = 0;
	while (total < max_len) {
		int n = read(fd, buffer + total, (uint32_t)(max_len - total));
		if (n <= 0) {
			break;
		}
		total += n;
	}
	close(fd);
	return total;
}

static int img_min(int a, int b) {
	return a < b ? a : b;
}

static void img_clamp_view(image_state_t* state, int area_w, int area_h) {
	if (!state || !state->has_image) {
		return;
	}
	int view_w = 1;
	int view_h = 1;
	if (state->zoom >= 1) {
		view_w = area_w / state->zoom;
		view_h = area_h / state->zoom;
	} else {
		int down = -state->zoom;
		view_w = area_w * down;
		view_h = area_h * down;
	}
	if (view_w < 1) view_w = 1;
	if (view_h < 1) view_h = 1;
	if (view_w > state->img_w) view_w = state->img_w;
	if (view_h > state->img_h) view_h = state->img_h;
	int max_x = state->img_w - view_w;
	int max_y = state->img_h - view_h;
	if (max_x < 0) max_x = 0;
	if (max_y < 0) max_y = 0;
	if (state->view_x < 0) state->view_x = 0;
	if (state->view_y < 0) state->view_y = 0;
	if (state->view_x > max_x) state->view_x = max_x;
	if (state->view_y > max_y) state->view_y = max_y;
}

static void img_apply_fit(image_state_t* state, int area_w, int area_h) {
	if (!state || !state->has_image) {
		return;
	}
	int down_w = (state->img_w + area_w - 1) / area_w;
	int down_h = (state->img_h + area_h - 1) / area_h;
	int down = img_min(IMG_MAX_ZOOM_OUT, (down_w > down_h) ? down_w : down_h);
	if (down <= 1) {
		state->zoom = 1;
	} else {
		state->zoom = -down;
	}
	state->view_x = 0;
	state->view_y = 0;
}

static void img_set_zoom(image_state_t* state, int zoom, int area_w, int area_h) {
	if (!state) return;
	if (zoom == 0) zoom = 1;
	if (zoom > IMG_MAX_ZOOM_IN) zoom = IMG_MAX_ZOOM_IN;
	if (zoom < -IMG_MAX_ZOOM_OUT) zoom = -IMG_MAX_ZOOM_OUT;
	state->zoom = zoom;
	state->fit = false;
	img_clamp_view(state, area_w, area_h);
}

static void img_zoom_in(image_state_t* state, int area_w, int area_h) {
	if (!state) return;
	if (state->zoom < 0) {
		if (state->zoom == -2) {
			img_set_zoom(state, 1, area_w, area_h);
			return;
		}
		img_set_zoom(state, state->zoom + 1, area_w, area_h);
		return;
	}
	img_set_zoom(state, state->zoom + 1, area_w, area_h);
}

static void img_zoom_out(image_state_t* state, int area_w, int area_h) {
	if (!state) return;
	if (state->zoom > 1) {
		img_set_zoom(state, state->zoom - 1, area_w, area_h);
		return;
	}
	if (state->zoom == 1) {
		img_set_zoom(state, -2, area_w, area_h);
		return;
	}
	img_set_zoom(state, state->zoom - 1, area_w, area_h);
}

static void img_compute_buttons(image_state_t* state) {
	int x = IMG_PADDING;
	for (int i = 0; i < IMG_BTN_COUNT; i++) {
		int w = (int)strlen(img_button_labels[i]) * 8 + 8;
		state->btn_x[i] = x;
		state->btn_w[i] = w;
		x += w + 4;
	}
}

static int img_button_at(image_state_t* state, int x, int y) {
	if (!state) return -1;
	if (y < 0 || y >= IMG_TOOLBAR_H) {
		return -1;
	}
	for (int i = 0; i < IMG_BTN_COUNT; i++) {
		int bx = state->btn_x[i];
		int bw = state->btn_w[i];
		if (x >= bx && x < bx + bw) {
			return i;
		}
	}
	return -1;
}

static bool img_load_pnt(image_state_t* state, const uint8_t* data, int len) {
	if (!state || len < 12) {
		return false;
	}
	typedef struct {
		uint32_t magic;
		uint16_t version;
		uint16_t width;
		uint16_t height;
		uint16_t reserved;
	} __attribute__((packed)) img_pnt_header_t;

	const img_pnt_header_t* hdr = (const img_pnt_header_t*)data;
	if (hdr->magic != 0x544E4950 || hdr->version != 1) {
		return false;
	}
	int w = hdr->width;
	int h = hdr->height;
	if (w <= 0 || h <= 0) {
		return false;
	}
	int expected = (int)sizeof(img_pnt_header_t) + w * h;
	if (expected > len) {
		return false;
	}
	const uint8_t* pixels = data + sizeof(img_pnt_header_t);

	int target_w = w;
	int target_h = h;
	int step_x = 1;
	int step_y = 1;
	if (target_w > IMG_MAX_W) {
		step_x = (target_w + IMG_MAX_W - 1) / IMG_MAX_W;
		target_w = target_w / step_x;
	}
	if (target_h > IMG_MAX_H) {
		step_y = (target_h + IMG_MAX_H - 1) / IMG_MAX_H;
		target_h = target_h / step_y;
	}
	if (target_w < 1) target_w = 1;
	if (target_h < 1) target_h = 1;

	for (int y = 0; y < target_h; y++) {
		int src_y = y * step_y;
		for (int x = 0; x < target_w; x++) {
			int src_x = x * step_x;
			state->pixels[y * target_w + x] = pixels[src_y * w + src_x];
		}
	}

	state->img_w = (uint16_t)target_w;
	state->img_h = (uint16_t)target_h;
	state->has_image = true;
	return true;
}

static bool img_read_token(const uint8_t* data, int len, int* offset, char* out, int out_size) {
	if (!data || !offset || !out || out_size <= 1) {
		return false;
	}
	int pos = *offset;
	while (pos < len) {
		char c = (char)data[pos];
		if (c == '#') {
			while (pos < len && data[pos] != '\n') {
				pos++;
			}
			continue;
		}
		if (c > ' ') {
			break;
		}
		pos++;
	}
	if (pos >= len) {
		*offset = pos;
		return false;
	}
	int i = 0;
	while (pos < len && data[pos] > ' ' && i < out_size - 1) {
		out[i++] = (char)data[pos++];
	}
	out[i] = '\0';
	*offset = pos;
	return i > 0;
}

static bool img_load_ppm(image_state_t* state, const uint8_t* data, int len) {
	if (!state || len < 8) {
		return false;
	}
	char token[16];
	int offset = 0;

	if (!img_read_token(data, len, &offset, token, sizeof(token))) {
		return false;
	}
	bool is_p6 = (token[0] == 'P' && token[1] == '6');
	bool is_p5 = (token[0] == 'P' && token[1] == '5');
	if (!is_p6 && !is_p5) {
		return false;
	}
	if (!img_read_token(data, len, &offset, token, sizeof(token))) return false;
	int w = atoi(token);
	if (!img_read_token(data, len, &offset, token, sizeof(token))) return false;
	int h = atoi(token);
	if (!img_read_token(data, len, &offset, token, sizeof(token))) return false;
	int maxval = atoi(token);
	if (w <= 0 || h <= 0 || maxval <= 0 || maxval > 255) {
		return false;
	}
	if (offset < len && data[offset] <= ' ') {
		offset++;
	}

	int bpp = is_p6 ? 3 : 1;
	int needed = w * h * bpp;
	if (offset + needed > len) {
		return false;
	}

	int target_w = w;
	int target_h = h;
	int step_x = 1;
	int step_y = 1;
	if (target_w > IMG_MAX_W) {
		step_x = (target_w + IMG_MAX_W - 1) / IMG_MAX_W;
		target_w = target_w / step_x;
	}
	if (target_h > IMG_MAX_H) {
		step_y = (target_h + IMG_MAX_H - 1) / IMG_MAX_H;
		target_h = target_h / step_y;
	}
	if (target_w < 1) target_w = 1;
	if (target_h < 1) target_h = 1;

	const uint8_t* pix = data + offset;
	for (int y = 0; y < target_h; y++) {
		int src_y = y * step_y;
		for (int x = 0; x < target_w; x++) {
			int src_x = x * step_x;
			int src_idx = (src_y * w + src_x) * bpp;
			uint8_t r, g, b;
			if (is_p6) {
				r = pix[src_idx];
				g = pix[src_idx + 1];
				b = pix[src_idx + 2];
			} else {
				r = g = b = pix[src_idx];
			}
			state->pixels[y * target_w + x] = img_rgb_to_color(r, g, b);
		}
	}

	state->img_w = (uint16_t)target_w;
	state->img_h = (uint16_t)target_h;
	state->has_image = true;
	return true;
}

static bool img_load_file(image_state_t* state, const char* path) {
	if (!state || !path) return false;
	static uint8_t buffer[IMG_FILE_MAX];
	int bytes = img_read_file(path, buffer, (int)sizeof(buffer));
	if (bytes <= 0) {
		snprintf(state->status, sizeof(state->status), "Failed to read file");
		return false;
	}

	bool ok = false;
	const char* dot = strrchr(path, '.');
	if (dot && (strcmp(dot, ".pnt") == 0 || strcmp(dot, ".PNT") == 0)) {
		ok = img_load_pnt(state, buffer, bytes);
	} else if (dot && (strcmp(dot, ".ppm") == 0 || strcmp(dot, ".PPM") == 0 ||
	                   strcmp(dot, ".pgm") == 0 || strcmp(dot, ".PGM") == 0)) {
		ok = img_load_ppm(state, buffer, bytes);
	} else {
		ok = img_load_ppm(state, buffer, bytes);
		if (!ok) {
			ok = img_load_pnt(state, buffer, bytes);
		}
	}

	if (!ok) {
		snprintf(state->status, sizeof(state->status), "Unsupported image");
		return false;
	}

	strncpy(state->filename, path, sizeof(state->filename) - 1);
	state->filename[sizeof(state->filename) - 1] = '\0';
	state->fit = true;
	state->zoom = 1;
	state->view_x = 0;
	state->view_y = 0;
	snprintf(state->status, sizeof(state->status), "%dx%d", state->img_w, state->img_h);
	return true;
}

static void image_open_callback(const char* path, void* user_data) {
	window_t* win = (window_t*)user_data;
	if (!path || !win) return;
	image_state_t* state = (image_state_t*)window_get_user_data(win);
	if (!state) return;
	img_load_file(state, path);
	image_on_draw(win);
}

static void img_draw_image(window_t* win, image_state_t* state, int area_x, int area_y,
                           int area_w, int area_h) {
	if (!state->has_image) {
		return;
	}
	if (state->fit) {
		img_apply_fit(state, area_w, area_h);
	}
	img_clamp_view(state, area_w, area_h);

	int draw_x = area_x;
	int draw_y = area_y;
	int view_w;
	int view_h;
	if (state->zoom >= 1) {
		view_w = area_w / state->zoom;
		view_h = area_h / state->zoom;
		if (view_w < 1) view_w = 1;
		if (view_h < 1) view_h = 1;
		if (view_w >= state->img_w && state->view_x == 0) {
			int img_w_px = state->img_w * state->zoom;
			if (img_w_px < area_w) {
				draw_x += (area_w - img_w_px) / 2;
			}
		}
		if (view_h >= state->img_h && state->view_y == 0) {
			int img_h_px = state->img_h * state->zoom;
			if (img_h_px < area_h) {
				draw_y += (area_h - img_h_px) / 2;
			}
		}

		int end_y = state->view_y + view_h;
		if (end_y > state->img_h) end_y = state->img_h;
		int end_x = state->view_x + view_w;
		if (end_x > state->img_w) end_x = state->img_w;
		for (int y = state->view_y; y < end_y; y++) {
			int sy = draw_y + (y - state->view_y) * state->zoom;
			for (int x = state->view_x; x < end_x; x++) {
				uint8_t color = state->pixels[y * state->img_w + x];
				int sx = draw_x + (x - state->view_x) * state->zoom;
				window_fill_rect(win, sx, sy, state->zoom, state->zoom, color);
			}
		}
	} else {
		int down = -state->zoom;
		view_w = area_w * down;
		view_h = area_h * down;
		if (view_w >= state->img_w && state->view_x == 0) {
			int img_w_px = (state->img_w + down - 1) / down;
			if (img_w_px < area_w) {
				draw_x += (area_w - img_w_px) / 2;
			}
		}
		if (view_h >= state->img_h && state->view_y == 0) {
			int img_h_px = (state->img_h + down - 1) / down;
			if (img_h_px < area_h) {
				draw_y += (area_h - img_h_px) / 2;
			}
		}

		for (int sy = 0; sy < area_h; sy++) {
			int img_y = state->view_y + sy * down;
			if (img_y >= state->img_h) break;
			for (int sx = 0; sx < area_w; sx++) {
				int img_x = state->view_x + sx * down;
				if (img_x >= state->img_w) break;
				uint8_t color = state->pixels[img_y * state->img_w + img_x];
				window_putpixel(win, draw_x + sx, draw_y + sy, color);
			}
		}
	}
}

static void image_on_draw(window_t* win) {
	image_state_t* state = (image_state_t*)window_get_user_data(win);
	int content_w = window_content_width(win);
	int content_h = window_content_height(win);

	img_compute_buttons(state);

	window_clear_content(win, COLOR_BLACK);

	window_fill_rect(win, 0, 0, content_w, IMG_TOOLBAR_H, COLOR_DARK_GRAY);
	for (int i = 0; i < IMG_BTN_COUNT; i++) {
		int bx = state->btn_x[i];
		int bw = state->btn_w[i];
		uint8_t bg = (state->hover_btn == i) ? COLOR_LIGHT_BLUE : COLOR_LIGHT_GRAY;
		if (i == IMG_BTN_FIT && state->fit) {
			bg = COLOR_LIGHT_GREEN;
		}
		window_fill_rect(win, bx, 2, bw, IMG_TOOLBAR_H - 4, bg);
		window_draw_rect(win, bx, 2, bw, IMG_TOOLBAR_H - 4, COLOR_BLACK);
		window_print(win, bx + 4, 6, img_button_labels[i], COLOR_BLACK);
	}

	int area_x = 0;
	int area_y = IMG_TOOLBAR_H;
	int area_w = content_w;
	int area_h = content_h - IMG_TOOLBAR_H - IMG_STATUS_H;
	if (area_h < 0) area_h = 0;
	window_fill_rect(win, area_x, area_y, area_w, area_h, COLOR_BLACK);

	if (state->has_image) {
		img_draw_image(win, state, area_x, area_y, area_w, area_h);
	} else {
		window_print(win, IMG_PADDING, area_y + IMG_PADDING, "No image loaded",
		             COLOR_LIGHT_GRAY);
	}

	int status_y = content_h - IMG_STATUS_H;
	window_fill_rect(win, 0, status_y, content_w, IMG_STATUS_H, COLOR_DARK_GRAY);
	char status[96];
	if (state->has_image) {
		int zoom_pct = (state->zoom >= 1) ? (state->zoom * 100)
		                                 : (100 / (-state->zoom));
		snprintf(status, sizeof(status), "%s  %dx%d  %d%%",
		         state->filename[0] ? state->filename : "(untitled)",
		         state->img_w, state->img_h, zoom_pct);
	} else if (state->status[0]) {
		snprintf(status, sizeof(status), "%s", state->status);
	} else {
		snprintf(status, sizeof(status), "Open a .pnt, .ppm, or .pgm file");
	}
	window_print(win, IMG_PADDING, status_y + 3, status, COLOR_LIGHT_GRAY);
}

static void image_on_mouse_down(window_t* win, int x, int y, int buttons) {
	image_state_t* state = (image_state_t*)window_get_user_data(win);
	if (!state || !(buttons & MOUSE_LEFT_BUTTON)) {
		return;
	}
	if (y < IMG_TOOLBAR_H) {
		int hit = img_button_at(state, x, y);
		if (hit >= 0) {
			if (hit == IMG_BTN_OPEN) {
				file_dialog_show_open("Open Image", "/", image_open_callback, win);
			} else if (hit == IMG_BTN_FIT) {
				state->fit = !state->fit;
			} else if (hit == IMG_BTN_ONE) {
				img_set_zoom(state, 1, window_content_width(win),
				             window_content_height(win) - IMG_TOOLBAR_H - IMG_STATUS_H);
			} else if (hit == IMG_BTN_ZOOM_IN) {
				img_zoom_in(state, window_content_width(win),
				            window_content_height(win) - IMG_TOOLBAR_H - IMG_STATUS_H);
			} else if (hit == IMG_BTN_ZOOM_OUT) {
				img_zoom_out(state, window_content_width(win),
				             window_content_height(win) - IMG_TOOLBAR_H - IMG_STATUS_H);
			}
			image_on_draw(win);
			return;
		}
	}

	int area_y = IMG_TOOLBAR_H;
	int area_h = window_content_height(win) - IMG_TOOLBAR_H - IMG_STATUS_H;
	if (y >= area_y && y < area_y + area_h && state->has_image) {
		state->dragging = true;
		state->drag_x = x;
		state->drag_y = y;
	}
}

static void image_on_mouse_up(window_t* win, int x, int y, int buttons) {
	(void)x;
	(void)y;
	(void)buttons;
	image_state_t* state = (image_state_t*)window_get_user_data(win);
	if (!state) return;
	state->dragging = false;
}

static void image_on_mouse_move(window_t* win, int x, int y, int buttons) {
	image_state_t* state = (image_state_t*)window_get_user_data(win);
	if (!state) return;

	int hover = -1;
	if (y < IMG_TOOLBAR_H) {
		hover = img_button_at(state, x, y);
	}
	if (hover != state->hover_btn) {
		state->hover_btn = hover;
		image_on_draw(win);
	}

	if (!state->dragging || !(buttons & MOUSE_LEFT_BUTTON)) {
		return;
	}

	int dx = x - state->drag_x;
	int dy = y - state->drag_y;
	state->drag_x = x;
	state->drag_y = y;

	int area_w = window_content_width(win);
	int area_h = window_content_height(win) - IMG_TOOLBAR_H - IMG_STATUS_H;
	if (area_h < 0) area_h = 0;

	if (state->zoom >= 1) {
		state->view_x -= dx / state->zoom;
		state->view_y -= dy / state->zoom;
	} else {
		int down = -state->zoom;
		state->view_x -= dx * down;
		state->view_y -= dy * down;
	}
	img_clamp_view(state, area_w, area_h);
	image_on_draw(win);
}

static void image_on_scroll(window_t* win, int delta) {
	image_state_t* state = (image_state_t*)window_get_user_data(win);
	if (!state) return;
	int area_w = window_content_width(win);
	int area_h = window_content_height(win) - IMG_TOOLBAR_H - IMG_STATUS_H;
	if (delta > 0) {
		img_zoom_in(state, area_w, area_h);
	} else if (delta < 0) {
		img_zoom_out(state, area_w, area_h);
	}
	image_on_draw(win);
}

static void image_on_key(window_t* win, int key) {
	image_state_t* state = (image_state_t*)window_get_user_data(win);
	if (!state) return;
	int area_w = window_content_width(win);
	int area_h = window_content_height(win) - IMG_TOOLBAR_H - IMG_STATUS_H;

	if (key == 'o' || key == 'O') {
		file_dialog_show_open("Open Image", "/", image_open_callback, win);
	} else if (key == 'f' || key == 'F') {
		state->fit = !state->fit;
	} else if (key == '0' || key == '1') {
		img_set_zoom(state, 1, area_w, area_h);
	} else if (key == '+' || key == '=') {
		img_zoom_in(state, area_w, area_h);
	} else if (key == '-') {
		img_zoom_out(state, area_w, area_h);
	} else if ((uint8_t)key == 0x80) {
		state->view_y -= 4;
	} else if ((uint8_t)key == 0x81) {
		state->view_y += 4;
	} else if ((uint8_t)key == 0x82) {
		state->view_x -= 4;
	} else if ((uint8_t)key == 0x83) {
		state->view_x += 4;
	}

	img_clamp_view(state, area_w, area_h);
	image_on_draw(win);
}

window_t* gui_image_viewer_create_window(int x, int y) {
	if (image_window && uwm_window_is_open(image_window)) {
		return image_window;
	}
	window_t* win = window_create(x, y, 280, 200, "Image Viewer");
	if (!win) return NULL;

	memset(&image_state, 0, sizeof(image_state));
	image_state.win = win;
	image_state.zoom = 1;
	image_state.fit = true;
	image_state.hover_btn = -1;
	image_state.status[0] = '\0';

	window_set_handlers(win, image_on_draw, image_on_mouse_down, image_on_mouse_up,
	                    image_on_mouse_move, image_on_scroll, image_on_key, &image_state);
	image_window = win;
	return win;
}
