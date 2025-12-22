#include <kernel/graphics.h>
#include <kernel/io.h>
#include <kernel/tty.h>
#include <string.h>
#include <stdlib.h>

// Current graphics mode
static uint8_t current_mode = MODE_TEXT;

// Dynamic display dimensions
static int display_width = MODE13H_WIDTH;
static int display_height = MODE13H_HEIGHT;
static int display_buffer_size = MODE13H_WIDTH * MODE13H_HEIGHT;

// Double buffering
static bool double_buffer_enabled = false;
static uint8_t* back_buffer = NULL;
static uint8_t temp_buffer[320 * 240];  // Largest mode we support

// Saved VGA state for text mode restoration
static uint8_t saved_crtc[25];
static uint8_t saved_seq[5];
static uint8_t saved_gfx[9];
static uint8_t saved_attr[21];
static bool state_saved = false;

// Save VGA font data (8KB = 256 chars * 32 bytes)
static uint8_t saved_font[256 * 32];
static bool font_saved = false;

// External 8x8 font (will be defined in font.c)
extern const uint8_t font_8x8[256][8];

void graphics_init(void) {
    current_mode = MODE_TEXT;
    display_width = MODE13H_WIDTH;
    display_height = MODE13H_HEIGHT;
    display_buffer_size = MODE13H_WIDTH * MODE13H_HEIGHT;
    double_buffer_enabled = false;
    back_buffer = NULL;
    state_saved = false;
    font_saved = false;
}

// Save VGA font from plane 2
static void save_vga_font(void) {
    if (font_saved) return;
    
    // Save current GC register states
    uint8_t old_seq2, old_seq4, old_gc4, old_gc5, old_gc6;
    
    outb(0x3C4, 0x02); old_seq2 = inb(0x3C5);
    outb(0x3C4, 0x04); old_seq4 = inb(0x3C5);
    outb(0x3CE, 0x04); old_gc4 = inb(0x3CF);
    outb(0x3CE, 0x05); old_gc5 = inb(0x3CF);
    outb(0x3CE, 0x06); old_gc6 = inb(0x3CF);
    
    // Set up to read from plane 2 (font data)
    outb(0x3C4, 0x02); outb(0x3C5, 0x04);  // Write plane mask
    outb(0x3C4, 0x04); outb(0x3C5, 0x07);  // Memory mode
    outb(0x3CE, 0x04); outb(0x3CF, 0x02);  // Read plane select
    outb(0x3CE, 0x05); outb(0x3CF, 0x00);  // Graphics mode
    outb(0x3CE, 0x06); outb(0x3CF, 0x00);  // Memory map (A0000-BFFFF)
    
    // Copy font data from VGA memory
    uint8_t* font_mem = (uint8_t*)0xA0000;
    for (int i = 0; i < 256 * 32; i++) {
        saved_font[i] = font_mem[i];
    }
    
    // Restore GC registers
    outb(0x3C4, 0x02); outb(0x3C5, old_seq2);
    outb(0x3C4, 0x04); outb(0x3C5, old_seq4);
    outb(0x3CE, 0x04); outb(0x3CF, old_gc4);
    outb(0x3CE, 0x05); outb(0x3CF, old_gc5);
    outb(0x3CE, 0x06); outb(0x3CF, old_gc6);
    
    font_saved = true;
}

// Restore VGA font to plane 2
static void restore_vga_font(void) {
    if (!font_saved) return;
    
    // Set up to write to plane 2
    outb(0x3C4, 0x02); outb(0x3C5, 0x04);  // Write plane 2
    outb(0x3C4, 0x04); outb(0x3C5, 0x07);  // Sequential mode
    outb(0x3CE, 0x05); outb(0x3CF, 0x00);  // Write mode 0
    outb(0x3CE, 0x06); outb(0x3CF, 0x00);  // Memory map A0000-BFFFF
    
    // Copy saved font data back to VGA memory
    uint8_t* font_mem = (uint8_t*)0xA0000;
    for (int i = 0; i < 256 * 32; i++) {
        font_mem[i] = saved_font[i];
    }
    
    // Restore normal text mode settings
    outb(0x3C4, 0x02); outb(0x3C5, 0x03);
    outb(0x3C4, 0x04); outb(0x3C5, 0x02);
    outb(0x3CE, 0x05); outb(0x3CF, 0x10);
    outb(0x3CE, 0x06); outb(0x3CF, 0x0E);
}

