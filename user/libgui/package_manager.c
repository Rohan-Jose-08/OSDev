#include <gui_window.h>
#include <graphics.h>
#include <mouse.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <uwm.h>

#define PKG_WIDTH 260
#define PKG_HEIGHT 190
#define PKG_TOOLBAR_H 18
#define PKG_STATUS_H 14
#define PKG_ROW_H 12
#define PKG_BTN_COUNT 4
#define PKG_MAX 80

typedef struct {
	const char* name;
	const char* path;
} pkg_entry_t;

static const pkg_entry_t pkg_list[] = {
	{"hello", "/bin/hello.elf"},
	{"cat", "/bin/cat.elf"},
	{"execdemo", "/bin/execdemo.elf"},
	{"statdemo", "/bin/statdemo.elf"},
	{"ls", "/bin/ls.elf"},
	{"rm", "/bin/rm.elf"},
	{"mkdir", "/bin/mkdir.elf"},
	{"touch", "/bin/touch.elf"},
	{"pwd", "/bin/pwd.elf"},
	{"echo", "/bin/echo.elf"},
	{"reverse", "/bin/reverse.elf"},
	{"strlen", "/bin/strlen.elf"},
	{"upper", "/bin/upper.elf"},
	{"lower", "/bin/lower.elf"},
	{"calc", "/bin/calc.elf"},
	{"draw", "/bin/draw.elf"},
	{"banner", "/bin/banner.elf"},
	{"clear", "/bin/clear.elf"},
	{"color", "/bin/color.elf"},
	{"colors", "/bin/colors.elf"},
	{"write", "/bin/write.elf"},
	{"history", "/bin/history.elf"},
	{"cd", "/bin/cd.elf"},
	{"help", "/bin/help.elf"},
	{"about", "/bin/about.elf"},
	{"sysinfo", "/bin/sysinfo.elf"},
	{"uptime", "/bin/uptime.elf"},
	{"randcolor", "/bin/randcolor.elf"},
	{"rainbow", "/bin/rainbow.elf"},
	{"art", "/bin/art.elf"},
	{"fortune", "/bin/fortune.elf"},
	{"animate", "/bin/animate.elf"},
	{"matrix", "/bin/matrix.elf"},
	{"guess", "/bin/guess.elf"},
	{"rps", "/bin/rps.elf"},
	{"tictactoe", "/bin/tictactoe.elf"},
	{"hangman", "/bin/hangman.elf"},
	{"timer", "/bin/timer.elf"},
	{"alias", "/bin/alias.elf"},
	{"unalias", "/bin/unalias.elf"},
	{"aliases", "/bin/aliases.elf"},
	{"theme", "/bin/theme.elf"},
	{"beep", "/bin/beep.elf"},
	{"soundtest", "/bin/soundtest.elf"},
	{"mixer", "/bin/mixer.elf"},
	{"halt", "/bin/halt.elf"},
	{"run", "/bin/run.elf"},
	{"rmdir", "/bin/rmdir.elf"},
	{"gfx", "/bin/gfx.elf"},
	{"gfxanim", "/bin/gfxanim.elf"},
	{"gfxpaint", "/bin/gfxpaint.elf"},
	{"gui", "/bin/gui.elf"},
	{"guipaint", "/bin/guipaint.elf"},
	{"guicalc", "/bin/guicalc.elf"},
	{"guifilemgr", "/bin/guifilemgr.elf"},
	{"desktop", "/bin/desktop.elf"},
	{"forktest", "/bin/forktest.elf"},
	{"schedtest", "/bin/schedtest.elf"},
	{"fault", "/bin/fault.elf"},
	{"abi_test", "/bin/abi_test.elf"},
};

typedef enum {
	PKG_BTN_INSTALL = 0,
	PKG_BTN_REMOVE,
	PKG_BTN_UPDATE_ALL,
	PKG_BTN_REFRESH
} pkg_button_t;

typedef struct {
	window_t* win;
	bool installed[PKG_MAX];
	int selected;
	int scroll;
	int hover_btn;
	int btn_x[PKG_BTN_COUNT];
	int btn_w[PKG_BTN_COUNT];
	char status[64];
} pkg_state_t;

static window_t* pkg_window = NULL;
static pkg_state_t pkg_state;

static const char* pkg_button_labels[PKG_BTN_COUNT] = {
	"Install",
	"Remove",
	"Update All",
	"Refresh"
};

static int pkg_count(void) {
	int count = (int)(sizeof(pkg_list) / sizeof(pkg_list[0]));
	if (count > PKG_MAX) count = PKG_MAX;
	return count;
}

static bool pkg_is_installed(const char* path) {
	int fd = open(path);
	if (fd >= 0) {
		close(fd);
		return true;
	}
	return false;
}

