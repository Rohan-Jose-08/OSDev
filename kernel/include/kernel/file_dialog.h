#ifndef _KERNEL_FILE_DIALOG_H
#define _KERNEL_FILE_DIALOG_H

#include <stdbool.h>

// Callback type for file dialog result
// Called with selected filepath (or NULL if cancelled)
typedef void (*file_dialog_callback_t)(const char* filepath, void* user_data);

// Dialog types
typedef enum {
    FILE_DIALOG_OPEN,
    FILE_DIALOG_SAVE
} file_dialog_type_t;

// Show a file open dialog
// Returns immediately, callback is invoked when user selects file or cancels
void file_dialog_show_open(const char* title, 
                           const char* default_path,
                           file_dialog_callback_t callback, 
                           void* user_data);

// Show a file save dialog
void file_dialog_show_save(const char* title, 
                           const char* default_filename,
                           file_dialog_callback_t callback, 
                           void* user_data);

#endif // _KERNEL_FILE_DIALOG_H
