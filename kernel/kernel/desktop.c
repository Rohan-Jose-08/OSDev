#include <kernel/desktop.h>
#include <kernel/window.h>
#include <kernel/graphics.h>
#include <kernel/mouse.h>
#include <kernel/keyboard.h>
#include <kernel/tty.h>
#include <kernel/gui_apps.h>
#include <kernel/fs.h>
#include <kernel/kmalloc.h>
#include <kernel/calculator.h>
#include <kernel/file_manager.h>
#include <kernel/paint.h>
#include <kernel/file_dialog.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Global desktop state
static desktop_state_t desktop;

// Forward declarations for app launchers
static void launch_calculator(void);
static void launch_paint(void);
static void launch_file_manager(void);
static void launch_text_editor(void);
static void launch_about(void);

void desktop_init(void) {
    memset(&desktop, 0, sizeof(desktop_state_t));
    desktop.running = false;
    desktop.menu_open = false;
    desktop.menu_width = 120;
    desktop.menu_height = 0;  // Will be calculated based on app count
    desktop.menu_hover_item = -1;
    desktop.app_count = 0;
    
    // Register built-in applications
    desktop_register_app("Calculator", APP_TYPE_CALCULATOR, launch_calculator);
    desktop_register_app("Paint", APP_TYPE_PAINT, launch_paint);
    desktop_register_app("Files", APP_TYPE_FILE_MANAGER, launch_file_manager);
    desktop_register_app("Editor", APP_TYPE_TEXT_EDITOR, launch_text_editor);
    desktop_register_app("About", APP_TYPE_ABOUT, launch_about);
    
    // Calculate menu height
    desktop.menu_height = desktop.app_count * 18 + 4;
}

void desktop_register_app(const char* name, app_type_t type, void (*launcher)(void)) {
    if (desktop.app_count >= DESKTOP_MAX_APPS) return;
    
    desktop_app_t* app = &desktop.apps[desktop.app_count];
    strncpy(app->name, name, DESKTOP_APP_NAME_MAX - 1);
    app->name[DESKTOP_APP_NAME_MAX - 1] = '\0';
    app->type = type;
    app->launcher = launcher;
    app->visible = true;
    
    // Position icons vertically along left side - dynamically adjust spacing
    int screen_height = graphics_get_height();
    int available_height = screen_height - DESKTOP_TASKBAR_HEIGHT - 8;  // Screen minus taskbar and margins
    int icon_spacing = (available_height / DESKTOP_MAX_APPS);  // Divide evenly
    if (icon_spacing < DESKTOP_ICON_SIZE + 8) {
        icon_spacing = DESKTOP_ICON_SIZE + 8;  // Minimum spacing
    }
    app->icon_x = 4;
    app->icon_y = 4 + desktop.app_count * icon_spacing;
    
    desktop.app_count++;
}

void desktop_launch_app(app_type_t type) {
    for (int i = 0; i < desktop.app_count; i++) {
        if (desktop.apps[i].type == type && desktop.apps[i].launcher) {
            desktop.apps[i].launcher();
            return;
        }
    }
}

void desktop_draw_background(void) {
    graphics_fill_rect(0, 0, graphics_get_width(), 
                      graphics_get_height() - DESKTOP_TASKBAR_HEIGHT, 
                      DESKTOP_COLOR_BACKGROUND);
}

void desktop_draw_taskbar(void) {
    int screen_width = graphics_get_width();
    int screen_height = graphics_get_height();
    int taskbar_y = screen_height - DESKTOP_TASKBAR_HEIGHT;
    
    // Draw taskbar background
    graphics_fill_rect(0, taskbar_y, screen_width, DESKTOP_TASKBAR_HEIGHT, 
                      DESKTOP_COLOR_TASKBAR);
    
    // Draw "Start" button
    graphics_fill_rect(2, taskbar_y + 2, 50, DESKTOP_TASKBAR_HEIGHT - 4, 
                      DESKTOP_COLOR_ICON_BG);
    graphics_print(6, taskbar_y + 6, "Start", DESKTOP_COLOR_ICON_TEXT, 
                  DESKTOP_COLOR_ICON_BG);
    
    // Draw window count indicator
    window_manager_t* wm = window_get_manager();
    int window_count = 0;
    window_t* w = wm->window_list;
    while (w) {
        window_count++;
        w = w->next;
    }
    
    char count_text[32];
    snprintf(count_text, sizeof(count_text), "Windows: %d", window_count);
    graphics_print(60, taskbar_y + 6, count_text, COLOR_WHITE, DESKTOP_COLOR_TASKBAR);
}

