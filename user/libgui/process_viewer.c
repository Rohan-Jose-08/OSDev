#include <gui_window.h>
#include <graphics.h>
#include <mouse.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <uwm.h>

#define PROC_VIEW_WIDTH 260
#define PROC_VIEW_HEIGHT 190
#define PROC_HEADER_H 18
#define PROC_ROW_H 12
#define PROC_STATUS_H 14
#define PROC_UPDATE_TICKS 50
#define PROC_MAX 32

typedef struct {
	window_t* win;
	user_process_info_t procs[PROC_MAX];
	int proc_count;
	int selected;
	int scroll;
	uint32_t last_update;
} process_view_state_t;

static window_t* process_window = NULL;
static process_view_state_t process_state;

static const char* process_state_label(uint8_t state) {
	if (state == 0) return "READY";
	if (state == 1) return "RUN";
	if (state == 2) return "BLOCK";
	if (state == 3) return "ZOMB";
	return "UNK";
}

static bool process_list_changed(const process_view_state_t* state,
                                 const user_process_info_t* list,
                                 int count) {
	if (!state) return true;
	if (count != state->proc_count) return true;
	for (int i = 0; i < count; i++) {
		const user_process_info_t* a = &state->procs[i];
		const user_process_info_t* b = &list[i];
		if (a->pid != b->pid ||
		    a->state != b->state ||
		    a->priority != b->priority ||
		    a->time_slice != b->time_slice ||
		    a->total_time != b->total_time ||
		    strcmp(a->name, b->name) != 0) {
			return true;
		}
	}
	return false;
}

static void process_view_draw(window_t* win) {
	process_view_state_t* state = (process_view_state_t*)window_get_user_data(win);
	int content_w = window_content_width(win);
	int content_h = window_content_height(win);

	window_clear_content(win, COLOR_WHITE);

	window_fill_rect(win, 0, 0, content_w, PROC_HEADER_H, COLOR_DARK_GRAY);
	window_print(win, 6, 5, "Process Viewer", COLOR_WHITE);

	int list_top = PROC_HEADER_H + 2;
	int list_h = content_h - list_top - PROC_STATUS_H;
	if (list_h < 0) list_h = 0;
	int visible = list_h / PROC_ROW_H;
	if (visible < 1) visible = 1;

	int header_y = list_top + 2;
	window_print(win, 6, header_y, "PID", COLOR_DARK_GRAY);
	window_print(win, 46, header_y, "Name", COLOR_DARK_GRAY);
	window_print(win, 150, header_y, "State", COLOR_DARK_GRAY);
	window_print(win, 204, header_y, "Time", COLOR_DARK_GRAY);

	int y = header_y + PROC_ROW_H;
	int max_display = state->scroll + visible - 1;
	if (max_display > state->proc_count) max_display = state->proc_count;

	for (int i = state->scroll; i < max_display; i++) {
		user_process_info_t* proc = &state->procs[i];
		if (i == state->selected) {
			window_fill_rect(win, 4, y - 1, content_w - 8, PROC_ROW_H, COLOR_LIGHT_CYAN);
		}

		char buf[40];
		snprintf(buf, sizeof(buf), "%u", proc->pid);
		window_print(win, 6, y, buf, COLOR_BLACK);

		char name_buf[32];
		if (strlen(proc->name) > 10) {
			strncpy(name_buf, proc->name, 9);
			name_buf[9] = '.';
			name_buf[10] = '\0';
		} else {
			snprintf(name_buf, sizeof(name_buf), "%s", proc->name);
		}
		window_print(win, 46, y, name_buf, COLOR_BLACK);

		window_print(win, 150, y, process_state_label(proc->state), COLOR_BLUE);

		snprintf(buf, sizeof(buf), "%u", proc->total_time);
		window_print(win, 204, y, buf, COLOR_BLACK);

		y += PROC_ROW_H;
	}

	int status_y = content_h - PROC_STATUS_H;
	window_fill_rect(win, 0, status_y, content_w, PROC_STATUS_H, COLOR_LIGHT_GRAY);
	char status[96];
	snprintf(status, sizeof(status), "Procs: %d | Up/Down:select Scroll:wheel",
	         state->proc_count);
	window_print(win, 5, status_y + 3, status, COLOR_DARK_GRAY);
}