// Save current VGA state before switching to graphics mode
static void save_vga_state(void) {
    if (state_saved) return;
    
    // Save CRTC registers
    for (int i = 0; i < 25; i++) {
        outb(0x3D4, i);
        saved_crtc[i] = inb(0x3D5);
    }
    
    // Save Sequencer registers
    for (int i = 0; i < 5; i++) {
        outb(0x3C4, i);
        saved_seq[i] = inb(0x3C5);
    }
    
    // Save Graphics Controller registers
    for (int i = 0; i < 9; i++) {
        outb(0x3CE, i);
        saved_gfx[i] = inb(0x3CF);
    }
    
    // Save Attribute Controller registers (carefully!)
    inb(0x3DA); // Reset flip-flop
    for (int i = 0; i < 21; i++) {
        inb(0x3DA);
        outb(0x3C0, i);
        saved_attr[i] = inb(0x3C1);
    }
    // Re-enable display
    inb(0x3DA);
    outb(0x3C0, 0x20);
    
    state_saved = true;
}

// Set display dimensions for a mode
static void set_display_dimensions(int width, int height) {
    display_width = width;
    display_height = height;
    display_buffer_size = width * height;
}

// Switch to Mode 13h (320x200, 256 colors) using VGA registers
static void set_mode_320x240(void) {
    __asm__ volatile ("cli");
    
    // Misc Output Register
    outb(0x3C2, 0x63);
    
    // Sequencer registers
    outb(0x3C4, 0x00); outb(0x3C5, 0x03);
    outb(0x3C4, 0x01); outb(0x3C5, 0x01);
    outb(0x3C4, 0x02); outb(0x3C5, 0x0F);
    outb(0x3C4, 0x03); outb(0x3C5, 0x00);
    outb(0x3C4, 0x04); outb(0x3C5, 0x0E);
    
    // Unprotect CRTC registers
    outb(0x3D4, 0x11); outb(0x3D5, 0x00);
    
    // CRTC registers - tried various Mode X 320x240 timings but they don't fully work
    // Reverting to working 320x200 Mode 13h-style timing
    const uint8_t crtc_regs[] = {
        0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0xBF, 0x1F,  // 0x00-0x07
        0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // 0x08-0x0F
        0x9C, 0x0E, 0x8F, 0x28, 0x40, 0x96, 0xB9, 0xA3, 0xFF  // 0x10-0x18
    };
    
    for (int i = 0; i < 25; i++) {
        outb(0x3D4, i);
        outb(0x3D5, crtc_regs[i]);
    }
    
    // Graphics Controller
    outb(0x3CE, 0x00); outb(0x3CF, 0x00);
    outb(0x3CE, 0x01); outb(0x3CF, 0x00);
    outb(0x3CE, 0x02); outb(0x3CF, 0x00);
    outb(0x3CE, 0x03); outb(0x3CF, 0x00);
    outb(0x3CE, 0x04); outb(0x3CF, 0x00);
    outb(0x3CE, 0x05); outb(0x3CF, 0x40);
    outb(0x3CE, 0x06); outb(0x3CF, 0x05);
    outb(0x3CE, 0x07); outb(0x3CF, 0x0F);
    outb(0x3CE, 0x08); outb(0x3CF, 0xFF);
    
    // Attribute Controller
    inb(0x3DA);
    const uint8_t attr_regs[] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
        0x41, 0x00, 0x0F, 0x00, 0x00
    };
    
    for (int i = 0; i < 21; i++) {
        outb(0x3C0, i);
        outb(0x3C0, attr_regs[i]);
    }
    
    outb(0x3C0, 0x20);
    
    // Clear screen memory
    uint8_t* vga = (uint8_t*)0xA0000;
    for (int i = 0; i < 320 * 200; i++) {
        vga[i] = 0;
    }
    
    set_display_dimensions(320, 200);
    __asm__ volatile ("sti");
}