void desktop_draw_icons(void) {
    for (int i = 0; i < desktop.app_count; i++) {
        desktop_app_t* app = &desktop.apps[i];
        if (!app->visible) continue;
        
        // Draw icon background
        graphics_fill_rect(app->icon_x, app->icon_y, DESKTOP_ICON_SIZE, 
                          DESKTOP_ICON_SIZE, DESKTOP_COLOR_ICON_BG);
        
        // Draw icon border
        graphics_draw_rect(app->icon_x, app->icon_y, DESKTOP_ICON_SIZE, 
                          DESKTOP_ICON_SIZE, COLOR_DARK_GRAY);
        
        // Draw logo based on app type
        int cx = app->icon_x + DESKTOP_ICON_SIZE / 2;
        int cy = app->icon_y + DESKTOP_ICON_SIZE / 2;
        
        switch (app->type) {
            case APP_TYPE_CALCULATOR:
                // Calculator: grid pattern with numbers
                graphics_draw_rect(app->icon_x + 6, app->icon_y + 4, 16, 20, COLOR_BLUE);
                graphics_fill_rect(app->icon_x + 7, app->icon_y + 5, 14, 5, COLOR_WHITE);
                // Button grid
                for (int r = 0; r < 3; r++) {
                    for (int c = 0; c < 3; c++) {
                        graphics_fill_rect(app->icon_x + 8 + c*4, app->icon_y + 12 + r*3, 3, 2, COLOR_LIGHT_BLUE);
                    }
                }
                break;
                
            case APP_TYPE_PAINT:
                // Paint: brush and palette
                graphics_fill_rect(app->icon_x + 6, app->icon_y + 18, 16, 6, COLOR_DARK_GRAY);
                // Color dots
                graphics_fill_rect(app->icon_x + 7, app->icon_y + 20, 2, 2, COLOR_RED);
                graphics_fill_rect(app->icon_x + 10, app->icon_y + 20, 2, 2, COLOR_GREEN);
                graphics_fill_rect(app->icon_x + 13, app->icon_y + 20, 2, 2, COLOR_BLUE);
                graphics_fill_rect(app->icon_x + 16, app->icon_y + 20, 2, 2, COLOR_YELLOW);
                // Brush
                graphics_fill_rect(app->icon_x + 16, app->icon_y + 6, 3, 8, COLOR_YELLOW);
                graphics_fill_rect(app->icon_x + 14, app->icon_y + 14, 6, 3, COLOR_BLACK);
                break;
                
            case APP_TYPE_FILE_MANAGER:
                // Files: folder icon
                graphics_fill_rect(app->icon_x + 6, app->icon_y + 10, 16, 13, COLOR_YELLOW);
                graphics_draw_rect(app->icon_x + 6, app->icon_y + 10, 16, 13, COLOR_DARK_GRAY);
                graphics_fill_rect(app->icon_x + 6, app->icon_y + 8, 8, 3, COLOR_YELLOW);
                graphics_draw_rect(app->icon_x + 6, app->icon_y + 8, 8, 3, COLOR_DARK_GRAY);
                break;
                
            case APP_TYPE_TEXT_EDITOR:
                // Editor: document with lines
                graphics_fill_rect(app->icon_x + 7, app->icon_y + 6, 14, 18, COLOR_WHITE);
                graphics_draw_rect(app->icon_x + 7, app->icon_y + 6, 14, 18, COLOR_BLACK);
                // Text lines
                for (int l = 0; l < 4; l++) {
                    graphics_fill_rect(app->icon_x + 9, app->icon_y + 9 + l*3, 10, 2, COLOR_BLUE);
                }
                break;
                
            case APP_TYPE_ABOUT:
                // About: info symbol (i)
                graphics_draw_rect(app->icon_x + 9, app->icon_y + 6, 10, 18, COLOR_BLUE);
                graphics_fill_rect(app->icon_x + 12, app->icon_y + 9, 3, 3, COLOR_BLUE);
                graphics_fill_rect(app->icon_x + 12, app->icon_y + 14, 3, 8, COLOR_BLUE);
                break;
                
            default:
                // Fallback: first letter
                char icon_char[2] = {app->name[0], '\0'};
                int text_x = app->icon_x + DESKTOP_ICON_SIZE / 2 - 4;
                int text_y = app->icon_y + DESKTOP_ICON_SIZE / 2 - 4;
                graphics_print(text_x, text_y, icon_char, COLOR_BLUE, DESKTOP_COLOR_ICON_BG);
                break;
        }
        
        // Draw app name below icon
        int name_x = app->icon_x;
        int name_y = app->icon_y + DESKTOP_ICON_SIZE + 2;
        graphics_print(name_x, name_y, app->name, DESKTOP_COLOR_ICON_TEXT, 
                      DESKTOP_COLOR_BACKGROUND);
    }
}

void desktop_draw_menu(void) {
    if (!desktop.menu_open) return;
    
    // Draw menu background
    graphics_fill_rect(desktop.menu_x, desktop.menu_y, 
                      desktop.menu_width, desktop.menu_height, 
                      DESKTOP_COLOR_MENU_BG);
    
    // Draw menu border
    graphics_draw_rect(desktop.menu_x, desktop.menu_y, 
                      desktop.menu_width, desktop.menu_height, 
                      COLOR_DARK_GRAY);
    
    // Draw menu items
    for (int i = 0; i < desktop.app_count; i++) {
        int item_y = desktop.menu_y + 2 + i * 18;
        uint8_t bg_color = (i == desktop.menu_hover_item) ? 
                          DESKTOP_COLOR_MENU_HOVER : DESKTOP_COLOR_MENU_BG;
        
        // Draw item background if hovered
        if (i == desktop.menu_hover_item) {
            graphics_fill_rect(desktop.menu_x + 1, item_y, 
                             desktop.menu_width - 2, 16, bg_color);
        }
        
        // Draw item text
        graphics_print(desktop.menu_x + 5, item_y + 4, 
                      desktop.apps[i].name, DESKTOP_COLOR_MENU_TEXT, bg_color);
    }
}

bool desktop_point_in_taskbar(int x, int y) {
    int taskbar_y = graphics_get_height() - DESKTOP_TASKBAR_HEIGHT;
    return y >= taskbar_y;
}

bool desktop_point_in_menu(int x, int y) {
    if (!desktop.menu_open) return false;
    
    return x >= desktop.menu_x && x < desktop.menu_x + desktop.menu_width &&
           y >= desktop.menu_y && y < desktop.menu_y + desktop.menu_height;
}

