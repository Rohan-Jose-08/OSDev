#include <dirent.h>
#include <gui_window.h>
#include <graphics.h>
#include <mouse.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define FILEMGR_MAX_ENTRIES 64

typedef enum {
    FILEMGR_INPUT_NONE = 0,
    FILEMGR_INPUT_RENAME,
    FILEMGR_INPUT_NEW_FILE,
    FILEMGR_INPUT_NEW_FOLDER,
    FILEMGR_INPUT_SEARCH
} filemgr_input_action_t;

typedef struct {
    char path[128];
    char name[NAME_MAX];
    uint32_t d_type;
} filemgr_search_entry_t;

typedef struct {
    char current_path[128];
    struct dirent entries[FILEMGR_MAX_ENTRIES];
    int entry_count;
    int scroll_offset;
    int selected;
    int last_click_item;
    unsigned int last_click_time;
    bool input_mode;
    filemgr_input_action_t input_action;
    char input_buffer[64];
    int input_cursor;
    bool input_selecting;
    int input_sel_anchor;
    int input_sel_end;
    bool menu_open;
    int menu_x;
    int menu_y;
    int menu_hover;
    bool search_active;
    char search_query[64];
    filemgr_search_entry_t search_results[FILEMGR_MAX_ENTRIES];
    int search_count;
    int search_scroll;
    int search_selected;
} filemgr_state_t;

static window_t* filemgr_window = NULL;
static filemgr_state_t filemgr_state;

static const char* filemgr_input_prompt(const filemgr_state_t* state);
static void filemgr_input_layout(window_t* win, const filemgr_state_t* state,
                                 int* input_x, int* input_y, int* input_w, int* input_h,
                                 int* text_x, int* text_y);
static void filemgr_search_dialog_layout(window_t* win,
                                         int* dialog_x, int* dialog_y,
                                         int* dialog_w, int* dialog_h,
                                         int* input_x, int* input_y,
                                         int* input_w, int* input_h,
                                         int* text_x, int* text_y);
static void filemgr_input_clear_selection(filemgr_state_t* state);
static bool filemgr_input_has_selection(const filemgr_state_t* state);
static void filemgr_input_normalize_selection(const filemgr_state_t* state, int* start, int* end);
static void filemgr_input_set_cursor_from_x(filemgr_state_t* state, int x, int text_x);
static void filemgr_input_copy_selection(filemgr_state_t* state);
static void filemgr_input_delete_selection(filemgr_state_t* state);

#define FILEMGR_TOP_BAR_HEIGHT 28
#define FILEMGR_MENU_WIDTH 96
#define FILEMGR_MENU_ITEM_HEIGHT 12
#define FILEMGR_MENU_PADDING 4
#define FILEMGR_MENU_BUTTON_X 5
#define FILEMGR_MENU_BUTTON_Y 5
#define FILEMGR_MENU_BUTTON_PADDING 6
#define FILEMGR_MENU_BUTTON_HEIGHT 16
#define FILEMGR_STATUS_HEIGHT 16
#define FILEMGR_LIST_BOTTOM_PADDING 2

static const char* filemgr_menu_items[] = {
    "New File",
    "New Folder",
    "Rename",
    "Search"
};

#define FILEMGR_MENU_ITEM_COUNT (int)(sizeof(filemgr_menu_items) / sizeof(filemgr_menu_items[0]))

// Helper to normalize path (remove double slashes, etc)
static void normalize_path(char* path) {
    // Remove trailing slash unless it's root
    int len = strlen(path);
    if (len > 1 && path[len-1] == '/') {
        path[len-1] = '\0';
    }
    
    // Fix double slashes
    char temp[128];
    int j = 0;
    bool last_was_slash = false;
    for (int i = 0; path[i]; i++) {
        if (path[i] == '/') {
            if (!last_was_slash) {
                temp[j++] = '/';
                last_was_slash = true;
            }
        } else {
            temp[j++] = path[i];
            last_was_slash = false;
        }
    }
    temp[j] = '\0';
    strcpy(path, temp);
    
    // Ensure starts with /
    if (path[0] != '/') {
        temp[0] = '/';
        strcpy(temp + 1, path);
        strcpy(path, temp);
    }
}

static void filemgr_begin_input(filemgr_state_t* state, filemgr_input_action_t action, const char* initial_text) {
	if (!state) return;
	state->input_mode = true;
	state->input_action = action;
    if (initial_text) {
        strncpy(state->input_buffer, initial_text, sizeof(state->input_buffer) - 1);
        state->input_buffer[sizeof(state->input_buffer) - 1] = '\0';
	} else {
        state->input_buffer[0] = '\0';
	}
	state->input_cursor = strlen(state->input_buffer);
	filemgr_input_clear_selection(state);
}

static void filemgr_input_insert(filemgr_state_t* state, const char* text) {
	if (!state || !text) return;

	char filtered[sizeof(state->input_buffer)];
	int j = 0;
	for (int i = 0; text[i] && j < (int)sizeof(filtered) - 1; i++) {
		if (text[i] == '\n' || text[i] == '\r') {
			continue;
		}
		filtered[j++] = text[i];
	}
	filtered[j] = '\0';
	if (filtered[0] == '\0') {
		return;
	}

	int len = strlen(state->input_buffer);
	int cur = state->input_cursor;
	if (cur < 0) cur = 0;
	if (cur > len) cur = len;

	int space = (int)sizeof(state->input_buffer) - 1 - len;
	int insert_len = strlen(filtered);
	if (space <= 0) {
		return;
	}
	if (insert_len > space) {
		insert_len = space;
	}

	for (int i = len; i >= cur; i--) {
		state->input_buffer[i + insert_len] = state->input_buffer[i];
	}
	memcpy(state->input_buffer + cur, filtered, (size_t)insert_len);
	state->input_cursor = cur + insert_len;
}

static const char* filemgr_input_prompt(const filemgr_state_t* state) {
	if (!state) return "";
	if (state->input_action == FILEMGR_INPUT_NEW_FILE) {
		return "New file:";
	}
	if (state->input_action == FILEMGR_INPUT_NEW_FOLDER) {
		return "New folder:";
	}
	if (state->input_action == FILEMGR_INPUT_SEARCH) {
		return "Search:";
	}
	return "Rename to:";
}

