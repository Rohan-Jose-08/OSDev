#include <dirent.h>
#include <gui_window.h>
#include <graphics.h>
#include <mouse.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

typedef enum {
    FILEMGR_INPUT_NONE = 0,
    FILEMGR_INPUT_RENAME,
    FILEMGR_INPUT_NEW_FILE,
    FILEMGR_INPUT_NEW_FOLDER
} filemgr_input_action_t;

typedef struct {
    char current_path[128];
    struct dirent entries[32];
    int entry_count;
    int scroll_offset;
    int selected;
    int last_click_item;
    unsigned int last_click_time;
    bool input_mode;
    filemgr_input_action_t input_action;
    char input_buffer[64];
    int input_cursor;
    bool menu_open;
    int menu_x;
    int menu_y;
    int menu_hover;
} filemgr_state_t;

static window_t* filemgr_window = NULL;
static filemgr_state_t filemgr_state;

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
    "Rename"
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
    }
}

static void filemgr_load_dir(filemgr_state_t* state) {
    normalize_path(state->current_path);
    
    // Get directory listing
    state->entry_count = listdir(state->current_path, state->entries, 32);
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
    
    // List entries
    int y = list_top + 5;
    int visible_items = list_height / 11;
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
        uint8_t bg = COLOR_WHITE;
        if (i == state->selected) {
            bg = COLOR_LIGHT_CYAN;
            window_fill_rect(win, 4, y - 2, content_w - 8, 11, bg);
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
    if (state->input_mode) {
        const char* prompt = "Rename to:";
        if (state->input_action == FILEMGR_INPUT_NEW_FILE) {
            prompt = "New file:";
        } else if (state->input_action == FILEMGR_INPUT_NEW_FOLDER) {
            prompt = "New folder:";
        }
        int prompt_x = 5;
        int status_text_y = status_y + 4;
        window_print(win, prompt_x, status_text_y, prompt, COLOR_DARK_GRAY);

        int input_x = prompt_x + (int)strlen(prompt) * 8 + 6;
        int input_w = content_w - input_x - 5;
        if (input_w < 20) input_w = 20;
        int input_y = status_y + 1;
        window_fill_rect(win, input_x, input_y, input_w, 12, COLOR_WHITE);
        window_draw_rect(win, input_x, input_y, input_w, 12, COLOR_BLACK);
        window_print(win, input_x + 4, status_text_y, state->input_buffer, COLOR_BLACK);

        int cursor_x = input_x + 4 + state->input_cursor * 8;
        if (cursor_x < input_x + input_w - 1) {
            window_fill_rect(win, cursor_x, input_y + 1, 1, 10, COLOR_BLACK);
        }
    } else {
        char status[96];
        snprintf(status, sizeof(status),
                 "%d items | Up/Down:scroll Bksp:up N:new F:folder R:rename",
                 state->entry_count);
        window_print(win, 5, status_y + 4, status, COLOR_DARK_GRAY);
    }
}

static void filemgr_click(window_t* win, int x, int y) {
    filemgr_state_t* state = (filemgr_state_t*)window_get_user_data(win);
    (void)x;

    if (state->input_mode) {
        state->input_mode = false;
        state->input_action = FILEMGR_INPUT_NONE;
        state->input_buffer[0] = '\0';
        state->input_cursor = 0;
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
        int item_idx = state->scroll_offset + (y - (list_top + 5)) / 11;
        
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
        if (c == '\n' || c == '\r') {
            bool success = false;
            bool exit_input = true;
            if (state->input_buffer[0] != '\0') {
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
                }

                if (success) {
                    filemgr_load_dir(state);
                    filemgr_select_by_name(win, state, target_name);
                } else {
                    exit_input = false;
                }
            }

            if (exit_input) {
                state->input_mode = false;
                state->input_action = FILEMGR_INPUT_NONE;
                state->input_buffer[0] = '\0';
                state->input_cursor = 0;
            }
            filemgr_redraw(win);
            return;
        } else if (c == 27) {
            state->input_mode = false;
            state->input_action = FILEMGR_INPUT_NONE;
            state->input_buffer[0] = '\0';
            state->input_cursor = 0;
            filemgr_redraw(win);
            return;
        } else if (c == 8 || c == 127) {
            if (state->input_cursor > 0) {
                state->input_cursor--;
                state->input_buffer[state->input_cursor] = '\0';
                filemgr_redraw(win);
            }
            return;
        } else if (c >= 32 && c < 127 && c != '/') {
            if (state->input_cursor < (int)sizeof(state->input_buffer) - 1) {
                state->input_buffer[state->input_cursor] = (char)c;
                state->input_cursor++;
                state->input_buffer[state->input_cursor] = '\0';
                filemgr_redraw(win);
            }
            return;
        }
        return;
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
            state->input_mode = false;
            state->input_action = FILEMGR_INPUT_NONE;
            state->input_buffer[0] = '\0';
            state->input_cursor = 0;
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
    filemgr_redraw(win);
}

static void filemgr_on_mouse_move(window_t* win, int x, int y, int buttons) {
    filemgr_state_t* state = (filemgr_state_t*)window_get_user_data(win);
    (void)buttons;
    if (!state || !state->menu_open) return;
    int idx = filemgr_menu_item_at(state, x, y);
    if (idx != state->menu_hover) {
        state->menu_hover = idx;
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
    filemgr_state.menu_open = false;
    filemgr_state.menu_hover = -1;
    filemgr_state.menu_x = 0;
    filemgr_state.menu_y = 0;
    filemgr_load_dir(&filemgr_state);

    window_set_handlers(win, filemgr_on_draw, filemgr_on_mouse_down, NULL, filemgr_on_mouse_move,
                        filemgr_on_scroll, filemgr_key, &filemgr_state);
    filemgr_window = win;
    return win;
}