int desktop_get_menu_item_at(int x, int y) {
    if (!desktop_point_in_menu(x, y)) return -1;
    
    int relative_y = y - desktop.menu_y - 2;
    int item_index = relative_y / 18;
    
    if (item_index >= 0 && item_index < desktop.app_count) {
        return item_index;
    }
    
    return -1;
}

void desktop_handle_click(int x, int y) {
    // Check if click is on Start button (taskbar)
    if (desktop_point_in_taskbar(x, y)) {
        if (x >= 2 && x < 52 && y >= graphics_get_height() - DESKTOP_TASKBAR_HEIGHT + 2) {
            // Toggle menu
            desktop.menu_open = !desktop.menu_open;
            if (desktop.menu_open) {
                desktop.menu_x = 2;
                desktop.menu_y = graphics_get_height() - DESKTOP_TASKBAR_HEIGHT - desktop.menu_height;
            }
            return;
        }
    }
    
    // Check if click is in menu
    if (desktop.menu_open && desktop_point_in_menu(x, y)) {
        int item = desktop_get_menu_item_at(x, y);
        if (item >= 0 && item < desktop.app_count) {
            // Launch the application
            if (desktop.apps[item].launcher) {
                desktop.apps[item].launcher();
            }
            desktop.menu_open = false;
        }
        return;
    }
    
    // Check if click is on desktop icon
    if (!desktop_point_in_taskbar(x, y)) {
        for (int i = 0; i < desktop.app_count; i++) {
            desktop_app_t* app = &desktop.apps[i];
            if (!app->visible) continue;
            
            if (x >= app->icon_x && x < app->icon_x + DESKTOP_ICON_SIZE &&
                y >= app->icon_y && y < app->icon_y + DESKTOP_ICON_SIZE) {
                // Launch the application
                if (app->launcher) {
                    app->launcher();
                }
                return;
            }
        }
    }
    
    // Close menu if clicking outside
    if (desktop.menu_open && !desktop_point_in_menu(x, y)) {
        desktop.menu_open = false;
    }
}

void desktop_handle_mouse_move(int x, int y) {
    // Update menu hover state
    if (desktop.menu_open) {
        desktop.menu_hover_item = desktop_get_menu_item_at(x, y);
    } else {
        desktop.menu_hover_item = -1;
    }
}

void desktop_run(void) {
    // Switch to graphics mode (320x240 for larger display)
    if (!graphics_set_mode(MODE_320x240)) {
        terminal_writestring("Failed to set graphics mode\n");
        return;
    }
    
    // Enable double buffering for smooth rendering
    graphics_enable_double_buffer();
    
    // Initialize window manager
    window_manager_init();
    
    // Initialize desktop
    desktop_init();
    desktop.running = true;
    
    terminal_writestring("Desktop Environment started.\n");
    terminal_writestring("Click 'Start' button or desktop icons to launch applications.\n");
    terminal_writestring("Press ESC to exit.\n");
    
    // Main event loop
    mouse_state_t prev_mouse = {0};
    
    while (desktop.running) {
        // Get mouse state
        mouse_state_t mouse = mouse_get_state();
        
        // Update cursor position
        int dx = mouse.x;
        int dy = -mouse.y;  // Invert Y axis
        
        int cursor_x, cursor_y;
        window_get_cursor_pos(&cursor_x, &cursor_y);
        
        cursor_x += dx;
        cursor_y += dy;
        
        // Clamp to screen bounds
        if (cursor_x < 0) cursor_x = 0;
        if (cursor_y < 0) cursor_y = 0;
        if (cursor_x >= graphics_get_width()) cursor_x = graphics_get_width() - 1;
        if (cursor_y >= graphics_get_height()) cursor_y = graphics_get_height() - 1;
        
        window_set_cursor_pos(cursor_x, cursor_y);
        
        // Handle mouse movement
        if (dx != 0 || dy != 0) {
            window_handle_mouse_move(cursor_x, cursor_y);
            desktop_handle_mouse_move(cursor_x, cursor_y);
            
            // Handle drag in focused window (for paint drawing)
            if ((mouse.buttons & MOUSE_LEFT_BUTTON)) {
                window_manager_t* wm = window_get_manager();
                if (wm->focused_window && wm->focused_window->on_drag) {
                    window_t* win = wm->focused_window;
                    if (!(win->flags & WINDOW_FLAG_DRAGGING)) {
                        int win_x = cursor_x - (win->x + WINDOW_BORDER_WIDTH);
                        int win_y = cursor_y - (win->y + WINDOW_TITLE_BAR_HEIGHT);
                        if (win_x >= 0 && win_x < win->content_width &&
                            win_y >= 0 && win_y < win->content_height) {
                            win->on_drag(win, win_x, win_y);
                        }
                    }
                }
            }
        }
        
        // Handle mouse clicks
        if ((mouse.buttons & MOUSE_LEFT_BUTTON) && !(prev_mouse.buttons & MOUSE_LEFT_BUTTON)) {
            // Button just pressed - check if on desktop elements first
            if (desktop_point_in_taskbar(cursor_x, cursor_y) || 
                desktop_point_in_menu(cursor_x, cursor_y)) {
                desktop_handle_click(cursor_x, cursor_y);
            } else {
                // First check if any window is at this position
                window_t* window_at_pos = window_at_position(cursor_x, cursor_y);
                
                if (window_at_pos) {
                    // A window is at this position - handle window click
                    bool handled = window_handle_mouse_click(cursor_x, cursor_y, mouse.buttons);
                    
                    // Only call content click handler if window chrome didn't handle it
                    if (!handled) {
                        // Check if clicking inside focused window content
                        window_manager_t* wm = window_get_manager();
                        if (wm->focused_window && wm->focused_window->on_click) {
                            window_t* win = wm->focused_window;
                            if (!(win->flags & WINDOW_FLAG_DRAGGING)) {
                                int win_x = cursor_x - (win->x + WINDOW_BORDER_WIDTH);
                                int win_y = cursor_y - (win->y + WINDOW_TITLE_BAR_HEIGHT);
                                if (win_x >= 0 && win_x < win->content_width &&
                                    win_y >= 0 && win_y < win->content_height) {
                                    win->on_click(win, win_x, win_y);
                                }
                            }
                        }
                    }
                } else {
                    // No window at this position - check desktop icons
                    bool clicked_icon = false;
                    for (int i = 0; i < desktop.app_count; i++) {
                        desktop_app_t* app = &desktop.apps[i];
                        if (app->visible && 
                            cursor_x >= app->icon_x && cursor_x < app->icon_x + DESKTOP_ICON_SIZE &&
                            cursor_y >= app->icon_y && cursor_y < app->icon_y + DESKTOP_ICON_SIZE) {
                            desktop_handle_click(cursor_x, cursor_y);
                            clicked_icon = true;
                            break;
                        }
                    }
                }
            }
        } else if (!(mouse.buttons & MOUSE_LEFT_BUTTON) && (prev_mouse.buttons & MOUSE_LEFT_BUTTON)) {
            // Button just released
            window_handle_mouse_release(cursor_x, cursor_y, mouse.buttons);
        }
        
        prev_mouse = mouse;
        
        // Handle mouse scroll
        if (mouse.scroll != 0) {
            window_manager_t* wm = window_get_manager();
            if (wm->focused_window && wm->focused_window->on_scroll) {
                wm->focused_window->on_scroll(wm->focused_window, mouse.scroll);
            }
        }
        
        // Handle keyboard
        if (keyboard_has_input()) {
            char c = keyboard_getchar();
            if (c == 27) {  // ESC key
                desktop.running = false;
            } else {
                // Send key to focused window
                window_manager_t* wm = window_get_manager();
                if (wm->focused_window && wm->focused_window->on_key) {
                    wm->focused_window->on_key(wm->focused_window, c);
                }
            }
        }
        
        // Render frame
        desktop_draw_background();
        desktop_draw_icons();
        window_draw_all();
        desktop_draw_taskbar();
        desktop_draw_menu();
        window_draw_cursor();
        
        graphics_flip_buffer();
        
        // Small delay to prevent 100% CPU usage
        for (volatile int i = 0; i < 10000; i++);
    }
    
    desktop_shutdown();
}

