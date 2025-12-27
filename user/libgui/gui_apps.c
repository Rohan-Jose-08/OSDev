#include <file_dialog.h>
#include <gui_apps.h>
#include <gui_window.h>
#include <graphics.h>
#include <mouse.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <uwm.h>

window_t* gui_calc_create_window(int x, int y);
window_t* gui_paint_create_window(int x, int y);
window_t* gui_filemgr_create_window(int x, int y);
window_t* gui_editor_create_window(int x, int y);
window_t* gui_terminal_create_window(int x, int y);
window_t* gui_image_viewer_create_window(int x, int y);
window_t* gui_music_player_create_window(int x, int y);
window_t* gui_sysmon_create_window(int x, int y);
window_t* gui_process_viewer_create_window(int x, int y);
window_t* gui_package_manager_create_window(int x, int y);

#define DESKTOP_TASKBAR_HEIGHT 24
#define DESKTOP_TASKBAR_START_WIDTH 50
#define DESKTOP_TASKBAR_BUTTON_MAX_WIDTH 80
#define DESKTOP_TASKBAR_BUTTON_MIN_WIDTH 24
#define DESKTOP_TASKBAR_TITLE_MAX 32
#define DESKTOP_ICON_SIZE 28
#define DESKTOP_ICON_PADDING 8
#define DESKTOP_MAX_APPS 12
#define DESKTOP_APP_NAME_MAX 32

#define DESKTOP_COLOR_BACKGROUND COLOR_LIGHT_CYAN
#define DESKTOP_COLOR_TASKBAR COLOR_DARK_GRAY
#define DESKTOP_COLOR_ICON_BG COLOR_LIGHT_GRAY
#define DESKTOP_COLOR_ICON_TEXT COLOR_BLACK
#define DESKTOP_COLOR_MENU_BG COLOR_WHITE
#define DESKTOP_COLOR_MENU_TEXT COLOR_BLACK
#define DESKTOP_COLOR_MENU_HOVER COLOR_LIGHT_BLUE
#define DESKTOP_COLOR_TASKBAR_BUTTON_BG COLOR_LIGHT_GRAY
#define DESKTOP_COLOR_TASKBAR_BUTTON_FOCUS COLOR_LIGHT_BLUE
#define DESKTOP_COLOR_TASKBAR_BUTTON_MIN COLOR_DARK_GRAY
#define DESKTOP_COLOR_TASKBAR_BUTTON_TEXT COLOR_BLACK
#define DESKTOP_COLOR_TASKBAR_BUTTON_TEXT_FOCUS COLOR_WHITE
#define DESKTOP_COLOR_TASKBAR_BUTTON_TEXT_MIN COLOR_LIGHT_GRAY
#define DESKTOP_CONTEXT_MENU_WIDTH 160
#define DESKTOP_CONTEXT_MENU_ITEM_HEIGHT 16
#define DESKTOP_CONTEXT_MENU_PADDING 4
#define DESKTOP_WALLPAPER_STYLES 3
#define DESKTOP_KEY_REPEAT_PROFILES 3
#define DESKTOP_DOCK_HEIGHT 36
#define DESKTOP_DOCK_ICON_SIZE 22
#define DESKTOP_DOCK_PADDING 6
#define DESKTOP_DOCK_RADIUS 4
#define DESKTOP_SETTINGS_WIDTH 200
#define DESKTOP_SETTINGS_HEIGHT 120
#define DESKTOP_WALLPAPER_CACHE_W 640
#define DESKTOP_WALLPAPER_CACHE_H 480

typedef struct {
	uwm_window_t* win;
	int x;
	int width;
} desktop_taskbar_button_t;

typedef struct {
	char name[DESKTOP_APP_NAME_MAX];
	void (*launcher)(void);
	int icon_x;
	int icon_y;
	int dock_x;
	int dock_y;
	bool visible;
	bool dock_pinned;
	const uint8_t* icon_bits;
	uint8_t icon_color;
	const char* window_prefix;
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
	bool context_open;
	int context_x;
	int context_y;
	int context_hover_item;
	int wallpaper_style;
	int key_repeat_profile;
	bool dock_visible;
	int dock_hover_item;
	int dock_x;
	int dock_y;
	int dock_w;
	int dock_h;
	int taskbar_button_count;
	uwm_window_t* taskbar_hover_window;
	desktop_app_t apps[DESKTOP_MAX_APPS];
	int app_count;
	desktop_taskbar_button_t taskbar_buttons[DESKTOP_MAX_APPS];
} desktop_state_t;

typedef struct {
	window_t* win;
	int hover_item;
} settings_state_t;

static desktop_state_t desktop;
static settings_state_t settings_state;
static window_t* settings_window = NULL;
static uint8_t wallpaper_cache[DESKTOP_WALLPAPER_CACHE_W * DESKTOP_WALLPAPER_CACHE_H];
static int wallpaper_cache_w = 0;
static int wallpaper_cache_h = 0;
static int wallpaper_cache_style = -1;

static void desktop_launch_calc(void);
static void desktop_launch_paint(void);
static void desktop_launch_files(void);
static void desktop_launch_editor(void);
static void desktop_launch_terminal(void);
static void desktop_launch_image(void);
static void desktop_launch_music(void);
static void desktop_launch_sysmon(void);
static void desktop_launch_process_viewer(void);
static void desktop_launch_package_manager(void);
static void desktop_launch_settings(void);
static void desktop_build_taskbar_buttons(void);
static uwm_window_t* desktop_get_taskbar_window_at(int x, int y);
static int gui_run_desktop_with_launcher(void (*launcher)(void));

