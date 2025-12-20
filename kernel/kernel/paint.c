#include <kernel/paint.h>
#include <kernel/graphics.h>
#include <kernel/keyboard.h>
#include <kernel/mouse.h>
#include <kernel/vfs.h>
#include <kernel/tty.h>
#include <stdio.h>
#include <string.h>

// BMP file header structures
typedef struct __attribute__((packed)) {
    uint16_t type;        // Magic identifier: 0x4D42 ("BM")
    uint32_t size;        // File size in bytes
    uint16_t reserved1;   // Not used
    uint16_t reserved2;   // Not used
    uint32_t offset;      // Offset to image data in bytes
} bmp_file_header_t;

typedef struct __attribute__((packed)) {
    uint32_t size;            // Header size in bytes
    int32_t width;            // Width of image
    int32_t height;           // Height of image
    uint16_t planes;          // Number of color planes
    uint16_t bits_per_pixel;  // Bits per pixel
    uint32_t compression;     // Compression type
    uint32_t image_size;      // Image size in bytes
    int32_t x_resolution;     // Pixels per meter
    int32_t y_resolution;     // Pixels per meter
    uint32_t colors_used;     // Number of colors
    uint32_t colors_important;// Important colors
} bmp_info_header_t;

// Default VGA palette (256 colors)
static void get_vga_palette(uint8_t *palette) {
    for (int i = 0; i < 256; i++) {
        uint8_t r, g, b;
        graphics_get_palette_color(i, &r, &g, &b);
        palette[i * 4 + 0] = b;  // BMP uses BGR format
        palette[i * 4 + 1] = g;
        palette[i * 4 + 2] = r;
        palette[i * 4 + 3] = 0;  // Reserved
    }
}

// Save canvas to BMP file
static int save_canvas_bmp(const uint8_t *canvas, int width, int height, const char *filename) {
    // Calculate sizes
    int row_size = ((width + 3) / 4) * 4;  // Rows are padded to 4 bytes
    int pixel_data_size = row_size * height;
    int palette_size = 256 * 4;  // 256 colors * 4 bytes per color
    int file_size = sizeof(bmp_file_header_t) + sizeof(bmp_info_header_t) + palette_size + pixel_data_size;
    
    // Allocate buffer for entire BMP file
    uint8_t *bmp_data = (uint8_t*)vfs_malloc(file_size);
    if (!bmp_data) {
        return -1;
    }
    
    uint8_t *ptr = bmp_data;
    
    // Write BMP file header
    bmp_file_header_t *file_header = (bmp_file_header_t*)ptr;
    file_header->type = 0x4D42;  // "BM"
    file_header->size = file_size;
    file_header->reserved1 = 0;
    file_header->reserved2 = 0;
    file_header->offset = sizeof(bmp_file_header_t) + sizeof(bmp_info_header_t) + palette_size;
    ptr += sizeof(bmp_file_header_t);
    
    // Write BMP info header
    bmp_info_header_t *info_header = (bmp_info_header_t*)ptr;
    info_header->size = sizeof(bmp_info_header_t);
    info_header->width = width;
    info_header->height = height;
    info_header->planes = 1;
    info_header->bits_per_pixel = 8;
    info_header->compression = 0;  // No compression
    info_header->image_size = pixel_data_size;
    info_header->x_resolution = 2835;  // 72 DPI
    info_header->y_resolution = 2835;
    info_header->colors_used = 256;
    info_header->colors_important = 0;
    ptr += sizeof(bmp_info_header_t);
    
    // Write palette
    get_vga_palette(ptr);
    ptr += palette_size;
    
    // Write pixel data (BMP is bottom-up)
    for (int y = height - 1; y >= 0; y--) {
        for (int x = 0; x < width; x++) {
            *ptr++ = canvas[y * width + x];
        }
        // Pad row to 4 bytes
        for (int pad = width; pad < row_size; pad++) {
            *ptr++ = 0;
        }
    }
    
    // Write to VFS
    int result = vfs_write_path(filename, bmp_data, file_size);
    
    return result;
}

