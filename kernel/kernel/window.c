#include <kernel/window.h>
#include <kernel/graphics.h>
#include <kernel/kmalloc.h>
#include <string.h>
#include <stdio.h>

// Define context menu colors
#define CONTEXT_MENU_HOVER_COLOR 9  // Light blue

// External font from graphics subsystem
extern const uint8_t font_8x8[256][8];

// Global window manager
static window_manager_t wm = {0};

void window_manager_init(void) {
    wm.window_list = NULL;
    wm.focused_window = NULL;
    wm.cursor_x = graphics_get_width() / 2;
    wm.cursor_y = graphics_get_height() / 2;
    wm.cursor_visible = true;
    wm.cursor_color = COLOR_WHITE;
}

window_manager_t* window_get_manager(void) {
    return &wm;
}

window_t* window_create(int x, int y, int width, int height, const char* title) {
    // Enforce minimum size
    if (width < WINDOW_MIN_WIDTH) width = WINDOW_MIN_WIDTH;
    if (height < WINDOW_MIN_HEIGHT) height = WINDOW_MIN_HEIGHT;
    
    // Enforce maximum size (don't exceed screen bounds)
    int screen_width = graphics_get_width();
    int screen_height = graphics_get_height();
    if (width > screen_width) width = screen_width;
    if (height > screen_height) height = screen_height;
    
    // Keep window position on screen
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + width > screen_width) x = screen_width - width;
    if (y + height > screen_height) y = screen_height - height;
    
    // Allocate window structure
    window_t* window = (window_t*)kmalloc(sizeof(window_t));
    if (!window) return NULL;
    
    // Calculate content area size
    int content_width = width - (WINDOW_BORDER_WIDTH * 2);
    int content_height = height - WINDOW_TITLE_BAR_HEIGHT - WINDOW_BORDER_WIDTH;
    
    // Allocate framebuffer for content area
    window->framebuffer = (uint8_t*)kmalloc(content_width * content_height);
    if (!window->framebuffer) {
        kfree(window);
        return NULL;
    }
    
    // Initialize window
    window->x = x;
    window->y = y;
    window->width = width;
    window->height = height;
    window->content_width = content_width;
    window->content_height = content_height;
    window->flags = WINDOW_FLAG_VISIBLE | WINDOW_FLAG_CLOSABLE | WINDOW_FLAG_HAS_BORDER;
    window->drag_offset_x = 0;
    window->drag_offset_y = 0;
    window->context_menu = NULL;
    window->user_data = NULL;
    window->on_priority_click = NULL;
    window->on_click = NULL;
    window->on_key = NULL;
    window->on_drag = NULL;
    window->on_right_click = NULL;
    window->on_scroll = NULL;
    window->on_destroy = NULL;
    
    // Copy title
    if (title) {
        strncpy(window->title, title, sizeof(window->title) - 1);
        window->title[sizeof(window->title) - 1] = '\0';
    } else {
        window->title[0] = '\0';
    }
    
    // Clear framebuffer
    memset(window->framebuffer, WINDOW_COLOR_BACKGROUND, content_width * content_height);
    
    // Add to window list (at front)
    window->next = wm.window_list;
    window->prev = NULL;
    if (wm.window_list) {
        wm.window_list->prev = window;
    }
    wm.window_list = window;
    
    // Focus the new window
    window_focus(window);
    
    return window;
}

void window_destroy(window_t* window) {
    if (!window) return;
    
    // Call destroy callback first (allows app to clean up)
    if (window->on_destroy) {
        window->on_destroy(window);
    }
    
    // Remove from list
    if (window->prev) {
        window->prev->next = window->next;
    } else {
        wm.window_list = window->next;
    }
    
    if (window->next) {
        window->next->prev = window->prev;
    }
    
    // Update focus if this was focused
    if (wm.focused_window == window) {
        wm.focused_window = wm.window_list;
        if (wm.focused_window) {
            wm.focused_window->flags |= WINDOW_FLAG_FOCUSED;
        }
    }
    
    // Free memory
    if (window->framebuffer) {
        kfree(window->framebuffer);
    }
    if (window->context_menu) {
        context_menu_destroy(window->context_menu);
    }
    if (window->user_data) {
        kfree(window->user_data);
    }
    kfree(window);
}

void window_move(window_t* window, int x, int y) {
    if (!window) return;
    window->x = x;
    window->y = y;
}

