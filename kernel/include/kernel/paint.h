#ifndef _KERNEL_PAINT_H
#define _KERNEL_PAINT_H

// Mouse-based paint program (like MS Paint)
void paint_program(const char *current_dir_path);

// Windowed paint application (with menu bar)
// Pass NULL or empty string to start with blank canvas
void paint_app_windowed(const char* filename);

// Open a BMP file in paint
void paint_open_file(const char* filepath);

#endif