static void filemgr_input_layout(window_t* win, const filemgr_state_t* state,
                                 int* input_x, int* input_y, int* input_w, int* input_h,
                                 int* text_x, int* text_y) {
	if (state && state->input_action == FILEMGR_INPUT_SEARCH) {
		filemgr_search_dialog_layout(win, NULL, NULL, NULL, NULL,
		                             input_x, input_y, input_w, input_h,
		                             text_x, text_y);
		return;
	}
	int content_w = window_content_width(win);
	int content_h = window_content_height(win);
	const char* prompt = filemgr_input_prompt(state);
	int status_y = content_h - FILEMGR_STATUS_HEIGHT;
	int prompt_x = 5;
	int status_text_y = status_y + 4;
	int x = prompt_x + (int)strlen(prompt) * 8 + 6;
	int w = content_w - x - 5;
	if (w < 20) w = 20;
	int y = status_y + 1;
	int h = 12;

	if (input_x) *input_x = x;
	if (input_y) *input_y = y;
	if (input_w) *input_w = w;
	if (input_h) *input_h = h;
	if (text_x) *text_x = x + 4;
	if (text_y) *text_y = status_text_y;
}

static void filemgr_search_dialog_layout(window_t* win,
                                         int* dialog_x, int* dialog_y,
                                         int* dialog_w, int* dialog_h,
                                         int* input_x, int* input_y,
                                         int* input_w, int* input_h,
                                         int* text_x, int* text_y) {
	int content_w = window_content_width(win);
	int content_h = window_content_height(win);
	int box_w = 200;
	int box_h = 40;
	int x = (content_w - box_w) / 2;
	int y = (content_h - box_h) / 2;
	if (x < 4) x = 4;
	if (y < FILEMGR_TOP_BAR_HEIGHT + 4) y = FILEMGR_TOP_BAR_HEIGHT + 4;
	if (x + box_w > content_w - 4) box_w = content_w - x - 4;
	if (y + box_h > content_h - 4) box_h = content_h - y - 4;
	int input_pad = 8;
	int prompt_y = y + 6;
	int input_y_local = y + 20;
	int input_h_local = 12;
	int input_x_local = x + input_pad;
	int input_w_local = box_w - input_pad * 2;
	if (input_w_local < 40) input_w_local = 40;

	if (dialog_x) *dialog_x = x;
	if (dialog_y) *dialog_y = y;
	if (dialog_w) *dialog_w = box_w;
	if (dialog_h) *dialog_h = box_h;
	if (input_x) *input_x = input_x_local;
	if (input_y) *input_y = input_y_local;
	if (input_w) *input_w = input_w_local;
	if (input_h) *input_h = input_h_local;
	if (text_x) *text_x = input_x_local + 4;
	if (text_y) *text_y = prompt_y;
}

static void filemgr_input_clear_selection(filemgr_state_t* state) {
	if (!state) return;
	state->input_selecting = false;
	state->input_sel_anchor = state->input_cursor;
	state->input_sel_end = state->input_cursor;
}

static bool filemgr_input_has_selection(const filemgr_state_t* state) {
	return state && state->input_sel_anchor != state->input_sel_end;
}

static void filemgr_input_normalize_selection(const filemgr_state_t* state, int* start, int* end) {
	int a = state->input_sel_anchor;
	int b = state->input_sel_end;
	if (b < a) {
		int tmp = a;
		a = b;
		b = tmp;
	}
	*start = a;
	*end = b;
}

static void filemgr_input_set_cursor_from_x(filemgr_state_t* state, int x, int text_x) {
	int len = strlen(state->input_buffer);
	int col = (x - text_x) / 8;
	if (col < 0) col = 0;
	if (col > len) col = len;
	state->input_cursor = col;
}

static void filemgr_input_copy_selection(filemgr_state_t* state) {
	if (!filemgr_input_has_selection(state)) {
		uwm_clipboard_set(state->input_buffer);
		return;
	}
	int start = 0;
	int end = 0;
	filemgr_input_normalize_selection(state, &start, &end);
	int len = strlen(state->input_buffer);
	if (start < 0) start = 0;
	if (end > len) end = len;
	int count = end - start;
	if (count <= 0) {
		return;
	}
	char clip[sizeof(state->input_buffer)];
	if (count >= (int)sizeof(clip)) {
		count = (int)sizeof(clip) - 1;
	}
	memcpy(clip, state->input_buffer + start, (size_t)count);
	clip[count] = '\0';
	uwm_clipboard_set(clip);
}

static void filemgr_input_delete_selection(filemgr_state_t* state) {
	if (!filemgr_input_has_selection(state)) {
		return;
	}
	int start = 0;
	int end = 0;
	filemgr_input_normalize_selection(state, &start, &end);
	int len = strlen(state->input_buffer);
	if (start < 0) start = 0;
	if (end > len) end = len;
	if (end <= start) {
		filemgr_input_clear_selection(state);
		return;
	}
	for (int i = end; i <= len; i++) {
		state->input_buffer[start + (i - end)] = state->input_buffer[i];
	}
	state->input_cursor = start;
	filemgr_input_clear_selection(state);
}

static void filemgr_clear_search(filemgr_state_t* state) {
	if (!state) return;
	state->search_active = false;
	state->search_query[0] = '\0';
	state->search_count = 0;
	state->search_scroll = 0;
	state->search_selected = -1;
}

static char filemgr_lower(char c) {
	if (c >= 'A' && c <= 'Z') {
		return (char)(c + 32);
	}
	return c;
}

static bool filemgr_match_query(const char* name, const char* query) {
	if (!name || !query || query[0] == '\0') {
		return false;
	}
	int nlen = strlen(name);
	int qlen = strlen(query);
	if (qlen > nlen) {
		return false;
	}
	for (int i = 0; i <= nlen - qlen; i++) {
		bool match = true;
		for (int j = 0; j < qlen; j++) {
			char a = filemgr_lower(name[i + j]);
			char b = filemgr_lower(query[j]);
			if (a != b) {
				match = false;
				break;
			}
		}
		if (match) return true;
	}
	return false;
}

static void filemgr_add_search_result(filemgr_state_t* state,
                                      const char* path,
                                      const char* name,
                                      uint32_t d_type) {
	if (!state || !path || !name) return;
	if (state->search_count >= FILEMGR_MAX_ENTRIES) return;

	filemgr_search_entry_t* entry = &state->search_results[state->search_count++];
	strncpy(entry->path, path, sizeof(entry->path) - 1);
	entry->path[sizeof(entry->path) - 1] = '\0';
	strncpy(entry->name, name, sizeof(entry->name) - 1);
	entry->name[sizeof(entry->name) - 1] = '\0';
	entry->d_type = d_type;
}

static bool filemgr_search_has_result(const filemgr_state_t* state, const char* path) {
	if (!state || !path) return false;
	for (int i = 0; i < state->search_count; i++) {
		if (strcmp(state->search_results[i].path, path) == 0) {
			return true;
		}
	}
	return false;
}