void window_resize(window_t* window, int width, int height) {
    if (!window) return;
    
    // Enforce minimum size
    if (width < WINDOW_MIN_WIDTH) width = WINDOW_MIN_WIDTH;
    if (height < WINDOW_MIN_HEIGHT) height = WINDOW_MIN_HEIGHT;
    
    // Enforce maximum size (don't exceed screen bounds)
    int screen_width = graphics_get_width();
    int screen_height = graphics_get_height();
    if (width > screen_width) width = screen_width;
    if (height > screen_height) height = screen_height;
    
    // Calculate new content area
    int new_content_width = width - (WINDOW_BORDER_WIDTH * 2);
    int new_content_height = height - WINDOW_TITLE_BAR_HEIGHT - WINDOW_BORDER_WIDTH;
    
    // Reallocate framebuffer if size changed
    if (new_content_width != window->content_width || 
        new_content_height != window->content_height) {
        
        uint8_t* new_fb = (uint8_t*)kmalloc(new_content_width * new_content_height);
        if (!new_fb) return;
        
        // Clear new buffer
        memset(new_fb, WINDOW_COLOR_BACKGROUND, new_content_width * new_content_height);
        
        // Copy old content (as much as fits)
        int copy_width = (new_content_width < window->content_width) ? 
                         new_content_width : window->content_width;
        int copy_height = (new_content_height < window->content_height) ? 
                          new_content_height : window->content_height;
        
        for (int y = 0; y < copy_height; y++) {
            memcpy(new_fb + y * new_content_width,
                   window->framebuffer + y * window->content_width,
                   copy_width);
        }
        
        kfree(window->framebuffer);
        window->framebuffer = new_fb;
        window->content_width = new_content_width;
        window->content_height = new_content_height;
    }
    
    window->width = width;
    window->height = height;
}

void window_set_title(window_t* window, const char* title) {
    if (!window || !title) return;
    strncpy(window->title, title, sizeof(window->title) - 1);
    window->title[sizeof(window->title) - 1] = '\0';
}

void window_show(window_t* window) {
    if (!window) return;
    window->flags |= WINDOW_FLAG_VISIBLE;
}

void window_hide(window_t* window) {
    if (!window) return;
    window->flags &= ~WINDOW_FLAG_VISIBLE;
}

void window_focus(window_t* window) {
    if (!window) return;
    
    // Unfocus all windows
    for (window_t* w = wm.window_list; w; w = w->next) {
        w->flags &= ~WINDOW_FLAG_FOCUSED;
    }
    
    // Focus this window
    window->flags |= WINDOW_FLAG_FOCUSED;
    wm.focused_window = window;
    
    // Raise it to front
    window_raise(window);
}

void window_raise(window_t* window) {
    if (!window || window == wm.window_list) return;
    
    // Remove from current position
    if (window->prev) {
        window->prev->next = window->next;
    }
    if (window->next) {
        window->next->prev = window->prev;
    }
    
    // Add to front
    window->next = wm.window_list;
    window->prev = NULL;
    if (wm.window_list) {
        wm.window_list->prev = window;
    }
    wm.window_list = window;
}