// Switch to Mode 13h (320x200, 256 colors)
static void set_mode_13h(void) {
    // VGA Mode 13h is accessed via BIOS interrupt, but in protected mode
    // we need to set it manually through VGA registers
    
    // Disable interrupts during mode switch
    __asm__ volatile ("cli");
    
    // Write to VGA registers to set mode 13h
    outb(0x3C2, 0x63); // Misc Output Register
    
    // Sequencer registers
    outb(0x3C4, 0x00); outb(0x3C5, 0x03);
    outb(0x3C4, 0x01); outb(0x3C5, 0x01);
    outb(0x3C4, 0x02); outb(0x3C5, 0x0F);
    outb(0x3C4, 0x03); outb(0x3C5, 0x00);
    outb(0x3C4, 0x04); outb(0x3C5, 0x0E);
    
    // CRTC registers
    outb(0x3D4, 0x11); outb(0x3D5, 0x00); // Unprotect CRTC registers
    
    const uint8_t crtc_regs[] = {
        0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0xBF, 0x1F,
        0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x9C, 0x0E, 0x8F, 0x28, 0x40, 0x96, 0xB9, 0xA3, 0xFF
    };
    
    for (int i = 0; i < 25; i++) {
        outb(0x3D4, i);
        outb(0x3D5, crtc_regs[i]);
    }
    
    // Graphics Controller registers
    outb(0x3CE, 0x00); outb(0x3CF, 0x00);
    outb(0x3CE, 0x01); outb(0x3CF, 0x00);
    outb(0x3CE, 0x02); outb(0x3CF, 0x00);
    outb(0x3CE, 0x03); outb(0x3CF, 0x00);
    outb(0x3CE, 0x04); outb(0x3CF, 0x00);
    outb(0x3CE, 0x05); outb(0x3CF, 0x40);
    outb(0x3CE, 0x06); outb(0x3CF, 0x05);
    outb(0x3CE, 0x07); outb(0x3CF, 0x0F);
    outb(0x3CE, 0x08); outb(0x3CF, 0xFF);
    
    // Attribute Controller registers
    inb(0x3DA); // Reset flip-flop
    
    const uint8_t attr_regs[] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
        0x41, 0x00, 0x0F, 0x00, 0x00
    };
    
    for (int i = 0; i < 21; i++) {
        outb(0x3C0, i);
        outb(0x3C0, attr_regs[i]);
    }
    
    outb(0x3C0, 0x20); // Enable video
    
    set_display_dimensions(320, 240);
    __asm__ volatile ("sti");
}

