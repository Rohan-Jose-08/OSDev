#include <kernel/paint.h>
#include <kernel/window.h>
#include <kernel/menu_bar.h>
#include <kernel/graphics.h>
#include <kernel/keyboard.h>
#include <kernel/mouse.h>
#include <kernel/fs.h>
#include <kernel/kmalloc.h>
#include <kernel/file_dialog.h>
#include <stdio.h>
#include <string.h>

#define PAINT_CANVAS_WIDTH 240
#define PAINT_CANVAS_HEIGHT 160

// Custom paint file format - simple and reliable
#pragma pack(push, 1)
typedef struct {
    uint32_t magic;          // Magic number: "PINT" (0x544E4950)
    uint16_t version;        // Format version: 1
    uint16_t width;          // Canvas width
    uint16_t height;         // Canvas height
    uint16_t reserved;       // Reserved for future use
} paint_file_header_t;
#pragma pack(pop)

#define PAINT_FILE_MAGIC 0x544E4950  // "PINT" in little-endian
#define PAINT_FILE_VERSION 1
#define PAINT_TOOLBAR_HEIGHT 24
#define PAINT_PALETTE_HEIGHT 56
#define PAINT_COLOR_SIZE 18
#define PAINT_PALETTE_COLORS_PER_ROW 16
#define PAINT_PALETTE_ROWS 3

typedef struct {
    window_t* window;
    menu_bar_t* menu_bar;
    uint8_t* canvas;
    uint8_t current_color;
    int brush_size;
    bool drawing;
    int last_x, last_y;
    char current_file[64];
} paint_state_t;

static paint_state_t* paint_state = NULL;

// Forward declarations
static bool paint_on_priority_click(window_t* window, int x, int y);
static void paint_draw_canvas(void);
static void paint_draw_toolbar(void);
static void paint_draw_palette(void);
static void paint_clear_canvas(void);
static void paint_save_to_file(const char* filepath);
static void paint_load_from_file(const char* filepath);
static void paint_on_destroy(window_t* window);

// Menu callbacks
static void paint_menu_new(window_t* window, void* user_data) {
    if (!paint_state) return;
    paint_clear_canvas();
    paint_state->current_file[0] = '\0';
    paint_draw_canvas();
    window_draw(window);
}

// File dialog callback for save
static void paint_save_callback(const char* filepath, void* user_data) {
    if (!paint_state || !filepath) return;
    paint_save_to_file(filepath);
}

static void paint_menu_save(window_t* window, void* user_data) {
    if (!paint_state) return;
    
    // Extract filename from current path if it exists
    const char* default_name = "painting.pnt";
    if (paint_state->current_file[0] != '\0') {
        const char* slash = strrchr(paint_state->current_file, '/');
        if (slash) {
            default_name = slash + 1;
        } else {
            default_name = paint_state->current_file;
        }
    }
    
    // Show save dialog
    file_dialog_show_save("Save Painting", default_name, paint_save_callback, NULL);
}

