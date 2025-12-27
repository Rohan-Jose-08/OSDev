#include <file_dialog.h>
#include <gui_window.h>
#include <graphics.h>
#include <mouse.h>
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
#define EDITOR_TEXT_X 5
#define EDITOR_TEXT_LINE_HEIGHT 10
#define EDITOR_TEXT_CHAR_WIDTH 8
#define EDITOR_TEXT_CHAR_HEIGHT 9

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
    bool selecting;
    bool selection_active;
    int sel_anchor_line;
    int sel_anchor_col;
    int sel_end_line;
    int sel_end_col;
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
static void editor_copy_line(editor_state_t* state);
static void editor_cut_line(editor_state_t* state);
static void editor_copy_selection(editor_state_t* state);
static void editor_delete_selection(editor_state_t* state);
static void editor_paste_clipboard(editor_state_t* state);
static void editor_click(window_t* win, int x, int y, int buttons);
static void editor_handle_mouse_move(window_t* win, int x, int y, int buttons);
static void editor_mouse_up(window_t* win, int x, int y, int buttons);
static void editor_key(window_t* win, int c);

static void editor_clear_selection(editor_state_t* state) {
    if (!state) return;
    state->selecting = false;
    state->selection_active = false;
}

static bool editor_selection_empty(const editor_state_t* state) {
    return state->sel_anchor_line == state->sel_end_line &&
           state->sel_anchor_col == state->sel_end_col;
}

static bool editor_has_selection(const editor_state_t* state) {
    return state && state->selection_active && !editor_selection_empty(state);
}

static void editor_normalize_selection(const editor_state_t* state,
                                       int* start_line, int* start_col,
                                       int* end_line, int* end_col) {
    int s_line = state->sel_anchor_line;
    int s_col = state->sel_anchor_col;
    int e_line = state->sel_end_line;
    int e_col = state->sel_end_col;

    if (e_line < s_line || (e_line == s_line && e_col < s_col)) {
        int tmp_line = s_line;
        int tmp_col = s_col;
        s_line = e_line;
        s_col = e_col;
        e_line = tmp_line;
        e_col = tmp_col;
    }

    *start_line = s_line;
    *start_col = s_col;
    *end_line = e_line;
    *end_col = e_col;
}

static void editor_point_to_cursor(editor_state_t* state, int x, int y, int content_h,
                                   int* out_line, int* out_col) {
    int text_start_y = EDITOR_MENU_HEIGHT + 4;
    int status_y = content_h - 14;
    int text_height = status_y - text_start_y;
    int visible_lines = text_height / EDITOR_TEXT_LINE_HEIGHT;
    if (visible_lines < 1) visible_lines = 1;

    int line_offset = (y - text_start_y) / EDITOR_TEXT_LINE_HEIGHT;
    if (line_offset < 0) line_offset = 0;
    if (line_offset >= visible_lines) line_offset = visible_lines - 1;

    int line = state->scroll_offset + line_offset;
    if (line < 0) line = 0;
    if (line >= state->line_count) line = state->line_count - 1;
    if (line < 0) line = 0;

    int line_len = strlen(state->lines[line]);
    int col;
    if (x < EDITOR_TEXT_X) {
        col = 0;
    } else {
        col = (x - EDITOR_TEXT_X) / EDITOR_TEXT_CHAR_WIDTH;
    }
    if (col < 0) col = 0;
    if (col > line_len) col = line_len;

    *out_line = line;
    *out_col = col;
}

static bool editor_get_selection(editor_state_t* state,
                                 int* start_line, int* start_col,
                                 int* end_line, int* end_col) {
    if (!editor_has_selection(state)) {
        return false;
    }

    int s_line = 0;
    int s_col = 0;
    int e_line = 0;
    int e_col = 0;
    editor_normalize_selection(state, &s_line, &s_col, &e_line, &e_col);

    if (state->line_count < 1) {
        return false;
    }
    if (s_line < 0) s_line = 0;
    if (e_line < 0) e_line = 0;
    if (s_line >= state->line_count) s_line = state->line_count - 1;
    if (e_line >= state->line_count) e_line = state->line_count - 1;

    int s_len = strlen(state->lines[s_line]);
    int e_len = strlen(state->lines[e_line]);
    if (s_col < 0) s_col = 0;
    if (e_col < 0) e_col = 0;
    if (s_col > s_len) s_col = s_len;
    if (e_col > e_len) e_col = e_len;

    if (s_line == e_line && s_col == e_col) {
        return false;
    }

    *start_line = s_line;
    *start_col = s_col;
    *end_line = e_line;
    *end_col = e_col;
    return true;
}

