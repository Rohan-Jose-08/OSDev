#include <kernel/calculator.h>
#include <kernel/window.h>
#include <kernel/menu_bar.h>
#include <kernel/graphics.h>
#include <kernel/keyboard.h>
#include <kernel/mouse.h>
#include <kernel/kmalloc.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Simple atof implementation
static double simple_atof(const char* str) {
    double result = 0.0;
    double fraction = 0.0;
    int divisor = 1;
    bool negative = false;
    bool after_decimal = false;
    
    if (*str == '-') {
        negative = true;
        str++;
    }
    
    while (*str) {
        if (*str >= '0' && *str <= '9') {
            if (after_decimal) {
                fraction = fraction * 10.0 + (*str - '0');
                divisor *= 10;
            } else {
                result = result * 10.0 + (*str - '0');
            }
        } else if (*str == '.') {
            after_decimal = true;
        }
        str++;
    }
    
    result += fraction / divisor;
    return negative ? -result : result;
}

// Convert double to string
static void double_to_string(double value, char* buffer, size_t size) {
    if (size == 0) return;
    
    // Handle negative numbers
    int pos = 0;
    if (value < 0) {
        buffer[pos++] = '-';
        value = -value;
    }
    
    // Get integer part
    long long int_part = (long long)value;
    double frac_part = value - int_part;
    
    // Convert integer part
    char temp[32];
    int temp_pos = 0;
    
    if (int_part == 0) {
        temp[temp_pos++] = '0';
    } else {
        while (int_part > 0 && temp_pos < 32) {
            temp[temp_pos++] = '0' + (int_part % 10);
            int_part /= 10;
        }
    }
    
    // Copy integer part in reverse
    for (int i = temp_pos - 1; i >= 0 && pos < (int)size - 1; i--) {
        buffer[pos++] = temp[i];
    }
    
    // Add decimal part if needed
    if (frac_part > 0.0000001 && pos < (int)size - 1) {
        buffer[pos++] = '.';
        
        // Get up to 6 decimal places
        for (int i = 0; i < 6 && pos < (int)size - 1; i++) {
            frac_part *= 10;
            int digit = (int)frac_part;
            buffer[pos++] = '0' + digit;
            frac_part -= digit;
            
            // Stop if we've reached the end of precision
            if (frac_part < 0.0000001) break;
        }
        
        // Remove trailing zeros
        while (pos > 0 && buffer[pos - 1] == '0') {
            pos--;
        }
        
        // Remove trailing decimal point
        if (pos > 0 && buffer[pos - 1] == '.') {
            pos--;
        }
    }
    
    buffer[pos] = '\0';
}

#define CALC_DISPLAY_HEIGHT 24
#define CALC_BUTTON_WIDTH   32
#define CALC_BUTTON_HEIGHT  24
#define CALC_BUTTON_PADDING 3
#define CALC_COLS           4
#define CALC_ROWS           5

typedef struct {
    window_t* window;
    menu_bar_t* menu_bar;
    char display[32];
    double accumulator;
    double current_value;
    char operation;
    bool new_number;
    bool error;
    int pressed_row;
    int pressed_col;
} calculator_state_t;

static calculator_state_t* calc_state = NULL;

// Button labels
static const char* button_labels[CALC_ROWS][CALC_COLS] = {
    {"7", "8", "9", "/"},
    {"4", "5", "6", "*"},
    {"1", "2", "3", "-"},
    {"0", ".", "=", "+"},
    {"C", "CE", "", ""}
};

// Forward declarations
static void calc_draw_display(void);
static void calc_draw_buttons(void);
static void calc_process_button(const char* label);
static void calc_on_destroy(window_t* window);

// Menu callbacks
static void calc_menu_clear(window_t* window, void* user_data) {
    if (!calc_state) return;
    calc_state->display[0] = '0';
    calc_state->display[1] = '\0';
    calc_state->accumulator = 0;
    calc_state->current_value = 0;
    calc_state->operation = 0;
    calc_state->new_number = true;
    calc_state->error = false;
    calc_state->pressed_row = -1;
    calc_state->pressed_col = -1;
    calc_draw_display();
    window_draw(window);
}