// Actual save implementation
static void paint_save_to_file(const char* filepath) {
    if (!paint_state || !filepath) return;
    
    // Update current file
    strncpy(paint_state->current_file, filepath, sizeof(paint_state->current_file) - 1);
    paint_state->current_file[sizeof(paint_state->current_file) - 1] = '\0';
    
    // Calculate file size: header + raw canvas data
    int canvas_size = PAINT_CANVAS_WIDTH * PAINT_CANVAS_HEIGHT;
    int file_size = sizeof(paint_file_header_t) + canvas_size;
    
    uint8_t* file_buffer = (uint8_t*)kmalloc(file_size);
    if (!file_buffer) return;
    
    // Fill header
    paint_file_header_t* header = (paint_file_header_t*)file_buffer;
    header->magic = PAINT_FILE_MAGIC;
    header->version = PAINT_FILE_VERSION;
    header->width = PAINT_CANVAS_WIDTH;
    header->height = PAINT_CANVAS_HEIGHT;
    header->reserved = 0;
    
    // Copy canvas data directly after header
    uint8_t* data = file_buffer + sizeof(paint_file_header_t);
    memcpy(data, paint_state->canvas, canvas_size);
    
    // Get free blocks before save
    uint32_t free_before = fs_get_free_blocks();
    
    // Create file and write
    int create_result = fs_create_file(paint_state->current_file);
    if (create_result < 0 && create_result != -2) {  // -2 means already exists, which is OK
        kfree(file_buffer);
        char title[64];
        snprintf(title, sizeof(title), "Paint - Create failed: %d (free:%u)", create_result, free_before);
        window_set_title(paint_state->window, title);
        window_draw(paint_state->window);
        return;
    }
    
    int write_result = fs_write_file(paint_state->current_file, file_buffer, file_size, 0);
    uint32_t free_after = fs_get_free_blocks();
    kfree(file_buffer);
    
    // Update title to show result
    char title[64];
    if (write_result != file_size) {
        // Calculate blocks written vs needed
        int blocks_written = (write_result + 511) / 512;
        int blocks_needed = (file_size + 511) / 512;
        snprintf(title, sizeof(title), "Paint - %d/%d B %d/%d blks (free:%u->%u)", 
                 write_result, file_size, blocks_written, blocks_needed, free_before, free_after);
    } else {
        const char* filename = strrchr(paint_state->current_file, '/');
        filename = filename ? filename + 1 : paint_state->current_file;
        snprintf(title, sizeof(title), "Paint - %s (saved, free:%u)", filename, free_after);
    }
    window_set_title(paint_state->window, title);
    window_draw(paint_state->window);
}

// File dialog callback for load
static void paint_load_callback(const char* filepath, void* user_data) {
    if (!paint_state || !filepath) return;
    paint_load_from_file(filepath);
}

static void paint_menu_load(window_t* window, void* user_data) {
    if (!paint_state) return;
    
    // Show open dialog
    file_dialog_show_open("Open Painting", "/", paint_load_callback, NULL);
}

// Actual load implementation
static void paint_load_from_file(const char* filepath) {
    if (!paint_state || !filepath) return;
    
    // Update current file
    strncpy(paint_state->current_file, filepath, sizeof(paint_state->current_file) - 1);
    paint_state->current_file[sizeof(paint_state->current_file) - 1] = '\0';
    
    // Calculate expected file size
    int canvas_size = PAINT_CANVAS_WIDTH * PAINT_CANVAS_HEIGHT;
    int expected_size = sizeof(paint_file_header_t) + canvas_size;
    
    // Allocate buffer for reading file
    uint8_t* file_buffer = (uint8_t*)kmalloc(expected_size + 1024); // Extra space for safety
    if (!file_buffer) return;
    
    // Read file
    int bytes_read = fs_read_file(paint_state->current_file, file_buffer, expected_size + 1024, 0);
    if (bytes_read < expected_size) {
        kfree(file_buffer);
        char title[64];
        if (bytes_read <= 0) {
            snprintf(title, sizeof(title), "Paint - File not found!");
        } else {
            snprintf(title, sizeof(title), "Paint - Read %d, need %d bytes", bytes_read, expected_size);
        }
        window_set_title(paint_state->window, title);
        window_draw(paint_state->window);
        return;
    }
    
    // Parse header
    paint_file_header_t* header = (paint_file_header_t*)file_buffer;
    
    // Verify magic number
    if (header->magic != PAINT_FILE_MAGIC) {
        kfree(file_buffer);
        char title[64];
        snprintf(title, sizeof(title), "Paint - Not a paint file! (0x%X)", header->magic);
        window_set_title(paint_state->window, title);
        window_draw(paint_state->window);
        return;
    }
    
    // Verify version
    if (header->version != PAINT_FILE_VERSION) {
        kfree(file_buffer);
        char title[64];
        snprintf(title, sizeof(title), "Paint - Wrong version (%d)!", header->version);
        window_set_title(paint_state->window, title);
        window_draw(paint_state->window);
        return;
    }
    
    // Verify dimensions
    if (header->width != PAINT_CANVAS_WIDTH || header->height != PAINT_CANVAS_HEIGHT) {
        kfree(file_buffer);
        char title[64];
        snprintf(title, sizeof(title), "Paint - Wrong size (%dx%d)!", header->width, header->height);
        window_set_title(paint_state->window, title);
        window_draw(paint_state->window);
        return;
    }
    
    // Load canvas data directly
    uint8_t* data = file_buffer + sizeof(paint_file_header_t);
    memcpy(paint_state->canvas, data, canvas_size);
    
    kfree(file_buffer);
    
    // Update display
    char title[64];
    const char* filename = strrchr(paint_state->current_file, '/');
    filename = filename ? filename + 1 : paint_state->current_file;
    snprintf(title, sizeof(title), "Paint - %s", filename);
    window_set_title(paint_state->window, title);
    paint_draw_canvas();
    window_draw(paint_state->window);
}

