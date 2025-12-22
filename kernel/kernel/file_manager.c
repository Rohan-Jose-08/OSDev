#include <kernel/file_manager.h>
#include <kernel/window.h>
#include <kernel/menu_bar.h>
#include <kernel/graphics.h>
#include <kernel/keyboard.h>
#include <kernel/mouse.h>
#include <kernel/fs.h>
#include <kernel/kmalloc.h>
#include <kernel/paint.h>
#include <stdio.h>
#include <string.h>

// External font for text rendering
extern const uint8_t font_8x8[256][8];

#define FM_MAX_FILES 50
#define FM_FILE_ITEM_HEIGHT 16
#define FM_SCROLL_AREA_HEIGHT 100
#define FM_BUTTON_HEIGHT 20
#define FM_BUTTON_WIDTH 80

typedef struct {
    char name[32];
    bool is_directory;
} file_entry_t;

typedef struct {
    window_t* window;
    menu_bar_t* menu_bar;
    char current_path[64];
    file_entry_t files[FM_MAX_FILES];
    int file_count;
    int selected_index;
    int scroll_offset;
    bool input_mode;
    char input_buffer[32];
    int input_cursor;
} file_manager_state_t;

static file_manager_state_t* fm_state = NULL;

// Forward declarations
static bool fm_on_priority_click(window_t* window, int x, int y);
static void fm_refresh_file_list(void);
static void fm_draw_file_list(void);
static void fm_draw_toolbar(void);
static void fm_draw_status_bar(void);
static void fm_on_destroy(window_t* window);

// Menu callbacks
static void fm_menu_new_file(window_t* window, void* user_data) {
    if (!fm_state) return;
    
    // Create a new empty file with default name
    char filename[64];
    snprintf(filename, sizeof(filename), "newfile.txt");
    
    char full_path[128];
    if (strcmp(fm_state->current_path, "/") == 0) {
        snprintf(full_path, sizeof(full_path), "/%s", filename);
    } else {
        snprintf(full_path, sizeof(full_path), "%s/%s", fm_state->current_path, filename);
    }
    
    // Create the file
    if (fs_create_file(full_path) >= 0) {
        // Write empty content
        fs_write_file(full_path, (const uint8_t*)"", 0, 0);
        
        // Refresh the list
        fm_refresh_file_list();
        fm_draw_file_list();
        window_draw(window);
    }
}

static void fm_menu_new_folder(window_t* window, void* user_data) {
    if (!fm_state) return;
    
    // Create a new directory with default name
    char dirname[64];
    snprintf(dirname, sizeof(dirname), "newfolder");
    
    char full_path[128];
    if (strcmp(fm_state->current_path, "/") == 0) {
        snprintf(full_path, sizeof(full_path), "/%s", dirname);
    } else {
        snprintf(full_path, sizeof(full_path), "%s/%s", fm_state->current_path, dirname);
    }
    
    // Create the directory
    if (fs_create_dir(full_path) >= 0) {
        // Refresh the list
        fm_refresh_file_list();
        fm_draw_file_list();
        window_draw(window);
    }
}

static void fm_menu_rename(window_t* window, void* user_data) {
    if (!fm_state || fm_state->selected_index < 0 || 
        fm_state->selected_index >= fm_state->file_count) {
        return;
    }
    
    file_entry_t* entry = &fm_state->files[fm_state->selected_index];
    if (strcmp(entry->name, "..") == 0) return; // Don't rename parent dir
    
    // Enter input mode with current name
    fm_state->input_mode = true;
    strncpy(fm_state->input_buffer, entry->name, sizeof(fm_state->input_buffer) - 1);
    fm_state->input_buffer[sizeof(fm_state->input_buffer) - 1] = '\0';
    fm_state->input_cursor = strlen(fm_state->input_buffer);
}

