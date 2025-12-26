#include <file_dialog.h>
#include <gui_apps.h>
#include <gui_window.h>
#include <graphics.h>
#include <mouse.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <uwm.h>

window_t* gui_calc_create_window(int x, int y);
window_t* gui_paint_create_window(int x, int y);
window_t* gui_filemgr_create_window(int x, int y);
window_t* gui_editor_create_window(int x, int y);

#define DESKTOP_TASKBAR_HEIGHT 24
#define DESKTOP_ICON_SIZE 28
#define DESKTOP_ICON_PADDING 8
#define DESKTOP_MAX_APPS 8
#define DESKTOP_APP_NAME_MAX 32

#define DESKTOP_COLOR_BACKGROUND COLOR_LIGHT_CYAN
#define DESKTOP_COLOR_TASKBAR COLOR_DARK_GRAY
#define DESKTOP_COLOR_ICON_BG COLOR_LIGHT_GRAY
#define DESKTOP_COLOR_ICON_TEXT COLOR_BLACK
#define DESKTOP_COLOR_MENU_BG COLOR_WHITE
#define DESKTOP_COLOR_MENU_TEXT COLOR_BLACK
#define DESKTOP_COLOR_MENU_HOVER COLOR_LIGHT_BLUE

typedef struct {
	char name[DESKTOP_APP_NAME_MAX];
	void (*launcher)(void);
	int icon_x;
	int icon_y;
	bool visible;
} desktop_app_t;

typedef struct {
	bool menu_open;
	int menu_x;
	int menu_y;
	int menu_width;
	int menu_height;
	int menu_hover_item;
	int icon_hover_item;
	bool start_hover;
	desktop_app_t apps[DESKTOP_MAX_APPS];
	int app_count;
} desktop_state_t;

static desktop_state_t desktop;

static void desktop_launch_calc(void);
static void desktop_launch_paint(void);
static void desktop_launch_files(void);
static void desktop_launch_editor(void);

static void desktop_layout_icons(void) {
	int screen_height = graphics_get_height();
	int available = screen_height - DESKTOP_TASKBAR_HEIGHT - 8;
	int min_spacing = DESKTOP_ICON_SIZE + DESKTOP_ICON_PADDING;
	if (desktop.app_count == 0) {
		return;
	}
	int icons_per_col = available / min_spacing;
	if (icons_per_col < 1) icons_per_col = 1;
	if (icons_per_col > desktop.app_count) icons_per_col = desktop.app_count;

	int spacing = available / icons_per_col;
	if (spacing < min_spacing) spacing = min_spacing;

	int col_spacing = min_spacing;
	for (int i = 0; i < desktop.app_count; i++) {
		int col = i / icons_per_col;
		int row = i % icons_per_col;
		desktop.apps[i].icon_x = 4 + col * col_spacing;
		desktop.apps[i].icon_y = 4 + row * spacing;
	}
}

static void desktop_register_app(const char* name, void (*launcher)(void)) {
	if (desktop.app_count >= DESKTOP_MAX_APPS) {
		return;
	}

	desktop_app_t* app = &desktop.apps[desktop.app_count];
	strncpy(app->name, name, DESKTOP_APP_NAME_MAX - 1);
	app->name[DESKTOP_APP_NAME_MAX - 1] = '\0';
	app->launcher = launcher;
	app->visible = true;
	desktop.app_count++;
	desktop_layout_icons();
}

static void desktop_init(void) {
	memset(&desktop, 0, sizeof(desktop));
	desktop.menu_open = false;
	desktop.menu_width = 120;
	desktop.menu_hover_item = -1;
	desktop.icon_hover_item = -1;
	desktop.start_hover = false;

	desktop_register_app("Calculator", desktop_launch_calc);
	desktop_register_app("Paint", desktop_launch_paint);
	desktop_register_app("Files", desktop_launch_files);
	desktop_register_app("Editor", desktop_launch_editor);

	desktop.menu_height = desktop.app_count * 18 + 4;
}

static void desktop_launch_calc(void) {
	gui_calc_create_window(40, 40);
}

static void desktop_launch_paint(void) {
	int x = 60;
	int y = 50;
	int max_y = graphics_get_height() - DESKTOP_TASKBAR_HEIGHT - 210;
	if (max_y < 0) max_y = 0;
	if (y > max_y) y = max_y;
	gui_paint_create_window(x, y);
}

static void desktop_launch_files(void) {
	gui_filemgr_create_window(50, 60);
}

static void desktop_launch_editor(void) {
	gui_editor_create_window(70, 50);
}

static void desktop_draw_taskbar(void) {
	int screen_width = graphics_get_width();
	int screen_height = graphics_get_height();
	int taskbar_y = screen_height - DESKTOP_TASKBAR_HEIGHT;
	uint8_t start_bg = (desktop.menu_open || desktop.start_hover) ? DESKTOP_COLOR_MENU_HOVER
	                                                             : DESKTOP_COLOR_ICON_BG;

	graphics_fill_rect(0, taskbar_y, screen_width, DESKTOP_TASKBAR_HEIGHT, DESKTOP_COLOR_TASKBAR);
	graphics_fill_rect(2, taskbar_y + 2, 50, DESKTOP_TASKBAR_HEIGHT - 4, start_bg);
	graphics_print(6, taskbar_y + 6, "Start", DESKTOP_COLOR_ICON_TEXT, start_bg);
}