static const char* desktop_context_items[] = {
	"Arrange Icons",
	"Toggle Pattern",
	"Key Repeat",
	"Settings"
};

#define DESKTOP_CONTEXT_ITEM_COUNT \
	(int)(sizeof(desktop_context_items) / sizeof(desktop_context_items[0]))

static const uint8_t desktop_key_repeat_delay[DESKTOP_KEY_REPEAT_PROFILES] = {3, 2, 1};
static const uint8_t desktop_key_repeat_rate[DESKTOP_KEY_REPEAT_PROFILES] = {28, 16, 8};
static const char* desktop_key_repeat_label[DESKTOP_KEY_REPEAT_PROFILES] = {
	"Slow",
	"Normal",
	"Fast"
};

static const uint8_t desktop_icon_calc[8] = {
	0x7E, 0x42, 0x5A, 0x5A, 0x5A, 0x42, 0x42, 0x7E
};
static const uint8_t desktop_icon_paint[8] = {
	0x10, 0x38, 0x7C, 0x38, 0x10, 0x34, 0x22, 0x41
};
static const uint8_t desktop_icon_files[8] = {
	0x7C, 0x44, 0x7F, 0x41, 0x41, 0x41, 0x7F, 0x00
};
static const uint8_t desktop_icon_editor[8] = {
	0x7E, 0x42, 0x5E, 0x5E, 0x5E, 0x42, 0x7E, 0x00
};
static const uint8_t desktop_icon_settings[8] = {
	0x3C, 0x42, 0x5A, 0x66, 0x66, 0x5A, 0x42, 0x3C
};
static const uint8_t desktop_icon_terminal[8] = {
	0x00, 0x60, 0x30, 0x18, 0x30, 0x60, 0x00, 0x3C
};
static const uint8_t desktop_icon_image[8] = {
	0x7E, 0x42, 0x5A, 0x6E, 0x52, 0x42, 0x7E, 0x00
};
static const uint8_t desktop_icon_music[8] = {
	0x18, 0x18, 0x18, 0x1C, 0x3C, 0x7C, 0x3C, 0x1C
};
static const uint8_t desktop_icon_sysmon[8] = {
	0x7E, 0x42, 0x5A, 0x5A, 0x42, 0x7E, 0x18, 0x18
};
static const uint8_t desktop_icon_process[8] = {
	0x7E, 0x42, 0x4A, 0x5A, 0x4A, 0x42, 0x7E, 0x00
};
static const uint8_t desktop_icon_pkg[8] = {
	0x7E, 0x42, 0x66, 0x5A, 0x5A, 0x66, 0x42, 0x7E
};

static void desktop_apply_key_repeat(void) {
	int idx = desktop.key_repeat_profile;
	if (idx < 0 || idx >= DESKTOP_KEY_REPEAT_PROFILES) {
		idx = 1;
	}
	keyboard_set_repeat(desktop_key_repeat_delay[idx], desktop_key_repeat_rate[idx]);
}

static void desktop_cycle_key_repeat(void) {
	desktop.key_repeat_profile = (desktop.key_repeat_profile + 1) % DESKTOP_KEY_REPEAT_PROFILES;
	desktop_apply_key_repeat();
}

static const char* desktop_context_label(int idx) {
	if (idx == 2) {
		static char label[32];
		const char* speed = desktop_key_repeat_label[desktop.key_repeat_profile];
		snprintf(label, sizeof(label), "Key Repeat: %s", speed);
		return label;
	}
	if (idx >= 0 && idx < DESKTOP_CONTEXT_ITEM_COUNT) {
		return desktop_context_items[idx];
	}
	return "";
}

static const char* desktop_wallpaper_label(int style) {
	if (style == 0) return "Solid";
	if (style == 1) return "Dots";
	if (style == 2) return "Diagonal";
	return "Custom";
}

static void desktop_draw_icon_bitmap_scaled(int x, int y, const uint8_t* bits,
                                            uint8_t color, int scale) {
	if (!bits || scale < 1) {
		return;
	}
	for (int row = 0; row < 8; row++) {
		uint8_t mask = bits[row];
		for (int col = 0; col < 8; col++) {
			if (mask & (1u << (7 - col))) {
				graphics_fill_rect(x + col * scale, y + row * scale, scale, scale, color);
			}
		}
	}
}

static void desktop_draw_app_icon(const desktop_app_t* app, int x, int y, int size, int scale) {
	if (!app) return;
	if (app->icon_bits) {
		int icon_size = 8 * scale;
		int offset_x = x + (size - icon_size) / 2;
		int offset_y = y + (size - icon_size) / 2;
		desktop_draw_icon_bitmap_scaled(offset_x, offset_y, app->icon_bits, app->icon_color,
		                                scale);
	} else {
		char icon_char[2] = {app->name[0], '\0'};
		int text_x = x + size / 2 - 4;
		int text_y = y + size / 2 - 4;
		graphics_print(text_x, text_y, icon_char, app->icon_color, DESKTOP_COLOR_ICON_BG);
	}
}