void window_draw(window_t* window) {
    if (!window || !(window->flags & WINDOW_FLAG_VISIBLE)) return;
    
    bool is_focused = (window->flags & WINDOW_FLAG_FOCUSED) != 0;
    int screen_width = graphics_get_width();
    int screen_height = graphics_get_height();
    
    // Draw outer border
    if (window->flags & WINDOW_FLAG_HAS_BORDER) {
        graphics_draw_rect(window->x, window->y, 
                          window->width, window->height, 
                          WINDOW_COLOR_BORDER);
    }
    
    // Draw title bar
    uint8_t title_color = is_focused ? WINDOW_COLOR_TITLE_BAR_ACTIVE : 
                                       WINDOW_COLOR_TITLE_BAR_INACTIVE;
    graphics_fill_rect(window->x + WINDOW_BORDER_WIDTH, 
                       window->y + WINDOW_BORDER_WIDTH,
                       window->width - (WINDOW_BORDER_WIDTH * 2), 
                       WINDOW_TITLE_BAR_HEIGHT - WINDOW_BORDER_WIDTH,
                       title_color);
    
    // Draw title text
    graphics_print(window->x + WINDOW_BORDER_WIDTH + 4,
                   window->y + WINDOW_BORDER_WIDTH + 4,
                   window->title,
                   COLOR_WHITE, title_color);
    
    // Draw close button if closable
    if (window->flags & WINDOW_FLAG_CLOSABLE) {
        int btn_x = window->x + window->width - WINDOW_BORDER_WIDTH - 14;
        int btn_y = window->y + WINDOW_BORDER_WIDTH + 2;
        graphics_fill_rect(btn_x, btn_y, 12, 12, WINDOW_COLOR_CLOSE_BUTTON);
        // Draw X
        graphics_draw_line(btn_x + 3, btn_y + 3, btn_x + 9, btn_y + 9, COLOR_WHITE);
        graphics_draw_line(btn_x + 9, btn_y + 3, btn_x + 3, btn_y + 9, COLOR_WHITE);
    }
    
    // Draw content area background
    int content_x = window->x + WINDOW_BORDER_WIDTH;
    int content_y = window->y + WINDOW_TITLE_BAR_HEIGHT;
    
    graphics_fill_rect(content_x, content_y,
                       window->content_width, window->content_height,
                       WINDOW_COLOR_BACKGROUND);
    
    // Copy framebuffer content
    for (int y = 0; y < window->content_height; y++) {
        int screen_y = content_y + y;
        if (screen_y < 0 || screen_y >= screen_height) continue;
        
        for (int x = 0; x < window->content_width; x++) {
            int screen_x = content_x + x;
            if (screen_x < 0 || screen_x >= screen_width) continue;
            
            uint8_t color = window->framebuffer[y * window->content_width + x];
            if (color != WINDOW_COLOR_BACKGROUND) {  // Simple transparency
                graphics_putpixel(screen_x, screen_y, color);
            }
        }
    }
}

void window_draw_all(void) {
    // Draw from back to front (reverse order)
    if (!wm.window_list) return;
    
    // Find the last window
    window_t* last = wm.window_list;
    while (last->next) last = last->next;
    
    // Draw backwards
    for (window_t* w = last; w; w = w->prev) {
        window_draw(w);
    }
    
    // Draw all visible context menus on top of windows
    // Draw from back to front so topmost window's context menu renders last (on top)
    for (window_t* w = last; w; w = w->prev) {
        if (w->context_menu && w->context_menu->visible) {
            context_menu_draw(w->context_menu);
        }
    }
    
    // Draw cursor on top of everything
    if (wm.cursor_visible) {
        window_draw_cursor();
    }
}

void window_clear_content(window_t* window, uint8_t color) {
    if (!window) return;
    memset(window->framebuffer, color, window->content_width * window->content_height);
}

void window_putpixel(window_t* window, int x, int y, uint8_t color) {
    if (!window) return;
    if (x < 0 || x >= window->content_width) return;
    if (y < 0 || y >= window->content_height) return;
    
    window->framebuffer[y * window->content_width + x] = color;
}

void window_draw_rect(window_t* window, int x, int y, int width, int height, uint8_t color) {
    if (!window) return;
    
    // Top and bottom
    for (int i = 0; i < width; i++) {
        window_putpixel(window, x + i, y, color);
        window_putpixel(window, x + i, y + height - 1, color);
    }
    
    // Left and right
    for (int i = 0; i < height; i++) {
        window_putpixel(window, x, y + i, color);
        window_putpixel(window, x + width - 1, y + i, color);
    }
}

void window_fill_rect(window_t* window, int x, int y, int width, int height, uint8_t color) {
    if (!window) return;
    
    for (int j = 0; j < height; j++) {
        for (int i = 0; i < width; i++) {
            window_putpixel(window, x + i, y + j, color);
        }
    }
}

void window_print(window_t* window, int x, int y, const char* text, uint8_t color) {
    if (!window || !text) return;
    
    int cx = x;
    int cy = y;
    
    while (*text) {
        if (*text == '\n') {
            cx = x;
            cy += 8;
        } else {
            // Render character using 8x8 font
            const uint8_t* glyph = font_8x8[(uint8_t)*text];
            
            for (int j = 0; j < 8; j++) {
                for (int i = 0; i < 8; i++) {
                    if (glyph[j] & (1 << (7 - i))) {
                        window_putpixel(window, cx + i, cy + j, color);
                    }
                }
            }
            cx += 8;
        }
        text++;
        
        if (cx + 8 > window->content_width) {
            cx = x;
            cy += 8;
        }
    }
}