static void filemgr_search(filemgr_state_t* state, const char* root, const char* query) {
	if (!state || !root || !query) return;

	filemgr_clear_search(state);
	strncpy(state->search_query, query, sizeof(state->search_query) - 1);
	state->search_query[sizeof(state->search_query) - 1] = '\0';
	if (state->search_query[0] == '\0') {
		return;
	}

	typedef struct {
		char path[128];
		uint8_t depth;
	} filemgr_search_node_t;

	filemgr_search_node_t stack[64];
	int stack_count = 0;

	char root_path[128];
	strncpy(root_path, root, sizeof(root_path) - 1);
	root_path[sizeof(root_path) - 1] = '\0';
	normalize_path(root_path);

	strncpy(stack[stack_count].path, root_path, sizeof(stack[stack_count].path) - 1);
	stack[stack_count].path[sizeof(stack[stack_count].path) - 1] = '\0';
	stack[stack_count].depth = 0;
	stack_count++;

	while (stack_count > 0 && state->search_count < FILEMGR_MAX_ENTRIES) {
		filemgr_search_node_t node = stack[--stack_count];
		struct dirent entries[FILEMGR_MAX_ENTRIES];
		int count = listdir(node.path, entries, FILEMGR_MAX_ENTRIES);
		if (count < 0) {
			continue;
		}
		for (int i = 0; i < count && state->search_count < FILEMGR_MAX_ENTRIES; i++) {
			const char* name = entries[i].d_name;
			if (!name || name[0] == '\0') continue;
			if ((name[0] == '.' && name[1] == '\0') ||
			    (name[0] == '.' && name[1] == '.' && name[2] == '\0')) {
				continue;
			}

			char full_path[128];
			int needed = 0;
			if (strcmp(node.path, "/") == 0) {
				needed = snprintf(full_path, sizeof(full_path), "/%s", name);
			} else {
				needed = snprintf(full_path, sizeof(full_path), "%s/%s", node.path, name);
			}
			if (needed <= 0 || needed >= (int)sizeof(full_path)) {
				continue;
			}
			normalize_path(full_path);

			if (filemgr_match_query(name, state->search_query) &&
			    !filemgr_search_has_result(state, full_path)) {
				filemgr_add_search_result(state, full_path, name, entries[i].d_type);
			}

			if (entries[i].d_type == 2 && node.depth < 8 && stack_count < 64) {
				strncpy(stack[stack_count].path, full_path,
				        sizeof(stack[stack_count].path) - 1);
				stack[stack_count].path[sizeof(stack[stack_count].path) - 1] = '\0';
				stack[stack_count].depth = (uint8_t)(node.depth + 1);
				stack_count++;
			}
		}
	}

	state->search_active = true;
	state->search_selected = (state->search_count > 0) ? 0 : -1;
	state->search_scroll = 0;
}

static void filemgr_select_by_name(window_t* win, filemgr_state_t* state, const char* name) {
    if (!win || !state || !name) return;
    for (int i = 0; i < state->entry_count; i++) {
        if (strcmp(state->entries[i].d_name, name) == 0) {
            int content_h = window_content_height(win);
            int list_top = FILEMGR_TOP_BAR_HEIGHT + 2;
            int list_height = content_h - list_top - FILEMGR_STATUS_HEIGHT - FILEMGR_LIST_BOTTOM_PADDING;
            if (list_height < 0) list_height = 0;
            int visible_items = list_height / 11;
            if (visible_items < 1) visible_items = 1;
            state->selected = i;
            if (i < state->scroll_offset) {
                state->scroll_offset = i;
            } else if (visible_items > 0 && i >= state->scroll_offset + visible_items) {
                state->scroll_offset = i - visible_items + 1;
            }
            return;
        }
    }
}

static int filemgr_menu_button_width(void) {
    return (int)strlen("File") * 8 + FILEMGR_MENU_BUTTON_PADDING * 2;
}

static void filemgr_menu_open_at(window_t* win, filemgr_state_t* state, int x, int y) {
    if (!win || !state) return;
    int content_w = window_content_width(win);
    state->menu_open = true;
    state->menu_hover = -1;
    state->menu_x = x;
    state->menu_y = y;
    if (state->menu_x + FILEMGR_MENU_WIDTH > content_w) {
        state->menu_x = content_w - FILEMGR_MENU_WIDTH;
    }
    if (state->menu_x < 0) state->menu_x = 0;
    if (state->menu_y < FILEMGR_TOP_BAR_HEIGHT) state->menu_y = FILEMGR_TOP_BAR_HEIGHT;
}

static int filemgr_menu_item_at(filemgr_state_t* state, int x, int y) {
    if (!state) return -1;
    int menu_h = FILEMGR_MENU_ITEM_COUNT * FILEMGR_MENU_ITEM_HEIGHT + FILEMGR_MENU_PADDING * 2;
    if (x < state->menu_x || x >= state->menu_x + FILEMGR_MENU_WIDTH ||
        y < state->menu_y || y >= state->menu_y + menu_h) {
        return -1;
    }
    int rel_y = y - state->menu_y - FILEMGR_MENU_PADDING;
    if (rel_y < 0) return -1;
    int idx = rel_y / FILEMGR_MENU_ITEM_HEIGHT;
    return (idx >= 0 && idx < FILEMGR_MENU_ITEM_COUNT) ? idx : -1;
}

static void filemgr_menu_select(window_t* win, filemgr_state_t* state, int idx) {
    if (!win || !state) return;
    if (idx == 0) {
        filemgr_begin_input(state, FILEMGR_INPUT_NEW_FILE, "newfile.txt");
    } else if (idx == 1) {
        filemgr_begin_input(state, FILEMGR_INPUT_NEW_FOLDER, "newfolder");
    } else if (idx == 2) {
        if (state->selected >= 0 && state->selected < state->entry_count &&
            strcmp(state->entries[state->selected].d_name, "..") != 0) {
            filemgr_begin_input(state, FILEMGR_INPUT_RENAME,
                                state->entries[state->selected].d_name);
        }
    } else if (idx == 3) {
        filemgr_begin_input(state, FILEMGR_INPUT_SEARCH, state->search_query);
    }
}

static void filemgr_load_dir(filemgr_state_t* state) {
    filemgr_clear_search(state);
    normalize_path(state->current_path);
    
    // Get directory listing
    int max_entries = FILEMGR_MAX_ENTRIES;
    if (strcmp(state->current_path, "/") != 0) {
        max_entries = FILEMGR_MAX_ENTRIES - 1;
    }
    state->entry_count = listdir(state->current_path, state->entries, max_entries);
    if (state->entry_count < 0) {
        state->entry_count = 0;
    }
    
    // Add ".." parent directory entry if not at root
    if (strcmp(state->current_path, "/") != 0) {
        // Shift entries down to make room for ".."
        for (int i = state->entry_count; i > 0; i--) {
            state->entries[i] = state->entries[i-1];
        }
        // Add ".." entry
        state->entries[0].d_type = 2;
        strcpy(state->entries[0].d_name, "..");
        state->entry_count++;
    }
    
    state->scroll_offset = 0;
    state->selected = -1;
}