static void editor_draw_selection_text(window_t* win, const char* line, int start_col, int end_col,
                                       int y) {
    int line_len = strlen(line);
    if (start_col < 0) start_col = 0;
    if (end_col > line_len) end_col = line_len;
    int count = end_col - start_col;
    if (count <= 0) {
        return;
    }
    char segment[EDITOR_MAX_LINE_LENGTH];
    if (count >= (int)sizeof(segment)) {
        count = (int)sizeof(segment) - 1;
    }
    memcpy(segment, line + start_col, (size_t)count);
    segment[count] = '\0';
    window_print(win, EDITOR_TEXT_X + start_col * EDITOR_TEXT_CHAR_WIDTH, y, segment, COLOR_WHITE);
}

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
    int visible_lines = text_height / EDITOR_TEXT_LINE_HEIGHT;

    int sel_start_line = 0;
    int sel_start_col = 0;
    int sel_end_line = 0;
    int sel_end_col = 0;
    bool has_selection = false;
    if (state->selection_active && !editor_selection_empty(state)) {
        editor_normalize_selection(state, &sel_start_line, &sel_start_col,
                                   &sel_end_line, &sel_end_col);
        has_selection = true;
    }
    
    for (int i = 0; i < visible_lines && (i + state->scroll_offset) < state->line_count; i++) {
        int line_idx = i + state->scroll_offset;
        int y = text_start_y + (i * EDITOR_TEXT_LINE_HEIGHT);
        const char* line = state->lines[line_idx];
        int line_len = strlen(line);

        if (has_selection && line_idx >= sel_start_line && line_idx <= sel_end_line) {
            int start_col = (line_idx == sel_start_line) ? sel_start_col : 0;
            int end_col = (line_idx == sel_end_line) ? sel_end_col : line_len;
            if (start_col < 0) start_col = 0;
            if (end_col > line_len) end_col = line_len;
            if (end_col > start_col) {
                int rect_x = EDITOR_TEXT_X + start_col * EDITOR_TEXT_CHAR_WIDTH;
                int rect_w = (end_col - start_col) * EDITOR_TEXT_CHAR_WIDTH;
                window_fill_rect(win, rect_x, y, rect_w, EDITOR_TEXT_CHAR_HEIGHT,
                                 COLOR_LIGHT_BLUE);
            }
        }
        
        // Draw line text
        window_print(win, EDITOR_TEXT_X, y, line, COLOR_BLACK);

        if (has_selection && line_idx >= sel_start_line && line_idx <= sel_end_line) {
            int start_col = (line_idx == sel_start_line) ? sel_start_col : 0;
            int end_col = (line_idx == sel_end_line) ? sel_end_col : line_len;
            editor_draw_selection_text(win, line, start_col, end_col, y);
        }
        
        // Draw cursor if on this line
        if (line_idx == state->cursor_line) {
            int cursor_x = EDITOR_TEXT_X + (state->cursor_col * EDITOR_TEXT_CHAR_WIDTH);
            window_fill_rect(win, cursor_x, y, 2, EDITOR_TEXT_CHAR_HEIGHT, COLOR_BLACK);
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

static void editor_copy_line(editor_state_t* state) {
    if (!state) return;
    if (state->cursor_line < 0 || state->cursor_line >= state->line_count) return;
    uwm_clipboard_set(state->lines[state->cursor_line]);
}

static void editor_cut_line(editor_state_t* state) {
    if (!state) return;
    if (state->cursor_line < 0 || state->cursor_line >= state->line_count) return;
    editor_copy_line(state);

    if (state->line_count <= 1) {
        state->lines[0][0] = '\0';
        state->cursor_line = 0;
        state->cursor_col = 0;
    } else {
        for (int i = state->cursor_line; i < state->line_count - 1; i++) {
            strcpy(state->lines[i], state->lines[i + 1]);
        }
        state->lines[state->line_count - 1][0] = '\0';
        state->line_count--;
        if (state->cursor_line >= state->line_count) {
            state->cursor_line = state->line_count - 1;
        }
        int line_len = strlen(state->lines[state->cursor_line]);
        if (state->cursor_col > line_len) state->cursor_col = line_len;
    }
    state->modified = true;
}

static void editor_copy_selection(editor_state_t* state) {
    int s_line = 0;
    int s_col = 0;
    int e_line = 0;
    int e_col = 0;
    if (!editor_get_selection(state, &s_line, &s_col, &e_line, &e_col)) {
        editor_copy_line(state);
        return;
    }

    char clip[256];
    int pos = 0;
    for (int line = s_line; line <= e_line && pos < (int)sizeof(clip) - 1; line++) {
        const char* text = state->lines[line];
        int len = strlen(text);
        int start = (line == s_line) ? s_col : 0;
        int end = (line == e_line) ? e_col : len;
        if (start < 0) start = 0;
        if (end > len) end = len;
        for (int i = start; i < end && pos < (int)sizeof(clip) - 1; i++) {
            clip[pos++] = text[i];
        }
        if (line != e_line && pos < (int)sizeof(clip) - 1) {
            clip[pos++] = '\n';
        }
    }
    clip[pos] = '\0';
    uwm_clipboard_set(clip);
}

static void editor_delete_selection(editor_state_t* state) {
    int s_line = 0;
    int s_col = 0;
    int e_line = 0;
    int e_col = 0;
    if (!editor_get_selection(state, &s_line, &s_col, &e_line, &e_col)) {
        return;
    }

    if (s_line == e_line) {
        char* line = state->lines[s_line];
        int len = strlen(line);
        if (e_col > len) e_col = len;
        if (s_col < 0) s_col = 0;
        for (int i = e_col; i <= len; i++) {
            line[s_col + (i - e_col)] = line[i];
        }
    } else {
        char merged[EDITOR_MAX_LINE_LENGTH];
        char* first = state->lines[s_line];
        char* last = state->lines[e_line];
        int first_len = strlen(first);
        int last_len = strlen(last);
        if (s_col > first_len) s_col = first_len;
        if (e_col > last_len) e_col = last_len;

        int prefix_len = s_col;
        int suffix_len = last_len - e_col;
        if (prefix_len < 0) prefix_len = 0;
        if (suffix_len < 0) suffix_len = 0;
        if (prefix_len >= EDITOR_MAX_LINE_LENGTH) {
            prefix_len = EDITOR_MAX_LINE_LENGTH - 1;
            suffix_len = 0;
        }
        if (prefix_len + suffix_len >= EDITOR_MAX_LINE_LENGTH) {
            suffix_len = EDITOR_MAX_LINE_LENGTH - 1 - prefix_len;
        }

        if (prefix_len > 0) {
            memcpy(merged, first, (size_t)prefix_len);
        }
        if (suffix_len > 0) {
            memcpy(merged + prefix_len, last + e_col, (size_t)suffix_len);
        }
        merged[prefix_len + suffix_len] = '\0';
        strcpy(state->lines[s_line], merged);

        int remove_count = e_line - s_line;
        for (int i = s_line + 1; i + remove_count < state->line_count; i++) {
            strcpy(state->lines[i], state->lines[i + remove_count]);
        }
        for (int i = state->line_count - remove_count; i < state->line_count; i++) {
            if (i >= 0 && i < EDITOR_MAX_LINES) {
                state->lines[i][0] = '\0';
            }
        }
        state->line_count -= remove_count;
        if (state->line_count < 1) {
            state->line_count = 1;
            state->lines[0][0] = '\0';
        }
    }

    state->cursor_line = s_line;
    if (state->cursor_line >= state->line_count) {
        state->cursor_line = state->line_count - 1;
    }
    int line_len = strlen(state->lines[state->cursor_line]);
    if (s_col > line_len) s_col = line_len;
    if (s_col < 0) s_col = 0;
    state->cursor_col = s_col;
    state->modified = true;
    editor_clear_selection(state);
}

static void editor_paste_clipboard(editor_state_t* state) {
    if (!state) return;
    char clip[256];
    int len = uwm_clipboard_get(clip, sizeof(clip));
    if (len <= 0) return;
    for (int i = 0; clip[i] != '\0'; i++) {
        if (clip[i] == '\n') {
            editor_new_line(state);
        } else if (clip[i] == '\r') {
            continue;
        } else {
            editor_insert_char(state, clip[i]);
        }
    }
}

static void editor_click(window_t* win, int x, int y, int buttons) {
    editor_state_t* state = (editor_state_t*)window_get_user_data(win);
    int content_h = window_content_height(win);
    
    // Check menu bar click
    if (y < EDITOR_MENU_HEIGHT) {
        if (x >= 3 && x < 30) {
            // Toggle File menu
            state->menu_open = !state->menu_open;
            editor_clear_selection(state);
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
            editor_clear_selection(state);
            editor_redraw(win);
            file_dialog_show_open("Open File", "/", editor_file_open_callback, state);
        } else if (item == 1) {
            // Save
            editor_save_file(state);
            state->menu_open = false;
            editor_clear_selection(state);
            editor_redraw(win);
        } else if (item == 2) {
            // Save As - show file dialog
            state->menu_open = false;
            editor_clear_selection(state);
            editor_redraw(win);
            const char* default_name = state->has_filename ? state->filename : "document.txt";
            file_dialog_show_save("Save File As", default_name, editor_file_save_callback, state);
        } else if (item == 3) {
            // New
            editor_new_file(state);
            state->menu_open = false;
            editor_clear_selection(state);
            editor_redraw(win);
        } else if (item == 4) {
            // Close menu
            state->menu_open = false;
            editor_clear_selection(state);
            editor_redraw(win);
        }
        return;
    }
    
    // Close menu if clicking elsewhere
    if (state->menu_open) {
        state->menu_open = false;
        editor_clear_selection(state);
        editor_redraw(win);
        return;
    }
    
    int text_start_y = EDITOR_MENU_HEIGHT + 4;
    int status_y = content_h - 14;
    
    // Check if click is in text area
    if ((buttons & MOUSE_LEFT_BUTTON) && y >= text_start_y && y < status_y) {
        int line = 0;
        int col = 0;
        editor_point_to_cursor(state, x, y, content_h, &line, &col);
        state->cursor_line = line;
        state->cursor_col = col;
        state->selecting = true;
        state->selection_active = true;
        state->sel_anchor_line = line;
        state->sel_anchor_col = col;
        state->sel_end_line = line;
        state->sel_end_col = col;
        editor_redraw(win);
        return;
    }

    editor_clear_selection(state);
    editor_redraw(win);
}

static void editor_handle_mouse_move(window_t* win, int x, int y, int buttons) {
    editor_state_t* state = (editor_state_t*)window_get_user_data(win);
    int content_h = window_content_height(win);
    int text_start_y = EDITOR_MENU_HEIGHT + 4;
    int status_y = content_h - 14;

    if (state->selecting && (buttons & MOUSE_LEFT_BUTTON)) {
        int line = 0;
        int col = 0;
        int clamped_y = y;
        if (clamped_y < text_start_y) clamped_y = text_start_y;
        if (clamped_y >= status_y) clamped_y = status_y - 1;
        editor_point_to_cursor(state, x, clamped_y, content_h, &line, &col);
        state->cursor_line = line;
        state->cursor_col = col;
        state->selection_active = true;
        state->sel_end_line = line;
        state->sel_end_col = col;
        editor_redraw(win);
        return;
    }
    
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

static void editor_mouse_up(window_t* win, int x, int y, int buttons) {
    (void)x;
    (void)y;
    (void)buttons;
    editor_state_t* state = (editor_state_t*)window_get_user_data(win);
    if (!state) return;
    if (state->selecting) {
        state->selecting = false;
        if (editor_selection_empty(state)) {
            state->selection_active = false;
        }
        editor_redraw(win);
    }
}

static void editor_key(window_t* win, int c) {
    editor_state_t* state = (editor_state_t*)window_get_user_data(win);
    int content_h = window_content_height(win);
    bool needs_redraw = false;
    
    // Cast to unsigned to handle arrow keys correctly
    unsigned char uc = (unsigned char)c;
    
    // Normal text editing mode
    if (c == 0x03) {
        if (editor_has_selection(state)) {
            editor_copy_selection(state);
        } else {
            editor_copy_line(state);
        }
        needs_redraw = true;
    } else if (c == 0x18) {
        if (editor_has_selection(state)) {
            editor_copy_selection(state);
            editor_delete_selection(state);
        } else {
            editor_cut_line(state);
        }
        needs_redraw = true;
    } else if (c == 0x16) {
        if (editor_has_selection(state)) {
            editor_delete_selection(state);
        }
        editor_paste_clipboard(state);
        needs_redraw = true;
    } else if (c == '\n' || c == '\r') {
        // Enter key - new line
        if (editor_has_selection(state)) {
            editor_delete_selection(state);
        }
        editor_new_line(state);
        needs_redraw = true;
    } else if (c == 8 || c == 127) {
        // Backspace
        if (editor_has_selection(state)) {
            editor_delete_selection(state);
        } else {
            editor_delete_char(state);
        }
        needs_redraw = true;
    } else if (c >= 32 && c <= 126) {
        // Printable character
        if (editor_has_selection(state)) {
            editor_delete_selection(state);
        }
        editor_insert_char(state, c);
        needs_redraw = true;
    } else if (uc == 0x80) {  // Up arrow
        if (editor_has_selection(state)) {
            editor_clear_selection(state);
            needs_redraw = true;
        }
        if (state->cursor_line > 0) {
            state->cursor_line--;
            int line_len = strlen(state->lines[state->cursor_line]);
            if (state->cursor_col > line_len) state->cursor_col = line_len;
            if (state->cursor_line < state->scroll_offset) state->scroll_offset--;
            needs_redraw = true;
        }
    } else if (uc == 0x81) {  // Down arrow
        if (editor_has_selection(state)) {
            editor_clear_selection(state);
            needs_redraw = true;
        }
        if (state->cursor_line < state->line_count - 1) {
            state->cursor_line++;
            int line_len = strlen(state->lines[state->cursor_line]);
            if (state->cursor_col > line_len) state->cursor_col = line_len;
            int visible_lines = (content_h - 32) / EDITOR_TEXT_LINE_HEIGHT;
            if (state->cursor_line >= state->scroll_offset + visible_lines) state->scroll_offset++;
            needs_redraw = true;
        }
    } else if (uc == 0x82) {  // Left arrow
        if (editor_has_selection(state)) {
            editor_clear_selection(state);
            needs_redraw = true;
        }
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
        if (editor_has_selection(state)) {
            editor_clear_selection(state);
            needs_redraw = true;
        }
        int line_len = strlen(state->lines[state->cursor_line]);
        if (state->cursor_col < line_len) {
            state->cursor_col++;
            needs_redraw = true;
        } else if (state->cursor_line < state->line_count - 1) {
            state->cursor_line++;
            state->cursor_col = 0;
            int visible_lines = (content_h - 32) / EDITOR_TEXT_LINE_HEIGHT;
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
    editor_state.selecting = false;
    editor_state.selection_active = false;
    editor_state.sel_anchor_line = 0;
    editor_state.sel_anchor_col = 0;
    editor_state.sel_end_line = 0;
    editor_state.sel_end_col = 0;
    editor_state.window = win;

    window_set_handlers(win, editor_redraw, editor_click, editor_mouse_up,
                        editor_handle_mouse_move, NULL, editor_key, &editor_state);

    editor_window = win;
    return win;
}
