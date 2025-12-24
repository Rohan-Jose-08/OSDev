#include <file_dialog.h>
#include <gui_window.h>
#include <graphics.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// ===== TEXT EDITOR APP =====
#define EDITOR_MAX_LINES 100
#define EDITOR_MAX_LINE_LENGTH 80
#define EDITOR_VISIBLE_LINES 15
#define EDITOR_VISIBLE_COLS 35
#define EDITOR_MENU_HEIGHT 14
#define EDITOR_MAX_FILES 20
#define EDITOR_BUFFER_MAX (EDITOR_MAX_LINES * (EDITOR_MAX_LINE_LENGTH + 1))

typedef struct {
    char lines[EDITOR_MAX_LINES][EDITOR_MAX_LINE_LENGTH];
    int line_count;
    int cursor_line;
    int cursor_col;
    int scroll_offset;
    bool modified;
    bool menu_open;
    int menu_hover;
    char filename[64];
    bool has_filename;
    window_t* window;  // Store window reference for callbacks
} editor_state_t;

static window_t* editor_window = NULL;
static editor_state_t editor_state;

// Forward declarations for editor functions
static void editor_redraw(window_t* win);
static void editor_load_file(editor_state_t* state, const char* filepath);
static void editor_save_file(editor_state_t* state);
static void editor_new_file(editor_state_t* state);
static void editor_insert_char(editor_state_t* state, char c);
static void editor_delete_char(editor_state_t* state);
static void editor_new_line(editor_state_t* state);
static void editor_click(window_t* win, int x, int y, int buttons);
static void editor_handle_mouse_move(window_t* win, int x, int y, int buttons);
static void editor_key(window_t* win, int c);

static void editor_redraw(window_t* win) {
    editor_state_t* state = (editor_state_t*)window_get_user_data(win);
    int content_w = window_content_width(win);
    int content_h = window_content_height(win);
    
    // Clear content area
    window_clear_content(win, COLOR_WHITE);
    
    // Draw menu bar
    window_fill_rect(win, 0, 0, content_w, EDITOR_MENU_HEIGHT, COLOR_LIGHT_GRAY);
    window_draw_rect(win, 0, 0, content_w, EDITOR_MENU_HEIGHT, COLOR_DARK_GRAY);
    
    // File menu
    window_print(win, 5, 2, "File", COLOR_BLACK);
    if (state->menu_open) {
        window_fill_rect(win, 3, 2, 25, 10, COLOR_LIGHT_BLUE);
        window_print(win, 5, 2, "File", COLOR_BLACK);
    }
    
    // Show filename if set
    if (state->has_filename) {
        char title[48];
        snprintf(title, sizeof(title), "- %s", state->filename);
        window_print(win, 35, 2, title, COLOR_DARK_GRAY);
    }
    
    // Modified indicator
    if (state->modified) {
        window_print(win, content_w - 15, 2, "*", COLOR_RED);
    }
    
    // Draw dropdown menu if open
    if (state->menu_open) {
        int menu_x = 3;
        int menu_y = EDITOR_MENU_HEIGHT;
        int menu_width = 80;
        int menu_height = 74;
        
        // Menu background
        window_fill_rect(win, menu_x, menu_y, menu_width, menu_height, COLOR_WHITE);
        window_draw_rect(win, menu_x, menu_y, menu_width, menu_height, COLOR_DARK_GRAY);
        
        // Menu items
        const char* items[] = {"Open...", "Save", "Save As...", "New", "Close"};
        for (int i = 0; i < 5; i++) {
            int item_y = menu_y + 2 + (i * 14);
            
            if (state->menu_hover == i) {
                window_fill_rect(win, menu_x + 1, item_y, menu_width - 2, 12, COLOR_LIGHT_BLUE);
            }
            window_print(win, menu_x + 5, item_y + 2, items[i], COLOR_BLACK);
        }
    }
    
    // Draw status bar at bottom
    int status_y = content_h - 14;
    window_fill_rect(win, 0, status_y, content_w, 14, COLOR_LIGHT_GRAY);
    window_draw_rect(win, 0, status_y, content_w, 1, COLOR_DARK_GRAY);
    
    char status[64];
    snprintf(status, sizeof(status), "Ln %d/%d Col %d", 
             state->cursor_line + 1, state->line_count, state->cursor_col + 1);
    window_print(win, 5, status_y + 2, status, COLOR_BLACK);
    
    // Draw help text
    window_print(win, content_w - 100, status_y + 2, 
                "Enter=Line Bksp=Del", COLOR_DARK_GRAY);
    
    // Draw text content
    int text_start_y = EDITOR_MENU_HEIGHT + 4;
    int text_height = status_y - text_start_y;
    int visible_lines = text_height / 10;
    
    for (int i = 0; i < visible_lines && (i + state->scroll_offset) < state->line_count; i++) {
        int line_idx = i + state->scroll_offset;
        int y = text_start_y + (i * 10);
        
        // Draw line text
        window_print(win, 5, y, state->lines[line_idx], COLOR_BLACK);
        
        // Draw cursor if on this line
        if (line_idx == state->cursor_line) {
            int cursor_x = 5 + (state->cursor_col * 8);
            window_fill_rect(win, cursor_x, y, 2, 9, COLOR_BLACK);
        }
    }
}

