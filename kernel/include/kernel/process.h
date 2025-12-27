#ifndef _KERNEL_PROCESS_H
#define _KERNEL_PROCESS_H

#include <stdbool.h>
#include <stdint.h>

#include <kernel/usermode.h>
#include <kernel/trap_frame.h>

// User processes run in ring 3 and are scheduled independently of kernel tasks.

#define PROCESS_MAX_FDS 16
#define PROCESS_FD_PATH_MAX 128
#define PROCESS_NAME_MAX 32
#define PROCESS_KERNEL_STACK_SIZE 4096
#define PROCESS_PRIORITY_LEVELS 3
#define PROCESS_PRIORITY_DEFAULT 1
#define PROCESS_TIME_QUANTUM 5
#define PROCESS_DEFAULT_UID 1000
#define PROCESS_DEFAULT_GID 1000

typedef struct pipe pipe_t;

typedef enum {
	PROCESS_FD_NONE = 0,
	PROCESS_FD_FILE,
	PROCESS_FD_PIPE_READ,
	PROCESS_FD_PIPE_WRITE,
	PROCESS_FD_TTY
} process_fd_type_t;

typedef struct {
	bool used;
	uint8_t type;
	char path[PROCESS_FD_PATH_MAX];
	uint32_t offset;
	pipe_t *pipe;
} process_fd_t;

typedef enum {
	PROCESS_READY = 0,
	PROCESS_RUNNING,
	PROCESS_BLOCKED,
	PROCESS_ZOMBIE
} process_state_t;

typedef struct process {
	uint32_t pid;
	char name[PROCESS_NAME_MAX];
	uint32_t *page_directory;
	uint32_t entry;
	uint32_t user_stack_top;
	uint32_t heap_base;
	uint32_t heap_end;
	void *kernel_stack_base;
	uint32_t kernel_stack_top;
	uint16_t uid;
	uint16_t gid;
	char cwd[USERMODE_MAX_PATH];
	char args[USERMODE_MAX_ARGS];
	uint32_t args_len;
	int exit_code;
	process_state_t state;
	uint8_t priority;
	uint32_t time_slice;
	uint32_t total_time;
	bool reschedule;
	trap_frame_t frame;
	process_fd_t fds[PROCESS_MAX_FDS];
	struct process *next;
	struct process *all_next;
	bool waiting;
	int32_t wait_pid;
	uint32_t wait_status_ptr;
	bool sleeping;
	uint32_t sleep_until;
	pipe_t *pipe_wait;
	uint8_t pipe_wait_op;
	uint32_t pipe_wait_buf;
	uint32_t pipe_wait_len;
	uint32_t pipe_wait_done;
} process_t;

typedef struct {
	uint32_t pid;
	uint8_t state;
	uint8_t priority;
	uint16_t reserved;
	uint32_t time_slice;
	uint32_t total_time;
	char name[PROCESS_NAME_MAX];
} process_info_t;

void process_init(void);
process_t *process_create(const char *name);
void process_destroy(process_t *proc);
void process_activate(process_t *proc);
void process_activate_user(process_t *proc);
process_t *process_current(void);
void process_set_current(process_t *proc);
uint32_t process_get_count(void);
uint32_t process_list(process_info_t *out, uint32_t max);

void process_set_default_cwd(const char *path);
const char *process_default_cwd(void);
void process_set_cwd(process_t *proc, const char *path);
const char *process_get_cwd(process_t *proc);

void process_set_args(process_t *proc, const char *args, uint32_t len);
uint32_t process_get_args(process_t *proc, char *dst, uint32_t max_len);

int process_spawn(const char *path, const char *args, uint32_t args_len);
bool process_exec(process_t *proc, const char *path, const char *args, uint32_t args_len);
int process_fork(trap_frame_t *frame);
bool process_schedule(trap_frame_t *frame);
bool process_exit_current(trap_frame_t *frame, int code);
bool process_wait(trap_frame_t *frame, int32_t pid, uint32_t status_ptr,
                  int *out_pid, int *out_status);
bool process_sleep_until(trap_frame_t *frame, uint32_t wake_tick);
void process_tick(uint32_t now_ticks);
void process_activate_kernel(void);
process_t *process_next_ready(void);
bool process_scheduler_is_active(void);
void process_scheduler_start(void);
void process_scheduler_stop(void);
bool process_brk(process_t *proc, uint32_t new_end, uint32_t *out_end);
process_t *process_spawn_proc(const char *path, const char *args, uint32_t args_len);
bool process_pipe_read(trap_frame_t *frame, process_t *proc, pipe_t *pipe,
                       uint32_t user_buf, uint32_t len, int *out_read);
bool process_pipe_write(trap_frame_t *frame, process_t *proc, pipe_t *pipe,
                        uint32_t user_buf, uint32_t len, int *out_written);
pipe_t *pipe_create(void);
void pipe_retain_read(pipe_t *pipe);
void pipe_retain_write(pipe_t *pipe);
void pipe_release_read(pipe_t *pipe);
void pipe_release_write(pipe_t *pipe);
void process_fd_close(process_t *proc, int fd);
bool process_fd_set_pipe(process_t *proc, int fd, pipe_t *pipe, bool writable);
bool process_kill_other(uint32_t pid, int exit_code);

#endif