static void paint_menu_close(window_t* window, void* user_data) {
    if (paint_state) {
        if (paint_state->canvas) {
            kfree(paint_state->canvas);
        }
        window_destroy(paint_state->window);
        menu_bar_destroy(paint_state->menu_bar);
        kfree(paint_state);
        paint_state = NULL;
    }
}

// Window destroy handler
static void paint_on_destroy(window_t* window) {
    if (!paint_state) return;
    
    // Clean up canvas
    if (paint_state->canvas) {
        kfree(paint_state->canvas);
    }
    
    // Clean up menu bar
    if (paint_state->menu_bar) {
        menu_bar_destroy(paint_state->menu_bar);
    }
    
    // Free state and reset global pointer
    kfree(paint_state);
    paint_state = NULL;
}

static void paint_menu_clear(window_t* window, void* user_data) {
    if (!paint_state) return;
    paint_clear_canvas();
    paint_draw_canvas();
    window_draw(window);
}

static void paint_menu_about(window_t* window, void* user_data) {
    // Could show about dialog
    (void)window;
    (void)user_data;
}

// Clear the canvas
static void paint_clear_canvas(void) {
    if (!paint_state || !paint_state->canvas) return;
    memset(paint_state->canvas, COLOR_WHITE, PAINT_CANVAS_WIDTH * PAINT_CANVAS_HEIGHT);
}

// Draw the canvas
static void paint_draw_canvas(void) {
    if (!paint_state || !paint_state->window || !paint_state->canvas) return;
    
    window_t* window = paint_state->window;
    int menu_height = menu_bar_get_height();
    int canvas_y = menu_height + PAINT_TOOLBAR_HEIGHT;
    int canvas_x = 1;
    
    // Copy canvas to window
    for (int y = 0; y < PAINT_CANVAS_HEIGHT && (canvas_y + y) < window->content_height; y++) {
        for (int x = 0; x < PAINT_CANVAS_WIDTH && (canvas_x + x) < window->content_width; x++) {
            uint8_t color = paint_state->canvas[y * PAINT_CANVAS_WIDTH + x];
            window_putpixel(window, canvas_x + x, canvas_y + y, color);
        }
    }
    
    // Draw border around canvas
    window_draw_rect(window, canvas_x - 1, canvas_y - 1, 
                    PAINT_CANVAS_WIDTH + 2, PAINT_CANVAS_HEIGHT + 2, 
                    COLOR_BLACK);
}

// Draw the toolbar
static void paint_draw_toolbar(void) {
    if (!paint_state || !paint_state->window) return;
    
    window_t* window = paint_state->window;
    int menu_height = menu_bar_get_height();
    int toolbar_y = menu_height;
    
    // Draw toolbar background
    window_fill_rect(window, 0, toolbar_y, 
                    window->content_width, PAINT_TOOLBAR_HEIGHT, 
                    COLOR_LIGHT_GRAY);
    
    // Draw brush size indicator
    char text[32];
    snprintf(text, sizeof(text), "Brush: %d", paint_state->brush_size);
    window_print(window, 10, toolbar_y + 11, text, COLOR_BLACK);
    
    // Draw current color indicator
    window_fill_rect(window, 100, toolbar_y + 5, 20, 20, paint_state->current_color);
    window_draw_rect(window, 100, toolbar_y + 5, 20, 20, COLOR_BLACK);
    window_print(window, 125, toolbar_y + 11, "Color", COLOR_BLACK);
}