// Return to text mode
static void set_text_mode(void) {
    __asm__ volatile ("cli");
    
    // Disable video output temporarily
    inb(0x3DA);
    outb(0x3C0, 0x00);
    
    // VGA text mode 80x25 registers
    // Miscellaneous Output Register
    outb(0x3C2, 0x67);
    
    // Sequencer reset
    outb(0x3C4, 0x00);
    outb(0x3C5, 0x01);
    
    // Sequencer registers for text mode
    outb(0x3C4, 0x01); outb(0x3C5, 0x00); // Clocking Mode
    outb(0x3C4, 0x02); outb(0x3C5, 0x03); // Map Mask
    outb(0x3C4, 0x03); outb(0x3C5, 0x00); // Character Map Select
    outb(0x3C4, 0x04); outb(0x3C5, 0x02); // Memory Mode
    
    // End sequencer reset
    outb(0x3C4, 0x00);
    outb(0x3C5, 0x03);
    
    // CRTC registers - unlock and set text mode timings
    outb(0x3D4, 0x11);
    outb(0x3D5, 0x0E); // Unlock CRTC registers
    
    const uint8_t crtc_regs[25] = {
        0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F,
        0x00, 0x4F, 0x0D, 0x0E, 0x00, 0x00, 0x00, 0x50,
        0x9C, 0x0E, 0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3, 0xFF
    };
    
    for (int i = 0; i < 25; i++) {
        outb(0x3D4, i);
        outb(0x3D5, crtc_regs[i]);
    }
    
    // Graphics Controller registers for text mode
    outb(0x3CE, 0x00); outb(0x3CF, 0x00); // Set/Reset
    outb(0x3CE, 0x01); outb(0x3CF, 0x00); // Enable Set/Reset
    outb(0x3CE, 0x02); outb(0x3CF, 0x00); // Color Compare
    outb(0x3CE, 0x03); outb(0x3CF, 0x00); // Data Rotate
    outb(0x3CE, 0x04); outb(0x3CF, 0x00); // Read Map Select
    outb(0x3CE, 0x05); outb(0x3CF, 0x10); // Graphics Mode
    outb(0x3CE, 0x06); outb(0x3CF, 0x0E); // Miscellaneous (text, B8000)
    outb(0x3CE, 0x07); outb(0x3CF, 0x00); // Color Don't Care
    outb(0x3CE, 0x08); outb(0x3CF, 0xFF); // Bit Mask
    
    // Attribute Controller registers for text mode
    inb(0x3DA); // Reset flip-flop
    
    // Palette registers (identity mapping)
    for (int i = 0; i < 16; i++) {
        outb(0x3C0, i);
        outb(0x3C0, i);
    }
    
    // Attribute mode control registers
    outb(0x3C0, 0x10); outb(0x3C0, 0x0C); // Mode Control
    outb(0x3C0, 0x11); outb(0x3C0, 0x00); // Overscan Color
    outb(0x3C0, 0x12); outb(0x3C0, 0x0F); // Color Plane Enable
    outb(0x3C0, 0x13); outb(0x3C0, 0x08); // Horizontal Pixel Panning
    outb(0x3C0, 0x14); outb(0x3C0, 0x00); // Color Select
    
    // Enable display
    inb(0x3DA);
    outb(0x3C0, 0x20);
    
    // Restore standard VGA text palette
    const uint8_t text_palette[16][3] = {
        {0x00, 0x00, 0x00}, {0x00, 0x00, 0xAA}, {0x00, 0xAA, 0x00}, {0x00, 0xAA, 0xAA},
        {0xAA, 0x00, 0x00}, {0xAA, 0x00, 0xAA}, {0xAA, 0x55, 0x00}, {0xAA, 0xAA, 0xAA},
        {0x55, 0x55, 0x55}, {0x55, 0x55, 0xFF}, {0x55, 0xFF, 0x55}, {0x55, 0xFF, 0xFF},
        {0xFF, 0x55, 0x55}, {0xFF, 0x55, 0xFF}, {0xFF, 0xFF, 0x55}, {0xFF, 0xFF, 0xFF}
    };
    
    for (int i = 0; i < 16; i++) {
        outb(0x3C8, i);
        outb(0x3C9, text_palette[i][0] >> 2);
        outb(0x3C9, text_palette[i][1] >> 2);
        outb(0x3C9, text_palette[i][2] >> 2);
    }
    
    __asm__ volatile ("sti");
}