static void desktop_draw_icons(void) {
	for (int i = 0; i < desktop.app_count; i++) {
		desktop_app_t* app = &desktop.apps[i];
		if (!app->visible) continue;

		uint8_t bg = (i == desktop.icon_hover_item) ? COLOR_WHITE : DESKTOP_COLOR_ICON_BG;
		uint8_t border = (i == desktop.icon_hover_item) ? COLOR_LIGHT_BLUE : COLOR_DARK_GRAY;
		graphics_fill_rect(app->icon_x, app->icon_y, DESKTOP_ICON_SIZE, DESKTOP_ICON_SIZE, bg);
		graphics_draw_rect(app->icon_x, app->icon_y, DESKTOP_ICON_SIZE, DESKTOP_ICON_SIZE,
		                   border);

		char icon_char[2] = {app->name[0], '\0'};
		int text_x = app->icon_x + DESKTOP_ICON_SIZE / 2 - 4;
		int text_y = app->icon_y + DESKTOP_ICON_SIZE / 2 - 4;
		graphics_print(text_x, text_y, icon_char, COLOR_BLUE, bg);

		int name_y = app->icon_y + DESKTOP_ICON_SIZE + 2;
		graphics_print(app->icon_x, name_y, app->name, DESKTOP_COLOR_ICON_TEXT,
		               DESKTOP_COLOR_BACKGROUND);
	}
}

static void desktop_draw_menu(void) {
	if (!desktop.menu_open) return;

	graphics_fill_rect(desktop.menu_x, desktop.menu_y, desktop.menu_width, desktop.menu_height,
	                   DESKTOP_COLOR_MENU_BG);
	graphics_draw_rect(desktop.menu_x, desktop.menu_y, desktop.menu_width, desktop.menu_height,
	                   COLOR_DARK_GRAY);

	for (int i = 0; i < desktop.app_count; i++) {
		int item_y = desktop.menu_y + 2 + i * 18;
		uint8_t bg = (i == desktop.menu_hover_item) ? DESKTOP_COLOR_MENU_HOVER
		                                           : DESKTOP_COLOR_MENU_BG;
		if (i == desktop.menu_hover_item) {
			graphics_fill_rect(desktop.menu_x + 1, item_y, desktop.menu_width - 2, 16, bg);
		}
		graphics_print(desktop.menu_x + 5, item_y + 4, desktop.apps[i].name,
		               DESKTOP_COLOR_MENU_TEXT, bg);
	}
}

static void desktop_draw_background(uwm_window_t* win) {
	(void)win;
	file_dialog_poll();
	graphics_fill_rect(0, 0, graphics_get_width(),
	                   graphics_get_height() - DESKTOP_TASKBAR_HEIGHT,
	                   DESKTOP_COLOR_BACKGROUND);
	desktop_draw_icons();
}

static void desktop_draw_overlay(uwm_window_t* win) {
	(void)win;
	desktop_draw_taskbar();
	desktop_draw_menu();
}

static bool desktop_point_in_taskbar(int x, int y) {
	(void)x;
	int taskbar_y = graphics_get_height() - DESKTOP_TASKBAR_HEIGHT;
	return y >= taskbar_y;
}

static bool desktop_point_in_start_button(int x, int y) {
	if (!desktop_point_in_taskbar(x, y)) return false;
	return x >= 2 && x < 52;
}

static bool desktop_point_in_menu(int x, int y) {
	return desktop.menu_open &&
	       x >= desktop.menu_x && x < desktop.menu_x + desktop.menu_width &&
	       y >= desktop.menu_y && y < desktop.menu_y + desktop.menu_height;
}

static int desktop_get_menu_item_at(int x, int y) {
	if (!desktop_point_in_menu(x, y)) return -1;
	int relative_y = y - desktop.menu_y - 2;
	int item_index = relative_y / 18;
	if (item_index >= 0 && item_index < desktop.app_count) {
		return item_index;
	}
	return -1;
}

static void desktop_launch_app(int index) {
	if (index < 0 || index >= desktop.app_count) return;
	if (desktop.apps[index].launcher) {
		desktop.apps[index].launcher();
	}
}

static void desktop_open_menu(void) {
	desktop.menu_open = true;
	desktop.menu_hover_item = -1;
	desktop.icon_hover_item = -1;
	desktop.menu_x = 2;
	int max_x = graphics_get_width() - desktop.menu_width - 2;
	if (max_x < 0) max_x = 0;
	if (desktop.menu_x > max_x) desktop.menu_x = max_x;
	desktop.menu_y = graphics_get_height() - DESKTOP_TASKBAR_HEIGHT - desktop.menu_height;
	if (desktop.menu_y < 0) desktop.menu_y = 0;
}

