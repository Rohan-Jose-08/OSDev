#include <gui_window.h>
#include <graphics.h>
#include <mouse.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <file_dialog.h>
#include <unistd.h>

// ===== PAINT APP =====
#define PAINT_CANVAS_W 240
#define PAINT_CANVAS_H 180
#define PAINT_TOP_BAR_HEIGHT 16
#define PAINT_MENU_WIDTH 88
#define PAINT_MENU_ITEM_HEIGHT 12
#define PAINT_MENU_PADDING 4
#define PAINT_PALETTE_COUNT 8
#define PAINT_PALETTE_X 5
#define PAINT_PALETTE_CELL_W 26
#define PAINT_PALETTE_SWATCH_W 22
#define PAINT_PALETTE_SWATCH_H 12
#define PAINT_PALETTE_Y_OFFSET 15
#define PAINT_PALETTE_LABEL_OFFSET 14

#define PAINT_FILE_MAGIC 0x544E4950  // "PINT" in little-endian
#define PAINT_FILE_VERSION 1

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t width;
    uint16_t height;
    uint16_t reserved;
} paint_file_header_t;
#pragma pack(pop)

typedef struct {
    uint8_t current_color;
    bool drawing;
    int last_x, last_y;
    int brush_size;
    int canvas_w;
    int canvas_h;
    uint8_t canvas[PAINT_CANVAS_W * PAINT_CANVAS_H];
    bool menu_open;
    int menu_x;
    int menu_y;
    int menu_hover;
    char current_file[64];
} paint_state_t;

static window_t* paint_window = NULL;
static paint_state_t paint_state;

static const char* paint_menu_items[] = {
    "New",
    "Open",
    "Save",
    "Save As",
    "Clear"
};

#define PAINT_MENU_ITEM_COUNT (int)(sizeof(paint_menu_items) / sizeof(paint_menu_items[0]))
static const uint8_t paint_palette_colors[PAINT_PALETTE_COUNT] = {
    COLOR_BLACK,
    COLOR_RED,
    COLOR_GREEN,
    COLOR_BLUE,
    COLOR_YELLOW,
    COLOR_MAGENTA,
    COLOR_CYAN,
    COLOR_WHITE
};

static void paint_redraw(window_t* win);
static void paint_canvas_clear(paint_state_t* state, uint8_t color);
static void paint_draw_canvas(window_t* win);

static int paint_abs(int value) {
    return value < 0 ? -value : value;
}

static int paint_draw_height(window_t* win) {
    int draw_h = window_content_height(win) - PAINT_TOP_BAR_HEIGHT - 31;
    return draw_h > 0 ? draw_h : 0;
}

static int paint_canvas_origin_y(void) {
    return PAINT_TOP_BAR_HEIGHT;
}

static int paint_palette_y(window_t* win) {
    return window_content_height(win) - PAINT_PALETTE_Y_OFFSET;
}

static bool paint_palette_hit_y_raw(window_t* win, int y) {
    int palette_y = paint_palette_y(win);
    int top = palette_y - PAINT_PALETTE_LABEL_OFFSET;
    int bottom = palette_y + PAINT_PALETTE_SWATCH_H;
    return y >= top && y < bottom;
}

static int paint_palette_index_at_raw(window_t* win, int x, int y) {
    if (!paint_palette_hit_y_raw(win, y)) {
        return -1;
    }
    if (x < PAINT_PALETTE_X) {
        return -1;
    }

    int rel_x = x - PAINT_PALETTE_X;
    int idx = rel_x / PAINT_PALETTE_CELL_W;
    if (idx < 0 || idx >= PAINT_PALETTE_COUNT) {
        return -1;
    }

    return idx;
}

static int paint_palette_index_at(window_t* win, int x, int y) {
    int idx = paint_palette_index_at_raw(win, x, y);
    if (idx >= 0) {
        return idx;
    }
    idx = paint_palette_index_at_raw(win, x, y + PAINT_TOP_BAR_HEIGHT);
    if (idx >= 0) {
        return idx;
    }
    return paint_palette_index_at_raw(win, x, y - PAINT_TOP_BAR_HEIGHT);
}

static bool paint_clear_hit_raw(window_t* win, int x, int y) {
    if (!paint_palette_hit_y_raw(win, y)) {
        return false;
    }
    int content_w = window_content_width(win);
    return x >= content_w - 40 && x < content_w - 5;
}