window_t* window_at_position(int x, int y) {
    // Check from front to back
    for (window_t* w = wm.window_list; w; w = w->next) {
        if (!(w->flags & WINDOW_FLAG_VISIBLE)) continue;
        
        if (x >= w->x && x < w->x + w->width &&
            y >= w->y && y < w->y + w->height) {
            return w;
        }
    }
    return NULL;
}

bool window_point_in_title_bar(window_t* window, int x, int y) {
    if (!window) return false;
    
    int title_y_start = window->y + WINDOW_BORDER_WIDTH;
    int title_y_end = window->y + WINDOW_TITLE_BAR_HEIGHT;
    
    return (x >= window->x + WINDOW_BORDER_WIDTH && 
            x < window->x + window->width - WINDOW_BORDER_WIDTH &&
            y >= title_y_start && y < title_y_end);
}

bool window_point_in_close_button(window_t* window, int x, int y) {
    if (!window || !(window->flags & WINDOW_FLAG_CLOSABLE)) return false;
    
    int btn_x = window->x + window->width - WINDOW_BORDER_WIDTH - 14;
    int btn_y = window->y + WINDOW_BORDER_WIDTH + 2;
    
    return (x >= btn_x && x < btn_x + 12 &&
            y >= btn_y && y < btn_y + 12);
}

void window_handle_mouse_move(int x, int y) {
    wm.cursor_x = x;
    wm.cursor_y = y;
    
    // Update hover state for all visible context menus
    window_t* window = wm.window_list;
    while (window) {
        if (window->context_menu && window->context_menu->visible) {
            context_menu_handle_mouse_move(window->context_menu, x, y);
        }
        window = window->next;
    }
    
    // Handle window dragging
    if (wm.focused_window && (wm.focused_window->flags & WINDOW_FLAG_DRAGGING)) {
        int new_x = x - wm.focused_window->drag_offset_x;
        int new_y = y - wm.focused_window->drag_offset_y;
        
        // Keep window on screen
        if (new_x < 0) new_x = 0;
        if (new_y < 0) new_y = 0;
        if (new_x + wm.focused_window->width > graphics_get_width()) {
            new_x = graphics_get_width() - wm.focused_window->width;
        }
        if (new_y + wm.focused_window->height > graphics_get_height()) {
            new_y = graphics_get_height() - wm.focused_window->height;
        }
        
        window_move(wm.focused_window, new_x, new_y);
    }
}

bool window_handle_mouse_click(int x, int y, uint8_t buttons) {
    // First, check if any context menu is open and handle clicks on it
    // Check from front to back (topmost windows first) for proper z-order
    window_t* window = wm.window_list;
    while (window) {
        if (window->context_menu && window->context_menu->visible) {
            if (context_menu_handle_click(window->context_menu, x, y)) {
                return true;  // Menu handled the click
            }
            // If click was outside menu, it was hidden, continue to handle window click
        }
        window = window->next;
    }
    
    // Get the topmost window at this position (respects z-order)
    window_t* clicked = window_at_position(x, y);
    
    // Handle right-click (button 0x02)
    if (buttons & 0x02) {
        if (clicked) {
            // Focus the window (brings it to front)
            window_focus(clicked);
            
            // Hide all other visible context menus first
            window_t* w = wm.window_list;
            while (w) {
                if (w != clicked && w->context_menu && w->context_menu->visible) {
                    context_menu_hide(w->context_menu);
                }
                w = w->next;
            }
            
            // Show context menu if window has one
            if (clicked->context_menu) {
                context_menu_show(clicked->context_menu, x, y);
            }
            
            // Call custom right-click handler if set
            if (clicked->on_right_click) {
                // Convert to window-relative coordinates
                int rel_x = x - clicked->x - WINDOW_BORDER_WIDTH;
                int rel_y = y - clicked->y - WINDOW_TITLE_BAR_HEIGHT;
                clicked->on_right_click(clicked, rel_x, rel_y);
            }
        }
        return true;
    }
    
    // Handle left-click (button 0x01)
    if (!(buttons & 0x01)) return false;  // Only handle left button
    
    if (clicked) {
        // Focus the window (brings it to front for proper z-order)
        window_focus(clicked);
        
        // Hide all visible context menus on left-click
        window_t* w = wm.window_list;
        while (w) {
            if (w->context_menu && w->context_menu->visible) {
                context_menu_hide(w->context_menu);
            }
            w = w->next;
        }
        
        // Check priority click handler first (e.g., menu bars)
        if (clicked->on_priority_click) {
            int rel_x = x - clicked->x - WINDOW_BORDER_WIDTH;
            int rel_y = y - clicked->y - WINDOW_TITLE_BAR_HEIGHT;
            
            if (rel_x >= 0 && rel_x < clicked->content_width &&
                rel_y >= 0 && rel_y < clicked->content_height) {
                
                // Priority handler returns true if it handled the click
                if (clicked->on_priority_click(clicked, rel_x, rel_y)) {
                    return true;  // Click was handled by priority handler (e.g., menu bar)
                }
            }
        }
        
        // Check if close button was clicked
        if (window_point_in_close_button(clicked, x, y)) {
            window_destroy(clicked);
            return true;
        }
        
        // Start dragging if title bar was clicked
        if (window_point_in_title_bar(clicked, x, y)) {
            clicked->flags |= WINDOW_FLAG_DRAGGING;
            clicked->drag_offset_x = x - clicked->x;
            clicked->drag_offset_y = y - clicked->y;
            return true;
        }
    }
    return false;  // Click was not handled, allow further processing
}