static void filemgr_redraw(window_t* win) {
    filemgr_state_t* state = (filemgr_state_t*)window_get_user_data(win);
    int content_w = window_content_width(win);
    int content_h = window_content_height(win);
    window_clear_content(win, COLOR_WHITE);
    
    // Header with better formatting
    window_fill_rect(win, 0, 0, content_w, FILEMGR_TOP_BAR_HEIGHT, COLOR_DARK_GRAY);
    window_draw_rect(win, 0, 0, content_w, FILEMGR_TOP_BAR_HEIGHT, COLOR_BLACK);

    // Menu button
    int menu_button_w = filemgr_menu_button_width();
    uint8_t menu_bg = state->menu_open ? COLOR_LIGHT_CYAN : COLOR_LIGHT_GRAY;
    window_fill_rect(win, FILEMGR_MENU_BUTTON_X, FILEMGR_MENU_BUTTON_Y,
                     menu_button_w, FILEMGR_MENU_BUTTON_HEIGHT, menu_bg);
    window_draw_rect(win, FILEMGR_MENU_BUTTON_X, FILEMGR_MENU_BUTTON_Y,
                     menu_button_w, FILEMGR_MENU_BUTTON_HEIGHT, COLOR_BLACK);
    window_print(win, FILEMGR_MENU_BUTTON_X + FILEMGR_MENU_BUTTON_PADDING,
                 FILEMGR_MENU_BUTTON_Y + 4, "File", COLOR_BLACK);

    int title_x = FILEMGR_MENU_BUTTON_X + menu_button_w + 8;
    window_print(win, title_x, 5, "File Explorer", COLOR_WHITE);
    
    // Current path display (truncate if too long)
    char path_str[50];
    if (strlen(state->current_path) > 28) {
        snprintf(path_str, sizeof(path_str), "...%s", 
                state->current_path + strlen(state->current_path) - 25);
    } else {
        snprintf(path_str, sizeof(path_str), "%s", state->current_path);
    }
    window_print(win, title_x, 16, path_str, COLOR_LIGHT_GRAY);
    
    // File list area background
    int list_top = FILEMGR_TOP_BAR_HEIGHT + 2;
    int list_height = content_h - list_top - FILEMGR_STATUS_HEIGHT - FILEMGR_LIST_BOTTOM_PADDING;
    if (list_height < 0) list_height = 0;
    window_fill_rect(win, 2, list_top, content_w - 4, list_height, COLOR_WHITE);
    window_draw_rect(win, 2, list_top, content_w - 4, list_height, COLOR_DARK_GRAY);
    
    // List entries or search results
    int y = list_top + 5;
    int visible_items = list_height / 11;
    if (visible_items < 1) visible_items = 1;

    if (state->search_active) {
        int max_display = state->search_scroll + visible_items;
        if (max_display > state->search_count) max_display = state->search_count;

        if (state->search_count == 0) {
            window_print(win, 8, y, "No matches found", COLOR_DARK_GRAY);
        }

        for (int i = state->search_scroll; i < max_display; i++) {
            filemgr_search_entry_t* entry = &state->search_results[i];
            uint8_t color = (entry->d_type == 2) ? COLOR_BLUE : COLOR_BLACK;
            uint8_t icon_color = (entry->d_type == 2) ? COLOR_BLUE : COLOR_GREEN;
            const char* icon = (entry->d_type == 2) ? "+" : "*";

            if (i == state->search_selected) {
                window_fill_rect(win, 4, y - 2, content_w - 8, 11, COLOR_LIGHT_CYAN);
            }

            window_print(win, 8, y, icon, icon_color);

            char display_name[50];
            if (strlen(entry->path) > 46) {
                snprintf(display_name, sizeof(display_name), "...%s",
                         entry->path + strlen(entry->path) - 43);
            } else {
                snprintf(display_name, sizeof(display_name), "%s", entry->path);
            }
            window_print(win, 18, y, display_name, color);
            y += 11;
        }
    } else {
        int max_display = state->scroll_offset + visible_items;
        if (max_display > state->entry_count) max_display = state->entry_count;

        for (int i = state->scroll_offset; i < max_display; i++) {
            // Determine type and color
            uint8_t color = COLOR_BLACK;
            uint8_t icon_color = COLOR_BLACK;
            char icon[3] = "";

            if (strcmp(state->entries[i].d_name, "..") == 0) {
                strcpy(icon, "^");
                color = COLOR_MAGENTA;
                icon_color = COLOR_MAGENTA;
            } else {
                if (state->entries[i].d_type == 2) {  // Directory
                    strcpy(icon, "+");
                    color = COLOR_BLUE;
                    icon_color = COLOR_BLUE;
                } else {  // File
                    strcpy(icon, "*");
                    color = COLOR_BLACK;
                    icon_color = COLOR_GREEN;
                }
            }

            // Selection highlighting
            if (i == state->selected) {
                window_fill_rect(win, 4, y - 2, content_w - 8, 11, COLOR_LIGHT_CYAN);
            }

            // Draw icon and name
            window_print(win, 8, y, icon, icon_color);

            // Truncate long filenames
            char display_name[35];
            if (strlen(state->entries[i].d_name) > 32) {
                strncpy(display_name, state->entries[i].d_name, 29);
                display_name[29] = '.';
                display_name[30] = '.';
                display_name[31] = '.';
                display_name[32] = '\0';
            } else {
                strcpy(display_name, state->entries[i].d_name);
            }

            window_print(win, 18, y, display_name, color);
            y += 11;
        }
    }
    
    // Menu dropdown
    if (state->menu_open) {
        int menu_h = FILEMGR_MENU_ITEM_COUNT * FILEMGR_MENU_ITEM_HEIGHT + FILEMGR_MENU_PADDING * 2;
        window_fill_rect(win, state->menu_x, state->menu_y, FILEMGR_MENU_WIDTH, menu_h, COLOR_WHITE);
        window_draw_rect(win, state->menu_x, state->menu_y, FILEMGR_MENU_WIDTH, menu_h, COLOR_DARK_GRAY);
        for (int i = 0; i < FILEMGR_MENU_ITEM_COUNT; i++) {
            int item_y = state->menu_y + FILEMGR_MENU_PADDING + i * FILEMGR_MENU_ITEM_HEIGHT;
            if (i == state->menu_hover) {
                window_fill_rect(win, state->menu_x + 1, item_y,
                                 FILEMGR_MENU_WIDTH - 2, FILEMGR_MENU_ITEM_HEIGHT,
                                 COLOR_LIGHT_BLUE);
            }
            window_print(win, state->menu_x + 6, item_y + 2, filemgr_menu_items[i], COLOR_BLACK);
        }
    }

	// Status bar at bottom
	int status_y = content_h - FILEMGR_STATUS_HEIGHT;
	window_fill_rect(win, 0, status_y, content_w, FILEMGR_STATUS_HEIGHT, COLOR_LIGHT_GRAY);
	if (state->input_mode && state->input_action != FILEMGR_INPUT_SEARCH) {
		const char* prompt = filemgr_input_prompt(state);
		int input_x = 0;
		int input_y = 0;
		int input_w = 0;
		int input_h = 0;
		int text_x = 0;
		int text_y = 0;
		filemgr_input_layout(win, state, &input_x, &input_y, &input_w, &input_h, &text_x, &text_y);

		window_print(win, 5, text_y, prompt, COLOR_DARK_GRAY);
		window_fill_rect(win, input_x, input_y, input_w, input_h, COLOR_WHITE);
		window_draw_rect(win, input_x, input_y, input_w, input_h, COLOR_BLACK);

		if (filemgr_input_has_selection(state)) {
			int start = 0;
			int end = 0;
			filemgr_input_normalize_selection(state, &start, &end);
			int len = strlen(state->input_buffer);
			if (start < 0) start = 0;
			if (end > len) end = len;
			if (end > start) {
				int rect_x = text_x + start * 8;
				int rect_w = (end - start) * 8;
				window_fill_rect(win, rect_x, input_y + 1, rect_w, input_h - 2,
				                 COLOR_LIGHT_BLUE);
			}
		}

		window_print(win, text_x, text_y, state->input_buffer, COLOR_BLACK);

		if (filemgr_input_has_selection(state)) {
			int start = 0;
			int end = 0;
			filemgr_input_normalize_selection(state, &start, &end);
			int len = strlen(state->input_buffer);
			if (start < 0) start = 0;
			if (end > len) end = len;
			int count = end - start;
			if (count > 0) {
				char segment[sizeof(state->input_buffer)];
				if (count >= (int)sizeof(segment)) {
					count = (int)sizeof(segment) - 1;
				}
				memcpy(segment, state->input_buffer + start, (size_t)count);
				segment[count] = '\0';
				window_print(win, text_x + start * 8, text_y, segment, COLOR_WHITE);
			}
		}

		int cursor_x = text_x + state->input_cursor * 8;
		if (cursor_x < input_x + input_w - 1) {
			window_fill_rect(win, cursor_x, input_y + 1, 1, input_h - 2, COLOR_BLACK);
		}
	} else if (state->input_mode && state->input_action == FILEMGR_INPUT_SEARCH) {
		char status[96];
		snprintf(status, sizeof(status), "Type a name and press Enter");
		window_print(win, 5, status_y + 4, status, COLOR_DARK_GRAY);
    } else if (state->search_active) {
        char status[96];
        snprintf(status, sizeof(status),
                 "Search: %s | %d results | Enter:open C:clear S:search",
                 state->search_query, state->search_count);
        window_print(win, 5, status_y + 4, status, COLOR_DARK_GRAY);
    } else {
        char status[96];
        snprintf(status, sizeof(status),
                 "%d items | Up/Down:scroll Bksp:up N:new F:folder R:rename S:search",
                 state->entry_count);
        window_print(win, 5, status_y + 4, status, COLOR_DARK_GRAY);
    }

	if (state->input_mode && state->input_action == FILEMGR_INPUT_SEARCH) {
		int dialog_x = 0;
		int dialog_y = 0;
		int dialog_w = 0;
		int dialog_h = 0;
		int input_x = 0;
		int input_y = 0;
		int input_w = 0;
		int input_h = 0;
		int text_x = 0;
		filemgr_search_dialog_layout(win, &dialog_x, &dialog_y, &dialog_w, &dialog_h,
		                             &input_x, &input_y, &input_w, &input_h,
		                             &text_x, NULL);

		window_fill_rect(win, dialog_x, dialog_y, dialog_w, dialog_h, COLOR_LIGHT_GRAY);
		window_draw_rect(win, dialog_x, dialog_y, dialog_w, dialog_h, COLOR_DARK_GRAY);
		window_print(win, dialog_x + 8, dialog_y + 6, "Search", COLOR_BLACK);
		window_fill_rect(win, input_x, input_y, input_w, input_h, COLOR_WHITE);
		window_draw_rect(win, input_x, input_y, input_w, input_h, COLOR_BLACK);

		if (filemgr_input_has_selection(state)) {
			int start = 0;
			int end = 0;
			filemgr_input_normalize_selection(state, &start, &end);
			int len = strlen(state->input_buffer);
			if (start < 0) start = 0;
			if (end > len) end = len;
			if (end > start) {
				int rect_x = text_x + start * 8;
				int rect_w = (end - start) * 8;
				window_fill_rect(win, rect_x, input_y + 1, rect_w, input_h - 2,
				                 COLOR_LIGHT_BLUE);
			}
		}

		window_print(win, text_x, input_y + 2, state->input_buffer, COLOR_BLACK);

		if (filemgr_input_has_selection(state)) {
			int start = 0;
			int end = 0;
			filemgr_input_normalize_selection(state, &start, &end);
			int len = strlen(state->input_buffer);
			if (start < 0) start = 0;
			if (end > len) end = len;
			int count = end - start;
			if (count > 0) {
				char segment[sizeof(state->input_buffer)];
				if (count >= (int)sizeof(segment)) {
					count = (int)sizeof(segment) - 1;
				}
				memcpy(segment, state->input_buffer + start, (size_t)count);
				segment[count] = '\0';
				window_print(win, text_x + start * 8, input_y + 2, segment, COLOR_WHITE);
			}
		}

		int cursor_x = text_x + state->input_cursor * 8;
		if (cursor_x < input_x + input_w - 1) {
			window_fill_rect(win, cursor_x, input_y + 1, 1, input_h - 2, COLOR_BLACK);
		}
	}
}