// Draw the color palette
static void paint_draw_palette(void) {
    if (!paint_state || !paint_state->window) return;
    
    window_t* window = paint_state->window;
    int palette_y = window->content_height - PAINT_PALETTE_HEIGHT;
    
    // Draw palette background
    window_fill_rect(window, 0, palette_y, 
                    window->content_width, PAINT_PALETTE_HEIGHT, 
                    COLOR_DARK_GRAY);
    
    // Draw color swatches (48 colors in 3 rows of 16)
    for (int row = 0; row < PAINT_PALETTE_ROWS; row++) {
        for (int col = 0; col < PAINT_PALETTE_COLORS_PER_ROW; col++) {
            int color_idx = row * PAINT_PALETTE_COLORS_PER_ROW + col;
            int x = 5 + col * (PAINT_COLOR_SIZE - 1);
            int y = palette_y + 2 + row * (PAINT_COLOR_SIZE + 1);
            
            window_fill_rect(window, x, y, PAINT_COLOR_SIZE - 2, PAINT_COLOR_SIZE - 2, color_idx);
            
            // Highlight selected color
            if (color_idx == paint_state->current_color) {
                window_draw_rect(window, x - 1, y - 1, PAINT_COLOR_SIZE, PAINT_COLOR_SIZE, COLOR_YELLOW);
                window_draw_rect(window, x - 2, y - 2, PAINT_COLOR_SIZE + 2, PAINT_COLOR_SIZE + 2, COLOR_WHITE);
            } else {
                window_draw_rect(window, x, y, PAINT_COLOR_SIZE - 2, PAINT_COLOR_SIZE - 2, COLOR_BLACK);
            }
        }
    }
}

// Draw on canvas
static void paint_draw_at(int x, int y) {
    if (!paint_state || !paint_state->canvas) return;
    
    // Draw with brush
    for (int dy = -paint_state->brush_size; dy <= paint_state->brush_size; dy++) {
        for (int dx = -paint_state->brush_size; dx <= paint_state->brush_size; dx++) {
            if (dx * dx + dy * dy <= paint_state->brush_size * paint_state->brush_size) {
                int px = x + dx;
                int py = y + dy;
                if (px >= 0 && px < PAINT_CANVAS_WIDTH && 
                    py >= 0 && py < PAINT_CANVAS_HEIGHT) {
                    paint_state->canvas[py * PAINT_CANVAS_WIDTH + px] = paint_state->current_color;
                }
            }
        }
    }
}

// Draw line between two points
static void paint_draw_line(int x0, int y0, int x1, int y1) {
    int dx = x1 - x0;
    int dy = y1 - y0;
    int steps = (dx > 0 ? dx : -dx) > (dy > 0 ? dy : -dy) ? 
                (dx > 0 ? dx : -dx) : (dy > 0 ? dy : -dy);
    
    if (steps == 0) {
        paint_draw_at(x0, y0);
        return;
    }
    
    for (int i = 0; i <= steps; i++) {
        int x = x0 + (dx * i) / steps;
        int y = y0 + (dy * i) / steps;
        paint_draw_at(x, y);
    }
}

// Priority click handler for menu bar
static bool paint_on_priority_click(window_t* window, int x, int y) {
    if (!paint_state) return false;
    
    // Check if click is in menu bar
    if (menu_bar_handle_click(paint_state->menu_bar, x, y)) {
        // Redraw everything to clear old dropdown graphics
        paint_draw_toolbar();
        paint_draw_canvas();
        paint_draw_palette();
        menu_bar_draw(paint_state->menu_bar);
        window_draw(window);
        return true;  // Menu bar handled the click
    }
    
    return false;  // Menu bar didn't handle, proceed with normal click handling
}