static bool desktop_app_window_state(const desktop_app_t* app, bool* focused_out) {
	if (focused_out) {
		*focused_out = false;
	}
	if (!app || !app->window_prefix || app->window_prefix[0] == '\0') {
		return false;
	}
	size_t prefix_len = strlen(app->window_prefix);
	if (prefix_len == 0) {
		return false;
	}
	int total = uwm_window_count();
	for (int i = 0; i < total; i++) {
		uwm_window_t* win = uwm_window_get_at(i);
		if (!win || !uwm_window_is_open(win)) {
			continue;
		}
		const char* title = uwm_window_get_title(win);
		if (!title) {
			continue;
		}
		bool match = true;
		for (size_t j = 0; j < prefix_len; j++) {
			if (title[j] == '\0' || title[j] != app->window_prefix[j]) {
				match = false;
				break;
			}
		}
		if (match) {
			if (focused_out && uwm_window_is_focused(win) && !uwm_window_is_minimized(win)) {
				*focused_out = true;
			}
			return true;
		}
	}
	return false;
}

static void desktop_update_dock_layout(void) {
	int screen_w = graphics_get_width();
	int screen_h = graphics_get_height();
	int dock_y = screen_h - DESKTOP_TASKBAR_HEIGHT - DESKTOP_DOCK_HEIGHT - 4;
	if (dock_y < 0) dock_y = 0;

	int count = 0;
	for (int i = 0; i < desktop.app_count; i++) {
		if (desktop.apps[i].visible && desktop.apps[i].dock_pinned) {
			count++;
		}
	}
	if (count == 0) {
		desktop.dock_w = 0;
		desktop.dock_h = 0;
		return;
	}

	int dock_w = count * DESKTOP_DOCK_ICON_SIZE + (count + 1) * DESKTOP_DOCK_PADDING;
	int dock_h = DESKTOP_DOCK_HEIGHT;
	int dock_x = (screen_w - dock_w) / 2;
	if (dock_x < 2) dock_x = 2;
	if (dock_x + dock_w > screen_w - 2) {
		dock_x = screen_w - dock_w - 2;
		if (dock_x < 2) dock_x = 2;
	}

	desktop.dock_x = dock_x;
	desktop.dock_y = dock_y;
	desktop.dock_w = dock_w;
	desktop.dock_h = dock_h;

	int idx = 0;
	for (int i = 0; i < desktop.app_count; i++) {
		desktop_app_t* app = &desktop.apps[i];
		if (!app->visible || !app->dock_pinned) {
			continue;
		}
		app->dock_x = dock_x + DESKTOP_DOCK_PADDING +
		              idx * (DESKTOP_DOCK_ICON_SIZE + DESKTOP_DOCK_PADDING);
		app->dock_y = dock_y + (dock_h - DESKTOP_DOCK_ICON_SIZE) / 2;
		idx++;
	}
}

static bool desktop_point_in_dock(int x, int y) {
	if (!desktop.dock_visible || desktop.dock_w <= 0 || desktop.dock_h <= 0) {
		return false;
	}
	return x >= desktop.dock_x && x < desktop.dock_x + desktop.dock_w &&
	       y >= desktop.dock_y && y < desktop.dock_y + desktop.dock_h;
}

static int desktop_get_dock_item_at(int x, int y) {
	if (!desktop_point_in_dock(x, y)) {
		return -1;
	}
	for (int i = 0; i < desktop.app_count; i++) {
		desktop_app_t* app = &desktop.apps[i];
		if (!app->visible || !app->dock_pinned) {
			continue;
		}
		if (x >= app->dock_x && x < app->dock_x + DESKTOP_DOCK_ICON_SIZE &&
		    y >= app->dock_y && y < app->dock_y + DESKTOP_DOCK_ICON_SIZE) {
			return i;
		}
	}
	return -1;
}

static void desktop_layout_icons(void) {
	int screen_w = graphics_get_width();
	int screen_h = graphics_get_height();
	int margin = 4;
	int available_w = screen_w - margin * 2;
	int available_h = screen_h - DESKTOP_TASKBAR_HEIGHT - margin * 2;
	if (desktop.dock_visible) {
		available_h -= (DESKTOP_DOCK_HEIGHT + 4);
	}
	int min_spacing = DESKTOP_ICON_SIZE + DESKTOP_ICON_PADDING;
	if (desktop.app_count == 0 || available_w <= 0 || available_h <= 0) {
		return;
	}

	int max_rows = available_h / min_spacing;
	if (max_rows < 1) max_rows = 1;
	if (max_rows > desktop.app_count) max_rows = desktop.app_count;
	int max_cols = available_w / min_spacing;
	if (max_cols < 1) max_cols = 1;

	int rows = max_rows;
	int cols = (desktop.app_count + rows - 1) / rows;
	if (cols > max_cols) {
		cols = max_cols;
		rows = (desktop.app_count + cols - 1) / cols;
		if (rows < 1) rows = 1;
	}

	int total_w = cols * DESKTOP_ICON_SIZE;
	int total_h = rows * DESKTOP_ICON_SIZE;
	int gap_x = (available_w - total_w) / (cols + 1);
	int gap_y = (available_h - total_h) / (rows + 1);
	if (gap_x < DESKTOP_ICON_PADDING) gap_x = DESKTOP_ICON_PADDING;
	if (gap_y < DESKTOP_ICON_PADDING) gap_y = DESKTOP_ICON_PADDING;

	int start_x = margin;
	int start_y = margin;
	for (int i = 0; i < desktop.app_count; i++) {
		int col = i / rows;
		int row = i % rows;
		desktop.apps[i].icon_x = start_x + col * (DESKTOP_ICON_SIZE + gap_x);
		desktop.apps[i].icon_y = start_y + row * (DESKTOP_ICON_SIZE + gap_y);
	}
}

