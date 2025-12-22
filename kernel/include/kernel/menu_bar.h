#ifndef _KERNEL_MENU_BAR_H
#define _KERNEL_MENU_BAR_H

#include <stdint.h>
#include <stdbool.h>
#include <kernel/window.h>

// Menu bar configuration
#define MENU_BAR_HEIGHT          16
#define MENU_BAR_PADDING         4
#define MENU_BAR_ITEM_PADDING    8
#define MENU_MAX_ITEMS           8
#define MENU_MAX_DROPDOWN_ITEMS  12
#define MENU_ITEM_LABEL_MAX      16
#define MENU_DROPDOWN_MIN_WIDTH  100

// Menu bar colors
#define MENU_BAR_BG_COLOR        7   // Light gray
#define MENU_BAR_TEXT_COLOR      0   // Black
#define MENU_BAR_HOVER_COLOR     9   // Light blue
#define MENU_BAR_ACTIVE_COLOR    1   // Blue
#define MENU_DROPDOWN_BG_COLOR   15  // White
#define MENU_DROPDOWN_TEXT_COLOR 0   // Black
#define MENU_DROPDOWN_HOVER_COLOR 9  // Light blue
#define MENU_DROPDOWN_BORDER_COLOR 8 // Dark gray

// Forward declarations
struct menu_bar;
struct menu_item;
struct dropdown_item;

// Dropdown menu item callback
typedef void (*menu_callback_t)(window_t* window, void* user_data);

// Dropdown menu item
typedef struct dropdown_item {
    char label[MENU_ITEM_LABEL_MAX];
    menu_callback_t callback;
    bool enabled;
    bool separator;
    struct dropdown_item* next;
} dropdown_item_t;

// Top-level menu item (e.g., "File", "Edit")
typedef struct menu_item {
    char label[MENU_ITEM_LABEL_MAX];
    int x, width;                    // Position and width in menu bar
    dropdown_item_t* dropdown_items; // Linked list of dropdown items
    int dropdown_item_count;
    bool open;                       // Is dropdown currently shown
    int dropdown_width;              // Calculated dropdown width
    int dropdown_height;             // Calculated dropdown height
    int hover_index;                 // Currently hovered dropdown item
    struct menu_item* next;
} menu_item_t;

// Menu bar structure
typedef struct menu_bar {
    window_t* window;                // Parent window
    menu_item_t* items;              // Linked list of menu items
    int item_count;
    int active_menu_index;           // Which menu is currently open (-1 = none)
    bool visible;
} menu_bar_t;

// Menu bar functions
menu_bar_t* menu_bar_create(window_t* window);
void menu_bar_destroy(menu_bar_t* menu_bar);
menu_item_t* menu_bar_add_menu(menu_bar_t* menu_bar, const char* label);
void menu_item_add_dropdown(menu_item_t* menu, const char* label, menu_callback_t callback);
void menu_item_add_separator(menu_item_t* menu);
void menu_bar_show(menu_bar_t* menu_bar);
void menu_bar_hide(menu_bar_t* menu_bar);
void menu_bar_draw(menu_bar_t* menu_bar);
bool menu_bar_handle_click(menu_bar_t* menu_bar, int x, int y);
void menu_bar_handle_mouse_move(menu_bar_t* menu_bar, int x, int y);
void menu_bar_close_all_dropdowns(menu_bar_t* menu_bar);
int menu_bar_get_height(void);

#endif
