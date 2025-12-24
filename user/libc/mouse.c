#include <mouse.h>
#include "syscall.h"

int mouse_get_state(mouse_state_t *state) {
	if (!state) {
		return -1;
	}
	return syscall3(SYSCALL_MOUSE_GET_STATE, (uint32_t)state, 0, 0);
}