void desktop_shutdown(void) {
    // Clean up - destroy all windows
    window_manager_t* wm = window_get_manager();
    while (wm->window_list) {
        window_destroy(wm->window_list);
    }
    
    // Return to text mode
    graphics_return_to_text();
    terminal_writestring("Desktop Environment closed.\n");
}

// Application launcher implementations

// ===== CALCULATOR APP =====
typedef struct {
    char display[32];
    double value1;
    double value2;
    char operation;
    bool new_number;
} calc_state_t;

static double simple_atof(const char* str) {
    double result = 0.0, sign = 1.0, fraction = 0.0;
    int divisor = 1;
    bool after_decimal = false;
    
    while (*str == ' ' || *str == '\t') str++;
    if (*str == '-') { sign = -1.0; str++; }
    else if (*str == '+') str++;
    
    while (*str) {
        if (*str >= '0' && *str <= '9') {
            if (after_decimal) {
                fraction = fraction * 10.0 + (*str - '0');
                divisor *= 10;
            } else {
                result = result * 10.0 + (*str - '0');
            }
        } else if (*str == '.' && !after_decimal) {
            after_decimal = true;
        } else break;
        str++;
    }
    return sign * (result + fraction / divisor);
}

// Convert double to string with 2 decimal places
static void double_to_str(double value, char* buffer, int size) {
    // Handle negative numbers
    int pos = 0;
    if (value < 0) {
        buffer[pos++] = '-';
        value = -value;
    }
    
    // Get integer part
    long int_part = (long)value;
    
    // Get fractional part (2 decimal places)
    double frac = value - int_part;
    int frac_part = (int)(frac * 100 + 0.5); // Round to 2 decimals
    
    // Handle rounding overflow
    if (frac_part >= 100) {
        int_part++;
        frac_part = 0;
    }
    
    // Convert integer part to string (backwards)
    char temp[32];
    int temp_pos = 0;
    
    if (int_part == 0) {
        temp[temp_pos++] = '0';
    } else {
        long n = int_part;
        while (n > 0) {
            temp[temp_pos++] = '0' + (n % 10);
            n /= 10;
        }
    }
    
    // Reverse and copy integer part
    for (int i = temp_pos - 1; i >= 0; i--) {
        if (pos < size - 1) buffer[pos++] = temp[i];
    }
    
    // Add decimal point and fractional part
    if (pos < size - 1) buffer[pos++] = '.';
    if (pos < size - 1) buffer[pos++] = '0' + (frac_part / 10);
    if (pos < size - 1) buffer[pos++] = '0' + (frac_part % 10);
    
    buffer[pos] = '\0';
}

