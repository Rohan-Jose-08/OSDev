#ifndef _USER_MOUSE_H
#define _USER_MOUSE_H

#include <stdint.h>
#include <stdbool.h>

#define MOUSE_LEFT_BUTTON   0x01
#define MOUSE_RIGHT_BUTTON  0x02
#define MOUSE_MIDDLE_BUTTON 0x04

typedef struct {
	int8_t x;
	int8_t y;
	int8_t scroll;
	uint8_t buttons;
	bool has_scroll_wheel;
} mouse_state_t;

int mouse_get_state(mouse_state_t *state);

#endif
