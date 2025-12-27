#include <dirent.h>
#include <file_dialog.h>
#include <gui_window.h>
#include <graphics.h>
#include <mouse.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define FD_MAX_FILES 50
#define FD_ITEM_HEIGHT 16
#define FD_LIST_HEIGHT 40
#define FD_BUTTON_WIDTH 60
#define FD_BUTTON_HEIGHT 20
#define FD_INPUT_HEIGHT 20

typedef struct {
    char name[32];
    bool is_directory;
} fd_entry_t;

typedef struct {
    window_t* window;
    file_dialog_type_t type;
    file_dialog_callback_t callback;
    void* user_data;
    
    char current_path[64];
    char input_buffer[64];
    int input_cursor;
    bool input_selecting;
    int input_sel_anchor;
    int input_sel_end;
    
    fd_entry_t files[FD_MAX_FILES];
    int file_count;
    int selected_index;
    int scroll_offset;
    int last_clicked_index;
    
    bool input_focused;
} file_dialog_t;

static file_dialog_t* active_dialog = NULL;
static file_dialog_t dialog_storage;

// Forward declarations
static void fd_refresh_list(file_dialog_t* dialog);
static void fd_draw_ui(file_dialog_t* dialog);
static void fd_navigate_to(file_dialog_t* dialog, const char* name);
static void fd_confirm(file_dialog_t* dialog);
static void fd_cancel(file_dialog_t* dialog);

static void fd_input_clear_selection(file_dialog_t* dialog) {
    if (!dialog) return;
    dialog->input_selecting = false;
    dialog->input_sel_anchor = dialog->input_cursor;
    dialog->input_sel_end = dialog->input_cursor;
}

static bool fd_input_has_selection(const file_dialog_t* dialog) {
    return dialog && dialog->input_sel_anchor != dialog->input_sel_end;
}

static void fd_input_normalize_selection(const file_dialog_t* dialog, int* start, int* end) {
    int a = dialog->input_sel_anchor;
    int b = dialog->input_sel_end;
    if (b < a) {
        int tmp = a;
        a = b;
        b = tmp;
    }
    *start = a;
    *end = b;
}

static bool fd_input_get_selection(file_dialog_t* dialog,
                                   int* start, int* end) {
    if (!fd_input_has_selection(dialog)) {
        return false;
    }
    int s = 0;
    int e = 0;
    fd_input_normalize_selection(dialog, &s, &e);
    int len = strlen(dialog->input_buffer);
    if (s < 0) s = 0;
    if (e > len) e = len;
    if (s == e) {
        return false;
    }
    *start = s;
    *end = e;
    return true;
}

static void fd_input_set_cursor_from_x(file_dialog_t* dialog, int x, int text_x) {
    int len = strlen(dialog->input_buffer);
    int col = (x - text_x) / 8;
    if (col < 0) col = 0;
    if (col > len) col = len;
    dialog->input_cursor = col;
}

static void fd_input_copy_selection(file_dialog_t* dialog) {
    int start = 0;
    int end = 0;
    if (!fd_input_get_selection(dialog, &start, &end)) {
        uwm_clipboard_set(dialog->input_buffer);
        return;
    }
    int count = end - start;
    if (count <= 0) {
        return;
    }
    char clip[sizeof(dialog->input_buffer)];
    if (count >= (int)sizeof(clip)) {
        count = (int)sizeof(clip) - 1;
    }
    memcpy(clip, dialog->input_buffer + start, (size_t)count);
    clip[count] = '\0';
    uwm_clipboard_set(clip);
}

static void fd_input_delete_selection(file_dialog_t* dialog) {
    int start = 0;
    int end = 0;
    if (!fd_input_get_selection(dialog, &start, &end)) {
        return;
    }
    int len = strlen(dialog->input_buffer);
    if (end > len) end = len;
    if (start < 0) start = 0;
    for (int i = end; i <= len; i++) {
        dialog->input_buffer[start + (i - end)] = dialog->input_buffer[i];
    }
    dialog->input_cursor = start;
    fd_input_clear_selection(dialog);
}

