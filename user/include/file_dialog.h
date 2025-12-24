#ifndef _USER_FILE_DIALOG_H
#define _USER_FILE_DIALOG_H

#include <stdbool.h>

typedef void (*file_dialog_callback_t)(const char *filepath, void *user_data);

typedef enum {
	FILE_DIALOG_OPEN,
	FILE_DIALOG_SAVE
} file_dialog_type_t;

void file_dialog_show_open(const char *title,
                           const char *default_path,
                           file_dialog_callback_t callback,
                           void *user_data);
void file_dialog_show_save(const char *title,
                           const char *default_filename,
                           file_dialog_callback_t callback,
                           void *user_data);
void file_dialog_poll(void);

#endif