// Simple text input function for filenames
static void input_filename(char *buffer, int max_len, int x, int y) {
    int pos = 0;
    buffer[0] = '\0';
    
    while (1) {
        // Display current input
        char display[128];
        snprintf(display, sizeof(display), "%-50s", buffer);
        graphics_print(x, y, display, COLOR_WHITE, COLOR_BLACK);
        
        // Draw cursor
        int cursor_x = x + pos * 8;
        graphics_draw_line(cursor_x, y + 8, cursor_x + 7, y + 8, COLOR_WHITE);
        graphics_flip_buffer();
        
        // Get key
        char c = 0;
        while (c == 0) {
            c = keyboard_getchar();
        }
        
        if (c == '\n') {  // Enter
            break;
        } else if (c == '\b') {  // Backspace
            if (pos > 0) {
                pos--;
                buffer[pos] = '\0';
            }
        } else if (c == 27) {  // ESC - cancel
            buffer[0] = '\0';
            break;
        } else if (c >= 32 && c < 127 && pos < max_len - 1) {  // Printable character
            buffer[pos] = c;
            pos++;
            buffer[pos] = '\0';
        }
    }
}

// Load canvas from BMP file
static int load_canvas_bmp(uint8_t *canvas, int width, int height, const char *filename) {
    // First, get the file size by reading the file header
    bmp_file_header_t file_header;
    if (vfs_read_path(filename, (uint8_t*)&file_header, sizeof(file_header), 0) < 0) {
        return -1;  // File not found or read error
    }
    
    // Check BMP signature
    if (file_header.type != 0x4D42) {
        return -2;  // Not a valid BMP file
    }
    
    // Allocate buffer for entire BMP file
    uint8_t *bmp_data = (uint8_t*)vfs_malloc(file_header.size);
    if (!bmp_data) {
        return -3;  // Out of memory
    }
    
    // Read entire file
    if (vfs_read_path(filename, bmp_data, file_header.size, 0) < 0) {
        return -4;  // Read error
    }
    
    // Parse headers
    bmp_file_header_t *fh = (bmp_file_header_t*)bmp_data;
    bmp_info_header_t *ih = (bmp_info_header_t*)(bmp_data + sizeof(bmp_file_header_t));
    
    // Validate dimensions
    if (ih->width != width || ih->height != height) {
        return -5;  // Size mismatch
    }
    
    // Calculate row size with padding
    int row_size = ((width + 3) / 4) * 4;
    
    // Get pointer to pixel data
    uint8_t *pixel_data = bmp_data + fh->offset;
    
    // Read pixel data (BMP is bottom-up, so we need to flip it)
    for (int y = 0; y < height; y++) {
        uint8_t *row = pixel_data + (height - 1 - y) * row_size;
        for (int x = 0; x < width; x++) {
            canvas[y * width + x] = row[x];
        }
    }
    
    return 0;  // Success
}