static void desktop_handle_click(int x, int y) {
	if (desktop_point_in_taskbar(x, y)) {
		if (desktop_point_in_start_button(x, y)) {
			if (desktop.menu_open) {
				desktop.menu_open = false;
			} else {
				desktop_open_menu();
			}
		} else if (desktop.menu_open) {
			desktop.menu_open = false;
		}
		return;
	}

	if (desktop.menu_open) {
		if (desktop_point_in_menu(x, y)) {
			int item = desktop_get_menu_item_at(x, y);
			if (item >= 0) {
				desktop_launch_app(item);
			}
		}
		desktop.menu_open = false;
		return;
	}

	for (int i = 0; i < desktop.app_count; i++) {
		desktop_app_t* app = &desktop.apps[i];
		if (!app->visible) continue;
		if (x >= app->icon_x && x < app->icon_x + DESKTOP_ICON_SIZE &&
		    y >= app->icon_y && y < app->icon_y + DESKTOP_ICON_SIZE) {
			desktop_launch_app(i);
			return;
		}
	}

	desktop.menu_open = false;
}

static void desktop_handle_mouse_move(int x, int y) {
	desktop.start_hover = desktop_point_in_start_button(x, y);

	if (desktop.menu_open) {
		desktop.menu_hover_item = desktop_get_menu_item_at(x, y);
	} else {
		desktop.menu_hover_item = -1;
	}

	desktop.icon_hover_item = -1;
	if (!desktop.menu_open && !desktop_point_in_taskbar(x, y)) {
		for (int i = 0; i < desktop.app_count; i++) {
			desktop_app_t* app = &desktop.apps[i];
			if (!app->visible) continue;
			if (x >= app->icon_x && x < app->icon_x + DESKTOP_ICON_SIZE &&
			    y >= app->icon_y && y < app->icon_y + DESKTOP_ICON_SIZE) {
				desktop.icon_hover_item = i;
				break;
			}
		}
	}
}

static bool desktop_capture(int x, int y) {
	if (desktop.menu_open) {
		return true;
	}
	return desktop_point_in_taskbar(x, y);
}

static void desktop_on_mouse_down(uwm_window_t* win, int x, int y, int buttons) {
	(void)win;
	if (buttons & MOUSE_LEFT_BUTTON) {
		desktop_handle_click(x, y);
	}
}

static void desktop_on_mouse_move(uwm_window_t* win, int x, int y, int buttons) {
	(void)win;
	(void)buttons;
	desktop_handle_mouse_move(x, y);
}

static void gui_simple_background(uwm_window_t* win) {
	(void)win;
	graphics_clear(COLOR_LIGHT_CYAN);
	file_dialog_poll();
}

static window_t* gui_single_window = NULL;

static void gui_single_overlay(uwm_window_t* win) {
	(void)win;
	if (!gui_single_window || !uwm_window_is_open(gui_single_window)) {
		uwm_quit();
	}
}

static void gui_abort_run(void) {
	graphics_disable_double_buffer();
	graphics_return_to_text();
}

int gui_run_desktop(void) {
	if (!uwm_init(MODE_320x240)) {
		return 1;
	}

	desktop_init();
	uwm_set_background(desktop_draw_background);
	uwm_set_overlay(desktop_draw_overlay);
	uwm_set_background_input(desktop_on_mouse_down, NULL, desktop_on_mouse_move,
	                         NULL, NULL, desktop_capture);
	uwm_run();
	return 0;
}

static void gui_center_window(int* x, int* y, int w, int h) {
	int screen_w = graphics_get_width();
	int screen_h = graphics_get_height();
	*x = (screen_w - w) / 2;
	*y = (screen_h - h) / 2;
	if (*x < 0) *x = 0;
	if (*y < 0) *y = 0;
}

int gui_run_calc(void) {
	if (!uwm_init(MODE_320x240)) {
		return 1;
	}

	gui_single_window = NULL;
	int x = 0;
	int y = 0;
	gui_center_window(&x, &y, 180, 190);
	gui_single_window = gui_calc_create_window(x, y);
	if (!gui_single_window) {
		gui_abort_run();
		return 1;
	}

	uwm_set_background(gui_simple_background);
	uwm_set_overlay(gui_single_overlay);
	uwm_run();
	return 0;
}

int gui_run_paint(void) {
	if (!uwm_init(MODE_320x240)) {
		return 1;
	}

	gui_single_window = NULL;
	int x = 0;
	int y = 0;
	gui_center_window(&x, &y, 260, 210);
	gui_single_window = gui_paint_create_window(x, y);
	if (!gui_single_window) {
		gui_abort_run();
		return 1;
	}

	uwm_set_background(gui_simple_background);
	uwm_set_overlay(gui_single_overlay);
	uwm_run();
	return 0;
}

int gui_run_filemgr(void) {
	if (!uwm_init(MODE_320x240)) {
		return 1;
	}

	gui_single_window = NULL;
	int x = 0;
	int y = 0;
	gui_center_window(&x, &y, 260, 200);
	gui_single_window = gui_filemgr_create_window(x, y);
	if (!gui_single_window) {
		gui_abort_run();
		return 1;
	}

	uwm_set_background(gui_simple_background);
	uwm_set_overlay(gui_single_overlay);
	uwm_run();
	return 0;
}
