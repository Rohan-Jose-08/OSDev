#include <stdint.h>
#include <stdbool.h>
#include <kernel/mouse.h>
#include <kernel/tty.h>
#include <kernel/io.h>

#define MOUSE_PORT 0x60
#define MOUSE_STATUS 0x64
#define MOUSE_ABIT 0x02
#define MOUSE_BBIT 0x01

static uint8_t mouse_cycle = 0;
static int8_t mouse_byte[4];
static mouse_state_t current_state = {0};

void mouse_wait_output(void) {
	uint32_t timeout = 100000;
	while (timeout--) {
		if ((inb(MOUSE_STATUS) & MOUSE_ABIT) == 0) {
			io_wait();
			return;
		}
	}
}

void mouse_wait_input(void) {
	uint32_t timeout = 100000;
	while (timeout--) {
		if (inb(MOUSE_STATUS) & MOUSE_BBIT) {
			io_wait();
			return;
		}
	}
}

void mouse_write(uint8_t data) {
	mouse_wait_output();
	outb(MOUSE_STATUS, 0xD4);
	io_wait();
	mouse_wait_output();
	outb(MOUSE_PORT, data);
	io_wait();
}

uint8_t mouse_read(void) {
	mouse_wait_input();
	uint8_t data = inb(MOUSE_PORT);
	io_wait();
	return data;
}

void mouse_init(void) {
	uint8_t status;
	
	// Enable the auxiliary mouse device
	mouse_wait_output();
	outb(MOUSE_STATUS, 0xA8);
	io_wait();
	
	// Enable interrupts
	mouse_wait_output();
	outb(MOUSE_STATUS, 0x20);
	io_wait();
	mouse_wait_input();
	status = (inb(MOUSE_PORT) | 2);
	io_wait();
	mouse_wait_output();
	outb(MOUSE_STATUS, 0x60);
	io_wait();
	mouse_wait_output();
	outb(MOUSE_PORT, status);
	io_wait();
	
	// Use default settings
	mouse_write(0xF6);
	mouse_read(); // Acknowledge
	
	// Enable scroll wheel (IntelliMouse protocol)
	// Send magic sequence: Set sample rate 200, 100, 80
	mouse_write(0xF3); // Set sample rate
	mouse_read();
	mouse_write(200);
	mouse_read();
	
	mouse_write(0xF3);
	mouse_read();
	mouse_write(100);
	mouse_read();
	
	mouse_write(0xF3);
	mouse_read();
	mouse_write(80);
	mouse_read();
	
	// Get device ID - should return 3 for IntelliMouse
	mouse_write(0xF2);
	mouse_read(); // ACK
	mouse_read(); // Device ID
	
	// Enable the mouse
	mouse_write(0xF4);
	mouse_read(); // Acknowledge
	
	mouse_cycle = 0;
}

void mouse_handler(void) {
	uint8_t mouse_in = inb(MOUSE_PORT);
	io_wait();
	
	switch (mouse_cycle) {
		case 0:
			mouse_byte[0] = mouse_in;
			if (mouse_in & 0x08) {
				mouse_cycle++;
			}
			break;
		case 1:
			mouse_byte[1] = mouse_in;
			mouse_cycle++;
			break;
		case 2:
			mouse_byte[2] = mouse_in;
			mouse_cycle++;
			break;
		case 3:
			mouse_byte[3] = mouse_in;
			mouse_cycle = 0;
			
			// Parse mouse data
			current_state.buttons = mouse_byte[0] & 0x07;
			current_state.x = mouse_byte[1];
			current_state.y = mouse_byte[2];
			
			// Handle scroll wheel from 4th byte
			int8_t scroll = (int8_t)mouse_byte[3];
			if (scroll != 0) {
				current_state.scroll = scroll;
				
				// Note: Terminal scrolling removed from IRQ handler to prevent
				// nested I/O operations that cause QEMU mutex issues.
				// Application code should poll mouse_get_state() instead.
			}
			break;
	}
}

mouse_state_t mouse_get_state(void) {
	mouse_state_t state = current_state;
	// Reset deltas and scroll after reading to prevent sliding
	current_state.x = 0;
	current_state.y = 0;
	current_state.scroll = 0;
	return state;
}