static void desktop_draw_wallpaper(void) {
	int screen_w = graphics_get_width();
	int screen_h = graphics_get_height() - DESKTOP_TASKBAR_HEIGHT;
	if (screen_h < 0) {
		screen_h = 0;
	}

	if (desktop.wallpaper_style == 0) {
		graphics_fill_rect(0, 0, screen_w, screen_h, DESKTOP_COLOR_BACKGROUND);
		return;
	}

	if (screen_w > DESKTOP_WALLPAPER_CACHE_W) screen_w = DESKTOP_WALLPAPER_CACHE_W;
	if (screen_h > DESKTOP_WALLPAPER_CACHE_H) screen_h = DESKTOP_WALLPAPER_CACHE_H;

	if (wallpaper_cache_w != screen_w ||
	    wallpaper_cache_h != screen_h ||
	    wallpaper_cache_style != desktop.wallpaper_style) {
		wallpaper_cache_w = screen_w;
		wallpaper_cache_h = screen_h;
		wallpaper_cache_style = desktop.wallpaper_style;

		for (int y = 0; y < screen_h; y++) {
			uint8_t* row = &wallpaper_cache[y * DESKTOP_WALLPAPER_CACHE_W];
			for (int x = 0; x < screen_w; x++) {
				row[x] = DESKTOP_COLOR_BACKGROUND;
			}
		}

		if (desktop.wallpaper_style == 1) {
			for (int y = 0; y < screen_h; y += 8) {
				for (int x = 0; x < screen_w; x += 8) {
					wallpaper_cache[y * DESKTOP_WALLPAPER_CACHE_W + x] = COLOR_WHITE;
				}
			}
		} else if (desktop.wallpaper_style == 2) {
			for (int y = 0; y < screen_h; y++) {
				uint8_t* row = &wallpaper_cache[y * DESKTOP_WALLPAPER_CACHE_W];
				for (int x = 0; x < screen_w; x++) {
					if (((x + y) % 12) == 0) {
						row[x] = COLOR_LIGHT_GRAY;
					}
				}
			}
		}
	}

	graphics_blit(0, 0, screen_w, screen_h, wallpaper_cache, DESKTOP_WALLPAPER_CACHE_W);
}

static int desktop_context_menu_height(void) {
	return DESKTOP_CONTEXT_ITEM_COUNT * DESKTOP_CONTEXT_MENU_ITEM_HEIGHT +
	       DESKTOP_CONTEXT_MENU_PADDING * 2;
}

static void desktop_context_open_at(int x, int y) {
	int screen_w = graphics_get_width();
	int screen_h = graphics_get_height() - DESKTOP_TASKBAR_HEIGHT;
	int menu_h = desktop_context_menu_height();

	desktop.context_open = true;
	desktop.context_hover_item = -1;
	desktop.context_x = x;
	desktop.context_y = y;

	int max_x = screen_w - DESKTOP_CONTEXT_MENU_WIDTH - 2;
	int max_y = screen_h - menu_h - 2;
	if (max_x < 0) max_x = 0;
	if (max_y < 0) max_y = 0;
	if (desktop.context_x > max_x) desktop.context_x = max_x;
	if (desktop.context_y > max_y) desktop.context_y = max_y;
	if (desktop.context_x < 0) desktop.context_x = 0;
	if (desktop.context_y < 0) desktop.context_y = 0;
}

static bool desktop_point_in_context(int x, int y) {
	if (!desktop.context_open) {
		return false;
	}
	int menu_h = desktop_context_menu_height();
	return x >= desktop.context_x && x < desktop.context_x + DESKTOP_CONTEXT_MENU_WIDTH &&
	       y >= desktop.context_y && y < desktop.context_y + menu_h;
}

static int desktop_context_item_at(int x, int y) {
	if (!desktop_point_in_context(x, y)) {
		return -1;
	}
	int rel_y = y - desktop.context_y - DESKTOP_CONTEXT_MENU_PADDING;
	if (rel_y < 0) {
		return -1;
	}
	int idx = rel_y / DESKTOP_CONTEXT_MENU_ITEM_HEIGHT;
	return (idx >= 0 && idx < DESKTOP_CONTEXT_ITEM_COUNT) ? idx : -1;
}

static void desktop_context_select(int idx) {
	if (idx == 0) {
		desktop_layout_icons();
	} else if (idx == 1) {
		desktop.wallpaper_style = (desktop.wallpaper_style + 1) % DESKTOP_WALLPAPER_STYLES;
	} else if (idx == 2) {
		desktop_cycle_key_repeat();
	} else if (idx == 3) {
		desktop_launch_settings();
	}
}

