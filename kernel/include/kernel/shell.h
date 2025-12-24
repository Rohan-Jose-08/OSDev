#ifndef _KERNEL_SHELL_H
#define _KERNEL_SHELL_H

#include <stddef.h>

void shell_init(void);
void shell_set_cwd(const char *path);
int shell_history_count(void);
const char *shell_history_entry(int index);
unsigned int shell_command_count(void);
unsigned int shell_tick_count(void);
int shell_timer_start(void);
int shell_timer_stop(unsigned int *elapsed);
int shell_timer_status(void);
int shell_alias_set(const char *name, const char *cmd);
int shell_alias_remove(const char *name);
int shell_alias_count(void);
int shell_alias_get(int index, char *name, size_t name_len, char *cmd, size_t cmd_len);
void shell_halt(void);

#endif