static bool paint_clear_hit(window_t* win, int x, int y) {
    if (paint_clear_hit_raw(win, x, y)) {
        return true;
    }
    if (paint_clear_hit_raw(win, x, y + PAINT_TOP_BAR_HEIGHT)) {
        return true;
    }
    if (paint_clear_hit_raw(win, x, y - PAINT_TOP_BAR_HEIGHT)) {
        return true;
    }
    return false;
}

static bool paint_handle_palette_click(window_t* win, int x, int y) {
    paint_state_t* state = (paint_state_t*)window_get_user_data(win);
    if (!state) {
        return false;
    }

    int palette_idx = paint_palette_index_at(win, x, y);
    if (palette_idx >= 0) {
        uint8_t next = paint_palette_colors[palette_idx];
        if (next != state->current_color) {
            state->current_color = next;
            paint_redraw(win);
        }
        return true;
    }

    if (paint_clear_hit(win, x, y)) {
        paint_canvas_clear(state, COLOR_WHITE);
        paint_draw_canvas(win);
        paint_redraw(win);
        return true;
    }

    return false;
}

static void paint_canvas_clear(paint_state_t* state, uint8_t color) {
    int total = state->canvas_w * state->canvas_h;
    for (int i = 0; i < total; i++) {
        state->canvas[i] = color;
    }
}

static void paint_canvas_putpixel(paint_state_t* state, int x, int y, uint8_t color) {
    if (x < 0 || y < 0 || x >= state->canvas_w || y >= state->canvas_h) {
        return;
    }
    state->canvas[y * state->canvas_w + x] = color;
}

static void paint_draw_canvas(window_t* win) {
    paint_state_t* state = (paint_state_t*)window_get_user_data(win);
    int content_w = window_content_width(win);
    int draw_h = paint_draw_height(win);
    int origin_y = paint_canvas_origin_y();
    window_fill_rect(win, 0, origin_y, content_w, draw_h, COLOR_WHITE);
    int max_w = content_w < state->canvas_w ? content_w : state->canvas_w;
    int max_h = draw_h < state->canvas_h ? draw_h : state->canvas_h;
    window_blit(win, 0, origin_y, max_w, max_h, state->canvas, state->canvas_w);
}

