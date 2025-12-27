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
#include <uwm.h>

#define MUSIC_MAX_NOTES 256
#define MUSIC_FILE_MAX (16 * 1024)

#define MUSIC_TOOLBAR_H 18
#define MUSIC_STATUS_H 14
#define MUSIC_PADDING 4
#define MUSIC_BTN_COUNT 3

typedef struct {
	uint16_t freq;
	uint16_t dur_ms;
	uint16_t gap_ms;
} music_note_t;

typedef enum {
	MUSIC_PHASE_IDLE = 0,
	MUSIC_PHASE_NOTE,
	MUSIC_PHASE_GAP
} music_phase_t;

typedef struct {
	window_t* win;
	music_note_t notes[MUSIC_MAX_NOTES];
	int note_count;
	int current;
	bool playing;
	bool tone_active;
	music_phase_t phase;
	uint32_t phase_end;
	uint32_t gap_ticks;
	uint32_t total_ms;
	int hover_btn;
	int btn_x[MUSIC_BTN_COUNT];
	int btn_w[MUSIC_BTN_COUNT];
	char filename[64];
	char status[64];
} music_state_t;

static window_t* music_window = NULL;
static music_state_t music_state;

static const char* music_button_labels[MUSIC_BTN_COUNT] = {
	"Open",
	"Play",
	"Stop"
};

static uint32_t music_ms_to_ticks(uint32_t ms) {
	return (ms * 100 + 999) / 1000;
}

