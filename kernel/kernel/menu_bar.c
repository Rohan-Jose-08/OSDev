#include <kernel/menu_bar.h>
#include <kernel/graphics.h>
#include <kernel/kmalloc.h>
#include <string.h>
#include <stdio.h>

// External font
extern const uint8_t font_8x8[256][8];

// Helper: Draw text in window content area
static void draw_text(window_t* window, int x, int y, const char* text, uint8_t color) {
    if (!window || !text) return;
    
    int cx = x;
    while (*text) {
        const uint8_t* glyph = font_8x8[(uint8_t)*text];
        for (int j = 0; j < 8; j++) {
            for (int i = 0; i < 8; i++) {
                if (glyph[j] & (1 << (7 - i))) {
                    window_putpixel(window, cx + i, y + j, color);
                }
            }
        }
        cx += 8;
        text++;
    }
}

// Create a menu bar for a window
menu_bar_t* menu_bar_create(window_t* window) {
    if (!window) return NULL;
    
    menu_bar_t* menu_bar = (menu_bar_t*)kmalloc(sizeof(menu_bar_t));
    if (!menu_bar) return NULL;
    
    menu_bar->window = window;
    menu_bar->items = NULL;
    menu_bar->item_count = 0;
    menu_bar->active_menu_index = -1;
    menu_bar->visible = true;
    
    return menu_bar;
}

// Destroy menu bar and all items
void menu_bar_destroy(menu_bar_t* menu_bar) {
    if (!menu_bar) return;
    
    menu_item_t* menu = menu_bar->items;
    while (menu) {
        menu_item_t* next_menu = menu->next;
        
        // Free dropdown items
        dropdown_item_t* item = menu->dropdown_items;
        while (item) {
            dropdown_item_t* next_item = item->next;
            kfree(item);
            item = next_item;
        }
        
        kfree(menu);
        menu = next_menu;
    }
    
    kfree(menu_bar);
}

// Add a top-level menu (e.g., "File", "Edit")
menu_item_t* menu_bar_add_menu(menu_bar_t* menu_bar, const char* label) {
    if (!menu_bar || !label) return NULL;
    
    menu_item_t* menu = (menu_item_t*)kmalloc(sizeof(menu_item_t));
    if (!menu) return NULL;
    
    strncpy(menu->label, label, sizeof(menu->label) - 1);
    menu->label[sizeof(menu->label) - 1] = '\0';
    menu->dropdown_items = NULL;
    menu->dropdown_item_count = 0;
    menu->open = false;
    menu->dropdown_width = MENU_DROPDOWN_MIN_WIDTH;
    menu->dropdown_height = 0;
    menu->hover_index = -1;
    menu->next = NULL;
    
    // Calculate position
    int x = MENU_BAR_PADDING;
    menu_item_t* current = menu_bar->items;
    while (current) {
        x = current->x + current->width + MENU_BAR_ITEM_PADDING;
        if (!current->next) break;
        current = current->next;
    }
    
    menu->x = x;
    menu->width = strlen(label) * 8 + MENU_BAR_ITEM_PADDING * 2;
    
    // Add to list
    if (!menu_bar->items) {
        menu_bar->items = menu;
    } else {
        current->next = menu;
    }
    
    menu_bar->item_count++;
    return menu;
}

// Add a dropdown item to a menu
void menu_item_add_dropdown(menu_item_t* menu, const char* label, menu_callback_t callback) {
    if (!menu || !label) return;
    
    dropdown_item_t* item = (dropdown_item_t*)kmalloc(sizeof(dropdown_item_t));
    if (!item) return;
    
    strncpy(item->label, label, sizeof(item->label) - 1);
    item->label[sizeof(item->label) - 1] = '\0';
    item->callback = callback;
    item->enabled = true;
    item->separator = false;
    item->next = NULL;
    
    // Add to end of list
    if (!menu->dropdown_items) {
        menu->dropdown_items = item;
    } else {
        dropdown_item_t* last = menu->dropdown_items;
        while (last->next) last = last->next;
        last->next = item;
    }
    
    menu->dropdown_item_count++;
    menu->dropdown_height = menu->dropdown_item_count * 16 + 4;
    
    // Update width based on label length
    int label_width = strlen(label) * 8 + 16;
    if (label_width > menu->dropdown_width) {
        menu->dropdown_width = label_width;
    }
}