static void calc_menu_about(window_t* window, void* user_data) {
    (void)window;
    (void)user_data;
    // Simple About dialog
    window_t* about = window_create(0, 0, 220, 80, "About Calculator");
    if (!about) return;
    window_clear_content(about, WINDOW_COLOR_BACKGROUND);
    window_print(about, 10, 18, "Calculator v1.0", COLOR_BLACK);
    window_print(about, 10, 34, "Author: Your Name", COLOR_BLACK);
    window_print(about, 10, 50, "Use Backspace to delete, Esc to close", COLOR_BLACK);
    window_draw(about);
}

static void calc_menu_close(window_t* window, void* user_data) {
    if (calc_state) {
        window_destroy(calc_state->window);
        menu_bar_destroy(calc_state->menu_bar);
        kfree(calc_state);
        calc_state = NULL;
    }
}

// Draw the calculator display
static void calc_draw_display(void) {
    if (!calc_state || !calc_state->window) return;
    
    window_t* window = calc_state->window;
    int menu_height = menu_bar_get_height();
    
    // Draw display background
    window_fill_rect(window, 5, menu_height + 5, 
                    window->content_width - 10, CALC_DISPLAY_HEIGHT, 
                    COLOR_WHITE);
    
    // Draw display border
    window_draw_rect(window, 5, menu_height + 5, 
                    window->content_width - 10, CALC_DISPLAY_HEIGHT, 
                    COLOR_BLACK);
    
    // Draw display text (right-aligned)
    int text_len = strlen(calc_state->display);
    int text_x = window->content_width - 15 - (text_len * 8);
    if (text_x < 10) text_x = 10;
    
    uint8_t text_color = calc_state->error ? COLOR_RED : COLOR_BLACK;
    window_print(window, text_x, menu_height + 15, calc_state->display, text_color);
}

// Draw calculator buttons
static void calc_draw_buttons(void) {
    if (!calc_state || !calc_state->window) return;
    
    window_t* window = calc_state->window;
    int menu_height = menu_bar_get_height();
    int start_y = menu_height + CALC_DISPLAY_HEIGHT + 15;
    
    for (int row = 0; row < CALC_ROWS; row++) {
        for (int col = 0; col < CALC_COLS; col++) {
            const char* label = button_labels[row][col];
            if (label[0] == '\0') continue;
            
            int x = 5 + col * (CALC_BUTTON_WIDTH + CALC_BUTTON_PADDING);
            int y = start_y + row * (CALC_BUTTON_HEIGHT + CALC_BUTTON_PADDING);
            
            // Draw button background
            uint8_t btn_color = COLOR_LIGHT_GRAY;
            if (label[0] >= '0' && label[0] <= '9') {
                btn_color = COLOR_WHITE;
            } else if (label[0] == '=' || label[0] == 'C') {
                btn_color = COLOR_LIGHT_CYAN;
            } else {
                btn_color = COLOR_YELLOW;
            }
            // Pressed visual
            if (calc_state->pressed_row == row && calc_state->pressed_col == col) {
                btn_color = COLOR_DARK_GRAY;
            }
            
            window_fill_rect(window, x, y, CALC_BUTTON_WIDTH, CALC_BUTTON_HEIGHT, btn_color);
            window_draw_rect(window, x, y, CALC_BUTTON_WIDTH, CALC_BUTTON_HEIGHT, COLOR_BLACK);
            
            // Draw button label (centered)
            int label_len = strlen(label);
            int label_x = x + (CALC_BUTTON_WIDTH - label_len * 8) / 2;
            int label_y = y + (CALC_BUTTON_HEIGHT - 8) / 2;
            window_print(window, label_x, label_y, label, COLOR_BLACK);
        }
    }
}