static int music_read_file(const char* path, uint8_t* buffer, int max_len) {
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

static bool music_read_line(const uint8_t* data, int len, int* offset, char* out, int out_size) {
	if (!data || !offset || !out || out_size <= 0 || *offset >= len) {
		return false;
	}
	int pos = *offset;
	int i = 0;
	while (pos < len && data[pos] != '\n') {
		if (i < out_size - 1) {
			out[i++] = (char)data[pos];
		}
		pos++;
	}
	if (pos < len && data[pos] == '\n') {
		pos++;
	}
	out[i] = '\0';
	*offset = pos;
	return true;
}

static char* music_next_token(char** cursor) {
	if (!cursor || !*cursor) {
		return NULL;
	}
	char* s = *cursor;
	while (*s && *s <= ' ') {
		s++;
	}
	if (*s == '\0' || *s == '#') {
		*cursor = s;
		return NULL;
	}
	char* start = s;
	while (*s && *s > ' ') {
		s++;
	}
	if (*s) {
		*s = '\0';
		s++;
	}
	*cursor = s;
	return start;
}

static void music_stop(music_state_t* state) {
	if (!state) return;
	if (state->tone_active) {
		speaker_stop();
		state->tone_active = false;
	}
	state->playing = false;
	state->phase = MUSIC_PHASE_IDLE;
}

static void music_finish(music_state_t* state) {
	if (!state) return;
	music_stop(state);
	snprintf(state->status, sizeof(state->status), "Done");
}

static void music_start_note(music_state_t* state, uint32_t now_ticks) {
	if (!state) return;
	if (state->current >= state->note_count) {
		music_finish(state);
		return;
	}
	music_note_t* note = &state->notes[state->current];
	state->gap_ticks = music_ms_to_ticks(note->gap_ms);
	state->phase = MUSIC_PHASE_NOTE;
	state->phase_end = now_ticks + music_ms_to_ticks(note->dur_ms);
	state->tone_active = false;
	if (note->freq > 0) {
		speaker_start(note->freq);
		state->tone_active = true;
	}
}

static void music_start_playback(music_state_t* state) {
	if (!state) return;
	if (state->note_count <= 0) {
		snprintf(state->status, sizeof(state->status), "No track loaded");
		return;
	}
	if (state->playing) {
		return;
	}
	state->playing = true;
	state->current = 0;
	state->phase = MUSIC_PHASE_IDLE;
	music_start_note(state, get_ticks());
}

static void music_compute_buttons(music_state_t* state) {
	int x = MUSIC_PADDING;
	for (int i = 0; i < MUSIC_BTN_COUNT; i++) {
		int w = (int)strlen(music_button_labels[i]) * 8 + 10;
		state->btn_x[i] = x;
		state->btn_w[i] = w;
		x += w + 4;
	}
}

static int music_button_at(music_state_t* state, int x, int y) {
	if (!state) return -1;
	if (y < 0 || y >= MUSIC_TOOLBAR_H) {
		return -1;
	}
	for (int i = 0; i < MUSIC_BTN_COUNT; i++) {
		int bx = state->btn_x[i];
		int bw = state->btn_w[i];
		if (x >= bx && x < bx + bw) {
			return i;
		}
	}
	return -1;
}

static bool music_load_track(music_state_t* state, const char* path) {
	if (!state || !path) return false;
	static uint8_t buffer[MUSIC_FILE_MAX];
	int bytes = music_read_file(path, buffer, (int)sizeof(buffer));
	if (bytes <= 0) {
		snprintf(state->status, sizeof(state->status), "Failed to read file");
		return false;
	}

	state->note_count = 0;
	state->total_ms = 0;
	int offset = 0;
	char line[96];
	while (music_read_line(buffer, bytes, &offset, line, sizeof(line))) {
		char* cursor = line;
		char* tok = music_next_token(&cursor);
		if (!tok) {
			continue;
		}
		int freq = 0;
		if (tok[0] == 'R' || tok[0] == 'r') {
			freq = 0;
		} else {
			freq = atoi(tok);
		}
		char* tok_dur = music_next_token(&cursor);
		if (!tok_dur) {
			continue;
		}
		int dur = atoi(tok_dur);
		char* tok_gap = music_next_token(&cursor);
		int gap = tok_gap ? atoi(tok_gap) : 0;
		if (dur <= 0) {
			continue;
		}
		if (freq < 0) freq = 0;
		if (freq > 20000) freq = 20000;
		if (gap < 0) gap = 0;
		if (dur > 60000) dur = 60000;
		if (gap > 60000) gap = 60000;

		if (state->note_count < MUSIC_MAX_NOTES) {
			music_note_t* note = &state->notes[state->note_count++];
			note->freq = (uint16_t)freq;
			note->dur_ms = (uint16_t)dur;
			note->gap_ms = (uint16_t)gap;
			state->total_ms += (uint32_t)dur + (uint32_t)gap;
		}
	}

	if (state->note_count <= 0) {
		snprintf(state->status, sizeof(state->status), "No notes found");
		return false;
	}

	strncpy(state->filename, path, sizeof(state->filename) - 1);
	state->filename[sizeof(state->filename) - 1] = '\0';
	snprintf(state->status, sizeof(state->status), "%d notes", state->note_count);
	state->playing = false;
	state->phase = MUSIC_PHASE_IDLE;
	state->current = 0;
	state->tone_active = false;
	return true;
}

static void music_open_callback(const char* path, void* user_data) {
	window_t* win = (window_t*)user_data;
	if (!path || !win) return;
	music_state_t* state = (music_state_t*)window_get_user_data(win);
	if (!state) return;
	music_stop(state);
	music_load_track(state, path);
	uwm_request_redraw();
}

static void music_draw(window_t* win) {
	music_state_t* state = (music_state_t*)window_get_user_data(win);
	int content_w = window_content_width(win);
	int content_h = window_content_height(win);

	music_compute_buttons(state);

	window_clear_content(win, COLOR_LIGHT_GRAY);

	window_fill_rect(win, 0, 0, content_w, MUSIC_TOOLBAR_H, COLOR_DARK_GRAY);
	for (int i = 0; i < MUSIC_BTN_COUNT; i++) {
		int bx = state->btn_x[i];
		int bw = state->btn_w[i];
		uint8_t bg = (state->hover_btn == i) ? COLOR_LIGHT_BLUE : COLOR_LIGHT_GRAY;
		if (i == 1 && state->playing) {
			bg = COLOR_LIGHT_GREEN;
		}
		window_fill_rect(win, bx, 2, bw, MUSIC_TOOLBAR_H - 4, bg);
		window_draw_rect(win, bx, 2, bw, MUSIC_TOOLBAR_H - 4, COLOR_BLACK);
		window_print(win, bx + 4, 6, music_button_labels[i], COLOR_BLACK);
	}

	int body_y = MUSIC_TOOLBAR_H + 6;
	if (state->note_count > 0) {
		char line[80];
		snprintf(line, sizeof(line), "Track: %s",
		         state->filename[0] ? state->filename : "(untitled)");
		window_print(win, MUSIC_PADDING, body_y, line, COLOR_BLACK);
		body_y += 12;
		snprintf(line, sizeof(line), "Notes: %d  Length: %u ms",
		         state->note_count, state->total_ms);
		window_print(win, MUSIC_PADDING, body_y, line, COLOR_DARK_GRAY);
		body_y += 14;
		if (state->playing) {
			snprintf(line, sizeof(line), "Playing: %d / %d",
			         state->current + 1, state->note_count);
			window_print(win, MUSIC_PADDING, body_y, line, COLOR_BLUE);
			body_y += 12;
		}

		int preview = state->note_count;
		if (preview > 6) preview = 6;
		for (int i = 0; i < preview; i++) {
			music_note_t* note = &state->notes[i];
			int idx = i + 1;
			const char* pad = (idx < 10) ? " " : "";
			if (note->freq == 0) {
				snprintf(line, sizeof(line), "%s%d: R %dms +%d",
				         pad, idx, note->dur_ms, note->gap_ms);
			} else {
				snprintf(line, sizeof(line), "%s%d: %dHz %dms +%d",
				         pad, idx, note->freq, note->dur_ms, note->gap_ms);
			}
			window_print(win, MUSIC_PADDING, body_y, line, COLOR_BLACK);
			body_y += 10;
		}
	} else {
		window_print(win, MUSIC_PADDING, body_y, "Open a .tone/.txt track", COLOR_DARK_GRAY);
		body_y += 12;
		window_print(win, MUSIC_PADDING, body_y, "Format: freq dur [gap]", COLOR_DARK_GRAY);
	}

	int status_y = content_h - MUSIC_STATUS_H;
	window_fill_rect(win, 0, status_y, content_w, MUSIC_STATUS_H, COLOR_DARK_GRAY);
	if (state->playing) {
		window_print(win, MUSIC_PADDING, status_y + 3, "Playing", COLOR_LIGHT_GRAY);
	} else if (state->status[0]) {
		window_print(win, MUSIC_PADDING, status_y + 3, state->status, COLOR_LIGHT_GRAY);
	} else {
		window_print(win, MUSIC_PADDING, status_y + 3, "Ready", COLOR_LIGHT_GRAY);
	}
}

static void music_on_mouse_down(window_t* win, int x, int y, int buttons) {
	music_state_t* state = (music_state_t*)window_get_user_data(win);
	if (!state || !(buttons & MOUSE_LEFT_BUTTON)) {
		return;
	}
	if (y < MUSIC_TOOLBAR_H) {
		int hit = music_button_at(state, x, y);
		if (hit == 0) {
			file_dialog_show_open("Open Track", "/", music_open_callback, win);
		} else if (hit == 1) {
			music_start_playback(state);
			uwm_request_redraw();
		} else if (hit == 2) {
			music_stop(state);
			snprintf(state->status, sizeof(state->status), "Stopped");
			uwm_request_redraw();
		}
	}
}

static void music_on_mouse_move(window_t* win, int x, int y, int buttons) {
	(void)buttons;
	music_state_t* state = (music_state_t*)window_get_user_data(win);
	if (!state) return;
	int hover = -1;
	if (y < MUSIC_TOOLBAR_H) {
		hover = music_button_at(state, x, y);
	}
	if (hover != state->hover_btn) {
		state->hover_btn = hover;
		uwm_request_redraw();
	}
}

static void music_on_key(window_t* win, int key) {
	music_state_t* state = (music_state_t*)window_get_user_data(win);
	if (!state) return;
	if (key == 'o' || key == 'O') {
		file_dialog_show_open("Open Track", "/", music_open_callback, win);
	} else if (key == ' ') {
		if (state->playing) {
			music_stop(state);
			snprintf(state->status, sizeof(state->status), "Stopped");
		} else {
			music_start_playback(state);
		}
		uwm_request_redraw();
	}
}

static void music_on_tick(window_t* win, uint32_t now_ticks) {
	music_state_t* state = (music_state_t*)window_get_user_data(win);
	if (!state || !state->playing) {
		return;
	}
	if (state->phase == MUSIC_PHASE_IDLE) {
		return;
	}
	if ((int32_t)(now_ticks - state->phase_end) < 0) {
		return;
	}
	if (state->phase == MUSIC_PHASE_NOTE) {
		if (state->tone_active) {
			speaker_stop();
			state->tone_active = false;
		}
		if (state->gap_ticks > 0) {
			state->phase = MUSIC_PHASE_GAP;
			state->phase_end = now_ticks + state->gap_ticks;
			uwm_request_redraw();
			return;
		}
		state->current++;
		music_start_note(state, now_ticks);
		uwm_request_redraw();
		return;
	}
	if (state->phase == MUSIC_PHASE_GAP) {
		state->current++;
		music_start_note(state, now_ticks);
		uwm_request_redraw();
	}
}

static void music_on_close(window_t* win) {
	(void)win;
	music_stop(&music_state);
}

window_t* gui_music_player_create_window(int x, int y) {
	if (music_window && uwm_window_is_open(music_window)) {
		return music_window;
	}
	window_t* win = window_create(x, y, 260, 180, "Music Player");
	if (!win) return NULL;

	memset(&music_state, 0, sizeof(music_state));
	music_state.win = win;
	music_state.hover_btn = -1;
	music_state.status[0] = '\0';
	music_state.filename[0] = '\0';

	window_set_handlers(win, music_draw, music_on_mouse_down, NULL,
	                    music_on_mouse_move, NULL, music_on_key, &music_state);
	window_set_tick_handler(win, music_on_tick);
	window_set_close_handler(win, music_on_close);
	music_window = win;
	return win;
}
