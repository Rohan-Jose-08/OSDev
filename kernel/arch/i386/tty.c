#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <kernel/tty.h>
#include <kernel/io.h>
#include "vga.h"

// External 8x8 font
extern const uint8_t font_8x8[256][8];

// Display modes
static size_t VGA_WIDTH = 80;
static size_t VGA_HEIGHT = 25;
static uint16_t* const VGA_MEMORY = (uint16_t*) 0xB8000;

// Scrollback buffer (1000 lines)
#define SCROLLBACK_LINES 1000
static uint16_t scrollback_buffer[SCROLLBACK_LINES * 80];
static uint16_t saved_screen[25 * 80];  // Save current screen before scrolling
static size_t scrollback_position = 0;
static size_t scrollback_view_offset = 0;
static bool scrollback_active = false;

static size_t terminal_row;
static size_t terminal_column;
static uint8_t terminal_color;
static uint16_t* terminal_buffer;

// VGA mode setting functions
void terminal_set_mode_80x25(void) {
	VGA_WIDTH = 80;
	VGA_HEIGHT = 25;
	
	// Unlock CRTC registers
	outb(0x3D4, 0x11);
	io_wait();
	uint8_t val = inb(0x3D5);
	io_wait();
	outb(0x3D5, val & 0x7F);
	io_wait();
	
	// Set Maximum Scan Line Register to 15 (16 scan lines per character)
	outb(0x3D4, 0x09);
	io_wait();
	val = inb(0x3D5);
	io_wait();
	outb(0x3D5, (val & 0xE0) | 0x0F); // 16-line font
	io_wait();
	
	// Restore standard cursor shape
	outb(0x3D4, 0x0A); // Cursor Start
	io_wait();
	outb(0x3D5, 0x0E); // Start at line 14
	io_wait();
	
	outb(0x3D4, 0x0B); // Cursor End
	io_wait();
	outb(0x3D5, 0x0F); // End at line 15
	io_wait();
	
	terminal_initialize();
}

void terminal_set_mode_80x50(void) {
	VGA_WIDTH = 80;
	VGA_HEIGHT = 50;
	
	// Load 8x8 font into VGA memory plane 2
	// First, set up sequencer to write to plane 2
	outb(0x3C4, 0x02); 
	io_wait();
	outb(0x3C5, 0x04); // Write to plane 2
	io_wait();
	
	outb(0x3C4, 0x04);
	io_wait();
	outb(0x3C5, 0x07); // Sequential addressing
	io_wait();
	
	// Set up graphics controller
	outb(0x3CE, 0x05);
	io_wait();
	outb(0x3CF, 0x00); // Write mode 0
	io_wait();
	
	outb(0x3CE, 0x06);
	io_wait();
	outb(0x3CF, 0x00); // Map to A0000-BFFFF
	io_wait();
	
	// Copy 8x8 font to VGA memory
	volatile uint8_t* font_mem = (volatile uint8_t*)0xA0000;
	for (int ch = 0; ch < 256; ch++) {
		for (int line = 0; line < 8; line++) {
			font_mem[ch * 32 + line] = font_8x8[ch][line];
		}
		// Clear remaining lines (8-31) for this character
		for (int line = 8; line < 32; line++) {
			font_mem[ch * 32 + line] = 0;
		}
	}
	
	// Restore normal text mode settings
	outb(0x3C4, 0x02);
	io_wait();
	outb(0x3C5, 0x03); // Write to planes 0 and 1
	io_wait();
	
	outb(0x3C4, 0x04);
	io_wait();
	outb(0x3C5, 0x03); // Odd/even mode
	io_wait();
	
	outb(0x3CE, 0x05);
	io_wait();
	outb(0x3CF, 0x10); // Odd/even read mode
	io_wait();
	
	outb(0x3CE, 0x06);
	io_wait();
	outb(0x3CF, 0x0E); // Map to B8000-BFFFF (text mode)
	io_wait();
	
	// Now set up CRTC for 8-line characters
	// Unlock CRTC registers 0-7
	outb(0x3D4, 0x11);
	io_wait();
	uint8_t val = inb(0x3D5);
	io_wait();
	outb(0x3D5, val & 0x7F); // Clear bit 7 to unlock
	io_wait();
	
	// Set Maximum Scan Line Register to 7 (8 scan lines per character)
	outb(0x3D4, 0x09);
	io_wait();
	val = inb(0x3D5);
	io_wait();
	outb(0x3D5, (val & 0xE0) | 0x07); // Keep top 3 bits, set bottom 5 to 7
	io_wait();
	
	// Update cursor shape for smaller font
	outb(0x3D4, 0x0A); // Cursor Start
	io_wait();
	outb(0x3D5, 0x06); // Start at line 6
	io_wait();
	
	outb(0x3D4, 0x0B); // Cursor End
	io_wait();
	outb(0x3D5, 0x07); // End at line 7
	io_wait();
	
	terminal_initialize();
}

