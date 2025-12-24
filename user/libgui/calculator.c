#include <gui_window.h>
#include <graphics.h>
#include <mouse.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

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

static window_t* calc_window = NULL;
static calc_state_t calc_state;

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
    calc_state_t* state = (calc_state_t*)window_get_user_data(win);
    int content_w = window_content_width(win);
    window_clear_content(win, COLOR_LIGHT_GRAY);
    
    // Display
    window_fill_rect(win, 5, 5, content_w - 10, 22, COLOR_WHITE);
    window_draw_rect(win, 5, 5, content_w - 10, 22, COLOR_BLACK);
    int text_x = content_w - 15 - (strlen(state->display) * 8);
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

static void calc_handle_label(window_t* win, const char* label) {
    calc_state_t* state = (calc_state_t*)window_get_user_data(win);

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
                if (strcmp(label, ".") == 0 && strchr(state->display, '.')) {
                    return;
                }
                if (strcmp(state->display, "0") == 0 && strcmp(label, ".") != 0) {
                    strcpy(state->display, label);
                } else {
                    strcat(state->display, label);
                }
            }
        }
    }
}

static void calc_click(window_t* win, int x, int y) {
    const char* labels[] = {"7","8","9","/","4","5","6","*","1","2","3","-","C","0","=","+"};
    
    for (int i = 0; i < 16; i++) {
        int row = i / 4, col = i % 4;
        int bx = 10 + col * 38, by = 35 + row * 28;
        if (x >= bx && x < bx + 34 && y >= by && y < by + 24) {
            const char* label = labels[i];
            calc_handle_label(win, label);
            calc_redraw(win);
            break;
        }
    }
}

static void calc_on_mouse_down(window_t* win, int x, int y, int buttons) {
    if (buttons & MOUSE_LEFT_BUTTON) {
        calc_click(win, x, y);
    }
}

static void calc_on_key(window_t* win, int key) {
    char label[2] = {(char)key, '\0'};

    if (key >= '0' && key <= '9') {
        calc_handle_label(win, label);
    } else if (key == '+' || key == '-' || key == '*' || key == '/') {
        calc_handle_label(win, label);
    } else if (key == '\n' || key == '=') {
        calc_handle_label(win, "=");
    } else if (key == 'c' || key == 'C') {
        calc_handle_label(win, "C");
    }
    calc_redraw(win);
}

window_t* gui_calc_create_window(int x, int y) {
    if (calc_window && uwm_window_is_open(calc_window)) {
        return calc_window;
    }

    window_t* win = window_create(x, y, 180, 190, "Calculator");
    if (!win) return NULL;

    memset(&calc_state, 0, sizeof(calc_state));
    strcpy(calc_state.display, "0");
    calc_state.new_number = true;

    window_set_handlers(win, calc_redraw, calc_on_mouse_down, NULL, NULL, NULL, calc_on_key, &calc_state);
    calc_window = win;
    return win;
}
