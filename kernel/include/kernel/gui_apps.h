#ifndef _KERNEL_GUI_APPS_H
#define _KERNEL_GUI_APPS_H

// GUI Applications (standalone mode - with their own event loop)
void gui_paint_app(void);
void gui_file_manager_app(void);
void gui_calculator_app(void);

// GUI Applications (windowed mode - creates window only, no event loop)
void gui_paint_create_window(void);
void gui_file_manager_create_window(void);
void gui_calculator_create_window(void);

#endif