void terminal_initialize(void) {
	terminal_row = 0;
	terminal_column = 0;
	terminal_color = vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
	terminal_buffer = VGA_MEMORY;
	scrollback_position = 0;
	scrollback_view_offset = 0;
	scrollback_active = false;
	
	for (size_t y = 0; y < VGA_HEIGHT; y++) {
		for (size_t x = 0; x < VGA_WIDTH; x++) {
			const size_t index = y * VGA_WIDTH + x;
			terminal_buffer[index] = vga_entry(' ', terminal_color);
		}
	}
	
	// Enable hardware cursor
	terminal_enable_cursor();
	terminal_update_cursor(terminal_column, terminal_row);
}

// Enable hardware cursor (blinking underline)
void terminal_enable_cursor(void) {
	// Set cursor shape: underline from scanline 14 to 15
	outb(0x3D4, 0x0A);
	io_wait();
	outb(0x3D5, 0x0E); // Start scanline
	io_wait();
	
	outb(0x3D4, 0x0B);
	io_wait();
	outb(0x3D5, 0x0F); // End scanline
	io_wait();
}

void terminal_disable_cursor(void) {
	outb(0x3D4, 0x0A);
	io_wait();
	outb(0x3D5, 0x20); // Disable cursor
	io_wait();
}

void terminal_update_cursor(size_t x, size_t y) {
	uint16_t pos = y * VGA_WIDTH + x;
	
	// Send low byte
	outb(0x3D4, 0x0F);
	io_wait();
	outb(0x3D5, (uint8_t)(pos & 0xFF));
	io_wait();
	
	// Send high byte
	outb(0x3D4, 0x0E);
	io_wait();
	outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
	io_wait();
}

size_t terminal_get_row(void) {
	return terminal_row;
}

size_t terminal_get_column(void) {
	return terminal_column;
}

void terminal_setcolor(uint8_t color) {
	terminal_color = color;
}

uint8_t terminal_getcolor(void) {
	return terminal_color;
}

void terminal_putentryat(unsigned char c, uint8_t color, size_t x, size_t y) {
	const size_t index = y * VGA_WIDTH + x;
	terminal_buffer[index] = vga_entry(c, color);
}

// Save current screen to scrollback buffer
static void save_to_scrollback(void) {
	size_t buf_index = (scrollback_position % SCROLLBACK_LINES) * VGA_WIDTH;
	for (size_t x = 0; x < VGA_WIDTH; x++) {
		scrollback_buffer[buf_index + x] = terminal_buffer[x];
	}
	scrollback_position++;
}

void terminal_scroll(void) {
	// Save top line to scrollback
	save_to_scrollback();
	
	// Scroll screen up
	for (size_t y = 0; y < VGA_HEIGHT - 1; y++) {
		for (size_t x = 0; x < VGA_WIDTH; x++) {
			const size_t index = y * VGA_WIDTH + x;
			const size_t next_index = (y + 1) * VGA_WIDTH + x;
			terminal_buffer[index] = terminal_buffer[next_index];
		}
	}
	
	// Clear bottom line
	for (size_t x = 0; x < VGA_WIDTH; x++) {
		const size_t index = (VGA_HEIGHT - 1) * VGA_WIDTH + x;
		terminal_buffer[index] = vga_entry(' ', terminal_color);
	}
	
	// Reset scrollback view
	scrollback_active = false;
	scrollback_view_offset = 0;
}