static int paint_read_file(const char* path, uint8_t* buffer, int max_len) {
    int fd = open(path);
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

static void paint_save_to_file(window_t* win, const char* filepath) {
    paint_state_t* state = (paint_state_t*)window_get_user_data(win);
    if (!state || !filepath) return;

    strncpy(state->current_file, filepath, sizeof(state->current_file) - 1);
    state->current_file[sizeof(state->current_file) - 1] = '\0';

    int height = paint_draw_height(win);
    if (height > state->canvas_h) height = state->canvas_h;
    int width = state->canvas_w;
    int canvas_size = width * height;
    int file_size = (int)sizeof(paint_file_header_t) + canvas_size;
    static uint8_t file_buffer[sizeof(paint_file_header_t) + PAINT_CANVAS_W * PAINT_CANVAS_H];

    if (file_size > (int)sizeof(file_buffer)) {
        return;
    }

    paint_file_header_t* header = (paint_file_header_t*)file_buffer;
    header->magic = PAINT_FILE_MAGIC;
    header->version = PAINT_FILE_VERSION;
    header->width = (uint16_t)width;
    header->height = (uint16_t)height;
    header->reserved = 0;

    uint8_t* data = file_buffer + sizeof(paint_file_header_t);
    for (int y = 0; y < height; y++) {
        memcpy(data + y * width, state->canvas + y * state->canvas_w, (size_t)width);
    }

    writefile(filepath, file_buffer, (uint32_t)file_size);
}

static void paint_load_from_file(window_t* win, const char* filepath) {
    paint_state_t* state = (paint_state_t*)window_get_user_data(win);
    if (!state || !filepath) return;

    static uint8_t file_buffer[sizeof(paint_file_header_t) + PAINT_CANVAS_W * PAINT_CANVAS_H];
    int bytes_read = paint_read_file(filepath, file_buffer, (int)sizeof(file_buffer));
    if (bytes_read < (int)sizeof(paint_file_header_t)) {
        return;
    }

    paint_file_header_t* header = (paint_file_header_t*)file_buffer;
    if (header->magic != PAINT_FILE_MAGIC || header->version != PAINT_FILE_VERSION) {
        return;
    }

    int width = header->width;
    int height = header->height;
    if (width <= 0 || height <= 0) {
        return;
    }

    int expected = (int)sizeof(paint_file_header_t) + width * height;
    if (expected > (int)sizeof(file_buffer)) {
        return;
    }
    if (bytes_read < expected) {
        return;
    }

    strncpy(state->current_file, filepath, sizeof(state->current_file) - 1);
    state->current_file[sizeof(state->current_file) - 1] = '\0';

    paint_canvas_clear(state, COLOR_WHITE);
    int max_w = width < state->canvas_w ? width : state->canvas_w;
    int max_h = height < state->canvas_h ? height : state->canvas_h;
    uint8_t* data = file_buffer + sizeof(paint_file_header_t);

    for (int y = 0; y < max_h; y++) {
        memcpy(state->canvas + y * state->canvas_w, data + y * width, (size_t)max_w);
    }

    paint_draw_canvas(win);
    paint_redraw(win);
}

static void paint_save_dialog_callback(const char* filepath, void* user_data) {
    window_t* win = (window_t*)user_data;
    if (!filepath || !win) return;
    paint_save_to_file(win, filepath);
}

static void paint_open_dialog_callback(const char* filepath, void* user_data) {
    window_t* win = (window_t*)user_data;
    if (!filepath || !win) return;
    paint_load_from_file(win, filepath);
}

static void paint_menu_open_at(window_t* win, int x, int y) {
    paint_state_t* state = (paint_state_t*)window_get_user_data(win);
    int content_w = window_content_width(win);
    int content_h = window_content_height(win);
    int menu_h = PAINT_MENU_ITEM_COUNT * PAINT_MENU_ITEM_HEIGHT + PAINT_MENU_PADDING * 2;

    state->drawing = false;
    state->last_x = -1;
    state->last_y = -1;
    state->menu_open = true;
    state->menu_x = x;
    state->menu_y = y;
    state->menu_hover = -1;

    if (state->menu_x + PAINT_MENU_WIDTH > content_w) {
        state->menu_x = content_w - PAINT_MENU_WIDTH;
    }
    if (state->menu_y + menu_h > content_h) {
        state->menu_y = content_h - menu_h;
    }
    if (state->menu_x < 0) state->menu_x = 0;
    if (state->menu_y < 0) state->menu_y = 0;
}

static void paint_menu_close(paint_state_t* state) {
    state->menu_open = false;
    state->menu_hover = -1;
}

static int paint_menu_item_at(paint_state_t* state, int x, int y) {
    int menu_h = PAINT_MENU_ITEM_COUNT * PAINT_MENU_ITEM_HEIGHT + PAINT_MENU_PADDING * 2;
    if (!state->menu_open) return -1;
    if (x < state->menu_x || x >= state->menu_x + PAINT_MENU_WIDTH ||
        y < state->menu_y || y >= state->menu_y + menu_h) {
        return -1;
    }
    int rel_y = y - state->menu_y - PAINT_MENU_PADDING;
    if (rel_y < 0) return -1;
    int idx = rel_y / PAINT_MENU_ITEM_HEIGHT;
    return (idx >= 0 && idx < PAINT_MENU_ITEM_COUNT) ? idx : -1;
}

static void paint_menu_select(window_t* win, int idx) {
    paint_state_t* state = (paint_state_t*)window_get_user_data(win);
    if (idx == 0) {
        paint_canvas_clear(state, COLOR_WHITE);
        state->current_file[0] = '\0';
    } else if (idx == 1) {
        file_dialog_show_open("Open Painting", "/", paint_open_dialog_callback, win);
    } else if (idx == 2) {
        if (state->current_file[0]) {
            paint_save_to_file(win, state->current_file);
        } else {
            file_dialog_show_save("Save Painting", "painting.pnt", paint_save_dialog_callback, win);
        }
    } else if (idx == 3) {
        const char* default_name = "painting.pnt";
        if (state->current_file[0]) {
            const char* slash = strrchr(state->current_file, '/');
            default_name = slash ? slash + 1 : state->current_file;
        }
        file_dialog_show_save("Save Painting", default_name, paint_save_dialog_callback, win);
    } else if (idx == 4) {
        paint_canvas_clear(state, COLOR_WHITE);
    }
}

static void paint_draw_line(window_t* win, int x0, int y0, int x1, int y1, uint8_t color, int size) {
    paint_state_t* state = (paint_state_t*)window_get_user_data(win);
    int draw_h = paint_draw_height(win);
    // Bresenham's line algorithm with brush size
    int dx = paint_abs(x1 - x0);
    int dy = paint_abs(y1 - y0);
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
                    if (px >= 0 && px < state->canvas_w && py >= 0 && py < draw_h) {
                        paint_canvas_putpixel(state, px, py, color);
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
    paint_state_t* state = (paint_state_t*)window_get_user_data(win);
    int content_w = window_content_width(win);
    int palette_y = paint_palette_y(win);
    
    // Top bar
    window_fill_rect(win, 0, 0, content_w, PAINT_TOP_BAR_HEIGHT, COLOR_DARK_GRAY);
    window_draw_rect(win, 0, 0, content_w, PAINT_TOP_BAR_HEIGHT, COLOR_BLACK);
    if (state->menu_open) {
        window_fill_rect(win, 3, 3, 28, 10, COLOR_LIGHT_BLUE);
    }
    window_print(win, 5, 3, "File", COLOR_WHITE);

    // Palette title
    window_print(win, PAINT_PALETTE_X, palette_y - PAINT_PALETTE_LABEL_OFFSET, "Colors:", COLOR_BLACK);
    
    // Color palette with selection indicator
    for (int i = 0; i < PAINT_PALETTE_COUNT; i++) {
        int swatch_x = PAINT_PALETTE_X + i * PAINT_PALETTE_CELL_W;
        window_fill_rect(win, swatch_x, palette_y, PAINT_PALETTE_SWATCH_W,
                         PAINT_PALETTE_SWATCH_H, paint_palette_colors[i]);
        // Highlight selected color
        if (paint_palette_colors[i] == state->current_color) {
            window_draw_rect(win, swatch_x - 1, palette_y - 1,
                             PAINT_PALETTE_SWATCH_W + 2, PAINT_PALETTE_SWATCH_H + 2,
                             COLOR_WHITE);
            window_draw_rect(win, swatch_x, palette_y, PAINT_PALETTE_SWATCH_W,
                             PAINT_PALETTE_SWATCH_H, COLOR_BLACK);
        } else {
            window_draw_rect(win, swatch_x, palette_y, PAINT_PALETTE_SWATCH_W,
                             PAINT_PALETTE_SWATCH_H, COLOR_DARK_GRAY);
        }
    }
    
    // Clear button
    window_fill_rect(win, content_w - 40, palette_y, 35, PAINT_PALETTE_SWATCH_H,
                     COLOR_LIGHT_GRAY);
    window_draw_rect(win, content_w - 40, palette_y, 35, PAINT_PALETTE_SWATCH_H,
                     COLOR_DARK_GRAY);
    window_print(win, content_w - 37, palette_y + 2, "Clear", COLOR_BLACK);

    if (state->menu_open) {
        int menu_h = PAINT_MENU_ITEM_COUNT * PAINT_MENU_ITEM_HEIGHT + PAINT_MENU_PADDING * 2;
        window_fill_rect(win, state->menu_x, state->menu_y, PAINT_MENU_WIDTH, menu_h, COLOR_WHITE);
        window_draw_rect(win, state->menu_x, state->menu_y, PAINT_MENU_WIDTH, menu_h, COLOR_DARK_GRAY);

        for (int i = 0; i < PAINT_MENU_ITEM_COUNT; i++) {
            int item_y = state->menu_y + PAINT_MENU_PADDING + i * PAINT_MENU_ITEM_HEIGHT;
            if (i == state->menu_hover) {
                window_fill_rect(win, state->menu_x + 1, item_y, PAINT_MENU_WIDTH - 2,
                                PAINT_MENU_ITEM_HEIGHT, COLOR_LIGHT_BLUE);
            }
            window_print(win, state->menu_x + 6, item_y + 2, paint_menu_items[i], COLOR_BLACK);
        }
    }
}

static void paint_click(window_t* win, int x, int y) {
    paint_state_t* state = (paint_state_t*)window_get_user_data(win);
    int origin_y = paint_canvas_origin_y();
    int draw_h = paint_draw_height(win);

    if (y < PAINT_TOP_BAR_HEIGHT) {
        return;
    }

    if (paint_handle_palette_click(win, x, y)) {
        return;
    }

    // Drawing area
    if (y >= origin_y && y < origin_y + draw_h) {
        int canvas_y = y - origin_y;
        state->drawing = true;
        paint_draw_line(win, x, canvas_y, x, canvas_y, state->current_color, state->brush_size);
        state->last_x = x;
        state->last_y = canvas_y;
    }
}

static void paint_handle_drag(window_t* win, int x, int y) {
    paint_state_t* state = (paint_state_t*)window_get_user_data(win);
    int origin_y = paint_canvas_origin_y();
    int draw_h = paint_draw_height(win);
    
    if (state->drawing && y >= origin_y && y < origin_y + draw_h) {
        int canvas_y = y - origin_y;
        if (state->last_x >= 0 && state->last_y >= 0) {
            paint_draw_line(win, state->last_x, state->last_y, x, canvas_y,
                          state->current_color, state->brush_size);
        }
        state->last_x = x;
        state->last_y = canvas_y;
    }
}

static void paint_key(window_t* win, int c) {
    paint_state_t* state = (paint_state_t*)window_get_user_data(win);
    
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
        int idx = c - '1';
        if (idx < PAINT_PALETTE_COUNT) {
            state->current_color = paint_palette_colors[idx];
            paint_redraw(win);
        }
    }
    // Clear with 'c' or 'C'
    else if (c == 'c' || c == 'C') {
        paint_canvas_clear(state, COLOR_WHITE);
        paint_redraw(win);
    }
}

static void paint_on_draw(window_t* win) {
    paint_draw_canvas(win);
    paint_redraw(win);
}

static void paint_on_mouse_down(window_t* win, int x, int y, int buttons) {
    paint_state_t* state = (paint_state_t*)window_get_user_data(win);
    if (buttons & MOUSE_RIGHT_BUTTON) {
        paint_menu_open_at(win, 3, PAINT_TOP_BAR_HEIGHT);
        return;
    }

    if (state->menu_open) {
        int idx = paint_menu_item_at(state, x, y);
        if (idx >= 0) {
            paint_menu_select(win, idx);
        }
        paint_menu_close(state);
        return;
    }

    if (buttons & MOUSE_LEFT_BUTTON) {
        if (y < PAINT_TOP_BAR_HEIGHT && x >= 3 && x < 40) {
            paint_menu_open_at(win, 3, PAINT_TOP_BAR_HEIGHT);
            return;
        }
        if (paint_handle_palette_click(win, x, y)) {
            state->drawing = false;
            return;
        }
        paint_click(win, x, y);
    }
}

static void paint_on_mouse_up(window_t* win, int x, int y, int buttons) {
    (void)x;
    (void)y;
    (void)buttons;
    paint_state_t* state = (paint_state_t*)window_get_user_data(win);
    paint_handle_palette_click(win, x, y);
    state->drawing = false;
    state->last_x = -1;
    state->last_y = -1;
}

static void paint_on_mouse_move(window_t* win, int x, int y, int buttons) {
    paint_state_t* state = (paint_state_t*)window_get_user_data(win);
    if (state->menu_open) {
        state->menu_hover = paint_menu_item_at(state, x, y);
        return;
    }
    if ((buttons & MOUSE_LEFT_BUTTON) && !state->drawing) {
        if (paint_handle_palette_click(win, x, y)) {
            return;
        }
    }
    paint_handle_drag(win, x, y);
}

window_t* gui_paint_create_window(int x, int y) {
    if (paint_window && uwm_window_is_open(paint_window)) {
        return paint_window;
    }

    int win_h = 210;
    int screen_h = graphics_get_height();
    if (screen_h > 0 && win_h > screen_h) {
        win_h = screen_h;
    }
    window_t* win = window_create(x, y, 260, win_h, "Paint");
    if (!win) return NULL;

    memset(&paint_state, 0, sizeof(paint_state));
    paint_state.current_color = COLOR_BLACK;
    paint_state.brush_size = 1;
    paint_state.last_x = -1;
    paint_state.last_y = -1;
    paint_state.menu_open = false;
    paint_state.menu_hover = -1;
    paint_state.current_file[0] = '\0';
    paint_state.canvas_w = window_content_width(win);
    paint_state.canvas_h = window_content_height(win);
    if (paint_state.canvas_w > PAINT_CANVAS_W) paint_state.canvas_w = PAINT_CANVAS_W;
    if (paint_state.canvas_h > PAINT_CANVAS_H) paint_state.canvas_h = PAINT_CANVAS_H;
    paint_canvas_clear(&paint_state, COLOR_WHITE);

    window_set_handlers(win, paint_on_draw, paint_on_mouse_down, paint_on_mouse_up,
                        paint_on_mouse_move, NULL, paint_key, &paint_state);
    paint_window = win;
    return win;
}