static void calc_redraw(window_t* win) {
    calc_state_t* state = (calc_state_t*)win->user_data;
    window_clear_content(win, COLOR_LIGHT_GRAY);
    
    // Display
    window_fill_rect(win, 5, 5, win->content_width - 10, 22, COLOR_WHITE);
    window_draw_rect(win, 5, 5, win->content_width - 10, 22, COLOR_BLACK);
    int text_x = win->content_width - 15 - (strlen(state->display) * 8);
    if (text_x < 10) text_x = 10;
    window_print(win, text_x, 10, state->display, COLOR_BLACK);
    
    // Buttons
    const char* labels[] = {"7","8","9","/","4","5","6","*","1","2","3","-","C","0","=","+"};
    for (int i = 0; i < 16; i++) {
        int row = i / 4, col = i % 4;
        int x = 10 + col * 38, y = 35 + row * 28;
        uint8_t color = COLOR_LIGHT_BLUE;
        if (col == 3) color = COLOR_YELLOW;
        if (row == 3 && col == 0) color = COLOR_RED;
        if (row == 3 && col == 2) color = COLOR_GREEN;
        window_fill_rect(win, x, y, 34, 24, color);
        window_draw_rect(win, x, y, 34, 24, COLOR_BLACK);
        window_print(win, x + 13, y + 8, labels[i], COLOR_BLACK);
    }
}

static void calc_click(window_t* win, int x, int y) {
    calc_state_t* state = (calc_state_t*)win->user_data;
    const char* labels[] = {"7","8","9","/","4","5","6","*","1","2","3","-","C","0","=","+"};
    
    for (int i = 0; i < 16; i++) {
        int row = i / 4, col = i % 4;
        int bx = 10 + col * 38, by = 35 + row * 28;
        if (x >= bx && x < bx + 34 && y >= by && y < by + 24) {
            const char* label = labels[i];
            
            if (strcmp(label, "C") == 0) {
                // Clear everything
                strcpy(state->display, "0");
                state->value1 = 0; 
                state->value2 = 0;
                state->operation = '\0'; 
                state->new_number = true;
                
            } else if (strcmp(label, "=") == 0) {
                // Calculate result
                if (state->operation != '\0') {
                    state->value2 = simple_atof(state->display);
                    double result = 0;
                    if (state->operation == '+') result = state->value1 + state->value2;
                    else if (state->operation == '-') result = state->value1 - state->value2;
                    else if (state->operation == '*') result = state->value1 * state->value2;
                    else if (state->operation == '/') {
                        result = state->value2 != 0 ? state->value1 / state->value2 : 0;
                    }
                    double_to_str(result, state->display, sizeof(state->display));
                    state->value1 = result;
                    state->operation = '\0';
                    state->new_number = true;
                }
                
            } else if (strchr("+-*/", label[0])) {
                // Operation button pressed
                // If we already have a pending operation, calculate it first
                if (state->operation != '\0' && !state->new_number) {
                    state->value2 = simple_atof(state->display);
                    double result = 0;
                    if (state->operation == '+') result = state->value1 + state->value2;
                    else if (state->operation == '-') result = state->value1 - state->value2;
                    else if (state->operation == '*') result = state->value1 * state->value2;
                    else if (state->operation == '/') {
                        result = state->value2 != 0 ? state->value1 / state->value2 : 0;
                    }
                    double_to_str(result, state->display, sizeof(state->display));
                    state->value1 = result;
                } else {
                    state->value1 = simple_atof(state->display);
                }
                state->operation = label[0];
                state->new_number = true;
                
            } else {
                // Number or decimal point
                if (state->new_number) {
                    strcpy(state->display, label);
                    state->new_number = false;
                } else {
                    if (strlen(state->display) < 15) {
                        // Don't allow multiple decimal points
                        if (strcmp(label, ".") == 0 && strchr(state->display, '.')) {
                            // Already has decimal, ignore
                        } else {
                            if (strcmp(state->display, "0") == 0 && strcmp(label, ".") != 0) {
                                strcpy(state->display, label);
                            } else {
                                strcat(state->display, label);
                            }
                        }
                    }
                }
            }
            calc_redraw(win);
            break;
        }
    }
}

static void launch_calculator(void) {
    calculator_app();
}

// ===== PAINT APP =====
typedef struct {
    uint8_t current_color;
    bool drawing;
    int last_x, last_y;
    int brush_size;
} paint_state_t;

static void paint_draw_line(window_t* win, int x0, int y0, int x1, int y1, uint8_t color, int size) {
    // Bresenham's line algorithm with brush size
    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;
    
    while (1) {
        // Draw brush at current position
        for (int by = -size; by <= size; by++) {
            for (int bx = -size; bx <= size; bx++) {
                if (bx*bx + by*by <= size*size) {  // Circle brush
                    int px = x0 + bx;
                    int py = y0 + by;
                    if (px >= 0 && px < win->content_width && py >= 0 && py < win->content_height - 30) {
                        window_putpixel(win, px, py, color);
                    }
                }
            }
        }
        
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx) { err += dx; y0 += sy; }
    }
}

static void paint_redraw(window_t* win) {
    paint_state_t* state = (paint_state_t*)win->user_data;
    int palette_y = win->content_height - 15;
    uint8_t colors[] = {COLOR_BLACK, COLOR_RED, COLOR_GREEN, COLOR_BLUE, 
                        COLOR_YELLOW, COLOR_MAGENTA, COLOR_CYAN, COLOR_WHITE};
    
    // Title bar
    window_print(win, 5, palette_y - 14, "Colors:", COLOR_BLACK);
    
    // Color palette with selection indicator
    for (int i = 0; i < 8; i++) {
        window_fill_rect(win, i * 26 + 5, palette_y, 22, 12, colors[i]);
        // Highlight selected color
        if (colors[i] == state->current_color) {
            window_draw_rect(win, i * 26 + 4, palette_y - 1, 24, 14, COLOR_WHITE);
            window_draw_rect(win, i * 26 + 5, palette_y, 22, 12, COLOR_BLACK);
        } else {
            window_draw_rect(win, i * 26 + 5, palette_y, 22, 12, COLOR_DARK_GRAY);
        }
    }
    
    // Clear button
    window_fill_rect(win, win->content_width - 40, palette_y, 35, 12, COLOR_LIGHT_GRAY);
    window_draw_rect(win, win->content_width - 40, palette_y, 35, 12, COLOR_DARK_GRAY);
    window_print(win, win->content_width - 37, palette_y + 2, "Clear", COLOR_BLACK);
}