void terminal_scroll_up(void) {
	if (!scrollback_active) {
		// Save current screen before entering scroll mode
		for (size_t i = 0; i < VGA_HEIGHT * VGA_WIDTH; i++) {
			saved_screen[i] = terminal_buffer[i];
		}
		scrollback_active = true;
		scrollback_view_offset = 0;
	}
	
	if (scrollback_view_offset < scrollback_position && scrollback_view_offset < SCROLLBACK_LINES - VGA_HEIGHT) {
		scrollback_view_offset++;
		terminal_redraw_scrollback();
	}
}

void terminal_scroll_down(void) {
	if (scrollback_view_offset > 0) {
		scrollback_view_offset--;
		if (scrollback_view_offset == 0) {
			// Restore saved screen
			scrollback_active = false;
			for (size_t i = 0; i < VGA_HEIGHT * VGA_WIDTH; i++) {
				terminal_buffer[i] = saved_screen[i];
			}
		} else {
			terminal_redraw_scrollback();
		}
	}
}

void terminal_redraw_scrollback(void) {
	if (!scrollback_active || scrollback_position == 0) {
		return;
	}
	
	// Calculate starting position in scrollback
	size_t start_line = 0;
	if (scrollback_position > SCROLLBACK_LINES) {
		start_line = (scrollback_position - SCROLLBACK_LINES + scrollback_view_offset) % SCROLLBACK_LINES;
	} else {
		if (scrollback_view_offset >= scrollback_position) {
			return;
		}
		start_line = scrollback_position - VGA_HEIGHT - scrollback_view_offset;
	}
	
	// Redraw screen from scrollback
	for (size_t y = 0; y < VGA_HEIGHT; y++) {
		size_t buf_line = (start_line + y) % SCROLLBACK_LINES;
		size_t buf_index = buf_line * VGA_WIDTH;
		size_t screen_index = y * VGA_WIDTH;
		
		for (size_t x = 0; x < VGA_WIDTH; x++) {
			terminal_buffer[screen_index + x] = scrollback_buffer[buf_index + x];
		}
	}
	
	// Show scroll indicator in top-right corner
	const char* indicator = "[SCROLLED]";
	size_t indicator_len = 10;
	uint8_t indicator_color = vga_entry_color(VGA_COLOR_BLACK, VGA_COLOR_LIGHT_CYAN);
	for (size_t i = 0; i < indicator_len; i++) {
		terminal_buffer[VGA_WIDTH - indicator_len + i] = vga_entry(indicator[i], indicator_color);
	}
}

void terminal_putchar(char c) {
	// Exit scrollback mode when typing
	if (scrollback_active) {
		scrollback_active = false;
		scrollback_view_offset = 0;
		// Restore saved screen
		for (size_t i = 0; i < VGA_HEIGHT * VGA_WIDTH; i++) {
			terminal_buffer[i] = saved_screen[i];
		}
	}
	
	if (terminal_row == VGA_HEIGHT) {
		terminal_scroll();
		terminal_row = VGA_HEIGHT - 1;
	}
	
	if (c == '\n') {
		terminal_row++;
		terminal_column = 0;
		terminal_update_cursor(terminal_column, terminal_row);
		return;
	}
	
	if (c == '\b') {
		if (terminal_column > 0) {
			terminal_column--;
			terminal_putentryat(' ', terminal_color, terminal_column, terminal_row);
			terminal_update_cursor(terminal_column, terminal_row);
		}
		return;
	}
	
	unsigned char uc = c;
	terminal_putentryat(uc, terminal_color, terminal_column, terminal_row);
	if (++terminal_column == VGA_WIDTH) {
		terminal_column = 0;
		if (++terminal_row == VGA_HEIGHT) {
			terminal_scroll();
			terminal_row = VGA_HEIGHT - 1;
		}
	}
	terminal_update_cursor(terminal_column, terminal_row);
}

void terminal_write(const char* data, size_t size) {
	for (size_t i = 0; i < size; i++)
		terminal_putchar(data[i]);
}

void terminal_writestring(const char* data) {
	terminal_write(data, strlen(data));
}

size_t terminal_get_width(void) {
	return VGA_WIDTH;
}

size_t terminal_get_height(void) {
	return VGA_HEIGHT;
}

bool terminal_is_scrolled(void) {
	return scrollback_active;
}

size_t terminal_get_scroll_offset(void) {
	return scrollback_view_offset;
}
