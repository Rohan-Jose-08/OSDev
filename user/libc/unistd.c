#include <unistd.h>
#include <stdint.h>
#include "syscall.h"

int write(const void *buf, uint32_t len) {
	return syscall3(SYSCALL_WRITE, (uint32_t)buf, len, 0);
}

int read(int fd, void *buf, uint32_t len) {
	return syscall3(SYSCALL_READ, (uint32_t)fd, (uint32_t)buf, len);
}

int open(const char *path) {
	return syscall3(SYSCALL_OPEN, (uint32_t)path, 0, 0);
}

int close(int fd) {
	return syscall3(SYSCALL_CLOSE, (uint32_t)fd, 0, 0);
}

int exec(const char *path, const char *args, uint32_t args_len) {
	return syscall3(SYSCALL_EXEC, (uint32_t)path, (uint32_t)args, args_len);
}

uint32_t getargs(char *buf, uint32_t len) {
	return (uint32_t)syscall3(SYSCALL_GETARGS, (uint32_t)buf, len, 0);
}

int lseek(int fd, int offset, int whence) {
	return syscall3(SYSCALL_SEEK, (uint32_t)fd, (uint32_t)offset, (uint32_t)whence);
}

int listdir(const char *path, void *entries, uint32_t max_entries) {
	return syscall3(SYSCALL_LISTDIR, (uint32_t)path, (uint32_t)entries, max_entries);
}

int mkdir(const char *path) {
	return syscall3(SYSCALL_MKDIR, (uint32_t)path, 0, 0);
}

int rm(const char *path) {
	return syscall3(SYSCALL_RM, (uint32_t)path, 0, 0);
}

int touch(const char *path) {
	return syscall3(SYSCALL_TOUCH, (uint32_t)path, 0, 0);
}

int rename(const char *old_path, const char *new_name) {
	return syscall3(SYSCALL_RENAME, (uint32_t)old_path, (uint32_t)new_name, 0);
}

int getcwd(char *buf, uint32_t len) {
	return syscall3(SYSCALL_GETCWD, (uint32_t)buf, len, 0);
}

int setcwd(const char *path) {
	return syscall3(SYSCALL_SETCWD, (uint32_t)path, 0, 0);
}

int clear(void) {
	return syscall3(SYSCALL_CLEAR, 0, 0, 0);
}

int setcolor(uint32_t fg, uint32_t bg) {
	return syscall3(SYSCALL_SETCOLOR, fg, bg, 0);
}

int writefile(const char *path, const void *buf, uint32_t len) {
	return syscall3(SYSCALL_WRITEFILE, (uint32_t)path, (uint32_t)buf, len);
}

int history_count(void) {
	return syscall3(SYSCALL_HISTORY_COUNT, 0, 0, 0);
}

int history_get(uint32_t index, char *buf, uint32_t len) {
	return syscall3(SYSCALL_HISTORY_GET, index, (uint32_t)buf, len);
}

uint32_t get_ticks(void) {
	return (uint32_t)syscall3(SYSCALL_GET_TICKS, 0, 0, 0);
}

uint32_t get_command_count(void) {
	return (uint32_t)syscall3(SYSCALL_GET_COMMAND_COUNT, 0, 0, 0);
}

int getchar(void) {
	return syscall3(SYSCALL_GETCHAR, 0, 0, 0);
}

int sleep_ms(uint32_t ms) {
	return syscall3(SYSCALL_SLEEP_MS, ms, 0, 0);
}

int alias_set(const char *name, const char *cmd) {
	return syscall3(SYSCALL_ALIAS_SET, (uint32_t)name, (uint32_t)cmd, 0);
}

int alias_remove(const char *name) {
	return syscall3(SYSCALL_ALIAS_REMOVE, (uint32_t)name, 0, 0);
}

int alias_count(void) {
	return syscall3(SYSCALL_ALIAS_COUNT, 0, 0, 0);
}

int alias_get(uint32_t index, char *name, char *cmd) {
	return syscall3(SYSCALL_ALIAS_GET, index, (uint32_t)name, (uint32_t)cmd);
}

int timer_start(void) {
	return syscall3(SYSCALL_TIMER_START, 0, 0, 0);
}

int timer_stop(void) {
	return syscall3(SYSCALL_TIMER_STOP, 0, 0, 0);
}

int timer_status(void) {
	return syscall3(SYSCALL_TIMER_STATUS, 0, 0, 0);
}

int beep(void) {
	return syscall3(SYSCALL_BEEP, 0, 0, 0);
}

int halt(void) {
	return syscall3(SYSCALL_HALT, 0, 0, 0);
}

int gfx_demo(void) {
	return syscall3(SYSCALL_GFX_DEMO, 0, 0, 0);
}

int gfx_anim(void) {
	return syscall3(SYSCALL_GFX_ANIM, 0, 0, 0);
}

int gfx_paint(const char *path) {
	return syscall3(SYSCALL_GFX_PAINT, (uint32_t)path, 0, 0);
}

int gui_desktop(void) {
	return syscall3(SYSCALL_GUI_DESKTOP, 0, 0, 0);
}

int gui_run(void) {
	return syscall3(SYSCALL_GUI, 0, 0, 0);
}

int gui_paint(const char *path) {
	return syscall3(SYSCALL_GUI_PAINT, (uint32_t)path, 0, 0);
}

int gui_calc(void) {
	return syscall3(SYSCALL_GUI_CALC, 0, 0, 0);
}

int gui_filemgr(void) {
	return syscall3(SYSCALL_GUI_FILEMGR, 0, 0, 0);
}

int keyboard_has_input(void) {
	return syscall3(SYSCALL_KEYBOARD_HAS_INPUT, 0, 0, 0);
}