static void fm_menu_delete_file(window_t* window, void* user_data) {
    if (!fm_state || fm_state->selected_index < 0 || 
        fm_state->selected_index >= fm_state->file_count) {
        return;
    }
    
    file_entry_t* entry = &fm_state->files[fm_state->selected_index];
    if (strcmp(entry->name, "..") == 0) return; // Don't delete parent dir
    
    char full_path[128];
    if (strcmp(fm_state->current_path, "/") == 0) {
        snprintf(full_path, sizeof(full_path), "/%s", entry->name);
    } else {
        snprintf(full_path, sizeof(full_path), "%s/%s", fm_state->current_path, entry->name);
    }
    
    if (fs_delete(full_path)) {
        fm_state->selected_index = -1;
        fm_refresh_file_list();
        fm_draw_file_list();
        fm_draw_status_bar();
        window_draw(window);
    }
}

static void fm_menu_refresh(window_t* window, void* user_data) {
    if (!fm_state) return;
    fm_refresh_file_list();
    fm_draw_file_list();
    window_draw(window);
}

static void fm_menu_home(window_t* window, void* user_data) {
    if (!fm_state) return;
    strcpy(fm_state->current_path, "/");
    fm_state->selected_index = -1;
    fm_state->scroll_offset = 0;
    fm_refresh_file_list();
    fm_draw_toolbar();
    fm_draw_file_list();
    window_draw(window);
}

static void fm_menu_close(window_t* window, void* user_data) {
    if (fm_state) {
        window_destroy(fm_state->window);
        menu_bar_destroy(fm_state->menu_bar);
        kfree(fm_state);
        fm_state = NULL;
    }
}

// Window destroy handler
static void fm_on_destroy(window_t* window) {
    if (!fm_state) return;
    
    // Clean up menu bar
    if (fm_state->menu_bar) {
        menu_bar_destroy(fm_state->menu_bar);
    }
    
    // Free state and reset global pointer
    kfree(fm_state);
    fm_state = NULL;
}

// Refresh the file list from filesystem
static void fm_refresh_file_list(void) {
    if (!fm_state) return;
    
    fm_state->file_count = 0;
    
    // Add parent directory if not at root
    if (strcmp(fm_state->current_path, "/") != 0) {
        strcpy(fm_state->files[fm_state->file_count].name, "..");
        fm_state->files[fm_state->file_count].is_directory = true;
        fm_state->file_count++;
    }
    
    // List files from filesystem
    fs_dirent_t entries[FM_MAX_FILES];
    int count = fs_list_dir(fm_state->current_path, entries, FM_MAX_FILES);
    
    for (int i = 0; i < count && fm_state->file_count < FM_MAX_FILES; i++) {
        strncpy(fm_state->files[fm_state->file_count].name, 
               entries[i].name, sizeof(fm_state->files[0].name) - 1);
        fm_state->files[fm_state->file_count].name[sizeof(fm_state->files[0].name) - 1] = '\0';
        
        // Check if it's a directory by getting inode info
        fs_inode_t inode;
        char full_path[128];
        snprintf(full_path, sizeof(full_path), "%s/%s", fm_state->current_path, entries[i].name);
        if (fs_stat(full_path, &inode)) {
            fm_state->files[fm_state->file_count].is_directory = (inode.type == 2);
        } else {
            fm_state->files[fm_state->file_count].is_directory = false;
        }
        fm_state->file_count++;
    }
}

// Draw the current path toolbar
static void fm_draw_toolbar(void) {
    if (!fm_state || !fm_state->window) return;
    
    window_t* window = fm_state->window;
    int menu_height = menu_bar_get_height();
    int toolbar_y = menu_height + 5;
    
    // Draw toolbar background
    window_fill_rect(window, 5, toolbar_y, 
                    window->content_width - 10, FM_BUTTON_HEIGHT, 
                    COLOR_LIGHT_GRAY);
    
    // Draw current path
    char path_display[64];
    snprintf(path_display, sizeof(path_display), "Path: %s", fm_state->current_path);
    window_print(window, 10, toolbar_y + 6, path_display, COLOR_BLACK);
}