// Refresh file list
static void fd_refresh_list(file_dialog_t* dialog) {
    if (!dialog) return;
    
    dialog->file_count = 0;
    
    // Add parent directory if not at root
    if (strcmp(dialog->current_path, "/") != 0) {
        strcpy(dialog->files[dialog->file_count].name, "..");
        dialog->files[dialog->file_count].is_directory = true;
        dialog->file_count++;
    }
    
    // List files
    struct dirent entries[FD_MAX_FILES];
    int count = listdir(dialog->current_path, entries, FD_MAX_FILES);
    if (count < 0) {
        count = 0;
    }
    
    for (int i = 0; i < count && dialog->file_count < FD_MAX_FILES; i++) {
        strncpy(dialog->files[dialog->file_count].name, entries[i].d_name, 31);
        dialog->files[dialog->file_count].name[31] = '\0';

        dialog->files[dialog->file_count].is_directory = (entries[i].d_type == 2);
        dialog->file_count++;
    }
}

// Draw the dialog UI
static void fd_draw_ui(file_dialog_t* dialog) {
    if (!dialog || !dialog->window) return;
    
    window_t* win = dialog->window;
    int content_w = window_content_width(win);
    window_clear_content(win, COLOR_LIGHT_GRAY);
    
    int y = 10;
    
    // Title based on dialog type
    const char* title_text = (dialog->type == FILE_DIALOG_OPEN) ? "Open File" : "Save File";
    window_print(win, 10, y, title_text, COLOR_BLACK);
    y += 20;
    
    // Current path
    char path_text[70];
    snprintf(path_text, sizeof(path_text), "Path: %s", dialog->current_path);
    window_print(win, 10, y, path_text, COLOR_BLACK);
    y += 20;
    
    // File list area
    int list_y = y;
    int list_width = content_w - 30; // Leave room for scroll bar
    window_fill_rect(win, 10, list_y, list_width, FD_LIST_HEIGHT, COLOR_WHITE);
    window_draw_rect(win, 10, list_y, list_width, FD_LIST_HEIGHT, COLOR_BLACK);
    
    // Draw files
    int visible_items = FD_LIST_HEIGHT / FD_ITEM_HEIGHT;
    for (int i = dialog->scroll_offset; 
         i < dialog->file_count && i < dialog->scroll_offset + visible_items; 
         i++) {
        int item_y = list_y + (i - dialog->scroll_offset) * FD_ITEM_HEIGHT + 2;
        
        // Highlight selection
        if (i == dialog->selected_index) {
            window_fill_rect(win, 11, item_y, list_width - 2, FD_ITEM_HEIGHT - 1, COLOR_LIGHT_BLUE);
        }
        
        // Draw name
        char display[40];
        if (dialog->files[i].is_directory) {
            snprintf(display, sizeof(display), "[DIR] %s", dialog->files[i].name);
        } else {
            snprintf(display, sizeof(display), "      %s", dialog->files[i].name);
        }
        window_print(win, 15, item_y + 4, display, COLOR_BLACK);
    }
    
    // Draw scroll bar if needed
    if (dialog->file_count > visible_items) {
        int scrollbar_x = 10 + list_width + 2;
        int scrollbar_width = 8;
        
        // Scroll bar background
        window_fill_rect(win, scrollbar_x, list_y, scrollbar_width, FD_LIST_HEIGHT, COLOR_LIGHT_GRAY);
        window_draw_rect(win, scrollbar_x, list_y, scrollbar_width, FD_LIST_HEIGHT, COLOR_BLACK);
        
        // Scroll bar thumb
        int thumb_height = (visible_items * FD_LIST_HEIGHT) / dialog->file_count;
        if (thumb_height < 10) thumb_height = 10;
        int thumb_y = list_y + (dialog->scroll_offset * (FD_LIST_HEIGHT - thumb_height)) / (dialog->file_count - visible_items);
        
        window_fill_rect(win, scrollbar_x + 1, thumb_y, scrollbar_width - 2, thumb_height, COLOR_BLUE);
    }
    
    y = list_y + FD_LIST_HEIGHT + 10;
    
    // Filename input (for save dialog or manual entry)
    window_print(win, 10, y, "File:", COLOR_BLACK);
    y += 15;
    
    window_fill_rect(win, 10, y, content_w - 20, FD_INPUT_HEIGHT, COLOR_WHITE);
    window_draw_rect(win, 10, y, content_w - 20, FD_INPUT_HEIGHT,
                     dialog->input_focused ? COLOR_BLUE : COLOR_BLACK);

    if (dialog->input_focused && fd_input_has_selection(dialog)) {
        int start = 0;
        int end = 0;
        fd_input_normalize_selection(dialog, &start, &end);
        int len = strlen(dialog->input_buffer);
        if (start < 0) start = 0;
        if (end > len) end = len;
        if (end > start) {
            int rect_x = 15 + start * 8;
            int rect_w = (end - start) * 8;
            window_fill_rect(win, rect_x, y + 4, rect_w, 12, COLOR_LIGHT_BLUE);
        }
    }

    window_print(win, 15, y + 6, dialog->input_buffer, COLOR_BLACK);

    if (dialog->input_focused && fd_input_has_selection(dialog)) {
        int start = 0;
        int end = 0;
        fd_input_normalize_selection(dialog, &start, &end);
        int len = strlen(dialog->input_buffer);
        if (start < 0) start = 0;
        if (end > len) end = len;
        int count = end - start;
        if (count > 0) {
            char segment[sizeof(dialog->input_buffer)];
            if (count >= (int)sizeof(segment)) {
                count = (int)sizeof(segment) - 1;
            }
            memcpy(segment, dialog->input_buffer + start, (size_t)count);
            segment[count] = '\0';
            window_print(win, 15 + start * 8, y + 6, segment, COLOR_WHITE);
        }
    }
    
    // Draw cursor if focused
    if (dialog->input_focused) {
        int cursor_x = 15 + dialog->input_cursor * 8;
        window_fill_rect(win, cursor_x, y + 4, 2, 12, COLOR_BLACK);
    }
    
    y += FD_INPUT_HEIGHT + 10;
    
    // Buttons
    int button_y = y;
    int button_spacing = 10;
    int ok_x = content_w / 2 - FD_BUTTON_WIDTH - button_spacing / 2;
    int cancel_x = content_w / 2 + button_spacing / 2;
    
    // OK/Save/Open button
    const char* button_text = (dialog->type == FILE_DIALOG_SAVE) ? "Save" : "Open";
    int button_text_offset = (dialog->type == FILE_DIALOG_SAVE) ? 16 : 16;
    window_fill_rect(win, ok_x, button_y, FD_BUTTON_WIDTH, FD_BUTTON_HEIGHT, COLOR_LIGHT_GREEN);
    window_draw_rect(win, ok_x, button_y, FD_BUTTON_WIDTH, FD_BUTTON_HEIGHT, COLOR_BLACK);
    window_print(win, ok_x + button_text_offset, button_y + 6, button_text, COLOR_BLACK);
    
    // Cancel button
    window_fill_rect(win, cancel_x, button_y, FD_BUTTON_WIDTH, FD_BUTTON_HEIGHT, COLOR_LIGHT_RED);
    window_draw_rect(win, cancel_x, button_y, FD_BUTTON_WIDTH, FD_BUTTON_HEIGHT, COLOR_BLACK);
    window_print(win, cancel_x + 12, button_y + 6, "Cancel", COLOR_BLACK);
    
    window_draw(win);
}

