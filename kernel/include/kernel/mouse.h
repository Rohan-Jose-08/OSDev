#ifndef _KERNEL_MOUSE_H
#define _KERNEL_MOUSE_H

#include <stdint.h>
#include <stdbool.h>

// Mouse button flags
#define MOUSE_LEFT_BUTTON   0x01
#define MOUSE_RIGHT_BUTTON  0x02
#define MOUSE_MIDDLE_BUTTON 0x04

// Mouse state structure
typedef struct {
	int8_t x;
	int8_t y;
	int8_t scroll;
	uint8_t buttons;
	bool has_scroll_wheel;
} mouse_state_t;

void mouse_init(void);
void mouse_handler(void);
mouse_state_t mouse_get_state(void);
void mouse_wait_output(void);
void mouse_wait_input(void);
void mouse_write(uint8_t data);
uint8_t mouse_read(void);

#endif