static void filemgr_open_search_result(window_t* win, filemgr_state_t* state, int index) {
    if (!win || !state) return;
    if (index < 0 || index >= state->search_count) return;

    filemgr_search_entry_t* entry = &state->search_results[index];
    if (entry->d_type == 2) {
        strncpy(state->current_path, entry->path, sizeof(state->current_path) - 1);
        state->current_path[sizeof(state->current_path) - 1] = '\0';
        filemgr_load_dir(state);
        filemgr_redraw(win);
        return;
    }

    char parent[128];
    strncpy(parent, entry->path, sizeof(parent) - 1);
    parent[sizeof(parent) - 1] = '\0';
    char* last_slash = strrchr(parent, '/');
    if (last_slash && last_slash != parent) {
        *last_slash = '\0';
    } else {
        strcpy(parent, "/");
    }
    strncpy(state->current_path, parent, sizeof(state->current_path) - 1);
    state->current_path[sizeof(state->current_path) - 1] = '\0';
    filemgr_load_dir(state);
    filemgr_select_by_name(win, state, entry->name);
    filemgr_redraw(win);
}

static void filemgr_click(window_t* win, int x, int y) {
    filemgr_state_t* state = (filemgr_state_t*)window_get_user_data(win);
    (void)x;

    if (state->input_mode) {
        state->input_mode = false;
        state->input_action = FILEMGR_INPUT_NONE;
        state->input_buffer[0] = '\0';
        state->input_cursor = 0;
        filemgr_input_clear_selection(state);
        filemgr_redraw(win);
        return;
    }
    
    int content_h = window_content_height(win);
    int list_top = FILEMGR_TOP_BAR_HEIGHT + 2;
    int list_height = content_h - list_top - FILEMGR_STATUS_HEIGHT - FILEMGR_LIST_BOTTOM_PADDING;
    if (list_height < 0) list_height = 0;
    int list_bottom = list_top + list_height;

    // Check if clicking in file list area
    if (y >= list_top + 5 && y < list_bottom) {
        int item_idx = (y - (list_top + 5)) / 11;

        if (state->search_active) {
            item_idx += state->search_scroll;
            if (item_idx >= 0 && item_idx < state->search_count) {
                bool is_double_click = (state->search_selected == item_idx);
                if (is_double_click) {
                    filemgr_open_search_result(win, state, item_idx);
                } else {
                    state->search_selected = item_idx;
                }
                filemgr_redraw(win);
            }
            return;
        }

        item_idx += state->scroll_offset;
        if (item_idx >= 0 && item_idx < state->entry_count) {
            // Check for double-click (same item clicked twice quickly)
            bool is_double_click = (state->selected == item_idx);

            if (is_double_click) {
                // Navigate into directory
                if (strcmp(state->entries[item_idx].d_name, "..") == 0) {
                    // Go up one directory
                    char* last_slash = strrchr(state->current_path, '/');
                    if (last_slash && last_slash != state->current_path) {
                        *last_slash = '\0';
                    } else {
                        strcpy(state->current_path, "/");
                    }
                    filemgr_load_dir(state);
                } else {
                    if (state->entries[item_idx].d_type == 2) {
                        char full_path[128];
                        if (strcmp(state->current_path, "/") == 0) {
                            snprintf(full_path, sizeof(full_path), "/%s",
                                    state->entries[item_idx].d_name);
                        } else {
                            snprintf(full_path, sizeof(full_path), "%s/%s",
                                    state->current_path, state->entries[item_idx].d_name);
                        }
                        if (strlen(full_path) < 120) {
                            strcpy(state->current_path, full_path);
                            filemgr_load_dir(state);
                        }
                    }
                }
            } else {
                // Single click - just select
                state->selected = item_idx;
            }

            filemgr_redraw(win);
        }
    }
}