static void pkg_scan(pkg_state_t* state) {
	if (!state) return;
	int count = pkg_count();
	for (int i = 0; i < count; i++) {
		state->installed[i] = pkg_is_installed(pkg_list[i].path);
	}
	if (state->selected >= count) {
		state->selected = count - 1;
	}
	if (state->selected < 0 && count > 0) {
		state->selected = 0;
	}
}

static void pkg_compute_buttons(pkg_state_t* state) {
	int x = 4;
	for (int i = 0; i < PKG_BTN_COUNT; i++) {
		int w = (int)strlen(pkg_button_labels[i]) * 8 + 10;
		state->btn_x[i] = x;
		state->btn_w[i] = w;
		x += w + 4;
	}
}

static int pkg_button_at(pkg_state_t* state, int x, int y) {
	if (!state) return -1;
	if (y < 0 || y >= PKG_TOOLBAR_H) return -1;
	for (int i = 0; i < PKG_BTN_COUNT; i++) {
		int bx = state->btn_x[i];
		int bw = state->btn_w[i];
		if (x >= bx && x < bx + bw) {
			return i;
		}
	}
	return -1;
}

static void pkg_set_status(pkg_state_t* state, const char* text) {
	if (!state || !text) return;
	snprintf(state->status, sizeof(state->status), "%s", text);
}

static void pkg_install_selected(pkg_state_t* state) {
	if (!state) return;
	int count = pkg_count();
	if (state->selected < 0 || state->selected >= count) return;
	const char* path = pkg_list[state->selected].path;
	if (install_embedded(path) == 0) {
		state->installed[state->selected] = true;
		pkg_set_status(state, "Installed/updated");
	} else {
		pkg_set_status(state, "Install failed");
	}
}

static void pkg_remove_selected(pkg_state_t* state) {
	if (!state) return;
	int count = pkg_count();
	if (state->selected < 0 || state->selected >= count) return;
	const char* path = pkg_list[state->selected].path;
	if (rm(path) == 0) {
		state->installed[state->selected] = false;
		pkg_set_status(state, "Removed");
	} else {
		pkg_set_status(state, "Remove failed");
	}
}

static void pkg_update_all(pkg_state_t* state) {
	if (!state) return;
	int count = pkg_count();
	int updated = 0;
	for (int i = 0; i < count; i++) {
		if (install_embedded(pkg_list[i].path) == 0) {
			state->installed[i] = true;
			updated++;
		}
	}
	char buf[64];
	snprintf(buf, sizeof(buf), "Updated %d", updated);
	pkg_set_status(state, buf);
}

static void pkg_draw(window_t* win) {
	pkg_state_t* state = (pkg_state_t*)window_get_user_data(win);
	int content_w = window_content_width(win);
	int content_h = window_content_height(win);

	window_clear_content(win, COLOR_WHITE);

	pkg_compute_buttons(state);
	window_fill_rect(win, 0, 0, content_w, PKG_TOOLBAR_H, COLOR_DARK_GRAY);
	for (int i = 0; i < PKG_BTN_COUNT; i++) {
		int bx = state->btn_x[i];
		int bw = state->btn_w[i];
		uint8_t bg = (state->hover_btn == i) ? COLOR_LIGHT_BLUE : COLOR_LIGHT_GRAY;
		window_fill_rect(win, bx, 2, bw, PKG_TOOLBAR_H - 4, bg);
		window_draw_rect(win, bx, 2, bw, PKG_TOOLBAR_H - 4, COLOR_BLACK);
		window_print(win, bx + 4, 6, pkg_button_labels[i], COLOR_BLACK);
	}

	int list_top = PKG_TOOLBAR_H + 2;
	int list_h = content_h - list_top - PKG_STATUS_H;
	if (list_h < 0) list_h = 0;
	int visible = list_h / PKG_ROW_H;
	if (visible < 1) visible = 1;

	int max_display = state->scroll + visible;
	int count = pkg_count();
	if (max_display > count) max_display = count;

	int y = list_top + 2;
	for (int i = state->scroll; i < max_display; i++) {
		if (i == state->selected) {
			window_fill_rect(win, 4, y - 1, content_w - 8, PKG_ROW_H, COLOR_LIGHT_CYAN);
		}

		window_print(win, 8, y, pkg_list[i].name, COLOR_BLACK);
		if (state->installed[i]) {
			window_print(win, content_w - 52, y, "INST", COLOR_GREEN);
		} else {
			window_print(win, content_w - 52, y, "----", COLOR_DARK_GRAY);
		}
		y += PKG_ROW_H;
	}

	int status_y = content_h - PKG_STATUS_H;
	window_fill_rect(win, 0, status_y, content_w, PKG_STATUS_H, COLOR_LIGHT_GRAY);
	if (state->status[0]) {
		window_print(win, 5, status_y + 3, state->status, COLOR_DARK_GRAY);
	} else {
		window_print(win, 5, status_y + 3, "Enter:install Del:remove U:update all R:refresh",
		             COLOR_DARK_GRAY);
	}
}