void window_handle_mouse_release(int x, int y, uint8_t buttons) {
    // Stop dragging
    if (wm.focused_window) {
        wm.focused_window->flags &= ~WINDOW_FLAG_DRAGGING;
    }
}

void window_set_cursor_pos(int x, int y) {
    wm.cursor_x = x;
    wm.cursor_y = y;
}

void window_get_cursor_pos(int* x, int* y) {
    if (x) *x = wm.cursor_x;
    if (y) *y = wm.cursor_y;
}

void window_show_cursor(void) {
    wm.cursor_visible = true;
}

void window_hide_cursor(void) {
    wm.cursor_visible = false;
}

void window_draw_cursor(void) {
    if (!wm.cursor_visible) return;
    
    // Draw a simple arrow cursor
    int x = wm.cursor_x;
    int y = wm.cursor_y;
    
    // Bounds check
    int width = graphics_get_width();
    int height = graphics_get_height();
    if (x < 0 || x >= width - 11 || y < 0 || y >= height - 11) return;
    
    // Draw black outline first for visibility
    int cursor_shape[][2] = {
        {0,0}, {0,1}, {0,2}, {0,3}, {0,4}, {0,5}, {0,6}, {0,7}, {0,8}, {0,9}, {0,10},
        {1,1}, {1,2}, {1,3}, {1,4}, {1,5}, {1,6}, {1,7}, {1,8}, {1,9},
        {2,2}, {2,3}, {2,4}, {2,5}, {2,6}, {2,7}, {2,8},
        {3,3}, {3,4}, {3,5}, {3,6}, {3,7},
        {4,4}, {4,5}, {4,6}, {4,7},
        {5,5}, {5,6},
        {6,6}
    };
    
    // Draw black outline
    for (int i = 0; i < 37; i++) {
        int px = x + cursor_shape[i][0];
        int py = y + cursor_shape[i][1];
        if (cursor_shape[i][0] == 0 || cursor_shape[i][1] <= cursor_shape[i][0]) {
            // Draw black border around
            if (px > 0) graphics_putpixel(px - 1, py, COLOR_BLACK);
            if (px < width - 1) graphics_putpixel(px + 1, py, COLOR_BLACK);
            if (py > 0) graphics_putpixel(px, py - 1, COLOR_BLACK);
            if (py < height - 1) graphics_putpixel(px, py + 1, COLOR_BLACK);
        }
    }
    
    // Draw white cursor
    for (int i = 0; i < 37; i++) {
        graphics_putpixel(x + cursor_shape[i][0], y + cursor_shape[i][1], COLOR_WHITE);
    }
}

// ============================================================================
// Context Menu Implementation
// ============================================================================

#define CONTEXT_MENU_ITEM_HEIGHT 16
#define CONTEXT_MENU_MIN_WIDTH 120
#define CONTEXT_MENU_PADDING 4

// Create a new context menu
context_menu_t* context_menu_create(window_t* owner) {
    context_menu_t* menu = (context_menu_t*)kmalloc(sizeof(context_menu_t));
    if (!menu) return NULL;
    
    menu->x = 0;
    menu->y = 0;
    menu->width = CONTEXT_MENU_MIN_WIDTH;
    menu->height = 0;
    menu->visible = false;
    menu->items = NULL;
    menu->item_count = 0;
    menu->hover_index = -1;
    menu->owner = owner;
    
    return menu;
}