// Draw the file list
static void fm_draw_file_list(void) {
    if (!fm_state || !fm_state->window) return;
    
    window_t* window = fm_state->window;
    int menu_height = menu_bar_get_height();
    int list_y = menu_height + FM_BUTTON_HEIGHT + 15;
    
    // Clear list area
    window_fill_rect(window, 5, list_y, 
                    window->content_width - 10, FM_SCROLL_AREA_HEIGHT, 
                    COLOR_WHITE);
    
    // Draw border around list
    window_draw_rect(window, 5, list_y, 
                    window->content_width - 10, FM_SCROLL_AREA_HEIGHT, 
                    COLOR_BLACK);
    
    // Draw file entries
    int visible_items = FM_SCROLL_AREA_HEIGHT / FM_FILE_ITEM_HEIGHT;
    for (int i = fm_state->scroll_offset; 
         i < fm_state->file_count && i < fm_state->scroll_offset + visible_items; 
         i++) {
        
        int item_y = list_y + (i - fm_state->scroll_offset) * FM_FILE_ITEM_HEIGHT + 2;
        
        // Highlight selected item
        if (i == fm_state->selected_index) {
            window_fill_rect(window, 6, item_y, 
                           window->content_width - 12, FM_FILE_ITEM_HEIGHT - 1, 
                           COLOR_LIGHT_BLUE);
        }
        
        // Draw file/folder icon and name
        char display[40];
        if (fm_state->files[i].is_directory) {
            snprintf(display, sizeof(display), "[DIR] %s", fm_state->files[i].name);
        } else {
            snprintf(display, sizeof(display), "      %s", fm_state->files[i].name);
        }
        
        window_print(window, 10, item_y + 4, display, COLOR_BLACK);
    }
}

// Draw status bar at bottom
static void fm_draw_status_bar(void) {
    if (!fm_state || !fm_state->window) return;
    
    window_t* window = fm_state->window;
    int menu_height = menu_bar_get_height();
    int list_bottom = menu_height + FM_BUTTON_HEIGHT + FM_SCROLL_AREA_HEIGHT + 6;
    int status_y = list_bottom;
    
    // Draw status background
    window_fill_rect(window, 0, status_y, 
                    window->content_width, 22, 
                    COLOR_LIGHT_GRAY);
    
    if (fm_state->input_mode) {
        // Show rename input prompt
        window_print(window, 10, status_y + 7, "Rename to:", COLOR_BLACK);
        
        // Draw input box
        int input_x = 85;
        int input_width = window->content_width - 95;
        window_fill_rect(window, input_x, status_y + 3, input_width, 16, COLOR_WHITE);
        window_draw_rect(window, input_x, status_y + 3, input_width, 16, COLOR_BLACK);
        
        // Draw input text
        window_print(window, input_x + 4, status_y + 7, fm_state->input_buffer, COLOR_BLACK);
        
        // Draw cursor
        int cursor_x = input_x + 4 + (fm_state->input_cursor * 8);
        for (int i = 0; i < 8; i++) {
            window_putpixel(window, cursor_x, status_y + 7 + i, COLOR_BLACK);
        }
    } else {
        // Draw file count - exclude ".." from count
        int actual_count = fm_state->file_count;
        if (strcmp(fm_state->current_path, "/") != 0 && actual_count > 0) {
            actual_count--; // Subtract the ".." entry
        }
        char status[32];
        snprintf(status, sizeof(status), "%d items", actual_count);
        window_print(window, 10, status_y + 7, status, COLOR_BLACK);
    }
}