static void desktop_build_taskbar_buttons(void) {
	desktop.taskbar_button_count = 0;
	int screen_width = graphics_get_width();
	int buttons_x = 2 + DESKTOP_TASKBAR_START_WIDTH + 4;
	int available = screen_width - buttons_x - 2;
	if (available <= 0) {
		return;
	}

	int total = uwm_window_count();
	if (total <= 0) {
		return;
	}

	int count = total;
	int start = 0;
	if (count > DESKTOP_MAX_APPS) {
		count = DESKTOP_MAX_APPS;
		start = total - count;
	}

	int width = available / count;
	if (width > DESKTOP_TASKBAR_BUTTON_MAX_WIDTH) {
		width = DESKTOP_TASKBAR_BUTTON_MAX_WIDTH;
	}
	if (width < DESKTOP_TASKBAR_BUTTON_MIN_WIDTH) {
		width = available / count;
	}
	if (width < 1) {
		width = 1;
	}

	bool hover_valid = false;
	for (int i = 0; i < count; i++) {
		uwm_window_t* win = uwm_window_get_at(start + i);
		if (!win) continue;
		desktop.taskbar_buttons[desktop.taskbar_button_count].win = win;
		desktop.taskbar_buttons[desktop.taskbar_button_count].x =
			buttons_x + desktop.taskbar_button_count * width;
		desktop.taskbar_buttons[desktop.taskbar_button_count].width = width;
		if (win == desktop.taskbar_hover_window) {
			hover_valid = true;
		}
		desktop.taskbar_button_count++;
	}
	if (!hover_valid) {
		desktop.taskbar_hover_window = NULL;
	}
}

static void desktop_draw_taskbar_title(int x, int y, int width, const char* title,
                                       uint8_t fg, uint8_t bg) {
	if (!title || width <= 6) {
		return;
	}
	int max_chars = (width - 6) / 8;
	if (max_chars <= 0) {
		return;
	}
	char label[DESKTOP_TASKBAR_TITLE_MAX];
	int i = 0;
	while (title[i] && i < max_chars && i < DESKTOP_TASKBAR_TITLE_MAX - 1) {
		label[i] = title[i];
		i++;
	}
	label[i] = '\0';
	graphics_print(x + 4, y + 6, label, fg, bg);
}

static void desktop_register_app(const char* name, void (*launcher)(void),
                                 const uint8_t* icon_bits, uint8_t icon_color,
                                 bool dock_pinned, const char* window_prefix) {
	if (desktop.app_count >= DESKTOP_MAX_APPS) {
		return;
	}

	desktop_app_t* app = &desktop.apps[desktop.app_count];
	strncpy(app->name, name, DESKTOP_APP_NAME_MAX - 1);
	app->name[DESKTOP_APP_NAME_MAX - 1] = '\0';
	app->launcher = launcher;
	app->visible = true;
	app->dock_pinned = dock_pinned;
	app->icon_bits = icon_bits;
	app->icon_color = icon_color;
	app->window_prefix = window_prefix;
	desktop.app_count++;
	desktop_layout_icons();
	desktop_update_dock_layout();
}