// Add a separator line
void menu_item_add_separator(menu_item_t* menu) {
    if (!menu) return;
    
    dropdown_item_t* item = (dropdown_item_t*)kmalloc(sizeof(dropdown_item_t));
    if (!item) return;
    
    item->label[0] = '\0';
    item->callback = NULL;
    item->enabled = false;
    item->separator = true;
    item->next = NULL;
    
    // Add to end of list
    if (!menu->dropdown_items) {
        menu->dropdown_items = item;
    } else {
        dropdown_item_t* last = menu->dropdown_items;
        while (last->next) last = last->next;
        last->next = item;
    }
    
    menu->dropdown_item_count++;
    menu->dropdown_height = menu->dropdown_item_count * 16 + 4;
}

// Show menu bar
void menu_bar_show(menu_bar_t* menu_bar) {
    if (!menu_bar) return;
    menu_bar->visible = true;
}

// Hide menu bar
void menu_bar_hide(menu_bar_t* menu_bar) {
    if (!menu_bar) return;
    menu_bar->visible = false;
}

// Close all dropdown menus
void menu_bar_close_all_dropdowns(menu_bar_t* menu_bar) {
    if (!menu_bar) return;
    
    menu_item_t* menu = menu_bar->items;
    while (menu) {
        menu->open = false;
        menu->hover_index = -1;
        menu = menu->next;
    }
    menu_bar->active_menu_index = -1;
}

// Draw the menu bar
void menu_bar_draw(menu_bar_t* menu_bar) {
    if (!menu_bar || !menu_bar->visible || !menu_bar->window) return;
    
    window_t* window = menu_bar->window;
    
    // Draw menu bar background
    window_fill_rect(window, 0, 0, window->content_width, MENU_BAR_HEIGHT, MENU_BAR_BG_COLOR);
    
    // Draw each menu item
    int menu_index = 0;
    menu_item_t* menu = menu_bar->items;
    while (menu) {
        // Highlight if this menu is open
        if (menu->open) {
            window_fill_rect(window, menu->x, 0, menu->width, MENU_BAR_HEIGHT, MENU_BAR_ACTIVE_COLOR);
            draw_text(window, menu->x + MENU_BAR_ITEM_PADDING / 2, 
                     (MENU_BAR_HEIGHT - 8) / 2, menu->label, COLOR_WHITE);
        } else {
            draw_text(window, menu->x + MENU_BAR_ITEM_PADDING / 2, 
                     (MENU_BAR_HEIGHT - 8) / 2, menu->label, MENU_BAR_TEXT_COLOR);
        }
        
        // Draw dropdown if open
        if (menu->open) {
            // Calculate dropdown position (in screen coordinates)
            int dropdown_x = menu->x;
            int dropdown_y = MENU_BAR_HEIGHT;
            
            // Draw dropdown background
            window_fill_rect(window, dropdown_x, dropdown_y, 
                           menu->dropdown_width, menu->dropdown_height, 
                           MENU_DROPDOWN_BG_COLOR);
            
            // Draw dropdown border
            window_draw_rect(window, dropdown_x, dropdown_y, 
                           menu->dropdown_width, menu->dropdown_height, 
                           MENU_DROPDOWN_BORDER_COLOR);
            
            // Draw dropdown items
            int item_y = dropdown_y + 2;
            int item_index = 0;
            dropdown_item_t* item = menu->dropdown_items;
            
            while (item) {
                if (item->separator) {
                    // Draw separator line
                    for (int x = dropdown_x + 4; x < dropdown_x + menu->dropdown_width - 4; x++) {
                        window_putpixel(window, x, item_y + 7, MENU_DROPDOWN_BORDER_COLOR);
                    }
                } else {
                    // Highlight hovered item
                    if (item_index == menu->hover_index && item->enabled) {
                        window_fill_rect(window, dropdown_x + 1, item_y, 
                                       menu->dropdown_width - 2, 14, 
                                       MENU_DROPDOWN_HOVER_COLOR);
                    }
                    
                    // Draw item text
                    uint8_t text_color = item->enabled ? MENU_DROPDOWN_TEXT_COLOR : 8;
                    draw_text(window, dropdown_x + 8, item_y + 3, item->label, text_color);
                }
                
                item_y += 16;
                item_index++;
                item = item->next;
            }
        }
        
        menu_index++;
        menu = menu->next;
    }
}