// Open/navigate to selected file or directory
static void fm_open_selected(void) {
    if (!fm_state || fm_state->selected_index < 0 || 
        fm_state->selected_index >= fm_state->file_count) {
        return;
    }
    
    file_entry_t* entry = &fm_state->files[fm_state->selected_index];
    
    if (entry->is_directory) {
        // Navigate to directory
        if (strcmp(entry->name, "..") == 0) {
            // Go to parent directory
            char* last_slash = strrchr(fm_state->current_path, '/');
            if (last_slash && last_slash != fm_state->current_path) {
                *last_slash = '\0';
            } else {
                strcpy(fm_state->current_path, "/");
            }
        } else {
            // Go to subdirectory
            if (strcmp(fm_state->current_path, "/") != 0) {
                strcat(fm_state->current_path, "/");
            }
            strcat(fm_state->current_path, entry->name);
        }
        
        fm_state->selected_index = -1;
        fm_state->scroll_offset = 0;
        fm_refresh_file_list();
        fm_draw_toolbar();
        fm_draw_file_list();
        window_draw(fm_state->window);
    } else {
        // Open file - check file extension to determine how to open it
        char full_path[128];
        if (strcmp(fm_state->current_path, "/") == 0) {
            snprintf(full_path, sizeof(full_path), "/%s", entry->name);
        } else {
            snprintf(full_path, sizeof(full_path), "%s/%s", fm_state->current_path, entry->name);
        }
        
        // Check if it's a PNT file
        int name_len = strlen(entry->name);
        bool is_pnt = (name_len > 4 && 
                      (strcmp(entry->name + name_len - 4, ".pnt") == 0 ||
                       strcmp(entry->name + name_len - 4, ".PNT") == 0));
        
        if (is_pnt) {
            // Open with paint application
            paint_open_file(full_path);
            return;
        }
        
        // Get file info
        fs_inode_t inode;
        if (!fs_stat(full_path, &inode)) {
            return; // File doesn't exist
        }
        
        // Read file contents for text viewer
        static char file_buffer[4096];
        int bytes_read = fs_read_file(full_path, (uint8_t*)file_buffer, sizeof(file_buffer) - 1, 0);
        
        if (bytes_read >= 0) {
            file_buffer[bytes_read] = '\0';
            
            // Create a viewer window with title showing filename
            char title[80];
            snprintf(title, sizeof(title), "View: %s", entry->name);
            window_t* viewer = window_create(200, 100, 450, 350, title);
            if (viewer) {
                window_clear_content(viewer, COLOR_WHITE);
                
                // Show file info at top
                char info[64];
                snprintf(info, sizeof(info), "File: %s (%d bytes)", entry->name, inode.size);
                window_fill_rect(viewer, 0, 0, viewer->content_width, 16, COLOR_LIGHT_GRAY);
                window_print(viewer, 5, 4, info, COLOR_BLACK);
                
                // Display file contents line by line
                int y_pos = 24;
                int x_pos = 5;
                int line = 0;
                
                for (int i = 0; i < bytes_read && y_pos < viewer->content_height - 30; i++) {
                    if (file_buffer[i] == '\n') {
                        y_pos += 8;
                        x_pos = 5;
                        line++;
                    } else if (file_buffer[i] == '\t') {
                        x_pos += 32; // Tab = 4 spaces
                    } else if (file_buffer[i] >= 32 && file_buffer[i] < 127) {
                        if (x_pos < viewer->content_width - 10) {
                            // Draw character pixel by pixel for better control
                            const uint8_t* glyph = font_8x8[(uint8_t)file_buffer[i]];
                            for (int cy = 0; cy < 8; cy++) {
                                for (int cx = 0; cx < 8; cx++) {
                                    if (glyph[cy] & (1 << (7 - cx))) {
                                        window_putpixel(viewer, x_pos + cx, y_pos + cy, COLOR_BLACK);
                                    }
                                }
                            }
                            x_pos += 8;
                        }
                    }
                }
                
                // Show help text at bottom
                window_fill_rect(viewer, 0, viewer->content_height - 20, 
                               viewer->content_width, 20, COLOR_LIGHT_GRAY);
                window_print(viewer, 5, viewer->content_height - 16, 
                           "Tip: Use text editor to edit files", COLOR_DARK_GRAY);
                
                window_draw(viewer);
            }
        }
    }
}

// Priority click handler for menu bar
static bool fm_on_priority_click(window_t* window, int x, int y) {
    if (!fm_state) return false;
    
    // Check if click is in menu bar
    if (menu_bar_handle_click(fm_state->menu_bar, x, y)) {
        // Clear the window content area to remove any dropdown remnants
        window_clear_content(window, WINDOW_COLOR_BACKGROUND);
        fm_draw_toolbar();
        fm_draw_file_list();
        fm_draw_status_bar();
        menu_bar_draw(fm_state->menu_bar);
        window_draw(window);
        return true;  // Menu bar handled the click
    }
    
    return false;  // Menu bar didn't handle, proceed with normal click handling
}

