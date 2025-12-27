#include <gui_window.h>
#include <graphics.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <uwm.h>

#define SYSMON_WIDTH 240
#define SYSMON_HEIGHT 200
#define SYSMON_HEADER_H 18
#define SYSMON_ROW_H 12
#define SYSMON_UPDATE_TICKS 50

typedef struct {
	window_t* win;
	uint32_t last_update;
	uint32_t ticks;
	uint32_t uptime_sec;
	uint32_t command_count;
	uint32_t process_count;
	int window_count;
	int screen_w;
	int screen_h;
	uint8_t gfx_mode;
	uint32_t free_blocks;
	user_heap_stats_t heap;
	bool heap_valid;
} sysmon_state_t;

static window_t* sysmon_window = NULL;
static sysmon_state_t sysmon_state;

static void sysmon_format_uptime(uint32_t seconds, char* out, int out_len) {
	if (!out || out_len <= 0) return;
	uint32_t hrs = seconds / 3600;
	uint32_t mins = (seconds % 3600) / 60;
	uint32_t secs = seconds % 60;
	snprintf(out, (unsigned int)out_len, "%uh %um %us", hrs, mins, secs);
}

static const char* sysmon_mode_label(uint8_t mode) {
	if (mode == MODE_TEXT) return "Text";
	if (mode == MODE_13H) return "320x200";
	if (mode == MODE_320x240) return "320x240";
	if (mode == MODE_640x480) return "640x480";
	return "Custom";
}

static void sysmon_draw_row(window_t* win, int y, const char* label, const char* value) {
	if (!win || !label || !value) return;
	window_print(win, 8, y, label, COLOR_DARK_GRAY);
	window_print(win, 120, y, value, COLOR_BLACK);
}

static void sysmon_draw(window_t* win) {
	sysmon_state_t* state = (sysmon_state_t*)window_get_user_data(win);
	int content_w = window_content_width(win);
	int content_h = window_content_height(win);

	window_clear_content(win, COLOR_WHITE);
	window_fill_rect(win, 0, 0, content_w, SYSMON_HEADER_H, COLOR_DARK_GRAY);
	window_print(win, 6, 5, "System Monitor", COLOR_WHITE);

	char buf[64];
	int y = SYSMON_HEADER_H + 6;

	char uptime[32];
	sysmon_format_uptime(state->uptime_sec, uptime, sizeof(uptime));
	sysmon_draw_row(win, y, "Uptime:", uptime);
	y += SYSMON_ROW_H;

	snprintf(buf, sizeof(buf), "%u", state->ticks);
	sysmon_draw_row(win, y, "Ticks:", buf);
	y += SYSMON_ROW_H;

	snprintf(buf, sizeof(buf), "%u", state->command_count);
	sysmon_draw_row(win, y, "Commands:", buf);
	y += SYSMON_ROW_H;

	snprintf(buf, sizeof(buf), "%u", state->process_count);
	sysmon_draw_row(win, y, "Processes:", buf);
	y += SYSMON_ROW_H;

	snprintf(buf, sizeof(buf), "%d", state->window_count);
	sysmon_draw_row(win, y, "Windows:", buf);
	y += SYSMON_ROW_H;

	snprintf(buf, sizeof(buf), "%dx%d", state->screen_w, state->screen_h);
	sysmon_draw_row(win, y, "Resolution:", buf);
	y += SYSMON_ROW_H;

	sysmon_draw_row(win, y, "Mode:", sysmon_mode_label(state->gfx_mode));
	y += SYSMON_ROW_H;

	snprintf(buf, sizeof(buf), "%u blocks (%u KB)", state->free_blocks,
	         state->free_blocks / 2);
	sysmon_draw_row(win, y, "Disk Free:", buf);
	y += SYSMON_ROW_H;

	if (state->heap_valid) {
		uint32_t used_kb = state->heap.used_size / 1024;
		uint32_t total_kb = state->heap.total_size / 1024;
		uint32_t largest_kb = state->heap.largest_free_block / 1024;
		snprintf(buf, sizeof(buf), "%u / %u KB", used_kb, total_kb);
		sysmon_draw_row(win, y, "Heap Used:", buf);
		y += SYSMON_ROW_H;
		snprintf(buf, sizeof(buf), "%u KB", largest_kb);
		sysmon_draw_row(win, y, "Heap Largest:", buf);
		y += SYSMON_ROW_H;
	} else {
		sysmon_draw_row(win, y, "Heap Used:", "n/a");
		y += SYSMON_ROW_H;
	}

	window_fill_rect(win, 0, content_h - 14, content_w, 14, COLOR_LIGHT_GRAY);
	window_print(win, 6, content_h - 10, "Updates every 0.5s", COLOR_DARK_GRAY);
}

