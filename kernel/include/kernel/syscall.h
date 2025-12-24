#ifndef _KERNEL_SYSCALL_H
#define _KERNEL_SYSCALL_H

#include <stdint.h>

// Syscall numbers
#define SYSCALL_WRITE 1
#define SYSCALL_EXIT  2
#define SYSCALL_OPEN  3
#define SYSCALL_READ  4
#define SYSCALL_CLOSE 5
#define SYSCALL_EXEC  6
#define SYSCALL_GETARGS 7
#define SYSCALL_STAT  8
#define SYSCALL_SEEK  9
#define SYSCALL_LISTDIR 10
#define SYSCALL_MKDIR   11
#define SYSCALL_RM      12
#define SYSCALL_TOUCH   13
#define SYSCALL_GETCWD  14
#define SYSCALL_SETCWD  15
#define SYSCALL_CLEAR   16
#define SYSCALL_SETCOLOR 17
#define SYSCALL_WRITEFILE 18
#define SYSCALL_HISTORY_COUNT 19
#define SYSCALL_HISTORY_GET 20
#define SYSCALL_GET_TICKS 21
#define SYSCALL_GET_COMMAND_COUNT 22
#define SYSCALL_GETCHAR 23
#define SYSCALL_SLEEP_MS 24
#define SYSCALL_ALIAS_SET 25
#define SYSCALL_ALIAS_REMOVE 26
#define SYSCALL_ALIAS_COUNT 27
#define SYSCALL_ALIAS_GET 28
#define SYSCALL_TIMER_START 29
#define SYSCALL_TIMER_STOP 30
#define SYSCALL_TIMER_STATUS 31
#define SYSCALL_BEEP 32
#define SYSCALL_HALT 33
#define SYSCALL_GFX_DEMO 34
#define SYSCALL_GFX_ANIM 35
#define SYSCALL_GFX_PAINT 36
#define SYSCALL_GUI_DESKTOP 37
#define SYSCALL_GUI 38
#define SYSCALL_GUI_PAINT 39
#define SYSCALL_GUI_CALC 40
#define SYSCALL_GUI_FILEMGR 41
#define SYSCALL_GFX_SET_MODE 42
#define SYSCALL_GFX_GET_MODE 43
#define SYSCALL_GFX_GET_WIDTH 44
#define SYSCALL_GFX_GET_HEIGHT 45
#define SYSCALL_GFX_CLEAR 46
#define SYSCALL_GFX_PUTPIXEL 47
#define SYSCALL_GFX_DRAW_RECT 48
#define SYSCALL_GFX_FILL_RECT 49
#define SYSCALL_GFX_DRAW_LINE 50
#define SYSCALL_GFX_DRAW_CHAR 51
#define SYSCALL_GFX_PRINT 52
#define SYSCALL_GFX_FLIP 53
#define SYSCALL_GFX_DOUBLEBUFFER_ENABLE 54
#define SYSCALL_GFX_DOUBLEBUFFER_DISABLE 55
#define SYSCALL_MOUSE_GET_STATE 56
#define SYSCALL_KEYBOARD_HAS_INPUT 57
#define SYSCALL_RENAME 58

typedef struct {
	uint32_t gs, fs, es, ds;
	uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
	uint32_t eip, cs, eflags, useresp, userss;
} __attribute__((packed)) syscall_frame_t;

void syscall_dispatch(syscall_frame_t *frame);
void syscall_reset_exit(void);
uint32_t syscall_exit_status(void);

// Used by assembly stubs.
extern volatile uint32_t syscall_exit_requested;
extern volatile uint32_t usermode_return_esp;
extern volatile uint32_t usermode_saved_ebx;
extern volatile uint32_t usermode_saved_esi;
extern volatile uint32_t usermode_saved_edi;
extern volatile uint32_t usermode_saved_ebp;
extern volatile uint32_t usermode_abort_requested;

#endif