// Window click handler
static void fm_on_click(window_t* window, int x, int y) {
    if (!fm_state) return;
    
    // If in input mode, clicking anywhere cancels it
    if (fm_state->input_mode) {
        fm_state->input_mode = false;
        fm_state->input_buffer[0] = '\0';
        fm_state->input_cursor = 0;
        fm_draw_status_bar();
        window_draw(window);
        return;
    }
    
    int menu_height = menu_bar_get_height();
    int list_y = menu_height + FM_BUTTON_HEIGHT + 15;
    
    // Check if click is in file list
    if (x >= 5 && x < window->content_width - 5 &&
        y >= list_y && y < list_y + FM_SCROLL_AREA_HEIGHT) {
        
        int clicked_index = (y - list_y) / FM_FILE_ITEM_HEIGHT + fm_state->scroll_offset;
        
        if (clicked_index >= 0 && clicked_index < fm_state->file_count) {
            if (fm_state->selected_index == clicked_index) {
                // Double-click: open the item
                fm_open_selected();
            } else {
                // Single click: select the item
                fm_state->selected_index = clicked_index;
                fm_draw_file_list();
                window_draw(window);
            }
        }
    }
}

// Window key handler
static void fm_on_key(window_t* window, char key) {
    if (!fm_state) return;
    
    // Handle input mode for rename
    if (fm_state->input_mode) {
        if (key == '\n') {
            // Enter - perform rename
            if (fm_state->selected_index >= 0 && 
                fm_state->selected_index < fm_state->file_count &&
                strlen(fm_state->input_buffer) > 0) {
                
                file_entry_t* entry = &fm_state->files[fm_state->selected_index];
                
                char full_path[128];
                if (strcmp(fm_state->current_path, "/") == 0) {
                    snprintf(full_path, sizeof(full_path), "/%s", entry->name);
                } else {
                    snprintf(full_path, sizeof(full_path), "%s/%s", 
                            fm_state->current_path, entry->name);
                }
                
                if (fs_rename(full_path, fm_state->input_buffer)) {
                    fm_state->selected_index = -1;
                    fm_refresh_file_list();
                }
            }
            
            // Exit input mode
            fm_state->input_mode = false;
            fm_state->input_buffer[0] = '\0';
            fm_state->input_cursor = 0;
            
            fm_draw_toolbar();
            fm_draw_file_list();
            fm_draw_status_bar();
            window_draw(window);
            return;
        } else if (key == 27) {
            // ESC - cancel
            fm_state->input_mode = false;
            fm_state->input_buffer[0] = '\0';
            fm_state->input_cursor = 0;
            
            fm_draw_status_bar();
            window_draw(window);
            return;
        } else if (key == '\b' || key == 127) {
            // Backspace
            if (fm_state->input_cursor > 0) {
                fm_state->input_cursor--;
                fm_state->input_buffer[fm_state->input_cursor] = '\0';
                fm_draw_status_bar();
                window_draw(window);
            }
            return;
        } else if (key >= 32 && key < 127 && key != '/') {
            // Regular character (but not slash)
            if (fm_state->input_cursor < (int)sizeof(fm_state->input_buffer) - 1) {
                fm_state->input_buffer[fm_state->input_cursor] = key;
                fm_state->input_cursor++;
                fm_state->input_buffer[fm_state->input_cursor] = '\0';
                fm_draw_status_bar();
                window_draw(window);
            }
            return;
        }
        return;
    }
    
    // Normal mode key handling
    bool redraw = false;
    
    switch (key) {
        case '\n': // Enter
            fm_open_selected();
            return;
            
        case 'w': // Up
        case 'W':
            if (fm_state->selected_index > 0) {
                fm_state->selected_index--;
                if (fm_state->selected_index < fm_state->scroll_offset) {
                    fm_state->scroll_offset = fm_state->selected_index;
                }
                redraw = true;
            }
            break;
            
        case 's': // Down
        case 'S':
            if (fm_state->selected_index < fm_state->file_count - 1) {
                fm_state->selected_index++;
                int visible_items = FM_SCROLL_AREA_HEIGHT / FM_FILE_ITEM_HEIGHT;
                if (fm_state->selected_index >= fm_state->scroll_offset + visible_items) {
                    fm_state->scroll_offset = fm_state->selected_index - visible_items + 1;
                }
                redraw = true;
            }
            break;
            
        case 'r': // Refresh
        case 'R':
            fm_refresh_file_list();
            redraw = true;
            break;
    }
    
    if (redraw) {
        fm_draw_file_list();
        window_draw(window);
    }
}