// Callback for file dialog open
static void editor_file_open_callback(const char* filepath, void* user_data) {
    editor_state_t* state = (editor_state_t*)user_data;
    if (!state || !state->window) return;
    
    if (filepath) {
        editor_load_file(state, filepath);
    }
    
    editor_redraw(state->window);
}

// Callback for file dialog save
static void editor_file_save_callback(const char* filepath, void* user_data) {
    editor_state_t* state = (editor_state_t*)user_data;
    if (!state || !state->window) return;
    
    if (filepath) {
        // Update filename and save
        strncpy(state->filename, filepath, sizeof(state->filename) - 1);
        state->filename[sizeof(state->filename) - 1] = '\0';
        state->has_filename = true;
        editor_save_file(state);
    }
    
    editor_redraw(state->window);
}

static int editor_read_file(const char* filepath, uint8_t* buffer, int max_len) {
    int fd = open(filepath);
    if (fd < 0) {
        return -1;
    }

    int total = 0;
    while (total < max_len) {
        int n = read(fd, buffer + total, (uint32_t)(max_len - total));
        if (n <= 0) break;
        total += n;
    }

    close(fd);
    return total;
}

static void editor_load_file(editor_state_t* state, const char* filepath) {
    // Read file content
    uint8_t buffer[4096];
    int bytes_read = editor_read_file(filepath, buffer, (int)sizeof(buffer) - 1);
    
    if (bytes_read <= 0) {
        return;  // File read failed
    }
    
    buffer[bytes_read] = '\0';
    
    // Clear existing content
    for (int i = 0; i < EDITOR_MAX_LINES; i++) {
        state->lines[i][0] = '\0';
    }
    
    // Parse into lines
    state->line_count = 0;
    state->cursor_line = 0;
    state->cursor_col = 0;
    state->scroll_offset = 0;
    
    int line_idx = 0;
    int col_idx = 0;
    
    for (int i = 0; i < bytes_read && line_idx < EDITOR_MAX_LINES; i++) {
        if (buffer[i] == '\n' || buffer[i] == '\r') {
            if (buffer[i] == '\r' && buffer[i+1] == '\n') {
                i++;  // Skip \r\n pairs
            }
            state->lines[line_idx][col_idx] = '\0';
            line_idx++;
            col_idx = 0;
        } else if (col_idx < EDITOR_MAX_LINE_LENGTH - 1) {
            state->lines[line_idx][col_idx++] = buffer[i];
        }
    }
    
    // Add final line
    if (col_idx > 0 || line_idx == 0) {
        state->lines[line_idx][col_idx] = '\0';
        line_idx++;
    }
    
    state->line_count = line_idx;
    strcpy(state->filename, filepath);
    state->has_filename = true;
    state->modified = false;
}

static void editor_save_file(editor_state_t* state) {
    if (!state->has_filename) {
        // Default filename if none set
        strcpy(state->filename, "/home/untitled.txt");
        state->has_filename = true;
    }
    
    // Use filename as-is (already contains full path)
    const char* filepath = state->filename;
    
    static uint8_t buffer[EDITOR_BUFFER_MAX];
    int pos = 0;

    for (int i = 0; i < state->line_count; i++) {
        int len = strlen(state->lines[i]);
        if (pos + len + 1 >= EDITOR_BUFFER_MAX) {
            break;
        }
        memcpy(buffer + pos, state->lines[i], len);
        pos += len;
        buffer[pos++] = '\n';
    }

    if (pos == 0) {
        state->modified = false;
        return;
    }

    int written = writefile(filepath, buffer, (uint32_t)pos);
    if (written >= 0) {
        state->modified = false;
    }
}

static void editor_new_file(editor_state_t* state) {
    // Clear document
    for (int i = 0; i < EDITOR_MAX_LINES; i++) {
        state->lines[i][0] = '\0';
    }
    state->line_count = 1;
    state->cursor_line = 0;
    state->cursor_col = 0;
    state->scroll_offset = 0;
    state->modified = false;
    state->has_filename = false;
    state->filename[0] = '\0';
}