static void filemgr_key(window_t* win, int c) {
    filemgr_state_t* state = (filemgr_state_t*)window_get_user_data(win);
    bool needs_redraw = false;
    int content_h = window_content_height(win);
    int list_top = FILEMGR_TOP_BAR_HEIGHT + 2;
    int list_height = content_h - list_top - FILEMGR_STATUS_HEIGHT - FILEMGR_LIST_BOTTOM_PADDING;
    if (list_height < 0) list_height = 0;
    int visible_items = list_height / 11;
    if (visible_items < 1) visible_items = 1;

	if (state->input_mode) {
		if (c == 0x03) {
			filemgr_input_copy_selection(state);
			filemgr_redraw(win);
			return;
		} else if (c == 0x18) {
			filemgr_input_copy_selection(state);
			if (filemgr_input_has_selection(state)) {
				filemgr_input_delete_selection(state);
			} else {
				state->input_buffer[0] = '\0';
				state->input_cursor = 0;
				filemgr_input_clear_selection(state);
			}
			filemgr_redraw(win);
			return;
		} else if (c == 0x16) {
			char clip[64];
			if (uwm_clipboard_get(clip, sizeof(clip)) > 0) {
				if (filemgr_input_has_selection(state)) {
					filemgr_input_delete_selection(state);
				}
				filemgr_input_insert(state, clip);
				filemgr_redraw(win);
			}
			return;
		} else if ((uint8_t)c == 0x82) {
			if (filemgr_input_has_selection(state)) {
				filemgr_input_clear_selection(state);
				filemgr_redraw(win);
				return;
			}
			if (state->input_cursor > 0) {
				state->input_cursor--;
				filemgr_input_clear_selection(state);
				filemgr_redraw(win);
			}
			return;
		} else if ((uint8_t)c == 0x83) {
			if (filemgr_input_has_selection(state)) {
				filemgr_input_clear_selection(state);
				filemgr_redraw(win);
				return;
			}
			int len = strlen(state->input_buffer);
			if (state->input_cursor < len) {
				state->input_cursor++;
				filemgr_input_clear_selection(state);
				filemgr_redraw(win);
			}
			return;
		}
		if (c == '\n' || c == '\r') {
			bool success = false;
			bool exit_input = true;
            if (state->input_action == FILEMGR_INPUT_SEARCH &&
                state->input_buffer[0] == '\0') {
                filemgr_clear_search(state);
                success = true;
            } else if (state->input_buffer[0] != '\0') {
                char target_name[sizeof(state->input_buffer)];
                strncpy(target_name, state->input_buffer, sizeof(target_name) - 1);
                target_name[sizeof(target_name) - 1] = '\0';

                if (state->input_action == FILEMGR_INPUT_RENAME) {
                    if (state->selected >= 0 && state->selected < state->entry_count &&
                        strcmp(state->entries[state->selected].d_name, "..") != 0) {
                        char full_path[128];
                        if (strcmp(state->current_path, "/") == 0) {
                            snprintf(full_path, sizeof(full_path), "/%s",
                                    state->entries[state->selected].d_name);
                        } else {
                            snprintf(full_path, sizeof(full_path), "%s/%s",
                                    state->current_path, state->entries[state->selected].d_name);
                        }
                        if (rename(full_path, target_name) == 0) {
                            success = true;
                        }
                    }
                } else if (state->input_action == FILEMGR_INPUT_NEW_FILE) {
                    char full_path[128];
                    if (strcmp(state->current_path, "/") == 0) {
                        snprintf(full_path, sizeof(full_path), "/%s", target_name);
                    } else {
                        snprintf(full_path, sizeof(full_path), "%s/%s",
                                state->current_path, target_name);
                    }
                    if (touch(full_path) == 0) {
                        success = true;
                    }
                } else if (state->input_action == FILEMGR_INPUT_NEW_FOLDER) {
                    char full_path[128];
                    if (strcmp(state->current_path, "/") == 0) {
                        snprintf(full_path, sizeof(full_path), "/%s", target_name);
                    } else {
                        snprintf(full_path, sizeof(full_path), "%s/%s",
                                state->current_path, target_name);
                    }
                    if (mkdir(full_path) == 0) {
                        success = true;
                    }
                } else if (state->input_action == FILEMGR_INPUT_SEARCH) {
                    filemgr_search(state, state->current_path, target_name);
                    success = true;
                }

                if (success) {
                    if (state->input_action != FILEMGR_INPUT_SEARCH) {
                        filemgr_load_dir(state);
                        filemgr_select_by_name(win, state, target_name);
                    }
                } else {
                    exit_input = false;
                }
            }

            if (exit_input) {
                state->input_mode = false;
                state->input_action = FILEMGR_INPUT_NONE;
                state->input_buffer[0] = '\0';
                state->input_cursor = 0;
                filemgr_input_clear_selection(state);
            }
            filemgr_redraw(win);
            return;
        } else if (c == 27) {
            state->input_mode = false;
            state->input_action = FILEMGR_INPUT_NONE;
            state->input_buffer[0] = '\0';
            state->input_cursor = 0;
            filemgr_input_clear_selection(state);
            filemgr_redraw(win);
            return;
        } else if (c == 8 || c == 127) {
            if (filemgr_input_has_selection(state)) {
                filemgr_input_delete_selection(state);
                filemgr_redraw(win);
            } else if (state->input_cursor > 0) {
                int len = strlen(state->input_buffer);
                for (int i = state->input_cursor - 1; i < len; i++) {
                    state->input_buffer[i] = state->input_buffer[i + 1];
                }
                state->input_cursor--;
                filemgr_redraw(win);
            }
            return;
        } else if (c >= 32 && c < 127 &&
                   (c != '/' || state->input_action == FILEMGR_INPUT_SEARCH)) {
            char insert[2] = {(char)c, '\0'};
            if (filemgr_input_has_selection(state)) {
                filemgr_input_delete_selection(state);
            }
            filemgr_input_insert(state, insert);
            filemgr_redraw(win);
            return;
        }
        return;
    }

    if (state->search_active) {
        if (c == 's' || c == 'S') {
            filemgr_begin_input(state, FILEMGR_INPUT_SEARCH, state->search_query);
            filemgr_redraw(win);
            return;
        } else if (c == 'c' || c == 'C' || c == 8 || c == 127) {
            filemgr_clear_search(state);
            filemgr_redraw(win);
            return;
        } else if (c == '\n' || c == '\r') {
            if (state->search_selected >= 0 && state->search_selected < state->search_count) {
                filemgr_open_search_result(win, state, state->search_selected);
            } else {
                filemgr_redraw(win);
            }
            return;
        } else if ((uint8_t)c == 0x80) {
            if (state->search_selected > 0) {
                state->search_selected--;
                if (state->search_selected < state->search_scroll) {
                    state->search_scroll = state->search_selected;
                }
                filemgr_redraw(win);
            }
            return;
        } else if ((uint8_t)c == 0x81) {
            if (state->search_selected < state->search_count - 1) {
                state->search_selected++;
                if (state->search_selected >= state->search_scroll + visible_items) {
                    state->search_scroll++;
                }
                filemgr_redraw(win);
            } else if (state->search_selected == -1 && state->search_count > 0) {
                state->search_selected = 0;
                filemgr_redraw(win);
            }
            return;
        }
    }

    if (c == 'n' || c == 'N') {
        filemgr_begin_input(state, FILEMGR_INPUT_NEW_FILE, "newfile.txt");
        needs_redraw = true;
    } else if (c == 'f' || c == 'F') {
        filemgr_begin_input(state, FILEMGR_INPUT_NEW_FOLDER, "newfolder");
        needs_redraw = true;
    } else if (c == 'r' || c == 'R') {
        if (state->selected >= 0 && state->selected < state->entry_count &&
            strcmp(state->entries[state->selected].d_name, "..") != 0) {
            filemgr_begin_input(state, FILEMGR_INPUT_RENAME,
                                state->entries[state->selected].d_name);
            needs_redraw = true;
        }
    } else if (c == 's' || c == 'S') {
        filemgr_begin_input(state, FILEMGR_INPUT_SEARCH, state->search_query);
        needs_redraw = true;
    } else if ((uint8_t)c == 0x80) {
        // Scroll up or move selection up
        if (state->selected > 0) {
            state->selected--;
            if (state->selected < state->scroll_offset) {
                state->scroll_offset = state->selected;
            }
            needs_redraw = true;
        }
    } else if ((uint8_t)c == 0x81) {
        // Scroll down or move selection down
        if (state->selected < state->entry_count - 1) {
            state->selected++;
            if (state->selected >= state->scroll_offset + visible_items) {
                state->scroll_offset++;
            }
            needs_redraw = true;
        } else if (state->selected == -1 && state->entry_count > 0) {
            state->selected = 0;
            needs_redraw = true;
        }
    } else if (c == 8 || c == 127) {  // Backspace - go up
        char* last_slash = strrchr(state->current_path, '/');
        if (last_slash && last_slash != state->current_path) {
            *last_slash = '\0';
            filemgr_load_dir(state);
            needs_redraw = true;
        } else if (strcmp(state->current_path, "/") != 0) {
            strcpy(state->current_path, "/");
            filemgr_load_dir(state);
            needs_redraw = true;
        }
    } else if (c == '\n' || c == '\r') {  // Enter - navigate into selected
        if (state->selected >= 0 && state->selected < state->entry_count) {
            if (strcmp(state->entries[state->selected].d_name, "..") == 0) {
                // Go up
                char* last_slash = strrchr(state->current_path, '/');
                if (last_slash && last_slash != state->current_path) {
                    *last_slash = '\0';
                } else {
                    strcpy(state->current_path, "/");
                }
                filemgr_load_dir(state);
                needs_redraw = true;
            } else {
                // Try to enter directory
                if (state->entries[state->selected].d_type == 2) {
                    char full_path[128];
                    if (strcmp(state->current_path, "/") == 0) {
                        snprintf(full_path, sizeof(full_path), "/%s",
                                state->entries[state->selected].d_name);
                    } else {
                        snprintf(full_path, sizeof(full_path), "%s/%s",
                                state->current_path, state->entries[state->selected].d_name);
                    }
                    strcpy(state->current_path, full_path);
                    filemgr_load_dir(state);
                    needs_redraw = true;
                }
            }
        }
    }
    
    if (needs_redraw) {
        filemgr_redraw(win);
    }
}