bool graphics_set_mode(uint8_t mode) {
    if (mode == MODE_TEXT) {
        if (current_mode != MODE_TEXT) {
            // Disable double buffering if active
            if (double_buffer_enabled) {
                double_buffer_enabled = false;
                back_buffer = NULL;
            }
            
            // Clear graphics memory before switching
            uint8_t* gfx_mem = VGA_MEMORY;
            for (int i = 0; i < display_buffer_size; i++) {
                gfx_mem[i] = 0;
            }
            
            set_text_mode();
            restore_vga_font();
            current_mode = MODE_TEXT;
            set_display_dimensions(0, 0);
            
            // Wait for hardware to stabilize
            for (volatile int i = 0; i < 100000; i++);
            
            // Clear text buffer at 0xB8000
            volatile uint16_t* text_mem = (volatile uint16_t*)0xB8000;
            for (int i = 0; i < 80 * 25; i++) {
                text_mem[i] = 0x0720; // White on black space
            }
            
            // Re-initialize terminal
            terminal_initialize();
        }
        return true;
    } else if (mode == MODE_13H) {
        // Save font and VGA state before switching to graphics mode
        save_vga_font();
        save_vga_state();
        
        set_mode_13h();
        current_mode = MODE_13H;
        graphics_clear(COLOR_BLACK);
        graphics_load_default_palette();
        return true;
    } else if (mode == MODE_320x240) {
        // Save font and VGA state before switching to graphics mode
        save_vga_font();
        save_vga_state();
        
        set_mode_320x240();
        current_mode = MODE_320x240;
        graphics_clear(COLOR_BLACK);
        graphics_load_default_palette();
        return true;
    }
    return false;
}

uint8_t graphics_get_mode(void) {
    return current_mode;
}

void graphics_return_to_text(void) {
    graphics_set_mode(MODE_TEXT);
}

// Basic drawing primitives
void graphics_putpixel(int x, int y, uint8_t color) {
    if (current_mode == MODE_TEXT) return;
    if (x < 0 || x >= display_width || y < 0 || y >= display_height) return;
    
    uint8_t* target = double_buffer_enabled ? back_buffer : VGA_MEMORY;
    target[y * display_width + x] = color;
}

uint8_t graphics_getpixel(int x, int y) {
    if (current_mode == MODE_TEXT) return 0;
    if (x < 0 || x >= display_width || y < 0 || y >= display_height) return 0;
    
    uint8_t* source = double_buffer_enabled ? back_buffer : VGA_MEMORY;
    return source[y * display_width + x];
}

void graphics_clear(uint8_t color) {
    if (current_mode == MODE_TEXT) return;
    
    uint8_t* target = double_buffer_enabled ? back_buffer : VGA_MEMORY;
    memset(target, color, display_buffer_size);
}

// Bresenham's line algorithm
void graphics_draw_line(int x1, int y1, int x2, int y2, uint8_t color) {
    int dx = abs(x2 - x1);
    int dy = abs(y2 - y1);
    int sx = x1 < x2 ? 1 : -1;
    int sy = y1 < y2 ? 1 : -1;
    int err = dx - dy;
    
    while (1) {
        graphics_putpixel(x1, y1, color);
        
        if (x1 == x2 && y1 == y2) break;
        
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x1 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y1 += sy;
        }
    }
}

void graphics_draw_rect(int x, int y, int width, int height, uint8_t color) {
    // Top and bottom
    for (int i = 0; i < width; i++) {
        graphics_putpixel(x + i, y, color);
        graphics_putpixel(x + i, y + height - 1, color);
    }
    // Left and right
    for (int i = 0; i < height; i++) {
        graphics_putpixel(x, y + i, color);
        graphics_putpixel(x + width - 1, y + i, color);
    }
}

void graphics_fill_rect(int x, int y, int width, int height, uint8_t color) {
    if (current_mode == MODE_TEXT) return;
    
    uint8_t* target = double_buffer_enabled ? back_buffer : VGA_MEMORY;
    
    for (int j = 0; j < height; j++) {
        int py = y + j;
        if (py < 0 || py >= display_height) continue;
        
        for (int i = 0; i < width; i++) {
            int px = x + i;
            if (px < 0 || px >= display_width) continue;
            
            target[py * display_width + px] = color;
        }
    }
}

