#include <kernel/graphics_demo.h>
#include <kernel/graphics.h>
#include <kernel/keyboard.h>
#include <kernel/mouse.h>
#include <kernel/paint.h>
#include <kernel/tty.h>
#include <stdio.h>
#include <string.h>

// Helper function to wait for keypress
static char wait_for_key(void) {
    while (1) {
        char c = keyboard_getchar();
        if (c != 0) return c;
    }
}

// Demo 1: Basic shapes and colors
static void demo_shapes(void) {
    graphics_clear(COLOR_BLACK);
    
    // Title
    graphics_print(80, 5, "SHAPE DEMO - Press any key", COLOR_YELLOW, COLOR_BLACK);
    
    // Draw rectangles
    graphics_fill_rect(20, 30, 60, 40, COLOR_RED);
    graphics_draw_rect(20, 30, 60, 40, COLOR_WHITE);
    graphics_print(25, 75, "Rect", COLOR_WHITE, 0xFF);
    
    // Draw circles
    graphics_fill_circle(130, 50, 20, COLOR_GREEN);
    graphics_draw_circle(130, 50, 20, COLOR_WHITE);
    graphics_print(110, 75, "Circle", COLOR_WHITE, 0xFF);
    
    // Draw lines
    for (int i = 0; i < 8; i++) {
        graphics_draw_line(200, 30 + i * 5, 280, 30 + i * 5, 32 + i * 4);
    }
    graphics_print(215, 75, "Lines", COLOR_WHITE, 0xFF);
    
    // Color palette showcase
    graphics_print(70, 90, "Color Palette:", COLOR_WHITE, COLOR_BLACK);
    for (int i = 0; i < 16; i++) {
        graphics_fill_rect(20 + i * 18, 105, 16, 16, i);
    }
    
    // Gradient showcase
    graphics_print(70, 130, "Gradients:", COLOR_WHITE, COLOR_BLACK);
    for (int i = 0; i < 64; i++) {
        graphics_draw_line(20 + i * 4, 145, 20 + i * 4, 175, 32 + i);
    }
    
    wait_for_key();
}

// Demo 2: Animation
static void demo_animation(void) {
    graphics_clear(COLOR_BLACK);
    graphics_enable_double_buffer();
    
    graphics_print(60, 5, "ANIMATION DEMO - ESC to exit", COLOR_YELLOW, COLOR_BLACK);
    
    int ball_x = 160, ball_y = 100;
    int dx = 2, dy = 2;
    int radius = 10;
    int color = COLOR_RED;
    int frame = 0;
    
    while (1) {
        // Check for ESC key
        char c = keyboard_getchar();
        if (c == 27) break; // ESC
        
        // Clear back buffer
        graphics_clear(COLOR_BLACK);
        graphics_print(60, 5, "ANIMATION DEMO - ESC to exit", COLOR_YELLOW, COLOR_BLACK);
        
        // Draw boundaries
        graphics_draw_rect(10, 20, 300, 170, COLOR_WHITE);
        
        // Update ball position
        ball_x += dx;
        ball_y += dy;
        
        // Bounce off walls
        if (ball_x - radius <= 10 || ball_x + radius >= 310) {
            dx = -dx;
            color = (color + 1) % 16;
        }
        if (ball_y - radius <= 20 || ball_y + radius >= 190) {
            dy = -dy;
            color = (color + 1) % 16;
        }
        
        // Draw ball with trail effect
        graphics_fill_circle(ball_x, ball_y, radius, color);
        graphics_draw_circle(ball_x, ball_y, radius, COLOR_WHITE);
        
        // Draw some animated stars
        for (int i = 0; i < 10; i++) {
            int sx = 15 + i * 30;
            int sy = 25 + ((frame + i * 10) % 150);
            graphics_putpixel(sx, sy, 128 + ((frame + i * 20) % 128));
        }
        
        // Show frame count
        char fps_str[32];
        snprintf(fps_str, sizeof(fps_str), "Frame: %d", frame);
        graphics_print(230, 185, fps_str, COLOR_LIGHT_CYAN, COLOR_BLACK);
        
        // Flip buffer to screen
        graphics_flip_buffer();
        
        frame++;
        
        // Simple delay
        for (volatile int i = 0; i < 50000; i++);
    }
    
    graphics_disable_double_buffer();
}

// Demo 3: Pattern generator
static void demo_patterns(void) {
    graphics_clear(COLOR_BLACK);
    
    graphics_print(70, 5, "PATTERN DEMO - Press any key", COLOR_YELLOW, COLOR_BLACK);
    
    // Checkerboard pattern
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 10; x++) {
            uint8_t color = ((x + y) % 2) ? COLOR_WHITE : COLOR_BLACK;
            graphics_fill_rect(20 + x * 12, 25 + y * 12, 12, 12, color);
        }
    }
    graphics_print(30, 125, "Checkerboard", COLOR_WHITE, 0xFF);
    
    // Gradient pattern
    for (int y = 0; y < 96; y++) {
        for (int x = 0; x < 96; x++) {
            uint8_t color = 16 + ((x + y) / 12);
            graphics_putpixel(170 + x, 25 + y, color);
        }
    }
    graphics_print(185, 125, "Gradient", COLOR_WHITE, 0xFF);
    
    // Circular pattern
    for (int r = 0; r < 40; r += 4) {
        graphics_draw_circle(75, 165, r, 32 + r);
    }
    graphics_print(50, 180, "Circles", COLOR_WHITE, 0xFF);
    
    // Spiral pattern
    for (int angle = 0; angle < 720; angle += 5) {
        float rad = angle * 3.14159f / 180.0f;
        int r = angle / 20;
        int x = 200 + (int)(r * (angle % 360) / 360.0f * 1.5f);
        int y = 165 + (int)(r * (angle % 360) / 360.0f);
        if (x >= 0 && x < graphics_get_width() && y >= 0 && y < graphics_get_height()) {
            graphics_putpixel(x, y, 64 + (angle % 64));
        }
    }
    graphics_print(175, 180, "Spiral", COLOR_WHITE, 0xFF);
    
    wait_for_key();
}