static void fd_on_draw(window_t* win) {
    file_dialog_t* dialog = (file_dialog_t*)window_get_user_data(win);
    if (dialog) {
        fd_draw_ui(dialog);
    }
}

// Navigate to directory or select file
static void fd_navigate_to(file_dialog_t* dialog, const char* name) {
    if (!dialog) return;
    
    // Find entry
    fd_entry_t* entry = NULL;
    for (int i = 0; i < dialog->file_count; i++) {
        if (strcmp(dialog->files[i].name, name) == 0) {
            entry = &dialog->files[i];
            break;
        }
    }
    
    if (!entry) return;
    
    if (entry->is_directory) {
        // Navigate to directory
        if (strcmp(name, "..") == 0) {
            // Go up
            char* last_slash = strrchr(dialog->current_path, '/');
            if (last_slash && last_slash != dialog->current_path) {
                *last_slash = '\0';
            } else {
                strcpy(dialog->current_path, "/");
            }
        } else {
            // Go down
            if (strcmp(dialog->current_path, "/") != 0) {
                strcat(dialog->current_path, "/");
            }
            strcat(dialog->current_path, name);
        }
        
        dialog->selected_index = -1;
        dialog->scroll_offset = 0;
        fd_refresh_list(dialog);
        fd_input_clear_selection(dialog);
        fd_draw_ui(dialog);
    } else {
        // Select file
        strncpy(dialog->input_buffer, name, sizeof(dialog->input_buffer) - 1);
        dialog->input_buffer[sizeof(dialog->input_buffer) - 1] = '\0';
        dialog->input_cursor = strlen(dialog->input_buffer);
        fd_input_clear_selection(dialog);
        fd_draw_ui(dialog);
    }
}