// Window click handler
static void paint_on_click(window_t* window, int x, int y) {
    if (!paint_state) return;
    
    int menu_height = menu_bar_get_height();
    int canvas_y = menu_height + PAINT_TOOLBAR_HEIGHT;
    int palette_y = window->content_height - PAINT_PALETTE_HEIGHT;
    
    // Check if click is in palette
    if (y >= palette_y && y < palette_y + PAINT_PALETTE_HEIGHT) {
        int rel_y = y - palette_y - 2;
        int row = rel_y / (PAINT_COLOR_SIZE + 1);
        int col = (x - 5) / (PAINT_COLOR_SIZE - 1);
        
        if (row >= 0 && row < PAINT_PALETTE_ROWS && 
            col >= 0 && col < PAINT_PALETTE_COLORS_PER_ROW) {
            int color_index = row * PAINT_PALETTE_COLORS_PER_ROW + col;
            if (color_index < 256) {  // VGA has 256 colors
                paint_state->current_color = color_index;
                paint_draw_toolbar();
                paint_draw_palette();
                window_draw(window);
            }
        }
        return;
    }
    
    // Check if click is in canvas
    int canvas_x_start = 1;
    if (y >= canvas_y && y < canvas_y + PAINT_CANVAS_HEIGHT &&
        x >= canvas_x_start && x < canvas_x_start + PAINT_CANVAS_WIDTH) {
        
        int canvas_x = x - canvas_x_start;
        int canvas_y_rel = y - canvas_y;
        
        paint_state->drawing = true;
        paint_state->last_x = canvas_x;
        paint_state->last_y = canvas_y_rel;
        
        paint_draw_at(canvas_x, canvas_y_rel);
        paint_draw_canvas();
        window_draw(window);
    }
}

// Window drag handler (mouse move while button held)
static void paint_on_drag(window_t* window, int x, int y) {
    if (!paint_state || !paint_state->drawing) return;
    
    int menu_height = menu_bar_get_height();
    int canvas_y = menu_height + PAINT_TOOLBAR_HEIGHT;
    int canvas_x_start = 1;
    
    // Check if still in canvas
    if (y >= canvas_y && y < canvas_y + PAINT_CANVAS_HEIGHT &&
        x >= canvas_x_start && x < canvas_x_start + PAINT_CANVAS_WIDTH) {
        
        int canvas_x = x - canvas_x_start;
        int canvas_y_rel = y - canvas_y;
        
        // Draw line from last position for smooth drawing
        if (paint_state->last_x >= 0 && paint_state->last_y >= 0) {
            paint_draw_line(paint_state->last_x, paint_state->last_y, 
                          canvas_x, canvas_y_rel);
        } else {
            paint_draw_at(canvas_x, canvas_y_rel);
        }
        
        paint_state->last_x = canvas_x;
        paint_state->last_y = canvas_y_rel;
        
        paint_draw_canvas();
        window_draw(window);
    }
}

// Window key handler
static void paint_on_key(window_t* window, char key) {
    if (!paint_state) return;
    
    bool redraw = false;
    
    switch (key) {
        case '+':
        case '=':
            if (paint_state->brush_size < 20) {
                paint_state->brush_size++;
                redraw = true;
            }
            break;
            
        case '-':
        case '_':
            if (paint_state->brush_size > 1) {
                paint_state->brush_size--;
                redraw = true;
            }
            break;
            
        case 'c':
        case 'C':
            paint_clear_canvas();
            redraw = true;
            break;
    }
    
    if (redraw) {
        paint_draw_toolbar();
        paint_draw_canvas();
        window_draw(window);
    }
}