static void paint_click(window_t* win, int x, int y) {
    paint_state_t* state = (paint_state_t*)win->user_data;
    int palette_y = win->content_height - 15;
    uint8_t colors[] = {COLOR_BLACK, COLOR_RED, COLOR_GREEN, COLOR_BLUE, 
                        COLOR_YELLOW, COLOR_MAGENTA, COLOR_CYAN, COLOR_WHITE};
    
    // Check color palette clicks
    if (y >= palette_y && y < palette_y + 12) {
        if (x >= 5 && x < 213) {
            int color_idx = (x - 5) / 26;
            if (color_idx < 8) {
                state->current_color = colors[color_idx];
                paint_redraw(win);
            }
        }
        // Check Clear button
        else if (x >= win->content_width - 40 && x < win->content_width - 5) {
            window_clear_content(win, COLOR_WHITE);
            paint_redraw(win);
        }
    }
    // Drawing area
    else if (y >= 0 && y < palette_y - 16) {
        state->drawing = true;
        paint_draw_line(win, x, y, x, y, state->current_color, state->brush_size);
        state->last_x = x;
        state->last_y = y;
    }
}

static void paint_handle_drag(window_t* win, int x, int y) {
    paint_state_t* state = (paint_state_t*)win->user_data;
    int palette_y = win->content_height - 15;
    
    if (state->drawing && y >= 0 && y < palette_y - 16) {
        if (state->last_x >= 0 && state->last_y >= 0) {
            paint_draw_line(win, state->last_x, state->last_y, x, y, 
                          state->current_color, state->brush_size);
        }
        state->last_x = x;
        state->last_y = y;
    }
}

static void paint_key(window_t* win, char c) {
    paint_state_t* state = (paint_state_t*)win->user_data;
    
    // Brush size controls
    if (c == '+' || c == '=') {
        if (state->brush_size < 5) {
            state->brush_size++;
        }
    } else if (c == '-' || c == '_') {
        if (state->brush_size > 0) {
            state->brush_size--;
        }
    }
    // Color shortcuts
    else if (c >= '1' && c <= '8') {
        uint8_t colors[] = {COLOR_BLACK, COLOR_RED, COLOR_GREEN, COLOR_BLUE, 
                           COLOR_YELLOW, COLOR_MAGENTA, COLOR_CYAN, COLOR_WHITE};
        state->current_color = colors[c - '1'];
        paint_redraw(win);
    }
    // Clear with 'c' or 'C'
    else if (c == 'c' || c == 'C') {
        window_clear_content(win, COLOR_WHITE);
        paint_redraw(win);
    }
}

static void launch_paint(void) {
    paint_app_windowed(NULL);
}

// ===== FILE MANAGER APP =====
typedef struct {
    char current_path[128];
    fs_dirent_t entries[32];
    int entry_count;
    int scroll_offset;
    int selected;
    int last_click_item;
    unsigned int last_click_time;
} filemgr_state_t;

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

static void filemgr_load_dir(filemgr_state_t* state) {
    normalize_path(state->current_path);
    
    // Get directory listing
    state->entry_count = fs_list_dir(state->current_path, state->entries, 32);
    
    // Add ".." parent directory entry if not at root
    if (strcmp(state->current_path, "/") != 0) {
        // Shift entries down to make room for ".."
        for (int i = state->entry_count; i > 0; i--) {
            state->entries[i] = state->entries[i-1];
        }
        // Add ".." entry
        state->entries[0].inode = 0;
        strcpy(state->entries[0].name, "..");
        state->entry_count++;
    }
    
    state->scroll_offset = 0;
    state->selected = -1;
}