// Confirm selection
static void fd_confirm(file_dialog_t* dialog) {
    if (!dialog) return;
    
    // Build full path
    char full_path[128];
    if (dialog->input_buffer[0] == '\0') {
        // No filename entered
        return;
    }
    
    if (strcmp(dialog->current_path, "/") == 0) {
        snprintf(full_path, sizeof(full_path), "/%s", dialog->input_buffer);
    } else {
        snprintf(full_path, sizeof(full_path), "%s/%s", dialog->current_path, dialog->input_buffer);
    }
    
    // Save callback and user_data before destroying dialog
    file_dialog_callback_t callback = dialog->callback;
    void* user_data = dialog->user_data;
    
    // Clear user_data pointer in window before destroy
    window_set_user_data(dialog->window, NULL);
    
    // Close dialog first
    window_destroy(dialog->window);
    active_dialog = NULL;
    
    // Then call callback
    if (callback) {
        callback(full_path, user_data);
    }
}

static void fd_input_insert(file_dialog_t* dialog, const char* text) {
	if (!dialog || !text) return;

	char filtered[sizeof(dialog->input_buffer)];
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

	int len = strlen(dialog->input_buffer);
	int cur = dialog->input_cursor;
	if (cur < 0) cur = 0;
	if (cur > len) cur = len;

	int space = (int)sizeof(dialog->input_buffer) - 1 - len;
	int insert_len = strlen(filtered);
	if (space <= 0) {
		return;
	}
	if (insert_len > space) {
		insert_len = space;
	}

	for (int i = len; i >= cur; i--) {
		dialog->input_buffer[i + insert_len] = dialog->input_buffer[i];
	}
	memcpy(dialog->input_buffer + cur, filtered, (size_t)insert_len);
	dialog->input_cursor = cur + insert_len;
}

// Cancel dialog
static void fd_cancel(file_dialog_t* dialog) {
    if (!dialog) return;
    
    // Save callback and user_data before destroying dialog
    file_dialog_callback_t callback = dialog->callback;
    void* user_data = dialog->user_data;
    
    // Clear user_data pointer in window before destroy
    window_set_user_data(dialog->window, NULL);
    
    // Close dialog first
    window_destroy(dialog->window);
    active_dialog = NULL;
    
    // Call callback with NULL
    if (callback) {
        callback(NULL, user_data);
    }
}