// Destroy context menu and all items
void context_menu_destroy(context_menu_t* menu) {
    if (!menu) return;
    
    // Free all menu items
    context_menu_item_t* item = menu->items;
    while (item) {
        context_menu_item_t* next = item->next;
        kfree(item);
        item = next;
    }
    
    kfree(menu);
}

// Add a menu item
void context_menu_add_item(context_menu_t* menu, const char* label, void (*on_select)(window_t*)) {
    if (!menu) return;
    
    context_menu_item_t* item = (context_menu_item_t*)kmalloc(sizeof(context_menu_item_t));
    if (!item) return;
    
    strncpy(item->label, label, sizeof(item->label) - 1);
    item->label[sizeof(item->label) - 1] = '\0';
    item->on_select = on_select;
    item->enabled = true;
    item->separator = false;
    item->next = NULL;
    
    // Add to end of list
    if (!menu->items) {
        menu->items = item;
    } else {
        context_menu_item_t* last = menu->items;
        while (last->next) last = last->next;
        last->next = item;
    }
    
    menu->item_count++;
    menu->height = menu->item_count * CONTEXT_MENU_ITEM_HEIGHT + CONTEXT_MENU_PADDING * 2;
    
    // Update width based on label length
    int label_width = strlen(label) * 8 + CONTEXT_MENU_PADDING * 2 + 16;
    if (label_width > menu->width) {
        menu->width = label_width;
    }
}

// Add a separator line
void context_menu_add_separator(context_menu_t* menu) {
    if (!menu) return;
    
    context_menu_item_t* item = (context_menu_item_t*)kmalloc(sizeof(context_menu_item_t));
    if (!item) return;
    
    item->label[0] = '\0';
    item->on_select = NULL;
    item->enabled = false;
    item->separator = true;
    item->next = NULL;
    
    // Add to end of list
    if (!menu->items) {
        menu->items = item;
    } else {
        context_menu_item_t* last = menu->items;
        while (last->next) last = last->next;
        last->next = item;
    }
    
    menu->item_count++;
    menu->height = menu->item_count * CONTEXT_MENU_ITEM_HEIGHT + CONTEXT_MENU_PADDING * 2;
}

// Show the context menu at a specific position
void context_menu_show(context_menu_t* menu, int x, int y) {
    if (!menu) return;
    
    menu->x = x;
    menu->y = y;
    
    // Keep menu on screen
    int screen_width = graphics_get_width();
    int screen_height = graphics_get_height();
    
    if (menu->x + menu->width > screen_width) {
        menu->x = screen_width - menu->width;
    }
    if (menu->y + menu->height > screen_height) {
        menu->y = screen_height - menu->height;
    }
    
    menu->visible = true;
    menu->hover_index = -1;
}

// Hide the context menu
void context_menu_hide(context_menu_t* menu) {
    if (!menu) return;
    menu->visible = false;
    menu->hover_index = -1;
}