static void desktop_init(void) {
	memset(&desktop, 0, sizeof(desktop));
	desktop.menu_open = false;
	desktop.menu_width = 120;
	desktop.menu_hover_item = -1;
	desktop.icon_hover_item = -1;
	desktop.start_hover = false;
	desktop.context_open = false;
	desktop.context_hover_item = -1;
	desktop.wallpaper_style = 1;
	desktop.key_repeat_profile = 1;
	desktop.dock_visible = true;
	desktop.dock_hover_item = -1;
	desktop_apply_key_repeat();

	desktop_register_app("Calculator", desktop_launch_calc, desktop_icon_calc, COLOR_BLUE, true,
	                     "Calculator");
	desktop_register_app("Paint", desktop_launch_paint, desktop_icon_paint, COLOR_RED, true,
	                     "Paint");
	desktop_register_app("Files", desktop_launch_files, desktop_icon_files, COLOR_BROWN, true,
	                     "File Explorer");
	desktop_register_app("Editor", desktop_launch_editor, desktop_icon_editor, COLOR_DARK_GRAY, true,
	                     "Text Editor");
	desktop_register_app("Terminal", desktop_launch_terminal, desktop_icon_terminal, COLOR_LIGHT_GREEN, true,
	                     "Terminal");
	desktop_register_app("Images", desktop_launch_image, desktop_icon_image, COLOR_LIGHT_CYAN, true,
	                     "Image Viewer");
	desktop_register_app("Music", desktop_launch_music, desktop_icon_music, COLOR_MAGENTA, true,
	                     "Music Player");
	desktop_register_app("Monitor", desktop_launch_sysmon, desktop_icon_sysmon, COLOR_YELLOW, true,
	                     "System Monitor");
	desktop_register_app("Processes", desktop_launch_process_viewer, desktop_icon_process,
	                     COLOR_LIGHT_MAGENTA, true, "Process Viewer");
	desktop_register_app("Packages", desktop_launch_package_manager, desktop_icon_pkg,
	                     COLOR_LIGHT_BLUE, true, "Package Manager");
	desktop_register_app("Settings", desktop_launch_settings, desktop_icon_settings, COLOR_GREEN, true,
	                     "Settings");

	desktop.menu_height = desktop.app_count * 18 + 4;
	desktop_update_dock_layout();
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

static void desktop_launch_terminal(void) {
	gui_terminal_create_window(60, 70);
}

static void desktop_launch_image(void) {
	gui_image_viewer_create_window(80, 60);
}

static void desktop_launch_music(void) {
	gui_music_player_create_window(90, 70);
}

static void desktop_launch_sysmon(void) {
	gui_sysmon_create_window(100, 80);
}

static void desktop_launch_process_viewer(void) {
	gui_process_viewer_create_window(110, 90);
}

static void desktop_launch_package_manager(void) {
	gui_package_manager_create_window(120, 100);
}

static int settings_item_at(int x, int y, int content_w) {
	int start_y = 24;
	int item_h = 18;
	int item_gap = 6;
	int item_w = content_w - 20;
	int item_x = 10;

	for (int i = 0; i < 3; i++) {
		int item_y = start_y + i * (item_h + item_gap);
		if (x >= item_x && x < item_x + item_w &&
		    y >= item_y && y < item_y + item_h) {
			return i;
		}
	}
	return -1;
}

static void settings_draw(window_t* win) {
	settings_state_t* state = (settings_state_t*)window_get_user_data(win);
	int content_w = window_content_width(win);
	int content_h = window_content_height(win);
	(void)content_h;

	window_clear_content(win, COLOR_LIGHT_GRAY);
	window_print(win, 8, 6, "Settings", COLOR_BLACK);

	for (int i = 0; i < 3; i++) {
		int item_y = 24 + i * 24;
		uint8_t bg = (state && state->hover_item == i) ? COLOR_LIGHT_BLUE : COLOR_WHITE;
		window_fill_rect(win, 10, item_y, content_w - 20, 18, bg);
		window_draw_rect(win, 10, item_y, content_w - 20, 18, COLOR_DARK_GRAY);
		if (i == 0) {
			char label[40];
			snprintf(label, sizeof(label), "Wallpaper: %s",
			         desktop_wallpaper_label(desktop.wallpaper_style));
			window_print(win, 16, item_y + 4, label, COLOR_BLACK);
		} else if (i == 1) {
			char label[40];
			snprintf(label, sizeof(label), "Key Repeat: %s",
			         desktop_key_repeat_label[desktop.key_repeat_profile]);
			window_print(win, 16, item_y + 4, label, COLOR_BLACK);
		} else if (i == 2) {
			const char* dock_state = desktop.dock_visible ? "On" : "Off";
			char label[32];
			snprintf(label, sizeof(label), "Dock: %s", dock_state);
			window_print(win, 16, item_y + 4, label, COLOR_BLACK);
		}
	}

	window_print(win, 10, content_h - 16, "Click to cycle", COLOR_DARK_GRAY);
}

static void settings_on_mouse_down(window_t* win, int x, int y, int buttons) {
	(void)win;
	if (!(buttons & MOUSE_LEFT_BUTTON)) {
		return;
	}
	int content_w = window_content_width(win);
	int item = settings_item_at(x, y, content_w);
	if (item == 0) {
		desktop.wallpaper_style = (desktop.wallpaper_style + 1) % DESKTOP_WALLPAPER_STYLES;
	} else if (item == 1) {
		desktop_cycle_key_repeat();
	} else if (item == 2) {
		desktop.dock_visible = !desktop.dock_visible;
		desktop.dock_hover_item = -1;
		desktop_layout_icons();
		desktop_update_dock_layout();
	}
	settings_draw(win);
}

static void settings_on_mouse_move(window_t* win, int x, int y, int buttons) {
	(void)buttons;
	settings_state_t* state = (settings_state_t*)window_get_user_data(win);
	if (!state) return;
	int content_w = window_content_width(win);
	int item = settings_item_at(x, y, content_w);
	if (item != state->hover_item) {
		state->hover_item = item;
		settings_draw(win);
	}
}

static window_t* gui_settings_create_window(int x, int y) {
	if (settings_window && uwm_window_is_open(settings_window)) {
		return settings_window;
	}
	window_t* win = window_create(x, y, DESKTOP_SETTINGS_WIDTH, DESKTOP_SETTINGS_HEIGHT,
	                              "Settings");
	if (!win) return NULL;
	memset(&settings_state, 0, sizeof(settings_state));
	settings_state.win = win;
	settings_state.hover_item = -1;
	window_set_handlers(win, settings_draw, settings_on_mouse_down, NULL,
	                    settings_on_mouse_move, NULL, NULL, &settings_state);
	settings_window = win;
	return win;
}

static void desktop_launch_settings(void) {
	int screen_w = graphics_get_width();
	int screen_h = graphics_get_height();
	int x = (screen_w - DESKTOP_SETTINGS_WIDTH) / 2;
	int y = (screen_h - DESKTOP_SETTINGS_HEIGHT) / 2;
	if (x < 0) x = 0;
	if (y < 0) y = 0;
	gui_settings_create_window(x, y);
}

static void desktop_draw_taskbar(void) {
	int screen_width = graphics_get_width();
	int screen_height = graphics_get_height();
	int taskbar_y = screen_height - DESKTOP_TASKBAR_HEIGHT;
	uint8_t start_bg = (desktop.menu_open || desktop.start_hover) ? DESKTOP_COLOR_MENU_HOVER
	                                                             : DESKTOP_COLOR_ICON_BG;

	graphics_fill_rect(0, taskbar_y, screen_width, DESKTOP_TASKBAR_HEIGHT, DESKTOP_COLOR_TASKBAR);
	graphics_fill_rect(2, taskbar_y + 2, DESKTOP_TASKBAR_START_WIDTH,
	                   DESKTOP_TASKBAR_HEIGHT - 4, start_bg);
	graphics_print(6, taskbar_y + 6, "Start", DESKTOP_COLOR_ICON_TEXT, start_bg);

	desktop_build_taskbar_buttons();
	int button_y = taskbar_y + 2;
	int button_h = DESKTOP_TASKBAR_HEIGHT - 4;
	for (int i = 0; i < desktop.taskbar_button_count; i++) {
		desktop_taskbar_button_t* button = &desktop.taskbar_buttons[i];
		uwm_window_t* win = button->win;
		if (!win || button->width <= 0) {
			continue;
		}
		bool focused = uwm_window_is_focused(win) && !uwm_window_is_minimized(win);
		bool minimized = uwm_window_is_minimized(win);
		bool hover = (win == desktop.taskbar_hover_window);
		uint8_t bg = DESKTOP_COLOR_TASKBAR_BUTTON_BG;
		uint8_t fg = DESKTOP_COLOR_TASKBAR_BUTTON_TEXT;
		if (minimized) {
			bg = DESKTOP_COLOR_TASKBAR_BUTTON_MIN;
			fg = DESKTOP_COLOR_TASKBAR_BUTTON_TEXT_MIN;
		} else if (focused) {
			bg = DESKTOP_COLOR_TASKBAR_BUTTON_FOCUS;
			fg = DESKTOP_COLOR_TASKBAR_BUTTON_TEXT_FOCUS;
		} else if (hover) {
			bg = DESKTOP_COLOR_MENU_HOVER;
		}
		uint8_t border = hover ? COLOR_WHITE : COLOR_DARK_GRAY;
		graphics_fill_rect(button->x, button_y, button->width, button_h, bg);
		graphics_draw_rect(button->x, button_y, button->width, button_h, border);
		desktop_draw_taskbar_title(button->x, button_y, button->width,
		                           uwm_window_get_title(win), fg, bg);
	}
}

static void desktop_draw_dock(void) {
	if (!desktop.dock_visible) {
		return;
	}
	desktop_update_dock_layout();
	if (desktop.dock_w <= 0 || desktop.dock_h <= 0) {
		return;
	}

	uint8_t dock_bg = COLOR_LIGHT_GRAY;
	uint8_t dock_border = COLOR_DARK_GRAY;
	graphics_fill_rect(desktop.dock_x, desktop.dock_y, desktop.dock_w, desktop.dock_h, dock_bg);
	graphics_draw_rect(desktop.dock_x, desktop.dock_y, desktop.dock_w, desktop.dock_h,
	                   dock_border);

	for (int i = 0; i < desktop.app_count; i++) {
		desktop_app_t* app = &desktop.apps[i];
		if (!app->visible || !app->dock_pinned) {
			continue;
		}
		bool hover = (i == desktop.dock_hover_item);
		bool focused = false;
		bool running = desktop_app_window_state(app, &focused);
		uint8_t bg = hover ? COLOR_WHITE : dock_bg;
		uint8_t border = hover ? COLOR_LIGHT_BLUE : dock_border;
		graphics_fill_rect(app->dock_x, app->dock_y, DESKTOP_DOCK_ICON_SIZE,
		                   DESKTOP_DOCK_ICON_SIZE, bg);
		graphics_draw_rect(app->dock_x, app->dock_y, DESKTOP_DOCK_ICON_SIZE,
		                   DESKTOP_DOCK_ICON_SIZE, border);
		desktop_draw_app_icon(app, app->dock_x, app->dock_y, DESKTOP_DOCK_ICON_SIZE, 2);
		if (running) {
			uint8_t dot_color = focused ? COLOR_LIGHT_BLUE : COLOR_DARK_GRAY;
			int dot_x = app->dock_x + DESKTOP_DOCK_ICON_SIZE / 2 - 2;
			int dot_y = app->dock_y + DESKTOP_DOCK_ICON_SIZE - 4;
			graphics_fill_rect(dot_x, dot_y, 4, 2, dot_color);
		}
	}
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

		desktop_draw_app_icon(app, app->icon_x, app->icon_y, DESKTOP_ICON_SIZE, 3);

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
		int icon_x = desktop.menu_x + 5;
		int icon_y = item_y + 4;
		desktop_draw_app_icon(&desktop.apps[i], icon_x, icon_y, 10, 1);
		graphics_print(desktop.menu_x + 20, item_y + 4, desktop.apps[i].name,
		               DESKTOP_COLOR_MENU_TEXT, bg);
	}
}