static void filemgr_redraw(window_t* win) {
    filemgr_state_t* state = (filemgr_state_t*)win->user_data;
    window_clear_content(win, COLOR_WHITE);
    
    // Header with better formatting
    window_fill_rect(win, 0, 0, win->content_width, 28, COLOR_LIGHT_GRAY);
    window_print(win, 5, 5, "File Explorer", COLOR_BLACK);
    
    // Current path display (truncate if too long)
    char path_str[50];
    if (strlen(state->current_path) > 28) {
        snprintf(path_str, sizeof(path_str), "...%s", 
                state->current_path + strlen(state->current_path) - 25);
    } else {
        snprintf(path_str, sizeof(path_str), "%s", state->current_path);
    }
    window_print(win, 5, 16, path_str, COLOR_DARK_GRAY);
    
    // File list area background
    window_fill_rect(win, 2, 30, win->content_width - 4, win->content_height - 48, COLOR_WHITE);
    window_draw_rect(win, 2, 30, win->content_width - 4, win->content_height - 48, COLOR_DARK_GRAY);
    
    // List entries
    int y = 35;
    int visible_items = (win->content_height - 50) / 11;
    int max_display = state->scroll_offset + visible_items;
    if (max_display > state->entry_count) max_display = state->entry_count;
    
    for (int i = state->scroll_offset; i < max_display; i++) {
        // Determine type and color
        uint8_t color = COLOR_BLACK;
        uint8_t icon_color = COLOR_BLACK;
        char icon[3] = "";
        
        if (strcmp(state->entries[i].name, "..") == 0) {
            strcpy(icon, "^");
            color = COLOR_MAGENTA;
            icon_color = COLOR_MAGENTA;
        } else {
            // Check if directory or file
            char full_path[128];
            if (strcmp(state->current_path, "/") == 0) {
                snprintf(full_path, sizeof(full_path), "/%s", state->entries[i].name);
            } else {
                snprintf(full_path, sizeof(full_path), "%s/%s", 
                        state->current_path, state->entries[i].name);
            }
            
            fs_inode_t inode;
            if (fs_stat(full_path, &inode)) {
                if (inode.type == 2) {  // Directory
                    strcpy(icon, "+");
                    color = COLOR_BLUE;
                    icon_color = COLOR_BLUE;
                } else {  // File
                    strcpy(icon, "*");
                    color = COLOR_BLACK;
                    icon_color = COLOR_GREEN;
                }
            }
        }
        
        // Selection highlighting
        uint8_t bg = COLOR_WHITE;
        if (i == state->selected) {
            bg = COLOR_LIGHT_CYAN;
            window_fill_rect(win, 4, y - 2, win->content_width - 8, 11, bg);
        }
        
        // Draw icon and name
        window_print(win, 8, y, icon, icon_color);
        
        // Truncate long filenames
        char display_name[35];
        if (strlen(state->entries[i].name) > 32) {
            strncpy(display_name, state->entries[i].name, 29);
            display_name[29] = '.';
            display_name[30] = '.';
            display_name[31] = '.';
            display_name[32] = '\0';
        } else {
            strcpy(display_name, state->entries[i].name);
        }
        
        window_print(win, 18, y, display_name, color);
        y += 11;
    }
    
    // Status bar at bottom
    window_fill_rect(win, 0, win->content_height - 16, win->content_width, 16, COLOR_LIGHT_GRAY);
    char status[50];
    snprintf(status, sizeof(status), "%d items | W/S:scroll Bksp:up", state->entry_count);
    window_print(win, 5, win->content_height - 12, status, COLOR_DARK_GRAY);
}