static void editor_insert_char(editor_state_t* state, char c) {
    if (state->cursor_line >= EDITOR_MAX_LINES) return;
    
    char* line = state->lines[state->cursor_line];
    int len = strlen(line);
    
    if (len < EDITOR_MAX_LINE_LENGTH - 1) {
        // Shift characters right to make room
        for (int i = len; i >= state->cursor_col; i--) {
            line[i + 1] = line[i];
        }
        line[state->cursor_col] = c;
        state->cursor_col++;
        state->modified = true;
    }
}

static void editor_delete_char(editor_state_t* state) {
    if (state->cursor_col > 0) {
        // Delete character before cursor
        char* line = state->lines[state->cursor_line];
        int len = strlen(line);
        
        for (int i = state->cursor_col - 1; i < len; i++) {
            line[i] = line[i + 1];
        }
        state->cursor_col--;
        state->modified = true;
    } else if (state->cursor_line > 0) {
        // At start of line - merge with previous line
        int prev_line = state->cursor_line - 1;
        int prev_len = strlen(state->lines[prev_line]);
        int curr_len = strlen(state->lines[state->cursor_line]);
        
        // Check if we can merge
        if (prev_len + curr_len < EDITOR_MAX_LINE_LENGTH - 1) {
            strcat(state->lines[prev_line], state->lines[state->cursor_line]);
            
            // Shift lines up
            for (int i = state->cursor_line; i < state->line_count - 1; i++) {
                strcpy(state->lines[i], state->lines[i + 1]);
            }
            state->lines[state->line_count - 1][0] = '\0';
            state->line_count--;
            
            state->cursor_line = prev_line;
            state->cursor_col = prev_len;
            state->modified = true;
        }
    }
}

static void editor_new_line(editor_state_t* state) {
    if (state->line_count >= EDITOR_MAX_LINES) return;
    
    // Shift lines down
    for (int i = state->line_count; i > state->cursor_line; i--) {
        strcpy(state->lines[i], state->lines[i - 1]);
    }
    
    // Split current line at cursor
    char* current_line = state->lines[state->cursor_line];
    char* next_line = state->lines[state->cursor_line + 1];
    
    strcpy(next_line, &current_line[state->cursor_col]);
    current_line[state->cursor_col] = '\0';
    
    state->line_count++;
    state->cursor_line++;
    state->cursor_col = 0;
    state->modified = true;
}

static void editor_click(window_t* win, int x, int y, int buttons) {
    (void)buttons;
    editor_state_t* state = (editor_state_t*)window_get_user_data(win);
    int content_h = window_content_height(win);
    
    // Check menu bar click
    if (y < EDITOR_MENU_HEIGHT) {
        if (x >= 3 && x < 30) {
            // Toggle File menu
            state->menu_open = !state->menu_open;
            editor_redraw(win);
            return;
        }
    }
    
    // Check dropdown menu clicks
    if (state->menu_open && y >= EDITOR_MENU_HEIGHT && y < EDITOR_MENU_HEIGHT + 74 && x >= 3 && x < 83) {
        int item = (y - EDITOR_MENU_HEIGHT - 2) / 14;
        
        if (item == 0) {
            // Open - show file dialog
            state->menu_open = false;
            editor_redraw(win);
            file_dialog_show_open("Open File", "/", editor_file_open_callback, state);
        } else if (item == 1) {
            // Save
            editor_save_file(state);
            state->menu_open = false;
            editor_redraw(win);
        } else if (item == 2) {
            // Save As - show file dialog
            state->menu_open = false;
            editor_redraw(win);
            const char* default_name = state->has_filename ? state->filename : "document.txt";
            file_dialog_show_save("Save File As", default_name, editor_file_save_callback, state);
        } else if (item == 3) {
            // New
            editor_new_file(state);
            state->menu_open = false;
            editor_redraw(win);
        } else if (item == 4) {
            // Close menu
            state->menu_open = false;
            editor_redraw(win);
        }
        return;
    }
    
    // Close menu if clicking elsewhere
    if (state->menu_open) {
        state->menu_open = false;
        editor_redraw(win);
        return;
    }
    
    int text_start_y = EDITOR_MENU_HEIGHT + 4;
    int status_y = content_h - 14;
    
    // Check if click is in text area
    if (y >= text_start_y && y < status_y) {
        int line_offset = (y - text_start_y) / 10;
        int clicked_line = state->scroll_offset + line_offset;
        
        if (clicked_line < state->line_count) {
            state->cursor_line = clicked_line;
            
            // Estimate column from x position
            int col = (x - 5) / 8;
            int line_len = strlen(state->lines[clicked_line]);
            state->cursor_col = (col < 0) ? 0 : (col > line_len) ? line_len : col;
            
            editor_redraw(win);
        }
    }
}