static void process_view_update(window_t* win, uint32_t now_ticks) {
	process_view_state_t* state = (process_view_state_t*)window_get_user_data(win);
	if (!state) return;
	if (now_ticks - state->last_update < PROC_UPDATE_TICKS) {
		return;
	}
	state->last_update = now_ticks;

	user_process_info_t list[PROC_MAX];
	int count = process_list(list, PROC_MAX);
	if (count < 0) count = 0;

	if (process_list_changed(state, list, count)) {
		memset(state->procs, 0, sizeof(state->procs));
		for (int i = 0; i < count && i < PROC_MAX; i++) {
			state->procs[i] = list[i];
		}
		state->proc_count = count;
		if (state->selected >= state->proc_count) {
			state->selected = state->proc_count - 1;
		}
		if (state->selected < 0 && state->proc_count > 0) {
			state->selected = 0;
		}
		if (state->scroll < 0) state->scroll = 0;
		uwm_request_redraw();
	}
}

static void process_view_on_scroll(window_t* win, int delta) {
	process_view_state_t* state = (process_view_state_t*)window_get_user_data(win);
	if (!state) return;
	int content_h = window_content_height(win);
	int list_top = PROC_HEADER_H + 2;
	int list_h = content_h - list_top - PROC_STATUS_H;
	if (list_h < 0) list_h = 0;
	int visible = list_h / PROC_ROW_H;
	if (visible < 1) visible = 1;
	int max_scroll = state->proc_count - (visible - 1);
	if (max_scroll < 0) max_scroll = 0;

	state->scroll += delta;
	if (state->scroll < 0) state->scroll = 0;
	if (state->scroll > max_scroll) state->scroll = max_scroll;
	uwm_request_redraw();
}

static void process_view_on_key(window_t* win, int key) {
	process_view_state_t* state = (process_view_state_t*)window_get_user_data(win);
	if (!state) return;

	int content_h = window_content_height(win);
	int list_top = PROC_HEADER_H + 2;
	int list_h = content_h - list_top - PROC_STATUS_H;
	if (list_h < 0) list_h = 0;
	int visible = list_h / PROC_ROW_H;
	if (visible < 1) visible = 1;

	if ((uint8_t)key == 0x80) {
		if (state->selected > 0) {
			state->selected--;
			if (state->selected < state->scroll) {
				state->scroll = state->selected;
			}
			uwm_request_redraw();
		}
	} else if ((uint8_t)key == 0x81) {
		if (state->selected < state->proc_count - 1) {
			state->selected++;
			if (state->selected >= state->scroll + (visible - 1)) {
				state->scroll++;
			}
			uwm_request_redraw();
		}
	}
}

static void process_view_on_mouse_down(window_t* win, int x, int y, int buttons) {
	process_view_state_t* state = (process_view_state_t*)window_get_user_data(win);
	if (!state || !(buttons & MOUSE_LEFT_BUTTON)) return;

	int list_top = PROC_HEADER_H + 2;
	int list_h = window_content_height(win) - list_top - PROC_STATUS_H;
	if (list_h < 0) list_h = 0;
	int header_y = list_top + 2;
	int rows_top = header_y + PROC_ROW_H;
	if (y < rows_top || y >= rows_top + list_h) {
		return;
	}
	int idx = (y - rows_top) / PROC_ROW_H;
	int item = state->scroll + idx;
	if (item >= 0 && item < state->proc_count) {
		state->selected = item;
		uwm_request_redraw();
	}
}

window_t* gui_process_viewer_create_window(int x, int y) {
	if (process_window && uwm_window_is_open(process_window)) {
		return process_window;
	}
	window_t* win = window_create(x, y, PROC_VIEW_WIDTH, PROC_VIEW_HEIGHT, "Process Viewer");
	if (!win) return NULL;

	memset(&process_state, 0, sizeof(process_state));
	process_state.win = win;
	process_state.proc_count = 0;
	process_state.selected = -1;
	process_state.scroll = 0;
	process_state.last_update = 0;

	window_set_handlers(win, process_view_draw, process_view_on_mouse_down, NULL,
	                    NULL, process_view_on_scroll, process_view_on_key, &process_state);
	window_set_tick_handler(win, process_view_update);
	process_window = win;
	return win;
}