// Scroll handler for mouse wheel
static void fm_on_scroll(window_t* window, int delta) {
    if (!fm_state) return;
    
    int visible_items = FM_SCROLL_AREA_HEIGHT / FM_FILE_ITEM_HEIGHT;
    int max_scroll = fm_state->file_count - visible_items;
    
    if (max_scroll <= 0) return; // No scrolling needed
    
    // Scroll by delta (positive = down, negative = up)
    fm_state->scroll_offset += delta;
    
    // Clamp scroll offset
    if (fm_state->scroll_offset < 0) fm_state->scroll_offset = 0;
    if (fm_state->scroll_offset > max_scroll) fm_state->scroll_offset = max_scroll;
    
    // Adjust selection to stay visible if one is selected
    if (fm_state->selected_index >= 0) {
        if (fm_state->selected_index < fm_state->scroll_offset) {
            fm_state->selected_index = fm_state->scroll_offset;
        } else if (fm_state->selected_index >= fm_state->scroll_offset + visible_items) {
            fm_state->selected_index = fm_state->scroll_offset + visible_items - 1;
        }
    }
    
    fm_draw_file_list();
    window_draw(window);
}

// Launch file manager application
void file_manager_app(void) {
    // Only allow one instance
    if (fm_state) return;
    
    // Calculate window size
    int win_width = 350;
    int win_height = menu_bar_get_height() + FM_BUTTON_HEIGHT + FM_SCROLL_AREA_HEIGHT + 45;
    
    // Create window
    window_t* window = window_create(150, 80, win_width, win_height, "File Manager");
    if (!window) return;
    
    // Allocate state
    fm_state = (file_manager_state_t*)kmalloc(sizeof(file_manager_state_t));
    if (!fm_state) {
        window_destroy(window);
        return;
    }
    
    // Initialize state
    fm_state->window = window;
    strcpy(fm_state->current_path, "/");
    fm_state->file_count = 0;
    fm_state->selected_index = -1;
    fm_state->scroll_offset = 0;
    fm_state->input_mode = false;
    fm_state->input_buffer[0] = '\0';
    fm_state->input_cursor = 0;
    
    // Create menu bar
    fm_state->menu_bar = menu_bar_create(window);
    if (fm_state->menu_bar) {
        menu_item_t* file_menu = menu_bar_add_menu(fm_state->menu_bar, "File");
        if (file_menu) {
            menu_item_add_dropdown(file_menu, "New File", fm_menu_new_file);
            menu_item_add_dropdown(file_menu, "New Folder", fm_menu_new_folder);
            menu_item_add_separator(file_menu);
            menu_item_add_dropdown(file_menu, "Rename", fm_menu_rename);
            menu_item_add_dropdown(file_menu, "Delete", fm_menu_delete_file);
            menu_item_add_separator(file_menu);
            menu_item_add_dropdown(file_menu, "Refresh", fm_menu_refresh);
            menu_item_add_dropdown(file_menu, "Home", fm_menu_home);
            menu_item_add_separator(file_menu);
            menu_item_add_dropdown(file_menu, "Close", fm_menu_close);
        }
        
        menu_item_t* view_menu = menu_bar_add_menu(fm_state->menu_bar, "View");
        if (view_menu) {
            menu_item_add_dropdown(view_menu, "Refresh", fm_menu_refresh);
        }
    }
    
    // Set window callbacks
    window->on_priority_click = fm_on_priority_click;
    window->on_click = fm_on_click;
    window->on_key = fm_on_key;
    window->on_scroll = fm_on_scroll;
    window->on_destroy = fm_on_destroy;
    window->user_data = fm_state;
    
    // Initial file list
    fm_refresh_file_list();
    
    // Draw initial state
    window_clear_content(window, WINDOW_COLOR_BACKGROUND);
    if (fm_state->menu_bar) {
        menu_bar_draw(fm_state->menu_bar);
    }
    fm_draw_toolbar();
    fm_draw_file_list();
    fm_draw_status_bar();
    window_draw(window);
}