// Process button press
static void calc_process_button(const char* label) {
    if (!calc_state || !label) return;
    
    if (calc_state->error && label[0] != 'C') {
        return;
    }
    
    // Number or decimal point
    if ((label[0] >= '0' && label[0] <= '9') || label[0] == '.') {
        if (calc_state->new_number) {
            if (label[0] == '.') {
                strcpy(calc_state->display, "0.");
            } else {
                calc_state->display[0] = label[0];
                calc_state->display[1] = '\0';
            }
            calc_state->new_number = false;
        } else {
            int len = strlen(calc_state->display);
            if (len < sizeof(calc_state->display) - 1) {
                if (label[0] == '.' && strchr(calc_state->display, '.')) {
                    return; // Already has decimal point
                }
                calc_state->display[len] = label[0];
                calc_state->display[len + 1] = '\0';
            }
        }
    }
    // Clear
    else if (strcmp(label, "C") == 0) {
        calc_state->display[0] = '0';
        calc_state->display[1] = '\0';
        calc_state->accumulator = 0;
        calc_state->current_value = 0;
        calc_state->operation = 0;
        calc_state->new_number = true;
        calc_state->error = false;
    }
    // Clear entry
    else if (strcmp(label, "CE") == 0) {
        calc_state->display[0] = '0';
        calc_state->display[1] = '\0';
        calc_state->new_number = true;
    }
    // Operations
    else if (label[0] == '+' || label[0] == '-' || label[0] == '*' || 
             label[0] == '/' || label[0] == '=') {
        
        calc_state->current_value = simple_atof(calc_state->display);
        
        // Perform previous operation if there was one
        if (calc_state->operation != 0) {
            // Perform previous operation
            switch (calc_state->operation) {
                case '+': calc_state->accumulator += calc_state->current_value; break;
                case '-': calc_state->accumulator -= calc_state->current_value; break;
                case '*': calc_state->accumulator *= calc_state->current_value; break;
                case '/':
                    if (calc_state->current_value == 0) {
                        strcpy(calc_state->display, "Error");
                        calc_state->error = true;
                        calc_state->operation = 0;
                        calc_state->new_number = true;
                        calc_draw_display();
                        window_draw(calc_state->window);
                        return;
                    }
                    calc_state->accumulator /= calc_state->current_value;
                    break;
            }
        } else {
            // First operation - just store the value
            calc_state->accumulator = calc_state->current_value;
        }
        
        // Display result
        double_to_string(calc_state->accumulator, calc_state->display, 
                        sizeof(calc_state->display));
        
        // Set new operation (or clear if =)
        if (label[0] != '=') {
            calc_state->operation = label[0];
        } else {
            calc_state->operation = 0;
        }
        
        calc_state->new_number = true;
    }
    
    calc_draw_display();
    window_draw(calc_state->window);
}

// Priority click handler for menu bar
static bool calc_on_priority_click(window_t* window, int x, int y) {
    if (!calc_state) return false;
    
    // Check if click is in menu bar
    if (menu_bar_handle_click(calc_state->menu_bar, x, y)) {
        // Redraw everything to clear old dropdown graphics
        calc_draw_display();
        calc_draw_buttons();
        menu_bar_draw(calc_state->menu_bar);
        window_draw(window);
        return true;  // Menu bar handled the click
    }
    
    return false;  // Menu bar didn't handle, proceed with normal click handling
}

// Window click handler
static void calc_on_click(window_t* window, int x, int y) {
    if (!calc_state) return;
    
    // Check if click is on a button
    int menu_height = menu_bar_get_height();
    int start_y = menu_height + CALC_DISPLAY_HEIGHT + 15;
    
    for (int row = 0; row < CALC_ROWS; row++) {
        for (int col = 0; col < CALC_COLS; col++) {
            const char* label = button_labels[row][col];
            if (label[0] == '\0') continue;
            
            int btn_x = 5 + col * (CALC_BUTTON_WIDTH + CALC_BUTTON_PADDING);
            int btn_y = start_y + row * (CALC_BUTTON_HEIGHT + CALC_BUTTON_PADDING);
            
            if (x >= btn_x && x < btn_x + CALC_BUTTON_WIDTH &&
                y >= btn_y && y < btn_y + CALC_BUTTON_HEIGHT) {
                /* show pressed visual briefly */
                calc_state->pressed_row = row;
                calc_state->pressed_col = col;
                calc_draw_buttons();
                window_draw(window);

                calc_process_button(label);

                calc_state->pressed_row = -1;
                calc_state->pressed_col = -1;
                calc_draw_buttons();
                window_draw(window);
                return;
            }
        }
    }
}