// Mouse-based paint program (like MS Paint)
void paint_program(vfs_node_t *current_dir) {
    graphics_clear(COLOR_BLACK);
    
    // Absolute cursor position (not delta)
    int cursor_x = 0, cursor_y = 0;
    int color = COLOR_WHITE;
    int brush_size = 3;
    int last_draw_x = -1, last_draw_y = -1;
    
    // Static canvas buffer to avoid stack overflow (64KB is too large for stack)
    static uint8_t canvas_buffer[320 * 200];
    for (int y = 0; y < 200; y++) {
        for (int x = 0; x < 320; x++) {
            canvas_buffer[y * 320 + x] = COLOR_BLACK;
        }
    }
    
    // Draw initial UI and palette
    graphics_print(5, 5, "PAINT - S:Save L:Load +/-:Brush C:Clear ESC", COLOR_WHITE, COLOR_BLACK);
    graphics_draw_line(0, 15, 319, 15, COLOR_WHITE);
    
    for (int i = 0; i < 16; i++) {
        graphics_fill_rect(5 + i * 19, 180, 17, 17, i);
        graphics_draw_rect(5 + i * 19, 180, 17, 17, COLOR_DARK_GRAY);
    }
    
    // Highlight selected color
    int sel_x = 5 + color * 19;
    graphics_draw_rect(sel_x - 1, 179, 19, 19, COLOR_WHITE);
    graphics_draw_rect(sel_x - 2, 178, 21, 21, COLOR_YELLOW);
    
    // Draw initial brush size indicator
    char brush_text[20];
    snprintf(brush_text, sizeof(brush_text), "Size:%d", brush_size);
    graphics_print(270, 5, brush_text, COLOR_LIGHT_CYAN, COLOR_BLACK);
    
    // Now enable double buffering after initial draw
    graphics_enable_double_buffer();
    
    while (1) {
        // Continuously poll mouse for smooth, responsive movement
        mouse_state_t mouse = mouse_get_state();
        
        // Update cursor position with mouse delta
        if (mouse.x != 0 || mouse.y != 0) {
            cursor_x += mouse.x;
            cursor_y -= mouse.y;  // Invert Y axis
            
            // Constrain cursor to screen area
            if (cursor_x < 0) cursor_x = 0;
            if (cursor_x >= 320) cursor_x = 319;
            if (cursor_y < 16) cursor_y = 16;
            if (cursor_y >= 199) cursor_y = 198;
        }
        
        // Check keyboard after mouse polling
        char c = keyboard_getchar();
        if (c == 27) break; // ESC
        
        // Handle brush size changes
        if (c == '+' || c == '=') {
            if (brush_size < 10) brush_size++;
        }
        if (c == '-' || c == '_') {
            if (brush_size > 1) brush_size--;
        }
        
        // Clear canvas
        if (c == 'c' || c == 'C') {
            for (int y = 16; y < 179; y++) {
                for (int x = 0; x < 320; x++) {
                    canvas_buffer[y * 320 + x] = COLOR_BLACK;
                }
            }
            last_draw_x = last_draw_y = -1;
        }
        
        // Save canvas
        if (c == 's' || c == 'S') {
            // Ask for filename
            graphics_print(5, 5, "Enter filename (e.g. art.bmp): ", COLOR_YELLOW, COLOR_BLACK);
            graphics_flip_buffer();
            
            char filename[64] = "painting.bmp";  // Default (relative)
            input_filename(filename, sizeof(filename), 5, 20);
            
            if (filename[0] != '\0') {
                // Show saving message
                graphics_print(5, 5, "Saving...                                         ", COLOR_YELLOW, COLOR_BLACK);
                graphics_print(5, 20, "                                                  ", COLOR_BLACK, COLOR_BLACK);
                graphics_flip_buffer();
                
                // Save the canvas area (excluding UI)
                static uint8_t save_buffer[320 * 163];  // Canvas area only (163 rows)
                for (int y = 0; y < 163; y++) {
                    for (int x = 0; x < 320; x++) {
                        save_buffer[y * 320 + x] = canvas_buffer[(y + 16) * 320 + x];
                    }
                }
                
                // Construct full path
                char full_path[VFS_MAX_PATH_LEN];
                if (filename[0] == '/') {
                    strncpy(full_path, filename, sizeof(full_path));
                } else if (current_dir) {
                    vfs_get_full_path(current_dir, full_path, sizeof(full_path));
                    int len = strlen(full_path);
                    if (full_path[len-1] != '/') {
                        strcat(full_path, "/");
                    }
                    strcat(full_path, filename);
                } else {
                    snprintf(full_path, sizeof(full_path), "/%s", filename);
                }
                
                // Save using the full path
                int result = save_canvas_bmp(save_buffer, 320, 163, full_path);
                
                if (result >= 0) {
                    char msg[64];
                    snprintf(msg, sizeof(msg), "Saved to %s! Press any key", filename);
                    graphics_print(5, 5, msg, COLOR_GREEN, COLOR_BLACK);
                } else {
                    graphics_print(5, 5, "Save failed! Press any key                        ", COLOR_RED, COLOR_BLACK);
                }
            } else {
                graphics_print(5, 5, "Save cancelled. Press any key                     ", COLOR_LIGHT_GRAY, COLOR_BLACK);
                graphics_print(5, 20, "                                                  ", COLOR_BLACK, COLOR_BLACK);
            }
            graphics_flip_buffer();
            
            // Wait for key press
            while (keyboard_getchar() == 0);
        }
        
        // Load canvas
        if (c == 'l' || c == 'L') {
            // Ask for filename
            graphics_print(5, 5, "Enter filename (e.g. art.bmp): ", COLOR_YELLOW, COLOR_BLACK);
            graphics_flip_buffer();
            
            char filename[64] = "painting.bmp";  // Default (relative)
            input_filename(filename, sizeof(filename), 5, 20);
            
            if (filename[0] != '\0') {
                // Show loading message
                graphics_print(5, 5, "Loading...                                        ", COLOR_YELLOW, COLOR_BLACK);
                graphics_print(5, 20, "                                                  ", COLOR_BLACK, COLOR_BLACK);
                graphics_flip_buffer();
                
                // Load into temporary buffer
                static uint8_t load_buffer[320 * 163];
                
                // Construct full path
                char full_path[VFS_MAX_PATH_LEN];
                if (filename[0] == '/') {
                    strncpy(full_path, filename, sizeof(full_path));
                } else if (current_dir) {
                    vfs_get_full_path(current_dir, full_path, sizeof(full_path));
                    int len = strlen(full_path);
                    if (full_path[len-1] != '/') {
                        strcat(full_path, "/");
                    }
                    strcat(full_path, filename);
                } else {
                    snprintf(full_path, sizeof(full_path), "/%s", filename);
                }
                
                int result = load_canvas_bmp(load_buffer, 320, 163, full_path);
                
                if (result == 0) {
                    // Copy to canvas buffer (with offset for UI)
                    for (int y = 0; y < 163; y++) {
                        for (int x = 0; x < 320; x++) {
                            canvas_buffer[(y + 16) * 320 + x] = load_buffer[y * 320 + x];
                        }
                    }
                    char msg[64];
                    snprintf(msg, sizeof(msg), "Loaded %s! Press any key", filename);
                    graphics_print(5, 5, msg, COLOR_GREEN, COLOR_BLACK);
                } else {
                    char error_msg[64];
                    snprintf(error_msg, sizeof(error_msg), "Load failed (code %d)! Press any key             ", result);
                    graphics_print(5, 5, error_msg, COLOR_RED, COLOR_BLACK);
                }
            } else {
                graphics_print(5, 5, "Load cancelled. Press any key                     ", COLOR_LIGHT_GRAY, COLOR_BLACK);
                graphics_print(5, 20, "                                                  ", COLOR_BLACK, COLOR_BLACK);
            }
            graphics_flip_buffer();
            
            // Wait for key press
            while (keyboard_getchar() == 0);
            last_draw_x = last_draw_y = -1;
        }
        
        // Get mouse state for button checking
        mouse_state_t mouse_buttons = mouse_get_state();
        
        // Check if clicking on color palette
        if ((mouse_buttons.buttons & MOUSE_LEFT_BUTTON) && cursor_y >= 180 && cursor_y < 197) {
            int palette_idx = (cursor_x - 5) / 19;
            if (palette_idx >= 0 && palette_idx < 16) {
                color = palette_idx;
            }
        }
        
        // Draw on canvas with left button
        if ((mouse_buttons.buttons & MOUSE_LEFT_BUTTON) && cursor_y >= 16 && cursor_y < 179) {
            // Draw line from last position for smooth drawing
            if (last_draw_x >= 0 && last_draw_y >= 0) {
                // Interpolate between last and current position
                int dx = cursor_x - last_draw_x;
                int dy = cursor_y - last_draw_y;
                int steps = (dx > dy ? (dx > -dx ? dx : -dx) : (dy > -dy ? dy : -dy));
                if (steps < 1) steps = 1;
                
                for (int i = 0; i <= steps; i++) {
                    int draw_x = last_draw_x + (dx * i) / steps;
                    int draw_y = last_draw_y + (dy * i) / steps;
                    
                    // Draw brush
                    for (int by = -brush_size; by <= brush_size; by++) {
                        for (int bx = -brush_size; bx <= brush_size; bx++) {
                            if (bx * bx + by * by <= brush_size * brush_size) {
                                int px = draw_x + bx;
                                int py = draw_y + by;
                                if (px >= 0 && px < 320 && py >= 16 && py < 179) {
                                    canvas_buffer[py * 320 + px] = color;
                                }
                            }
                        }
                    }
                }
            } else {
                // First click, just draw a circle
                for (int by = -brush_size; by <= brush_size; by++) {
                    for (int bx = -brush_size; bx <= brush_size; bx++) {
                        if (bx * bx + by * by <= brush_size * brush_size) {
                            int px = cursor_x + bx;
                            int py = cursor_y + by;
                            if (px >= 0 && px < 320 && py >= 16 && py < 179) {
                                canvas_buffer[py * 320 + px] = color;
                            }
                        }
                    }
                }
            }
            last_draw_x = cursor_x;
            last_draw_y = cursor_y;
        } else {
            // Reset last position when button released
            last_draw_x = last_draw_y = -1;
        }
        
        // Render frame
        graphics_clear(COLOR_BLACK);
        
        // Draw UI
        graphics_print(5, 5, "PAINT - S:Save +/-:Brush C:Clear ESC:Exit", COLOR_WHITE, COLOR_BLACK);
        graphics_print(5, 185, "+/-:Brush C:Clear ESC:Exit", COLOR_LIGHT_GRAY, COLOR_BLACK);
        graphics_draw_line(0, 15, 319, 15, COLOR_WHITE);
        
        // Render canvas from buffer
        for (int y = 16; y < 179; y++) {
            for (int x = 0; x < 320; x++) {
                graphics_putpixel(x, y, canvas_buffer[y * 320 + x]);
            }
        }
        
        // Draw color palette
        for (int i = 0; i < 16; i++) {
            graphics_fill_rect(5 + i * 19, 180, 17, 17, i);
            graphics_draw_rect(5 + i * 19, 180, 17, 17, COLOR_DARK_GRAY);
        }
        
        // Highlight selected color
        int sel_x = 5 + color * 19;
        graphics_draw_rect(sel_x - 1, 179, 19, 19, COLOR_WHITE);
        graphics_draw_rect(sel_x - 2, 178, 21, 21, COLOR_YELLOW);
        
        // Draw brush size indicator
        char brush_text[20];
        snprintf(brush_text, sizeof(brush_text), "Size:%d", brush_size);
        graphics_print(270, 5, brush_text, COLOR_LIGHT_CYAN, COLOR_BLACK);
        
        // Draw simple arrow cursor (like normal mouse pointer)
        // Arrow pointing up-left
        graphics_draw_line(cursor_x, cursor_y, cursor_x, cursor_y + 10, COLOR_WHITE);
        graphics_draw_line(cursor_x, cursor_y, cursor_x + 6, cursor_y + 6, COLOR_WHITE);
        graphics_draw_line(cursor_x, cursor_y + 10, cursor_x + 4, cursor_y + 7, COLOR_WHITE);
        graphics_draw_line(cursor_x + 4, cursor_y + 7, cursor_x + 6, cursor_y + 6, COLOR_WHITE);
        
        // Fill arrow with black background
        graphics_putpixel(cursor_x + 1, cursor_y + 2, COLOR_BLACK);
        graphics_putpixel(cursor_x + 1, cursor_y + 3, COLOR_BLACK);
        graphics_putpixel(cursor_x + 2, cursor_y + 4, COLOR_BLACK);
        graphics_putpixel(cursor_x + 1, cursor_y + 4, COLOR_BLACK);
        graphics_putpixel(cursor_x + 1, cursor_y + 5, COLOR_BLACK);
        graphics_putpixel(cursor_x + 2, cursor_y + 5, COLOR_BLACK);
        graphics_putpixel(cursor_x + 1, cursor_y + 6, COLOR_BLACK);
        graphics_putpixel(cursor_x + 2, cursor_y + 6, COLOR_BLACK);
        graphics_putpixel(cursor_x + 3, cursor_y + 6, COLOR_BLACK);
        graphics_putpixel(cursor_x + 1, cursor_y + 7, COLOR_BLACK);
        graphics_putpixel(cursor_x + 2, cursor_y + 7, COLOR_BLACK);
        graphics_putpixel(cursor_x + 1, cursor_y + 8, COLOR_BLACK);
        graphics_putpixel(cursor_x + 1, cursor_y + 9, COLOR_BLACK);
        
        // Show drawing indicator (small dot at cursor tip)
        if (mouse_buttons.buttons & MOUSE_LEFT_BUTTON) {
            graphics_fill_circle(cursor_x, cursor_y, 1, COLOR_RED);
        }
        
        graphics_flip_buffer();
    }
    
    graphics_disable_double_buffer();
}