// Click handler
static void fd_on_click(window_t* win, int x, int y, int buttons) {
    file_dialog_t* dialog = (file_dialog_t*)window_get_user_data(win);
    if (!dialog) return;
    int content_w = window_content_width(win);
    
    // Calculate positions matching fd_draw_ui
    int list_y = 50;  // 10 (start) + 20 (title) + 20 (path)
    int list_width = content_w - 30;
    int input_y = list_y + FD_LIST_HEIGHT + 25;
    int button_y = input_y + FD_INPUT_HEIGHT + 10;
    
    // Check scrollbar click
    int scrollbar_x = 10 + list_width + 2;
    int visible_items = FD_LIST_HEIGHT / FD_ITEM_HEIGHT;
    if (dialog->file_count > visible_items &&
        x >= scrollbar_x && x < scrollbar_x + 8 &&
        y >= list_y && y < list_y + FD_LIST_HEIGHT) {
        
        // Calculate scroll position from click y
        int relative_y = y - list_y;
        int max_scroll = dialog->file_count - visible_items;
        int new_scroll = (relative_y * max_scroll) / FD_LIST_HEIGHT;
        
        if (new_scroll < 0) new_scroll = 0;
        if (new_scroll > max_scroll) new_scroll = max_scroll;
        
        dialog->scroll_offset = new_scroll;
        
        // Adjust selection to stay visible
        if (dialog->selected_index < dialog->scroll_offset) {
            dialog->selected_index = dialog->scroll_offset;
        } else if (dialog->selected_index >= dialog->scroll_offset + visible_items) {
            dialog->selected_index = dialog->scroll_offset + visible_items - 1;
        }
        
        fd_draw_ui(dialog);
        return;
    }
    
    // Check file list click
    if (y >= list_y && y < list_y + FD_LIST_HEIGHT &&
        x >= 10 && x < 10 + list_width) {
        
        int clicked_index = (y - list_y) / FD_ITEM_HEIGHT + dialog->scroll_offset;
        
        if (clicked_index >= 0 && clicked_index < dialog->file_count) {
            // Check for double-click
            if (dialog->last_clicked_index == clicked_index && dialog->selected_index == clicked_index) {
                // Double-click: navigate directory or confirm file selection
                if (dialog->files[clicked_index].is_directory) {
                    fd_navigate_to(dialog, dialog->files[clicked_index].name);
                } else {
                    fd_confirm(dialog);
                }
                dialog->last_clicked_index = -1;
                return;
            }
            
            // Single click: select the clicked item
            dialog->selected_index = clicked_index;
            dialog->last_clicked_index = clicked_index;
            dialog->input_focused = false;
            fd_input_clear_selection(dialog);
            
            // Copy filename to input buffer if it's a file
            if (!dialog->files[clicked_index].is_directory) {
                strncpy(dialog->input_buffer, dialog->files[clicked_index].name, sizeof(dialog->input_buffer) - 1);
                dialog->input_buffer[sizeof(dialog->input_buffer) - 1] = '\0';
                dialog->input_cursor = strlen(dialog->input_buffer);
            }
            
            fd_draw_ui(dialog);
        }
        return;
    }
    
    // Check input box click
    if (y >= input_y && y < input_y + FD_INPUT_HEIGHT &&
        x >= 10 && x < content_w - 10) {
        dialog->input_focused = true;
        if (buttons & MOUSE_LEFT_BUTTON) {
            fd_input_set_cursor_from_x(dialog, x, 15);
            dialog->input_selecting = true;
            dialog->input_sel_anchor = dialog->input_cursor;
            dialog->input_sel_end = dialog->input_cursor;
        } else {
            fd_input_clear_selection(dialog);
        }
        fd_draw_ui(dialog);
        return;
    }
    
    // Check button clicks
    int ok_x = content_w / 2 - FD_BUTTON_WIDTH - 5;
    int cancel_x = content_w / 2 + 5;
    
    if (y >= button_y && y < button_y + FD_BUTTON_HEIGHT) {
        if (x >= ok_x && x < ok_x + FD_BUTTON_WIDTH) {
            fd_confirm(dialog);
            return;
        }
        if (x >= cancel_x && x < cancel_x + FD_BUTTON_WIDTH) {
            fd_cancel(dialog);
            return;
        }
    }

    if (dialog->input_focused) {
        dialog->input_focused = false;
        fd_input_clear_selection(dialog);
        fd_draw_ui(dialog);
    }
}