static void filemgr_on_draw(window_t* win) {
    filemgr_redraw(win);
}

static void filemgr_on_mouse_down(window_t* win, int x, int y, int buttons) {
    filemgr_state_t* state = (filemgr_state_t*)window_get_user_data(win);
    if (!state) return;
    if (buttons & MOUSE_LEFT_BUTTON) {
        int menu_button_w = filemgr_menu_button_width();
        bool in_menu_button = (x >= FILEMGR_MENU_BUTTON_X &&
                               x < FILEMGR_MENU_BUTTON_X + menu_button_w &&
                               y >= FILEMGR_MENU_BUTTON_Y &&
                               y < FILEMGR_MENU_BUTTON_Y + FILEMGR_MENU_BUTTON_HEIGHT);

        if (state->input_mode) {
            int input_x = 0;
            int input_y = 0;
            int input_w = 0;
            int input_h = 0;
            int text_x = 0;
            if (state->input_action == FILEMGR_INPUT_SEARCH) {
                filemgr_search_dialog_layout(win, NULL, NULL, NULL, NULL,
                                             &input_x, &input_y, &input_w, &input_h,
                                             &text_x, NULL);
            } else {
                filemgr_input_layout(win, state, &input_x, &input_y, &input_w, &input_h,
                                     &text_x, NULL);
            }
            if (x >= input_x && x < input_x + input_w &&
                y >= input_y && y < input_y + input_h) {
                filemgr_input_set_cursor_from_x(state, x, text_x);
                state->input_selecting = true;
                state->input_sel_anchor = state->input_cursor;
                state->input_sel_end = state->input_cursor;
                state->menu_open = false;
                state->menu_hover = -1;
                filemgr_redraw(win);
                return;
            }

            state->input_mode = false;
            state->input_action = FILEMGR_INPUT_NONE;
            state->input_buffer[0] = '\0';
            state->input_cursor = 0;
            filemgr_input_clear_selection(state);
            filemgr_redraw(win);
            return;
        }

        if (state->menu_open) {
            int idx = filemgr_menu_item_at(state, x, y);
            if (idx >= 0) {
                filemgr_menu_select(win, state, idx);
                state->menu_open = false;
                state->menu_hover = -1;
                filemgr_redraw(win);
                return;
            }
            if (in_menu_button) {
                state->menu_open = false;
                state->menu_hover = -1;
                filemgr_redraw(win);
                return;
            }
            state->menu_open = false;
            state->menu_hover = -1;
            filemgr_redraw(win);
        }

        if (y < FILEMGR_TOP_BAR_HEIGHT) {
            if (in_menu_button) {
                if (!state->menu_open) {
                    filemgr_menu_open_at(win, state, FILEMGR_MENU_BUTTON_X, FILEMGR_TOP_BAR_HEIGHT);
                }
                filemgr_redraw(win);
            }
            return;
        }

        filemgr_click(win, x, y);
    }
}