// Midpoint circle algorithm
void graphics_draw_circle(int cx, int cy, int radius, uint8_t color) {
    int x = radius;
    int y = 0;
    int err = 0;
    
    while (x >= y) {
        graphics_putpixel(cx + x, cy + y, color);
        graphics_putpixel(cx + y, cy + x, color);
        graphics_putpixel(cx - y, cy + x, color);
        graphics_putpixel(cx - x, cy + y, color);
        graphics_putpixel(cx - x, cy - y, color);
        graphics_putpixel(cx - y, cy - x, color);
        graphics_putpixel(cx + y, cy - x, color);
        graphics_putpixel(cx + x, cy - y, color);
        
        if (err <= 0) {
            y += 1;
            err += 2*y + 1;
        }
        if (err > 0) {
            x -= 1;
            err -= 2*x + 1;
        }
    }
}

void graphics_fill_circle(int cx, int cy, int radius, uint8_t color) {
    int x = radius;
    int y = 0;
    int err = 0;
    
    while (x >= y) {
        // Draw horizontal lines to fill the circle
        for (int i = cx - x; i <= cx + x; i++) {
            graphics_putpixel(i, cy + y, color);
            graphics_putpixel(i, cy - y, color);
        }
        for (int i = cx - y; i <= cx + y; i++) {
            graphics_putpixel(i, cy + x, color);
            graphics_putpixel(i, cy - x, color);
        }
        
        if (err <= 0) {
            y += 1;
            err += 2*y + 1;
        }
        if (err > 0) {
            x -= 1;
            err -= 2*x + 1;
        }
    }
}

// Double buffering
void graphics_enable_double_buffer(void) {
    if (!double_buffer_enabled) {
        back_buffer = temp_buffer;
        double_buffer_enabled = true;
        // Copy current screen to back buffer
        memcpy(back_buffer, VGA_MEMORY, display_buffer_size);
    }
}

void graphics_disable_double_buffer(void) {
    if (double_buffer_enabled) {
        // Flip one last time to ensure consistency
        graphics_flip_buffer();
        double_buffer_enabled = false;
        back_buffer = NULL;
    }
}

void graphics_flip_buffer(void) {
    if (double_buffer_enabled && back_buffer) {
        memcpy(VGA_MEMORY, back_buffer, display_buffer_size);
    }
}

bool graphics_is_double_buffered(void) {
    return double_buffer_enabled;
}

// Text rendering with 8x8 font
void graphics_putchar(int x, int y, char c, uint8_t fg_color, uint8_t bg_color) {
    if (current_mode == MODE_TEXT) return;
    
    const uint8_t* glyph = font_8x8[(uint8_t)c];
    
    for (int j = 0; j < 8; j++) {
        for (int i = 0; i < 8; i++) {
            if (glyph[j] & (1 << (7 - i))) {
                graphics_putpixel(x + i, y + j, fg_color);
            } else if (bg_color != 0xFF) { // 0xFF = transparent
                graphics_putpixel(x + i, y + j, bg_color);
            }
        }
    }
}

void graphics_print(int x, int y, const char* str, uint8_t fg_color, uint8_t bg_color) {
    int cx = x;
    int cy = y;
    
    while (*str) {
        if (*str == '\n') {
            cx = x;
            cy += 8;
        } else {
            graphics_putchar(cx, cy, *str, fg_color, bg_color);
            cx += 8;
            if (cx >= display_width) {
                cx = x;
                cy += 8;
            }
        }
        str++;
    }
}

// Alias for terminal compatibility
void graphics_draw_char(int x, int y, char c, uint8_t fg_color, uint8_t bg_color) {
    graphics_putchar(x, y, c, fg_color, bg_color);
}

