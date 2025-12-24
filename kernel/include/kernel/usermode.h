#ifndef _KERNEL_USERMODE_H
#define _KERNEL_USERMODE_H

#include <stdint.h>
#include <stdbool.h>

#define USER_STACK_SIZE 0x10000
#define USER_STACK_TOP  0x04000000
#define USERMODE_MAX_PATH 128
#define USERMODE_MAX_ARGS 128

bool usermode_run_elf(const char *path);
bool usermode_run_elf_with_args(const char *path, const char *args);
uint32_t usermode_last_exit_code(void);
void usermode_request_exec(const char *path, const char *args, uint32_t args_len);
uint32_t usermode_get_args(char *dst, uint32_t max_len);
void usermode_set_cwd(const char *path);
const char *usermode_get_cwd(void);

#endif