// Draw the context menu
void context_menu_draw(context_menu_t* menu) {
    if (!menu || !menu->visible) return;
    
    // Draw background
    for (int dy = 0; dy < menu->height; dy++) {
        for (int dx = 0; dx < menu->width; dx++) {
            graphics_putpixel(menu->x + dx, menu->y + dy, WINDOW_COLOR_BACKGROUND);
        }
    }
    
    // Draw border
    // Top and bottom
    for (int dx = 0; dx < menu->width; dx++) {
        graphics_putpixel(menu->x + dx, menu->y, COLOR_BLACK);
        graphics_putpixel(menu->x + dx, menu->y + menu->height - 1, COLOR_BLACK);
    }
    // Left and right
    for (int dy = 0; dy < menu->height; dy++) {
        graphics_putpixel(menu->x, menu->y + dy, COLOR_BLACK);
        graphics_putpixel(menu->x + menu->width - 1, menu->y + dy, COLOR_BLACK);
    }
    
    // Draw items
    int y_offset = CONTEXT_MENU_PADDING;
    context_menu_item_t* item = menu->items;
    int index = 0;
    
    while (item) {
        int item_y = menu->y + y_offset;
        
        if (item->separator) {
            // Draw separator line
            int line_y = item_y + CONTEXT_MENU_ITEM_HEIGHT / 2;
            for (int dx = CONTEXT_MENU_PADDING; dx < menu->width - CONTEXT_MENU_PADDING; dx++) {
                graphics_putpixel(menu->x + dx, line_y, COLOR_DARK_GRAY);
            }
        } else {
            // Draw highlight if hovered
            if (index == menu->hover_index && item->enabled) {
                for (int dy = 0; dy < CONTEXT_MENU_ITEM_HEIGHT; dy++) {
                    for (int dx = 2; dx < menu->width - 2; dx++) {
                        graphics_putpixel(menu->x + dx, item_y + dy, CONTEXT_MENU_HOVER_COLOR);
                    }
                }
            }
            
            // Draw text
            uint8_t text_color = item->enabled ? COLOR_BLACK : COLOR_DARK_GRAY;
            int text_x = menu->x + CONTEXT_MENU_PADDING + 4;
            int text_y = item_y + (CONTEXT_MENU_ITEM_HEIGHT - 8) / 2;
            
            // Draw each character
            const char* str = item->label;
            int char_x = text_x;
            while (*str) {
                if (char_x + 8 > menu->x + menu->width - CONTEXT_MENU_PADDING) break;
                
                for (int cy = 0; cy < 8; cy++) {
                    uint8_t row = font_8x8[(unsigned char)*str][cy];
                    for (int cx = 0; cx < 8; cx++) {
                        if (row & (1 << cx)) {
                            graphics_putpixel(char_x + cx, text_y + cy, text_color);
                        }
                    }
                }
                char_x += 8;
                str++;
            }
        }
        
        y_offset += CONTEXT_MENU_ITEM_HEIGHT;
        item = item->next;
        index++;
    }
}

// Handle mouse click on context menu
bool context_menu_handle_click(context_menu_t* menu, int x, int y) {
    if (!menu || !menu->visible) return false;
    
    // Check if click is within menu bounds
    if (x < menu->x || x >= menu->x + menu->width ||
        y < menu->y || y >= menu->y + menu->height) {
        // Click outside menu - hide it
        context_menu_hide(menu);
        return false;
    }
    
    // Find which item was clicked
    int relative_y = y - menu->y - CONTEXT_MENU_PADDING;
    int item_index = relative_y / CONTEXT_MENU_ITEM_HEIGHT;
    
    if (item_index < 0 || item_index >= menu->item_count) {
        return true;  // Click was in menu but not on an item
    }
    
    // Find the item
    context_menu_item_t* item = menu->items;
    for (int i = 0; i < item_index && item; i++) {
        item = item->next;
    }
    
    if (item && !item->separator && item->enabled && item->on_select) {
        // Execute the callback
        item->on_select(menu->owner);
    }
    
    // Hide menu after selection
    context_menu_hide(menu);
    return true;
}

// Handle mouse movement over context menu
void context_menu_handle_mouse_move(context_menu_t* menu, int x, int y) {
    if (!menu || !menu->visible) return;
    
    // Check if mouse is within menu bounds
    if (x < menu->x || x >= menu->x + menu->width ||
        y < menu->y || y >= menu->y + menu->height) {
        menu->hover_index = -1;
        return;
    }
    
    // Calculate which item is being hovered
    int relative_y = y - menu->y - CONTEXT_MENU_PADDING;
    int item_index = relative_y / CONTEXT_MENU_ITEM_HEIGHT;
    
    if (item_index >= 0 && item_index < menu->item_count) {
        menu->hover_index = item_index;
    } else {
        menu->hover_index = -1;
    }
}

// Default menu item callbacks
static void menu_item_close(window_t* window) {
    if (window) {
        window_destroy(window);
    }
}

static void menu_item_minimize(window_t* window) {
    if (window) {
        window_hide(window);
    }
}

static void menu_item_about(window_t* window) {
    // Could show an about dialog - for now just a placeholder
    (void)window;
}

// Add default context menu items to a window
void context_menu_add_default_items(window_t* window) {
    if (!window) return;
    
    if (!window->context_menu) {
        window->context_menu = context_menu_create(window);
    }
    
    context_menu_t* menu = window->context_menu;
    if (!menu) return;
    
    // Add default items
    context_menu_add_item(menu, "Minimize", menu_item_minimize);
    context_menu_add_separator(menu);
    context_menu_add_item(menu, "Close", menu_item_close);
}