// Handle click in menu bar
bool menu_bar_handle_click(menu_bar_t* menu_bar, int x, int y) {
    if (!menu_bar || !menu_bar->visible) return false;
    
    // Check if click is in menu bar area (top MENU_BAR_HEIGHT pixels) - use slightly larger hitbox
    if (y >= -1 && y <= MENU_BAR_HEIGHT) {
        // Click is in menu bar - find which menu
        menu_item_t* menu = menu_bar->items;
        int menu_index = 0;
        
        while (menu) {
            // Use slightly expanded hitbox for easier clicking
            int hit_x1 = menu->x - 2;
            int hit_x2 = menu->x + menu->width + 2;
            
            if (x >= hit_x1 && x < hit_x2) {
                // Toggle this menu: if it's already open, close it; otherwise open it
                if (menu->open) {
                    // Menu is currently open, close ALL menus to ensure clean state
                    menu_bar_close_all_dropdowns(menu_bar);
                    // Double-check this specific menu is closed
                    menu->open = false;
                    menu->hover_index = -1;
                    menu_bar->active_menu_index = -1;
                } else {
                    // Menu is currently closed, open it (and close any others first)
                    menu_bar_close_all_dropdowns(menu_bar);
                    menu->open = true;
                    menu_bar->active_menu_index = menu_index;
                }
                
                return true;
            }
            menu_index++;
            menu = menu->next;
        }
        return true; // Click was in menu bar but not on a menu item
    }
    
    // Check if any dropdown is open
    bool any_dropdown_open = false;
    menu_item_t* check_menu = menu_bar->items;
    while (check_menu) {
        if (check_menu->open) {
            any_dropdown_open = true;
            break;
        }
        check_menu = check_menu->next;
    }
    
    // Check if click is in an open dropdown
    if (y >= MENU_BAR_HEIGHT) {
        menu_item_t* menu = menu_bar->items;
        int menu_index = 0;
        
        while (menu) {
            if (menu->open) {
                int dropdown_x = menu->x;
                int dropdown_y = MENU_BAR_HEIGHT;
                
                if (x >= dropdown_x && x < dropdown_x + menu->dropdown_width &&
                    y >= dropdown_y && y < dropdown_y + menu->dropdown_height) {
                    
                    // Find which item was clicked
                    int item_index = (y - dropdown_y - 2) / 16;
                    
                    if (item_index >= 0 && item_index < menu->dropdown_item_count) {
                        dropdown_item_t* item = menu->dropdown_items;
                        for (int i = 0; i < item_index && item; i++) {
                            item = item->next;
                        }
                        
                        if (item && !item->separator && item->enabled && item->callback) {
                            item->callback(menu_bar->window, menu_bar->window->user_data);
                            menu_bar_close_all_dropdowns(menu_bar);
                            return true;
                        }
                    }
                    return true; // Click was on dropdown but not on valid item
                }
            }
            menu_index++;
            menu = menu->next;
        }
        
        // Click outside menu bar and dropdowns
        // If a dropdown was open, we consume the click (return true) to close it
        // If no dropdown was open, return false to allow normal window handling
        if (any_dropdown_open) {
            menu_bar_close_all_dropdowns(menu_bar);
            return true;  // Consumed the click to close dropdown
        }
        return false;  // No dropdown was open, allow normal handling
    }
    
    return false;
}

// Handle mouse move in menu bar
void menu_bar_handle_mouse_move(menu_bar_t* menu_bar, int x, int y) {
    if (!menu_bar || !menu_bar->visible) return;
    
    // Update hover state for open dropdown
    menu_item_t* menu = menu_bar->items;
    
    while (menu) {
        if (menu->open) {
            int dropdown_x = menu->x;
            int dropdown_y = MENU_BAR_HEIGHT;
            
            if (x >= dropdown_x && x < dropdown_x + menu->dropdown_width &&
                y >= dropdown_y && y < dropdown_y + menu->dropdown_height) {
                
                int item_index = (y - dropdown_y - 2) / 16;
                if (item_index >= 0 && item_index < menu->dropdown_item_count) {
                    menu->hover_index = item_index;
                } else {
                    menu->hover_index = -1;
                }
            } else {
                menu->hover_index = -1;
            }
        }
        menu = menu->next;
    }
}

// Get menu bar height
int menu_bar_get_height(void) {
    return MENU_BAR_HEIGHT;
}