static void filemgr_click(window_t* win, int x, int y) {
    filemgr_state_t* state = (filemgr_state_t*)win->user_data;
    
    // Check if clicking in file list area
    if (y >= 35 && y < win->content_height - 18) {
        int item_idx = state->scroll_offset + (y - 35) / 11;
        
        if (item_idx >= 0 && item_idx < state->entry_count) {
            // Check for double-click (same item clicked twice quickly)
            bool is_double_click = (state->selected == item_idx);
            
            if (is_double_click) {
                // Navigate into directory
                if (strcmp(state->entries[item_idx].name, "..") == 0) {
                    // Go up one directory
                    char* last_slash = strrchr(state->current_path, '/');
                    if (last_slash && last_slash != state->current_path) {
                        *last_slash = '\0';
                    } else {
                        strcpy(state->current_path, "/");
                    }
                    filemgr_load_dir(state);
                } else {
                    // Check if it's a directory
                    char full_path[128];
                    if (strcmp(state->current_path, "/") == 0) {
                        snprintf(full_path, sizeof(full_path), "/%s", 
                                state->entries[item_idx].name);
                    } else {
                        snprintf(full_path, sizeof(full_path), "%s/%s", 
                                state->current_path, state->entries[item_idx].name);
                    }
                    
                    fs_inode_t inode;
                    if (fs_stat(full_path, &inode) && inode.type == 2) {
                        // It's a directory, navigate into it
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

static void filemgr_key(window_t* win, char c) {
    filemgr_state_t* state = (filemgr_state_t*)win->user_data;
    bool needs_redraw = false;
    
    if (c == 'w' || c == 'W') {
        // Scroll up or move selection up
        if (state->selected > 0) {
            state->selected--;
            if (state->selected < state->scroll_offset) {
                state->scroll_offset = state->selected;
            }
            needs_redraw = true;
        }
    } else if (c == 's' || c == 'S') {
        // Scroll down or move selection down
        if (state->selected < state->entry_count - 1) {
            state->selected++;
            int visible_items = (win->content_height - 50) / 11;
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
            if (strcmp(state->entries[state->selected].name, "..") == 0) {
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
                char full_path[128];
                if (strcmp(state->current_path, "/") == 0) {
                    snprintf(full_path, sizeof(full_path), "/%s", 
                            state->entries[state->selected].name);
                } else {
                    snprintf(full_path, sizeof(full_path), "%s/%s", 
                            state->current_path, state->entries[state->selected].name);
                }
                
                fs_inode_t inode;
                if (fs_stat(full_path, &inode) && inode.type == 2) {
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

static void launch_file_manager(void) {
    file_manager_app();
}

// ===== TEXT EDITOR APP =====
#define EDITOR_MAX_LINES 100
#define EDITOR_MAX_LINE_LENGTH 80
#define EDITOR_VISIBLE_LINES 15
#define EDITOR_VISIBLE_COLS 35
#define EDITOR_MENU_HEIGHT 14
#define EDITOR_MAX_FILES 20

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

// Forward declarations for editor functions
static void editor_redraw(window_t* win);
static void editor_load_file(editor_state_t* state, const char* filepath);
static void editor_save_file(editor_state_t* state);
static void editor_new_file(editor_state_t* state);
static void editor_insert_char(editor_state_t* state, char c);
static void editor_delete_char(editor_state_t* state);
static void editor_new_line(editor_state_t* state);
static void editor_click(window_t* win, int x, int y);
static void editor_handle_mouse_move(window_t* win, int x, int y);
static void editor_key(window_t* win, char c);

static void editor_redraw(window_t* win) {
    editor_state_t* state = (editor_state_t*)win->user_data;
    
    // Clear content area
    window_clear_content(win, COLOR_WHITE);
    
    // Draw menu bar
    window_fill_rect(win, 0, 0, win->content_width, EDITOR_MENU_HEIGHT, COLOR_LIGHT_GRAY);
    window_draw_rect(win, 0, 0, win->content_width, EDITOR_MENU_HEIGHT, COLOR_DARK_GRAY);
    
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
        window_print(win, win->content_width - 15, 2, "*", COLOR_RED);
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
    int status_y = win->content_height - 14;
    window_fill_rect(win, 0, status_y, win->content_width, 14, COLOR_LIGHT_GRAY);
    window_draw_rect(win, 0, status_y, win->content_width, 1, COLOR_DARK_GRAY);
    
    char status[64];
    snprintf(status, sizeof(status), "Ln %d/%d Col %d", 
             state->cursor_line + 1, state->line_count, state->cursor_col + 1);
    window_print(win, 5, status_y + 2, status, COLOR_BLACK);
    
    // Draw help text
    window_print(win, win->content_width - 100, status_y + 2, 
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

static void editor_load_file(editor_state_t* state, const char* filepath) {
    // Read file content
    uint8_t buffer[4096];
    int bytes_read = fs_read_file(filepath, buffer, sizeof(buffer) - 1, 0);
    
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
    
    // Calculate total text size
    int total_size = 0;
    for (int i = 0; i < state->line_count; i++) {
        total_size += strlen(state->lines[i]) + 1;  // +1 for newline
    }
    
    if (total_size == 0) {
        state->modified = false;
        return;
    }
    
    // Allocate buffer for file content
    uint8_t* buffer = (uint8_t*)kmalloc(total_size + 1);
    if (!buffer) return;
    
    // Build file content
    int pos = 0;
    for (int i = 0; i < state->line_count; i++) {
        int len = strlen(state->lines[i]);
        memcpy(buffer + pos, state->lines[i], len);
        pos += len;
        buffer[pos++] = '\n';
    }
    buffer[pos] = '\0';
    
    // Try to create file (in case it doesn't exist)
    fs_create_file(filepath);
    
    // Write to file
    int written = fs_write_file(filepath, buffer, pos, 0);
    
    kfree(buffer);
    
    if (written > 0) {
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

static void editor_click(window_t* win, int x, int y) {
    editor_state_t* state = (editor_state_t*)win->user_data;
    
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
    int status_y = win->content_height - 14;
    
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

static void editor_handle_mouse_move(window_t* win, int x, int y) {
    editor_state_t* state = (editor_state_t*)win->user_data;
    
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

static void editor_key(window_t* win, char c) {
    editor_state_t* state = (editor_state_t*)win->user_data;
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
            int visible_lines = (win->content_height - 32) / 10;
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
            int visible_lines = (win->content_height - 32) / 10;
            if (state->cursor_line >= state->scroll_offset + visible_lines) state->scroll_offset++;
            needs_redraw = true;
        }
    }
    
    if (needs_redraw) {
        editor_redraw(win);
    }
}

static void launch_text_editor(void) {
    static int editor_count = 0;
    char title[64];
    snprintf(title, sizeof(title), "Text Editor %d", ++editor_count);
    
    // Calculate window size relative to screen (approx 70% width, 65% height)
    int screen_w = graphics_get_width();
    int screen_h = graphics_get_height();
    int win_w = screen_w * 70 / 100;
    int win_h = screen_h * 65 / 100;
    if (win_w < 270) win_w = 270;  // Minimum size
    if (win_h < 240) win_h = 240;
    
    window_t* win = window_create(80 + (editor_count * 20), 60 + (editor_count * 20), 
                                   win_w, win_h, title);
    if (win) {
        editor_state_t* state = (editor_state_t*)kmalloc(sizeof(editor_state_t));
        
        // Initialize empty document
        for (int i = 0; i < EDITOR_MAX_LINES; i++) {
            state->lines[i][0] = '\0';
        }
        state->line_count = 1;
        state->cursor_line = 0;
        state->cursor_col = 0;
        state->scroll_offset = 0;
        state->modified = false;
        state->menu_open = false;
        state->menu_hover = -1;
        state->has_filename = false;
        state->filename[0] = '\0';
        state->window = win;
        
        win->user_data = state;
        win->on_click = editor_click;
        win->on_key = editor_key;
        win->on_drag = editor_handle_mouse_move;
        
        editor_redraw(win);
    }
}

static void launch_about(void) {
    // Create about window
    static int about_count = 0;
    about_count++;
    
    // Calculate window size relative to screen (approx 40% width, 40% height)
    int screen_w = graphics_get_width();
    int screen_h = graphics_get_height();
    int win_w = screen_w * 40 / 100;
    int win_h = screen_h * 40 / 100;
    if (win_w < 220) win_w = 220;  // Minimum size
    if (win_h < 160) win_h = 160;
    
    window_t* win = window_create(100 + (about_count * 15), 80 + (about_count * 15), 
                                   win_w, win_h, "About RohanOS");
    if (win) {
        window_print(win, 5, 5, "RohanOS v0.3", COLOR_BLACK);
        window_print(win, 5, 20, "Desktop Environment", COLOR_DARK_GRAY);
        window_print(win, 5, 40, "Features:", COLOR_BLACK);
        window_print(win, 5, 55, "- Multi-window GUI", COLOR_DARK_GRAY);
        window_print(win, 5, 70, "- Application menu", COLOR_DARK_GRAY);
        window_print(win, 5, 85, "- Mouse support", COLOR_DARK_GRAY);
    }
}