// Demo 5: Plasma effect
static void demo_plasma(void) {
    graphics_clear(COLOR_BLACK);
    graphics_enable_double_buffer();
    
    graphics_print(60, 5, "PLASMA EFFECT - ESC to exit", COLOR_YELLOW, COLOR_BLACK);
    
    int time = 0;
    
    while (1) {
        char c = keyboard_getchar();
        if (c == 27) break; // ESC
        
        graphics_clear(COLOR_BLACK);
        graphics_print(60, 5, "PLASMA EFFECT - ESC to exit", COLOR_YELLOW, COLOR_BLACK);
        
        // Generate plasma effect
        for (int y = 20; y < 180; y += 2) {
            for (int x = 10; x < 310; x += 2) {
                // Simple plasma calculation
                int v1 = 128 + 127 * ((x + time) % 256) / 256;
                int v2 = 128 + 127 * ((y + time) % 256) / 256;
                int v3 = 128 + 127 * ((x + y + time) % 256) / 256;
                int color_idx = ((v1 + v2 + v3) / 3) % 128;
                
                uint8_t color = 128 + color_idx;
                graphics_putpixel(x, y, color);
                graphics_putpixel(x + 1, y, color);
                graphics_putpixel(x, y + 1, color);
                graphics_putpixel(x + 1, y + 1, color);
            }
        }
        
        graphics_flip_buffer();
        
        time += 3;
        if (time > 255) time = 0;
        
        // Delay
        for (volatile int i = 0; i < 30000; i++);
    }
    
    graphics_disable_double_buffer();
}

// Main graphics demo menu
void graphics_demo(void) {
    if (!graphics_set_mode(MODE_13H)) {
        printf("Failed to set graphics mode!\n");
        return;
    }
    
    while (1) {
        graphics_clear(COLOR_BLACK);
        
        // Title
        graphics_print(80, 20, "GRAPHICS DEMO MENU", COLOR_YELLOW, COLOR_BLACK);
        
        // Menu options
        graphics_print(50, 50, "1 - Basic Shapes", COLOR_WHITE, COLOR_BLACK);
        graphics_print(50, 65, "2 - Animation", COLOR_WHITE, COLOR_BLACK);
        graphics_print(50, 80, "3 - Patterns", COLOR_WHITE, COLOR_BLACK);
        graphics_print(50, 95, "4 - Paint Tool", COLOR_WHITE, COLOR_BLACK);
        graphics_print(50, 110, "5 - Plasma Effect", COLOR_WHITE, COLOR_BLACK);
        graphics_print(50, 125, "6 - All Demos", COLOR_WHITE, COLOR_BLACK);
        graphics_print(50, 145, "Q - Return to Text Mode", COLOR_LIGHT_RED, COLOR_BLACK);
        
        // Draw decorative border
        graphics_draw_rect(5, 5, 310, 190, COLOR_CYAN);
        graphics_draw_rect(6, 6, 308, 188, COLOR_CYAN);
        
        char choice = wait_for_key();
        
        if (choice == '1') {
            demo_shapes();
        } else if (choice == '2') {
            demo_animation();
        } else if (choice == '3') {
            demo_patterns();
        } else if (choice == '4') {
            paint_program(NULL);
        } else if (choice == '5') {
            demo_plasma();
        } else if (choice == '6') {
            demo_shapes();
            demo_animation();
            demo_patterns();
            paint_program(NULL);
            demo_plasma();
        } else if (choice == 'q' || choice == 'Q') {
            break;
        }
    }
    
    graphics_return_to_text();
}

// Simple animation demo (called from shell)
void graphics_animation_demo(void) {
    if (!graphics_set_mode(MODE_13H)) {
        printf("Failed to set graphics mode!\n");
        return;
    }
    
    demo_animation();
    graphics_return_to_text();
}

// Paint demo (called from shell)
void graphics_paint_demo(void) {
    if (!graphics_set_mode(MODE_13H)) {
        printf("Failed to set graphics mode!\n");
        return;
    }
    
    paint_program(NULL);
    graphics_return_to_text();
}

// Paint demo with directory context
void graphics_paint_demo_with_dir(const char *current_dir_path) {
    if (!graphics_set_mode(MODE_13H)) {
        printf("Failed to set graphics mode!\n");
        return;
    }
    
    paint_program(current_dir_path);
    graphics_return_to_text();
}