static void desktop_draw_context_menu(void) {
	if (!desktop.context_open) return;

	int menu_h = desktop_context_menu_height();
	graphics_fill_rect(desktop.context_x, desktop.context_y, DESKTOP_CONTEXT_MENU_WIDTH, menu_h,
	                   DESKTOP_COLOR_MENU_BG);
	graphics_draw_rect(desktop.context_x, desktop.context_y, DESKTOP_CONTEXT_MENU_WIDTH, menu_h,
	                   COLOR_DARK_GRAY);

	for (int i = 0; i < DESKTOP_CONTEXT_ITEM_COUNT; i++) {
		int item_y = desktop.context_y + DESKTOP_CONTEXT_MENU_PADDING +
		             i * DESKTOP_CONTEXT_MENU_ITEM_HEIGHT;
		uint8_t bg = (i == desktop.context_hover_item) ? DESKTOP_COLOR_MENU_HOVER
		                                              : DESKTOP_COLOR_MENU_BG;
		if (i == desktop.context_hover_item) {
			graphics_fill_rect(desktop.context_x + 1, item_y,
			                   DESKTOP_CONTEXT_MENU_WIDTH - 2, DESKTOP_CONTEXT_MENU_ITEM_HEIGHT,
			                   bg);
		}
		graphics_print(desktop.context_x + 6, item_y + 3, desktop_context_label(i),
		               DESKTOP_COLOR_MENU_TEXT, bg);
	}
}

static void desktop_draw_background(uwm_window_t* win) {
	(void)win;
	file_dialog_poll();
	desktop_draw_wallpaper();
	desktop_draw_icons();
}