static void fd_on_mouse_move(window_t* win, int x, int y, int buttons) {
    file_dialog_t* dialog = (file_dialog_t*)window_get_user_data(win);
    (void)y;
    if (!dialog) return;
    if (dialog->input_focused && dialog->input_selecting && (buttons & MOUSE_LEFT_BUTTON)) {
        fd_input_set_cursor_from_x(dialog, x, 15);
        dialog->input_sel_end = dialog->input_cursor;
        fd_draw_ui(dialog);
    }
}

static void fd_on_mouse_up(window_t* win, int x, int y, int buttons) {
    (void)x;
    (void)y;
    (void)buttons;
    file_dialog_t* dialog = (file_dialog_t*)window_get_user_data(win);
    if (!dialog) return;
    if (dialog->input_selecting) {
        dialog->input_selecting = false;
        if (!fd_input_has_selection(dialog)) {
            fd_input_clear_selection(dialog);
        }
        fd_draw_ui(dialog);
    }
}

// Key handler
static void fd_on_key(window_t* win, int key) {
    file_dialog_t* dialog = (file_dialog_t*)window_get_user_data(win);
    if (!dialog) return;
    
    // Up/Down arrows always navigate list
    if ((uint8_t)key == 0x80) { // Up arrow
        if (dialog->selected_index > 0) {
            dialog->selected_index--;
            if (dialog->selected_index < dialog->scroll_offset) {
                dialog->scroll_offset = dialog->selected_index;
            }
            fd_draw_ui(dialog);
        }
        return;
    } else if ((uint8_t)key == 0x81) { // Down arrow
        if (dialog->selected_index < dialog->file_count - 1) {
            dialog->selected_index++;
            int visible_items = FD_LIST_HEIGHT / FD_ITEM_HEIGHT;
            if (dialog->selected_index >= dialog->scroll_offset + visible_items) {
                dialog->scroll_offset = dialog->selected_index - visible_items + 1;
            }
            fd_draw_ui(dialog);
        }
        return;
    }
    
    if (dialog->input_focused) {
        // Handle input
        if (key == 0x03) {
            fd_input_copy_selection(dialog);
            fd_draw_ui(dialog);
        } else if (key == 0x18) {
            fd_input_copy_selection(dialog);
            if (fd_input_has_selection(dialog)) {
                fd_input_delete_selection(dialog);
            } else {
                dialog->input_buffer[0] = '\0';
                dialog->input_cursor = 0;
                fd_input_clear_selection(dialog);
            }
            fd_draw_ui(dialog);
        } else if (key == 0x16) {
            char clip[64];
            if (uwm_clipboard_get(clip, sizeof(clip)) > 0) {
                if (fd_input_has_selection(dialog)) {
                    fd_input_delete_selection(dialog);
                }
                fd_input_insert(dialog, clip);
                fd_draw_ui(dialog);
            }
        } else if (key == '\n') {
            fd_confirm(dialog);
        } else if (key == '\b') {
            // Backspace - delete character before cursor
            if (fd_input_has_selection(dialog)) {
                fd_input_delete_selection(dialog);
                fd_draw_ui(dialog);
            } else if (dialog->input_cursor > 0) {
                int len = strlen(dialog->input_buffer);
                for (int i = dialog->input_cursor - 1; i < len; i++) {
                    dialog->input_buffer[i] = dialog->input_buffer[i + 1];
                }
                dialog->input_cursor--;
                fd_draw_ui(dialog);
            }
        } else if (key == 0x7F) {
            // Delete - delete character at cursor
            if (fd_input_has_selection(dialog)) {
                fd_input_delete_selection(dialog);
                fd_draw_ui(dialog);
            } else {
                int len = strlen(dialog->input_buffer);
                if (dialog->input_cursor < len) {
                    for (int i = dialog->input_cursor; i < len; i++) {
                        dialog->input_buffer[i] = dialog->input_buffer[i + 1];
                    }
                    fd_draw_ui(dialog);
                }
            }
        } else if ((uint8_t)key == 0x82) {
            // Left arrow
            if (fd_input_has_selection(dialog)) {
                int start = 0;
                int end = 0;
                fd_input_normalize_selection(dialog, &start, &end);
                dialog->input_cursor = start;
                fd_input_clear_selection(dialog);
                fd_draw_ui(dialog);
            } else if (dialog->input_cursor > 0) {
                dialog->input_cursor--;
                fd_draw_ui(dialog);
            }
        } else if ((uint8_t)key == 0x83) {
            // Right arrow
            if (fd_input_has_selection(dialog)) {
                int start = 0;
                int end = 0;
                fd_input_normalize_selection(dialog, &start, &end);
                dialog->input_cursor = end;
                fd_input_clear_selection(dialog);
                fd_draw_ui(dialog);
            } else {
                int len = strlen(dialog->input_buffer);
                if (dialog->input_cursor < len) {
                    dialog->input_cursor++;
                    fd_draw_ui(dialog);
                }
            }
        } else if (key >= 32 && key < 127 && strlen(dialog->input_buffer) < 63) {
            // Insert character at cursor position
            if (fd_input_has_selection(dialog)) {
                fd_input_delete_selection(dialog);
            }
            int len = strlen(dialog->input_buffer);
            for (int i = len; i >= dialog->input_cursor; i--) {
                dialog->input_buffer[i + 1] = dialog->input_buffer[i];
            }
            dialog->input_buffer[dialog->input_cursor] = key;
            dialog->input_cursor++;
            fd_draw_ui(dialog);
        }
    } else {
        // Handle list navigation with Enter key
        if (key == '\n') {
            if (dialog->selected_index >= 0 && dialog->selected_index < dialog->file_count) {
                // If directory, navigate; if file, select it and focus input
                if (dialog->files[dialog->selected_index].is_directory) {
                    fd_navigate_to(dialog, dialog->files[dialog->selected_index].name);
                } else {
                    strncpy(dialog->input_buffer, dialog->files[dialog->selected_index].name, sizeof(dialog->input_buffer) - 1);
                    dialog->input_buffer[sizeof(dialog->input_buffer) - 1] = '\0';
                    dialog->input_cursor = strlen(dialog->input_buffer);
                    dialog->input_focused = true;
                    fd_input_clear_selection(dialog);
                    fd_draw_ui(dialog);
                }
            }
        }
    }
}