static void sysmon_tick(window_t* win, uint32_t now_ticks) {
	sysmon_state_t* state = (sysmon_state_t*)window_get_user_data(win);
	if (!state) return;
	if (now_ticks - state->last_update < SYSMON_UPDATE_TICKS) {
		return;
	}
	state->last_update = now_ticks;

	uint32_t ticks = now_ticks;
	uint32_t uptime = ticks / 100;
	uint32_t cmds = get_command_count();
	uint32_t procs = process_count();
	int windows = uwm_window_count();
	int sw = graphics_get_width();
	int sh = graphics_get_height();
	uint8_t mode = graphics_get_mode();
	uint32_t free_blocks = fs_get_free_blocks();
	user_heap_stats_t heap;
	bool heap_ok = (heap_get_stats(&heap) == 0);

	bool changed = (ticks != state->ticks) ||
	               (uptime != state->uptime_sec) ||
	               (cmds != state->command_count) ||
	               (procs != state->process_count) ||
	               (windows != state->window_count) ||
	               (sw != state->screen_w) ||
	               (sh != state->screen_h) ||
	               (mode != state->gfx_mode) ||
	               (free_blocks != state->free_blocks) ||
	               (heap_ok != state->heap_valid) ||
	               (heap_ok && (heap.total_size != state->heap.total_size ||
	                            heap.used_size != state->heap.used_size ||
	                            heap.free_size != state->heap.free_size ||
	                            heap.largest_free_block != state->heap.largest_free_block));

	if (changed) {
		state->ticks = ticks;
		state->uptime_sec = uptime;
		state->command_count = cmds;
		state->process_count = procs;
		state->window_count = windows;
		state->screen_w = sw;
		state->screen_h = sh;
		state->gfx_mode = mode;
		state->free_blocks = free_blocks;
		if (heap_ok) {
			state->heap = heap;
		}
		state->heap_valid = heap_ok;
		uwm_request_redraw();
	}
}

window_t* gui_sysmon_create_window(int x, int y) {
	if (sysmon_window && uwm_window_is_open(sysmon_window)) {
		return sysmon_window;
	}
	window_t* win = window_create(x, y, SYSMON_WIDTH, SYSMON_HEIGHT, "System Monitor");
	if (!win) return NULL;

	memset(&sysmon_state, 0, sizeof(sysmon_state));
	sysmon_state.win = win;
	sysmon_state.last_update = 0;
	sysmon_state.ticks = get_ticks();
	sysmon_state.uptime_sec = sysmon_state.ticks / 100;
	sysmon_state.command_count = get_command_count();
	sysmon_state.process_count = process_count();
	sysmon_state.window_count = uwm_window_count();
	sysmon_state.screen_w = graphics_get_width();
	sysmon_state.screen_h = graphics_get_height();
	sysmon_state.gfx_mode = graphics_get_mode();
	sysmon_state.free_blocks = fs_get_free_blocks();
	sysmon_state.heap_valid = (heap_get_stats(&sysmon_state.heap) == 0);

	window_set_handlers(win, sysmon_draw, NULL, NULL, NULL, NULL, NULL, &sysmon_state);
	window_set_tick_handler(win, sysmon_tick);
	sysmon_window = win;
	return win;
}
