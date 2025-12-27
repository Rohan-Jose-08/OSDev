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

int spawn(const char *path, const char *args, uint32_t args_len) {
	return syscall3(SYSCALL_SPAWN, (uint32_t)path, (uint32_t)args, args_len);
}

int fork(void) {
	return syscall3(SYSCALL_FORK, 0, 0, 0);
}

int wait(int *status) {
	return syscall3(SYSCALL_WAIT, (uint32_t)-1, (uint32_t)status, 0);
}

int waitpid(int pid, int *status) {
	return syscall3(SYSCALL_WAIT, (uint32_t)pid, (uint32_t)status, 0);
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

int beep(uint32_t frequency_hz, uint32_t duration_ms) {
	return syscall3(SYSCALL_BEEP, frequency_hz, duration_ms, 0);
}

void speaker_start(uint32_t frequency_hz) {
	syscall3(SYSCALL_SPEAKER_START, frequency_hz, 0, 0);
}

void speaker_stop(void) {
	syscall3(SYSCALL_SPEAKER_STOP, 0, 0, 0);
}

int audio_write(const void *buf, uint32_t bytes) {
	return syscall3(SYSCALL_AUDIO_WRITE, (uint32_t)buf, bytes, 0);
}

int audio_set_volume(uint8_t master, uint8_t pcm) {
	return syscall3(SYSCALL_AUDIO_SET_VOLUME, master, pcm, 0);
}

int audio_get_volume(uint8_t *master, uint8_t *pcm) {
	uint32_t res = (uint32_t)syscall3(SYSCALL_AUDIO_GET_VOLUME, 0, 0, 0);
	if (res == (uint32_t)-1) {
		return -1;
	}
	if (master) {
		*master = (uint8_t)(res & 0xFF);
	}
	if (pcm) {
		*pcm = (uint8_t)((res >> 8) & 0xFF);
	}
	return 0;
}

int audio_is_ready(void) {
	return (int)syscall3(SYSCALL_AUDIO_STATUS, 0, 0, 0);
}

uint32_t fs_get_free_blocks(void) {
	return (uint32_t)syscall3(SYSCALL_FS_FREE_BLOCKS, 0, 0, 0);
}

int heap_get_stats(user_heap_stats_t *stats) {
	if (!stats) {
		return -1;
	}
	return syscall3(SYSCALL_HEAP_STATS, (uint32_t)stats, sizeof(*stats), 0);
}

uint32_t process_count(void) {
	return (uint32_t)syscall3(SYSCALL_PROCESS_COUNT, 0, 0, 0);
}

int process_list(user_process_info_t *out, uint32_t max_entries) {
	if (!out || max_entries == 0) {
		return -1;
	}
	return syscall3(SYSCALL_PROCESS_LIST, (uint32_t)out, max_entries, 0);
}

int install_embedded(const char *path) {
	if (!path) {
		return -1;
	}
	return syscall3(SYSCALL_INSTALL_EMBEDDED, (uint32_t)path, 0, 0);
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

int keyboard_set_repeat(uint8_t delay, uint8_t rate) {
	return syscall3(SYSCALL_KEY_REPEAT, (uint32_t)delay, (uint32_t)rate, 0);
}

static uint32_t user_break = 0;

static uint32_t fetch_break(void) {
	if (user_break == 0) {
		uint32_t res = (uint32_t)syscall3(SYSCALL_BRK, 0, 0, 0);
		if (res == (uint32_t)-1) {
			return (uint32_t)-1;
		}
		user_break = res;
	}
	return user_break;
}

void *sbrk(intptr_t increment) {
	uint32_t cur = fetch_break();
	if (cur == (uint32_t)-1) {
		return (void *)-1;
	}
	if (increment == 0) {
		return (void *)cur;
	}

	int32_t inc = (int32_t)increment;
	uint32_t new_end = cur + inc;
	if ((inc > 0 && new_end < cur) || (inc < 0 && new_end > cur)) {
		return (void *)-1;
	}

	uint32_t res = (uint32_t)syscall3(SYSCALL_BRK, new_end, 0, 0);
	if (res == (uint32_t)-1) {
		return (void *)-1;
	}
	user_break = res;
	return (void *)cur;
}

int brk(void *addr) {
	uint32_t res = (uint32_t)syscall3(SYSCALL_BRK, (uint32_t)addr, 0, 0);
	if (res == (uint32_t)-1) {
		return -1;
	}
	user_break = res;
	return 0;
}

int pipe(int fds[2]) {
	if (!fds) {
		return -1;
	}
	return syscall3(SYSCALL_PIPE, (uint32_t)fds, 0, 0);
}

int dup2(int oldfd, int newfd) {
	return syscall3(SYSCALL_DUP2, (uint32_t)oldfd, (uint32_t)newfd, 0);
}

int kill(int pid, int sig) {
	return syscall3(SYSCALL_KILL, (uint32_t)pid, (uint32_t)sig, 0);
}