// Scroll handler
static void fd_on_scroll(window_t* win, int delta) {
    file_dialog_t* dialog = (file_dialog_t*)window_get_user_data(win);
    if (!dialog) return;
    
    int visible_items = FD_LIST_HEIGHT / FD_ITEM_HEIGHT;
    int max_scroll = dialog->file_count - visible_items;
    
    if (max_scroll <= 0) return; // No scrolling needed
    
    // Scroll by delta (positive = down, negative = up)
    // Invert delta because mouse wheel up should scroll up
    dialog->scroll_offset += delta;
    
    // Clamp scroll offset
    if (dialog->scroll_offset < 0) dialog->scroll_offset = 0;
    if (dialog->scroll_offset > max_scroll) dialog->scroll_offset = max_scroll;
    
    // Adjust selection to stay visible
    if (dialog->selected_index < dialog->scroll_offset) {
        dialog->selected_index = dialog->scroll_offset;
    } else if (dialog->selected_index >= dialog->scroll_offset + visible_items) {
        dialog->selected_index = dialog->scroll_offset + visible_items - 1;
    }
    
    fd_draw_ui(dialog);
}

// Show open dialog
void file_dialog_show_open(const char* title, 
                           const char* default_path,
                           file_dialog_callback_t callback, 
                           void* user_data) {
    // Only one dialog at a time
    if (active_dialog) return;
    
    // Create window
    window_t* win = window_create(180, 100, 260, 180, title ? title : "Open File");
    if (!win) return;
    
    // Create dialog state
    file_dialog_t* dialog = &dialog_storage;
    
    memset(dialog, 0, sizeof(*dialog));
    // Initialize
    dialog->window = win;
    dialog->type = FILE_DIALOG_OPEN;
    dialog->callback = callback;
    dialog->user_data = user_data;
    dialog->selected_index = 0;  // Start with first item selected
    dialog->scroll_offset = 0;
    dialog->input_cursor = 0;
    dialog->input_focused = false;
    dialog->input_buffer[0] = '\0';
    dialog->input_selecting = false;
    dialog->input_sel_anchor = 0;
    dialog->input_sel_end = 0;
    dialog->last_clicked_index = -1;
    fd_input_clear_selection(dialog);
    
    // Set path
    if (default_path && default_path[0]) {
        strncpy(dialog->current_path, default_path, sizeof(dialog->current_path) - 1);
    } else {
        strcpy(dialog->current_path, "/");
    }
    dialog->current_path[sizeof(dialog->current_path) - 1] = '\0';
    
    window_set_handlers(win, fd_on_draw, fd_on_click, fd_on_mouse_up, fd_on_mouse_move,
                        fd_on_scroll, fd_on_key, dialog);
    
    active_dialog = dialog;
    
    // Load and draw
    fd_refresh_list(dialog);
    fd_draw_ui(dialog);
}