static void editor_handle_mouse_move(window_t* win, int x, int y, int buttons) {
    (void)buttons;
    editor_state_t* state = (editor_state_t*)window_get_user_data(win);
    
    // Update menu hover state
    if (state->menu_open && y >= EDITOR_MENU_HEIGHT && y < EDITOR_MENU_HEIGHT + 74 && x >= 3 && x < 83) {
        int item = (y - EDITOR_MENU_HEIGHT - 2) / 14;
        if (item >= 0 && item < 5 && item != state->menu_hover) {
            state->menu_hover = item;
            editor_redraw(win);
        }
    } else if (state->menu_hover != -1) {
        state->menu_hover = -1;
        if (state->menu_open) editor_redraw(win);
    }
}

static void editor_key(window_t* win, int c) {
    editor_state_t* state = (editor_state_t*)window_get_user_data(win);
    int content_h = window_content_height(win);
    bool needs_redraw = false;
    
    // Cast to unsigned to handle arrow keys correctly
    unsigned char uc = (unsigned char)c;
    
    // Normal text editing mode
    if (c == '\n' || c == '\r') {
        // Enter key - new line
        editor_new_line(state);
        needs_redraw = true;
    } else if (c == 8 || c == 127) {
        // Backspace
        editor_delete_char(state);
        needs_redraw = true;
    } else if (c >= 32 && c <= 126) {
        // Printable character
        editor_insert_char(state, c);
        needs_redraw = true;
    } else if (uc == 0x80) {  // Up arrow
        if (state->cursor_line > 0) {
            state->cursor_line--;
            int line_len = strlen(state->lines[state->cursor_line]);
            if (state->cursor_col > line_len) state->cursor_col = line_len;
            if (state->cursor_line < state->scroll_offset) state->scroll_offset--;
            needs_redraw = true;
        }
    } else if (uc == 0x81) {  // Down arrow
        if (state->cursor_line < state->line_count - 1) {
            state->cursor_line++;
            int line_len = strlen(state->lines[state->cursor_line]);
            if (state->cursor_col > line_len) state->cursor_col = line_len;
            int visible_lines = (content_h - 32) / 10;
            if (state->cursor_line >= state->scroll_offset + visible_lines) state->scroll_offset++;
            needs_redraw = true;
        }
    } else if (uc == 0x82) {  // Left arrow
        if (state->cursor_col > 0) {
            state->cursor_col--;
            needs_redraw = true;
        } else if (state->cursor_line > 0) {
            state->cursor_line--;
            state->cursor_col = strlen(state->lines[state->cursor_line]);
            if (state->cursor_line < state->scroll_offset) state->scroll_offset--;
            needs_redraw = true;
        }
    } else if (uc == 0x83) {  // Right arrow
        int line_len = strlen(state->lines[state->cursor_line]);
        if (state->cursor_col < line_len) {
            state->cursor_col++;
            needs_redraw = true;
        } else if (state->cursor_line < state->line_count - 1) {
            state->cursor_line++;
            state->cursor_col = 0;
            int visible_lines = (content_h - 32) / 10;
            if (state->cursor_line >= state->scroll_offset + visible_lines) state->scroll_offset++;
            needs_redraw = true;
        }
    }
    
    if (needs_redraw) {
        editor_redraw(win);
    }
}

window_t* gui_editor_create_window(int x, int y) {
    static int editor_count = 0;
    if (editor_window && uwm_window_is_open(editor_window)) {
        return editor_window;
    }
    char title[64];
    snprintf(title, sizeof(title), "Text Editor %d", ++editor_count);
    
    // Calculate window size relative to screen (approx 70% width, 65% height)
    int screen_w = graphics_get_width();
    int screen_h = graphics_get_height();
    int win_w = screen_w * 70 / 100;
    int win_h = screen_h * 65 / 100;
    if (win_w < 270) win_w = 270;  // Minimum size
    if (win_h < 240) win_h = 240;
    
    window_t* win = window_create(x, y, win_w, win_h, title);
    if (!win) return NULL;

    memset(&editor_state, 0, sizeof(editor_state));

    // Initialize empty document
    for (int i = 0; i < EDITOR_MAX_LINES; i++) {
        editor_state.lines[i][0] = '\0';
    }
    editor_state.line_count = 1;
    editor_state.cursor_line = 0;
    editor_state.cursor_col = 0;
    editor_state.scroll_offset = 0;
    editor_state.modified = false;
    editor_state.menu_open = false;
    editor_state.menu_hover = -1;
    editor_state.has_filename = false;
    editor_state.filename[0] = '\0';
    editor_state.window = win;

    window_set_handlers(win, editor_redraw, editor_click, NULL,
                        editor_handle_mouse_move, NULL, editor_key, &editor_state);

    editor_window = win;
    return win;
}