static void filemgr_on_scroll(window_t* win, int delta) {
    filemgr_state_t* state = (filemgr_state_t*)window_get_user_data(win);
    int content_h = window_content_height(win);
    int list_top = FILEMGR_TOP_BAR_HEIGHT + 2;
    int list_height = content_h - list_top - FILEMGR_STATUS_HEIGHT - FILEMGR_LIST_BOTTOM_PADDING;
    if (list_height < 0) list_height = 0;
    int visible_items = list_height / 11;
    if (visible_items < 1) visible_items = 1;
    if (state->search_active) {
        int max_scroll = state->search_count - visible_items;
        if (max_scroll <= 0) return;

        state->search_scroll += delta;
        if (state->search_scroll < 0) state->search_scroll = 0;
        if (state->search_scroll > max_scroll) state->search_scroll = max_scroll;
        if (state->search_selected >= 0) {
            if (state->search_selected < state->search_scroll) {
                state->search_selected = state->search_scroll;
            } else if (state->search_selected >= state->search_scroll + visible_items) {
                state->search_selected = state->search_scroll + visible_items - 1;
            }
        }
    } else {
        int max_scroll = state->entry_count - visible_items;
        if (max_scroll <= 0) return;

        state->scroll_offset += delta;
        if (state->scroll_offset < 0) state->scroll_offset = 0;
        if (state->scroll_offset > max_scroll) state->scroll_offset = max_scroll;
        if (state->selected >= 0) {
            if (state->selected < state->scroll_offset) {
                state->selected = state->scroll_offset;
            } else if (state->selected >= state->scroll_offset + visible_items) {
                state->selected = state->scroll_offset + visible_items - 1;
            }
        }
    }
    filemgr_redraw(win);
}

static void filemgr_on_mouse_move(window_t* win, int x, int y, int buttons) {
    filemgr_state_t* state = (filemgr_state_t*)window_get_user_data(win);
    if (!state) return;
    if (state->input_mode && state->input_selecting && (buttons & MOUSE_LEFT_BUTTON)) {
        int text_x = 0;
        if (state->input_action == FILEMGR_INPUT_SEARCH) {
            filemgr_search_dialog_layout(win, NULL, NULL, NULL, NULL,
                                         NULL, NULL, NULL, NULL, &text_x, NULL);
        } else {
            filemgr_input_layout(win, state, NULL, NULL, NULL, NULL, &text_x, NULL);
        }
        filemgr_input_set_cursor_from_x(state, x, text_x);
        state->input_sel_end = state->input_cursor;
        filemgr_redraw(win);
        return;
    }
    if (!state->menu_open) return;
    int idx = filemgr_menu_item_at(state, x, y);
    if (idx != state->menu_hover) {
        state->menu_hover = idx;
        filemgr_redraw(win);
    }
}

static void filemgr_on_mouse_up(window_t* win, int x, int y, int buttons) {
    (void)x;
    (void)y;
    (void)buttons;
    filemgr_state_t* state = (filemgr_state_t*)window_get_user_data(win);
    if (!state) return;
    if (state->input_selecting) {
        state->input_selecting = false;
        if (!filemgr_input_has_selection(state)) {
            filemgr_input_clear_selection(state);
        }
        filemgr_redraw(win);
    }
}

window_t* gui_filemgr_create_window(int x, int y) {
    if (filemgr_window && uwm_window_is_open(filemgr_window)) {
        return filemgr_window;
    }

    window_t* win = window_create(x, y, 260, 200, "File Explorer");
    if (!win) return NULL;

    memset(&filemgr_state, 0, sizeof(filemgr_state));
    strcpy(filemgr_state.current_path, "/");
    filemgr_state.selected = -1;
    filemgr_state.input_mode = false;
    filemgr_state.input_action = FILEMGR_INPUT_NONE;
    filemgr_state.input_buffer[0] = '\0';
    filemgr_state.input_cursor = 0;
    filemgr_state.input_selecting = false;
    filemgr_state.input_sel_anchor = 0;
    filemgr_state.input_sel_end = 0;
    filemgr_state.menu_open = false;
    filemgr_state.menu_hover = -1;
    filemgr_state.menu_x = 0;
    filemgr_state.menu_y = 0;
    filemgr_clear_search(&filemgr_state);
    filemgr_load_dir(&filemgr_state);

    window_set_handlers(win, filemgr_on_draw, filemgr_on_mouse_down, filemgr_on_mouse_up,
                        filemgr_on_mouse_move,
                        filemgr_on_scroll, filemgr_key, &filemgr_state);
    filemgr_window = win;
    return win;
}