// Launch paint application (windowed version)
void paint_app_windowed(const char* filename) {
    // Only allow one instance
    if (paint_state) return;
    
    // Calculate window size with proper padding
    int win_width = PAINT_CANVAS_WIDTH + 12;
    int win_height = menu_bar_get_height() + PAINT_TOOLBAR_HEIGHT + 
                     PAINT_CANVAS_HEIGHT + PAINT_PALETTE_HEIGHT + 12;
    
    // Ensure minimum size
    if (win_width < 260) win_width = 260;
    if (win_height < 230) win_height = 230;
    
    // Create window
    window_t* window = window_create(120, 60, win_width, win_height, "Paint");
    if (!window) return;
    
    // Allocate state
    paint_state = (paint_state_t*)kmalloc(sizeof(paint_state_t));
    if (!paint_state) {
        window_destroy(window);
        return;
    }
    
    // Allocate canvas
    paint_state->canvas = (uint8_t*)kmalloc(PAINT_CANVAS_WIDTH * PAINT_CANVAS_HEIGHT);
    if (!paint_state->canvas) {
        kfree(paint_state);
        window_destroy(window);
        return;
    }
    
    // Initialize state
    paint_state->window = window;
    paint_state->current_color = COLOR_BLACK;
    paint_state->brush_size = 3;
    paint_state->drawing = false;
    paint_state->last_x = -1;
    paint_state->last_y = -1;
    paint_state->current_file[0] = '\0';
    
    // Set filename if provided
    if (filename && filename[0] != '\0') {
        strncpy(paint_state->current_file, filename, sizeof(paint_state->current_file) - 1);
        paint_state->current_file[sizeof(paint_state->current_file) - 1] = '\0';
    }
    
    // Clear canvas
    paint_clear_canvas();
    
    // Create menu bar
    paint_state->menu_bar = menu_bar_create(window);
    if (paint_state->menu_bar) {
        menu_item_t* file_menu = menu_bar_add_menu(paint_state->menu_bar, "File");
        if (file_menu) {
            menu_item_add_dropdown(file_menu, "New", paint_menu_new);
            menu_item_add_dropdown(file_menu, "Save", paint_menu_save);
            menu_item_add_dropdown(file_menu, "Load", paint_menu_load);
            menu_item_add_separator(file_menu);
            menu_item_add_dropdown(file_menu, "Close", paint_menu_close);
        }
        
        menu_item_t* edit_menu = menu_bar_add_menu(paint_state->menu_bar, "Edit");
        if (edit_menu) {
            menu_item_add_dropdown(edit_menu, "Clear", paint_menu_clear);
        }
        
        menu_item_t* help_menu = menu_bar_add_menu(paint_state->menu_bar, "Help");
        if (help_menu) {
            menu_item_add_dropdown(help_menu, "About", paint_menu_about);
        }
    }
    
    // Set window callbacks
    window->on_priority_click = paint_on_priority_click;
    window->on_click = paint_on_click;
    window->on_drag = paint_on_drag;
    window->on_key = paint_on_key;
    window->on_destroy = paint_on_destroy;
    window->user_data = paint_state;
    
    // Draw initial state
    window_clear_content(window, WINDOW_COLOR_BACKGROUND);
    if (paint_state->menu_bar) {
        menu_bar_draw(paint_state->menu_bar);
    }
    paint_draw_toolbar();
    paint_draw_canvas();
    paint_draw_palette();
    window_draw(window);
    
    // Load file if filename was provided
    if (filename && filename[0] != '\0') {
        paint_load_from_file(filename);
    }
}

// Open a BMP file in paint
void paint_open_file(const char* filepath) {
    // Launch paint with the file
    if (!paint_state) {
        paint_app_windowed(filepath);
    } else {
        // Paint already open, just load the file
        paint_load_from_file(filepath);
    }
}

// Keep the old paint_program for backward compatibility (non-windowed fullscreen version)
// Commented out to avoid duplicate definition - the original is in paint.c
/*
void paint_program(const char *current_dir_path) {
    // Call the windowed version instead
    paint_app_windowed();
}
*/
