#ifndef _KERNEL_WINDOW_H
#define _KERNEL_WINDOW_H

#include <stdint.h>
#include <stdbool.h>

// Window flags
#define WINDOW_FLAG_VISIBLE     0x01
#define WINDOW_FLAG_FOCUSED     0x02
#define WINDOW_FLAG_DRAGGING    0x04
#define WINDOW_FLAG_CLOSABLE    0x08
#define WINDOW_FLAG_HAS_BORDER  0x10

// Window colors
#define WINDOW_COLOR_TITLE_BAR_ACTIVE    9   // Light blue
#define WINDOW_COLOR_TITLE_BAR_INACTIVE  8   // Dark gray
#define WINDOW_COLOR_BORDER              7   // Light gray
#define WINDOW_COLOR_BACKGROUND          15  // White
#define WINDOW_COLOR_TEXT                0   // Black
#define WINDOW_COLOR_CLOSE_BUTTON        4   // Red

// Window dimensions
#define WINDOW_TITLE_BAR_HEIGHT  16
#define WINDOW_BORDER_WIDTH      2
#define WINDOW_MIN_WIDTH         80
#define WINDOW_MIN_HEIGHT        60

// Forward declarations
struct window;
struct context_menu;
struct context_menu_item;

// Context menu item structure
typedef struct context_menu_item {
    char label[32];                              // Menu item text
    void (*on_select)(struct window*);           // Callback when selected
    bool enabled;                                // Is this item clickable
    bool separator;                              // Is this a separator line
    struct context_menu_item* next;              // Linked list
} context_menu_item_t;

// Context menu structure
typedef struct context_menu {
    int x, y;                                    // Screen position
    int width, height;                           // Menu dimensions
    bool visible;                                // Is menu shown
    context_menu_item_t* items;                  // Linked list of items
    int item_count;                              // Number of items
    int hover_index;                             // Currently hovered item (-1 = none)
    struct window* owner;                        // Window that owns this menu
} context_menu_t;

// Window structure
typedef struct window {
    int x, y;                    // Position on screen
    int width, height;           // Total window size (including borders)
    int content_width;           // Content area width
    int content_height;          // Content area height
    char title[64];              // Window title
    uint8_t* framebuffer;        // Window's own framebuffer
    uint8_t flags;               // Window flags (visible, focused, etc.)
    int drag_offset_x;           // For dragging
    int drag_offset_y;
    context_menu_t* context_menu; // Right-click context menu
    void* user_data;             // Application-specific data
    bool (*on_priority_click)(struct window*, int, int);  // Priority click handler (e.g., menu bars) - return true if handled
    void (*on_click)(struct window*, int, int);  // Click handler
    void (*on_key)(struct window*, char);         // Key handler
    void (*on_drag)(struct window*, int, int);    // Drag handler (mouse move while button held)
    void (*on_right_click)(struct window*, int, int); // Right-click handler
    void (*on_scroll)(struct window*, int);       // Scroll handler (delta: positive=down, negative=up)
    void (*on_destroy)(struct window*);           // Destroy handler (cleanup callback)
    struct window* next;         // Linked list
    struct window* prev;
} window_t;

// Window manager state
typedef struct {
    window_t* window_list;       // Linked list of windows (front to back)
    window_t* focused_window;    // Currently focused window
    int cursor_x, cursor_y;      // Current cursor position
    bool cursor_visible;
    uint8_t cursor_color;
} window_manager_t;

// Initialize window manager
void window_manager_init(void);

// Window creation and destruction
window_t* window_create(int x, int y, int width, int height, const char* title);
void window_destroy(window_t* window);

// Window operations
void window_move(window_t* window, int x, int y);
void window_resize(window_t* window, int width, int height);
void window_set_title(window_t* window, const char* title);
void window_show(window_t* window);
void window_hide(window_t* window);
void window_focus(window_t* window);
void window_raise(window_t* window);

// Window drawing
void window_draw(window_t* window);
void window_draw_all(void);
void window_clear_content(window_t* window, uint8_t color);

// Window content drawing (for applications)
void window_putpixel(window_t* window, int x, int y, uint8_t color);
void window_draw_rect(window_t* window, int x, int y, int width, int height, uint8_t color);
void window_fill_rect(window_t* window, int x, int y, int width, int height, uint8_t color);
void window_print(window_t* window, int x, int y, const char* text, uint8_t color);

// Mouse interaction
void window_handle_mouse_move(int x, int y);
bool window_handle_mouse_click(int x, int y, uint8_t buttons);
void window_handle_mouse_release(int x, int y, uint8_t buttons);

// Cursor
void window_set_cursor_pos(int x, int y);
void window_get_cursor_pos(int* x, int* y);
void window_show_cursor(void);
void window_hide_cursor(void);
void window_draw_cursor(void);

// Utility
window_t* window_at_position(int x, int y);
bool window_point_in_title_bar(window_t* window, int x, int y);
bool window_point_in_close_button(window_t* window, int x, int y);
window_manager_t* window_get_manager(void);

// Context menu functions
context_menu_t* context_menu_create(window_t* owner);
void context_menu_destroy(context_menu_t* menu);
void context_menu_add_item(context_menu_t* menu, const char* label, void (*on_select)(window_t*));
void context_menu_add_separator(context_menu_t* menu);
void context_menu_show(context_menu_t* menu, int x, int y);
void context_menu_hide(context_menu_t* menu);
void context_menu_draw(context_menu_t* menu);
bool context_menu_handle_click(context_menu_t* menu, int x, int y);
void context_menu_handle_mouse_move(context_menu_t* menu, int x, int y);
void context_menu_add_default_items(window_t* window);

#endif