// Window key handler
static void calc_on_key(window_t* window, char key) {
    if (!calc_state) return;
    
    char label[2] = {key, '\0'};
    
    if ((key >= '0' && key <= '9') || key == '.') {
        calc_process_button(label);
    } else if (key == '+' || key == '-' || key == '*' || key == '/') {
        calc_process_button(label);
    } else if (key == '\n' || key == '=') {
        calc_process_button("=");
    } else if (key == 'c' || key == 'C') {
        calc_process_button("C");
    }
    // Backspace (ASCII BS or DEL)
    else if (key == '\b' || key == 127) {
        int len = strlen(calc_state->display);
        if (len > 1) {
            calc_state->display[len - 1] = '\0';
        } else {
            strcpy(calc_state->display, "0");
            calc_state->new_number = true;
        }
        calc_draw_display();
        window_draw(calc_state->window);
    }
    // Escape: close calculator
    else if (key == 27) {
        if (calc_state && calc_state->window) window_destroy(calc_state->window);
    }
}

// Window destroy handler
static void calc_on_destroy(window_t* window) {
    if (!calc_state) return;
    
    // Clean up menu bar
    if (calc_state->menu_bar) {
        menu_bar_destroy(calc_state->menu_bar);
    }
    
    // Free state and reset global pointer
    kfree(calc_state);
    calc_state = NULL;
}

// Launch calculator application
void calculator_app(void) {
    // Only allow one instance
    if (calc_state) return;
    
    // Calculate window size with proper padding
    int win_width = CALC_COLS * (CALC_BUTTON_WIDTH + CALC_BUTTON_PADDING) + 10 + CALC_BUTTON_PADDING;
    int win_height = menu_bar_get_height() + CALC_DISPLAY_HEIGHT + 15 + 
                     CALC_ROWS * (CALC_BUTTON_HEIGHT + CALC_BUTTON_PADDING) + 10;
    
    // Ensure minimum size
    if (win_width < 160) win_width = 160;
    if (win_height < 200) win_height = 200;
    
    // Create window
    window_t* window = window_create(100, 100, win_width, win_height, "Calculator");
    if (!window) return;
    
    // Allocate state
    calc_state = (calculator_state_t*)kmalloc(sizeof(calculator_state_t));
    if (!calc_state) {
        window_destroy(window);
        return;
    }
    
    // Initialize state
    calc_state->window = window;
    calc_state->display[0] = '0';
    calc_state->display[1] = '\0';
    calc_state->accumulator = 0;
    calc_state->current_value = 0;
    calc_state->operation = 0;
    calc_state->new_number = true;
    calc_state->error = false;
    
    // Create menu bar
    calc_state->menu_bar = menu_bar_create(window);
    if (calc_state->menu_bar) {
        menu_item_t* file_menu = menu_bar_add_menu(calc_state->menu_bar, "File");
        if (file_menu) {
            menu_item_add_dropdown(file_menu, "Clear", calc_menu_clear);
            menu_item_add_separator(file_menu);
            menu_item_add_dropdown(file_menu, "Close", calc_menu_close);
        }
        
        menu_item_t* help_menu = menu_bar_add_menu(calc_state->menu_bar, "Help");
        if (help_menu) {
            menu_item_add_dropdown(help_menu, "About", calc_menu_about);
        }
    }
    
    // Set window callbacks
    window->on_priority_click = calc_on_priority_click;
    window->on_click = calc_on_click;
    window->on_key = calc_on_key;
    window->on_destroy = calc_on_destroy;
    window->user_data = calc_state;
    
    // Draw initial state
    window_clear_content(window, WINDOW_COLOR_BACKGROUND);
    if (calc_state->menu_bar) {
        menu_bar_draw(calc_state->menu_bar);
    }
    calc_draw_display();
    calc_draw_buttons();
    window_draw(window);
}