// Show save dialog
void file_dialog_show_save(const char* title, 
                           const char* default_filename,
                           file_dialog_callback_t callback, 
                           void* user_data) {
    // Only one dialog at a time
    if (active_dialog) return;
    
    // Create window
    window_t* win = window_create(180, 100, 260, 180, title ? title : "Save File");
    if (!win) return;
    
    // Create dialog state
    file_dialog_t* dialog = &dialog_storage;
    
    memset(dialog, 0, sizeof(*dialog));
    // Initialize
    dialog->window = win;
    dialog->type = FILE_DIALOG_SAVE;
    dialog->callback = callback;
    dialog->user_data = user_data;
    dialog->selected_index = 0;  // Start with first item selected
    dialog->scroll_offset = 0;
    dialog->input_cursor = 0;
    dialog->input_focused = true;  // Auto-focus input for save
    strcpy(dialog->current_path, "/");
    dialog->input_selecting = false;
    dialog->input_sel_anchor = 0;
    dialog->input_sel_end = 0;
    dialog->last_clicked_index = -1;
    
    // Set default filename if provided
    if (default_filename && default_filename[0]) {
        strncpy(dialog->input_buffer, default_filename, sizeof(dialog->input_buffer) - 1);
        dialog->input_buffer[sizeof(dialog->input_buffer) - 1] = '\0';
        dialog->input_cursor = strlen(dialog->input_buffer);
    } else {
        dialog->input_buffer[0] = '\0';
    }
    fd_input_clear_selection(dialog);
    
    window_set_handlers(win, fd_on_draw, fd_on_click, fd_on_mouse_up, fd_on_mouse_move,
                        fd_on_scroll, fd_on_key, dialog);
    
    active_dialog = dialog;
    
    // Load and draw
    fd_refresh_list(dialog);
    fd_draw_ui(dialog);
}

void file_dialog_poll(void) {
    if (active_dialog && !uwm_window_is_open(active_dialog->window)) {
        active_dialog = NULL;
    }
}
