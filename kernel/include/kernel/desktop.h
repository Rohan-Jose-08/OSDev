#ifndef _KERNEL_DESKTOP_H
#define _KERNEL_DESKTOP_H

#include <stdint.h>
#include <stdbool.h>
#include <kernel/window.h>

// Desktop configuration
// These are base values - actual layout will scale with display resolution
#define DESKTOP_TASKBAR_HEIGHT      24
#define DESKTOP_ICON_SIZE          28
#define DESKTOP_ICON_PADDING       8
#define DESKTOP_MAX_APPS           16
#define DESKTOP_APP_NAME_MAX       32

// Desktop colors
#define DESKTOP_COLOR_BACKGROUND   11  // Light cyan
#define DESKTOP_COLOR_TASKBAR      8   // Dark gray
#define DESKTOP_COLOR_ICON_BG      7   // Light gray
#define DESKTOP_COLOR_ICON_TEXT    0   // Black
#define DESKTOP_COLOR_MENU_BG      15  // White
#define DESKTOP_COLOR_MENU_TEXT    0   // Black
#define DESKTOP_COLOR_MENU_HOVER   9   // Light blue

// Application types (can be extended)
typedef enum {
    APP_TYPE_CALCULATOR,
    APP_TYPE_PAINT,
    APP_TYPE_FILE_MANAGER,
    APP_TYPE_TEXT_EDITOR,
    APP_TYPE_ABOUT
} app_type_t;

// Application definition
typedef struct {
    char name[DESKTOP_APP_NAME_MAX];
    app_type_t type;
    void (*launcher)(void);  // Function to launch the app
    int icon_x, icon_y;      // Icon position on desktop
    bool visible;            // Is icon visible
} desktop_app_t;

// Desktop state
typedef struct {
    bool running;
    bool menu_open;
    int menu_x, menu_y;
    int menu_width, menu_height;
    int menu_hover_item;
    desktop_app_t apps[DESKTOP_MAX_APPS];
    int app_count;
} desktop_state_t;

// Desktop manager functions
void desktop_init(void);
void desktop_run(void);
void desktop_shutdown(void);

// Application registration
void desktop_register_app(const char* name, app_type_t type, void (*launcher)(void));
void desktop_launch_app(app_type_t type);

// Desktop drawing
void desktop_draw_background(void);
void desktop_draw_taskbar(void);
void desktop_draw_icons(void);
void desktop_draw_menu(void);

// Desktop event handling
void desktop_handle_click(int x, int y);
void desktop_handle_mouse_move(int x, int y);

// Utility
bool desktop_point_in_taskbar(int x, int y);
bool desktop_point_in_menu(int x, int y);
int desktop_get_menu_item_at(int x, int y);

#endif