static void desktop_draw_overlay(uwm_window_t* win) {
	(void)win;
	desktop_draw_dock();
	desktop_draw_taskbar();
	desktop_draw_menu();
	desktop_draw_context_menu();
}

static bool desktop_point_in_taskbar(int x, int y) {
	(void)x;
	int taskbar_y = graphics_get_height() - DESKTOP_TASKBAR_HEIGHT;
	return y >= taskbar_y;
}

static bool desktop_point_in_start_button(int x, int y) {
	if (!desktop_point_in_taskbar(x, y)) return false;
	return x >= 2 && x < 2 + DESKTOP_TASKBAR_START_WIDTH;
}

static bool desktop_point_in_menu(int x, int y) {
	return desktop.menu_open &&
	       x >= desktop.menu_x && x < desktop.menu_x + desktop.menu_width &&
	       y >= desktop.menu_y && y < desktop.menu_y + desktop.menu_height;
}

static uwm_window_t* desktop_get_taskbar_window_at(int x, int y) {
	if (!desktop_point_in_taskbar(x, y) || desktop_point_in_start_button(x, y)) {
		return NULL;
	}
	int taskbar_y = graphics_get_height() - DESKTOP_TASKBAR_HEIGHT;
	int button_y = taskbar_y + 2;
	int button_h = DESKTOP_TASKBAR_HEIGHT - 4;
	if (y < button_y || y >= button_y + button_h) {
		return NULL;
	}

	desktop_build_taskbar_buttons();
	for (int i = 0; i < desktop.taskbar_button_count; i++) {
		desktop_taskbar_button_t* button = &desktop.taskbar_buttons[i];
		if (x >= button->x && x < button->x + button->width) {
			return button->win;
		}
	}
	return NULL;
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
	if (desktop.context_open) {
		if (desktop_point_in_context(x, y)) {
			int item = desktop_context_item_at(x, y);
			if (item >= 0) {
				desktop_context_select(item);
			}
		}
		desktop.context_open = false;
		desktop.context_hover_item = -1;
		return;
	}

	if (desktop.dock_visible) {
		desktop_update_dock_layout();
	}

	if (desktop_point_in_taskbar(x, y)) {
		if (desktop_point_in_start_button(x, y)) {
			if (desktop.menu_open) {
				desktop.menu_open = false;
			} else {
				desktop_open_menu();
			}
		} else {
			uwm_window_t* win = desktop_get_taskbar_window_at(x, y);
			if (win) {
				if (uwm_window_is_minimized(win)) {
					uwm_window_set_minimized(win, false);
				} else if (uwm_window_is_focused(win)) {
					uwm_window_set_minimized(win, true);
				} else {
					uwm_window_focus(win);
				}
			} else if (desktop.menu_open) {
				desktop.menu_open = false;
			}
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

	if (desktop.dock_visible && desktop_point_in_dock(x, y)) {
		int item = desktop_get_dock_item_at(x, y);
		if (item >= 0) {
			desktop_launch_app(item);
		}
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
	if (desktop.dock_visible) {
		desktop_update_dock_layout();
	}
	desktop.start_hover = desktop_point_in_start_button(x, y);
	desktop.taskbar_hover_window = NULL;
	if (desktop_point_in_taskbar(x, y) && !desktop.start_hover) {
		desktop.taskbar_hover_window = desktop_get_taskbar_window_at(x, y);
	}

	if (desktop.context_open) {
		desktop.context_hover_item = desktop_context_item_at(x, y);
	} else {
		desktop.context_hover_item = -1;
	}

	if (desktop.menu_open) {
		desktop.menu_hover_item = desktop_get_menu_item_at(x, y);
	} else {
		desktop.menu_hover_item = -1;
	}

	desktop.dock_hover_item = -1;
	if (desktop.dock_visible && desktop_point_in_dock(x, y)) {
		desktop.dock_hover_item = desktop_get_dock_item_at(x, y);
	}

	desktop.icon_hover_item = -1;
	if (!desktop.menu_open && !desktop.context_open && !desktop_point_in_taskbar(x, y) &&
	    (!desktop.dock_visible || !desktop_point_in_dock(x, y))) {
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
	if (desktop.menu_open || desktop.context_open) {
		return true;
	}
	if (desktop.dock_visible && desktop_point_in_dock(x, y)) {
		return true;
	}
	return desktop_point_in_taskbar(x, y);
}

static void desktop_on_mouse_down(uwm_window_t* win, int x, int y, int buttons) {
	(void)win;
	if (buttons & MOUSE_RIGHT_BUTTON) {
		desktop.menu_open = false;
		desktop.context_open = false;
		desktop.context_hover_item = -1;
		if (!desktop_point_in_taskbar(x, y)) {
			desktop_context_open_at(x, y);
		}
		return;
	}
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

static int gui_run_desktop_with_launcher(void (*launcher)(void)) {
	if (!uwm_init(MODE_320x240)) {
		return 1;
	}

	desktop_init();
	if (launcher) {
		launcher();
	}
	uwm_set_background(desktop_draw_background);
	uwm_set_overlay(desktop_draw_overlay);
	uwm_set_background_input(desktop_on_mouse_down, NULL, desktop_on_mouse_move,
	                         NULL, NULL, desktop_capture);
	uwm_run();
	return 0;
}

int gui_run_desktop(void) {
	return gui_run_desktop_with_launcher(NULL);
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
	return gui_run_desktop_with_launcher(desktop_launch_paint);
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