// Scroll screen up by specified number of pixels
void graphics_scroll_up(int pixels) {
    if (current_mode == MODE_TEXT) return;
    
    uint8_t* vram = VGA_MEMORY;
    
    // Copy each row up by 'pixels' rows
    for (int y = pixels; y < display_height; y++) {
        for (int x = 0; x < display_width; x++) {
            vram[(y - pixels) * display_width + x] = vram[y * display_width + x];
        }
    }
    
    // Clear the bottom 'pixels' rows
    for (int y = display_height - pixels; y < display_height; y++) {
        for (int x = 0; x < display_width; x++) {
            vram[y * display_width + x] = COLOR_BLACK;
        }
    }
}

int graphics_get_width(void) {
    return display_width;
}

int graphics_get_height(void) {
    return display_height;
}

// Palette manipulation
void graphics_set_palette_color(uint8_t index, uint8_t r, uint8_t g, uint8_t b) {
    outb(0x3C8, index);
    outb(0x3C9, r >> 2); // VGA uses 6-bit color components
    outb(0x3C9, g >> 2);
    outb(0x3C9, b >> 2);
}

void graphics_get_palette_color(uint8_t index, uint8_t* r, uint8_t* g, uint8_t* b) {
    outb(0x3C7, index);
    *r = inb(0x3C9) << 2;
    *g = inb(0x3C9) << 2;
    *b = inb(0x3C9) << 2;
}

void graphics_load_default_palette(void) {
    // Set up a nice default palette with gradients
    // Standard 16 colors (0-15)
    const uint8_t standard_colors[16][3] = {
        {0x00, 0x00, 0x00}, // Black
        {0x00, 0x00, 0xAA}, // Blue
        {0x00, 0xAA, 0x00}, // Green
        {0x00, 0xAA, 0xAA}, // Cyan
        {0xAA, 0x00, 0x00}, // Red
        {0xAA, 0x00, 0xAA}, // Magenta
        {0xAA, 0x55, 0x00}, // Brown
        {0xAA, 0xAA, 0xAA}, // Light Gray
        {0x55, 0x55, 0x55}, // Dark Gray
        {0x55, 0x55, 0xFF}, // Light Blue
        {0x55, 0xFF, 0x55}, // Light Green
        {0x55, 0xFF, 0xFF}, // Light Cyan
        {0xFF, 0x55, 0x55}, // Light Red
        {0xFF, 0x55, 0xFF}, // Light Magenta
        {0xFF, 0xFF, 0x55}, // Yellow
        {0xFF, 0xFF, 0xFF}, // White
    };
    
    for (int i = 0; i < 16; i++) {
        graphics_set_palette_color(i, standard_colors[i][0], 
                                   standard_colors[i][1], 
                                   standard_colors[i][2]);
    }
    
    // Grayscale ramp (16-31)
    for (int i = 0; i < 16; i++) {
        uint8_t val = i * 17;
        graphics_set_palette_color(16 + i, val, val, val);
    }
    
    // Color gradients (32-255)
    // Red gradient
    for (int i = 0; i < 32; i++) {
        graphics_set_palette_color(32 + i, 128 + i * 4, 0, 0);
    }
    
    // Green gradient
    for (int i = 0; i < 32; i++) {
        graphics_set_palette_color(64 + i, 0, 128 + i * 4, 0);
    }
    
    // Blue gradient
    for (int i = 0; i < 32; i++) {
        graphics_set_palette_color(96 + i, 0, 0, 128 + i * 4);
    }
    
    // Rainbow colors
    for (int i = 0; i < 128; i++) {
        uint8_t r, g, b;
        if (i < 21) {
            r = 255; g = i * 12; b = 0;
        } else if (i < 42) {
            r = 255 - (i - 21) * 12; g = 255; b = 0;
        } else if (i < 64) {
            r = 0; g = 255; b = (i - 42) * 11;
        } else if (i < 85) {
            r = 0; g = 255 - (i - 64) * 12; b = 255;
        } else if (i < 106) {
            r = (i - 85) * 12; g = 0; b = 255;
        } else {
            r = 255; g = 0; b = 255 - (i - 106) * 11;
        }
        graphics_set_palette_color(128 + i, r, g, b);
    }
}
