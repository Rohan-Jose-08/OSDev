#ifndef _USER_UNISTD_H
#define _USER_UNISTD_H

#include <stddef.h>
#include <stdint.h>

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

int write(const void *buf, uint32_t len);
int read(int fd, void *buf, uint32_t len);
int open(const char *path);
int close(int fd);
int exec(const char *path, const char *args, uint32_t args_len);
int spawn(const char *path, const char *args, uint32_t args_len);
int fork(void);
int wait(int *status);
int waitpid(int pid, int *status);
uint32_t getargs(char *buf, uint32_t len);
int lseek(int fd, int offset, int whence);
int listdir(const char *path, void *entries, uint32_t max_entries);
int mkdir(const char *path);
int rm(const char *path);
int touch(const char *path);
int rename(const char *old_path, const char *new_name);
int getcwd(char *buf, uint32_t len);
int setcwd(const char *path);
int clear(void);
int setcolor(uint32_t fg, uint32_t bg);
int writefile(const char *path, const void *buf, uint32_t len);
int history_count(void);
int history_get(uint32_t index, char *buf, uint32_t len);
uint32_t get_ticks(void);
uint32_t get_command_count(void);
int getchar(void);
int sleep_ms(uint32_t ms);
int alias_set(const char *name, const char *cmd);
int alias_remove(const char *name);
int alias_count(void);
int alias_get(uint32_t index, char *name, char *cmd);
int timer_start(void);
int timer_stop(void);
int timer_status(void);
int beep(uint32_t frequency_hz, uint32_t duration_ms);
int halt(void);
int gfx_demo(void);
int gfx_anim(void);
int gfx_paint(const char *path);
int gui_desktop(void);
int gui_run(void);
int gui_paint(const char *path);
int gui_calc(void);
int gui_filemgr(void);
int keyboard_has_input(void);

#endif