static void pkg_on_mouse_down(window_t* win, int x, int y, int buttons) {
	pkg_state_t* state = (pkg_state_t*)window_get_user_data(win);
	if (!state || !(buttons & MOUSE_LEFT_BUTTON)) return;

	if (y < PKG_TOOLBAR_H) {
		int hit = pkg_button_at(state, x, y);
		if (hit == PKG_BTN_INSTALL) {
			pkg_install_selected(state);
		} else if (hit == PKG_BTN_REMOVE) {
			pkg_remove_selected(state);
		} else if (hit == PKG_BTN_UPDATE_ALL) {
			pkg_update_all(state);
		} else if (hit == PKG_BTN_REFRESH) {
			pkg_scan(state);
			pkg_set_status(state, "Refreshed");
		}
		uwm_request_redraw();
		return;
	}

	int list_top = PKG_TOOLBAR_H + 2;
	int list_h = window_content_height(win) - list_top - PKG_STATUS_H;
	if (list_h < 0) list_h = 0;
	int idx = (y - (list_top + 2)) / PKG_ROW_H;
	if (idx < 0) return;
	int item = state->scroll + idx;
	int count = pkg_count();
	if (item >= 0 && item < count) {
		state->selected = item;
		uwm_request_redraw();
	}
}

static void pkg_on_mouse_move(window_t* win, int x, int y, int buttons) {
	(void)buttons;
	pkg_state_t* state = (pkg_state_t*)window_get_user_data(win);
	if (!state) return;
	int hover = -1;
	if (y < PKG_TOOLBAR_H) {
		hover = pkg_button_at(state, x, y);
	}
	if (hover != state->hover_btn) {
		state->hover_btn = hover;
		uwm_request_redraw();
	}
}

static void pkg_on_scroll(window_t* win, int delta) {
	pkg_state_t* state = (pkg_state_t*)window_get_user_data(win);
	if (!state) return;
	int content_h = window_content_height(win);
	int list_top = PKG_TOOLBAR_H + 2;
	int list_h = content_h - list_top - PKG_STATUS_H;
	if (list_h < 0) list_h = 0;
	int visible = list_h / PKG_ROW_H;
	if (visible < 1) visible = 1;
	int count = pkg_count();
	int max_scroll = count - visible;
	if (max_scroll < 0) max_scroll = 0;
	state->scroll += delta;
	if (state->scroll < 0) state->scroll = 0;
	if (state->scroll > max_scroll) state->scroll = max_scroll;
	uwm_request_redraw();
}

static void pkg_on_key(window_t* win, int key) {
	pkg_state_t* state = (pkg_state_t*)window_get_user_data(win);
	if (!state) return;
	int content_h = window_content_height(win);
	int list_top = PKG_TOOLBAR_H + 2;
	int list_h = content_h - list_top - PKG_STATUS_H;
	if (list_h < 0) list_h = 0;
	int visible = list_h / PKG_ROW_H;
	if (visible < 1) visible = 1;
	int count = pkg_count();

	if ((uint8_t)key == 0x80) {
		if (state->selected > 0) {
			state->selected--;
			if (state->selected < state->scroll) {
				state->scroll = state->selected;
			}
			uwm_request_redraw();
		}
	} else if ((uint8_t)key == 0x81) {
		if (state->selected < count - 1) {
			state->selected++;
			if (state->selected >= state->scroll + visible) {
				state->scroll++;
			}
			uwm_request_redraw();
		}
	} else if (key == '\n' || key == '\r') {
		pkg_install_selected(state);
		uwm_request_redraw();
	} else if (key == 'u' || key == 'U') {
		pkg_update_all(state);
		uwm_request_redraw();
	} else if (key == 'r' || key == 'R') {
		pkg_scan(state);
		pkg_set_status(state, "Refreshed");
		uwm_request_redraw();
	} else if (key == 8 || key == 127) {
		pkg_remove_selected(state);
		uwm_request_redraw();
	}
}

window_t* gui_package_manager_create_window(int x, int y) {
	if (pkg_window && uwm_window_is_open(pkg_window)) {
		return pkg_window;
	}
	window_t* win = window_create(x, y, PKG_WIDTH, PKG_HEIGHT, "Package Manager");
	if (!win) return NULL;

	memset(&pkg_state, 0, sizeof(pkg_state));
	pkg_state.win = win;
	pkg_state.selected = 0;
	pkg_state.scroll = 0;
	pkg_state.hover_btn = -1;
	pkg_state.status[0] = '\0';
	pkg_scan(&pkg_state);

	window_set_handlers(win, pkg_draw, pkg_on_mouse_down, NULL,
	                    pkg_on_mouse_move, pkg_on_scroll, pkg_on_key, &pkg_state);
	pkg_window = win;
	return win;
}
