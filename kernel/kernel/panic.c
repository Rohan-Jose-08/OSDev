#include <kernel/panic.h>
#include <kernel/memory.h>
#include <stdint.h>
#include <stddef.h>

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define PANIC_COLOR 0x4F

static volatile uint16_t *const panic_vga = (volatile uint16_t *)KERNEL_PHYS_TO_VIRT(0xB8000);
static size_t panic_row = 0;
static size_t panic_col = 0;

static void panic_clear(void) {
	for (size_t y = 0; y < VGA_HEIGHT; y++) {
		for (size_t x = 0; x < VGA_WIDTH; x++) {
			panic_vga[y * VGA_WIDTH + x] = (uint16_t)' ' | ((uint16_t)PANIC_COLOR << 8);
		}
	}
}

static void panic_putc(char c) {
	if (c == '\n') {
		panic_col = 0;
		if (panic_row + 1 < VGA_HEIGHT) {
			panic_row++;
		}
		return;
	}
	if (panic_col >= VGA_WIDTH) {
		panic_col = 0;
		if (panic_row + 1 < VGA_HEIGHT) {
			panic_row++;
		}
	}
	panic_vga[panic_row * VGA_WIDTH + panic_col] = (uint16_t)c | ((uint16_t)PANIC_COLOR << 8);
	panic_col++;
}

static void panic_write(const char *s) {
	if (!s) {
		return;
	}
	while (*s) {
		panic_putc(*s++);
	}
}

static void panic_write_hex(uint32_t value) {
	static const char hex[] = "0123456789ABCDEF";
	panic_write("0x");
	for (int i = 7; i >= 0; i--) {
		uint8_t nibble = (value >> (i * 4)) & 0xF;
		panic_putc(hex[nibble]);
	}
}

static void panic_newline(void) {
	panic_putc('\n');
}

static void panic_backtrace(uint32_t ebp) {
	const uint32_t limit = KERNEL_VIRT_BASE + 0x10000000;
	panic_write("Backtrace:\n");
	for (int frame = 0; frame < 16; frame++) {
		if (ebp < KERNEL_VIRT_BASE || ebp + 8 > limit || (ebp & 0x3) != 0) {
			panic_write("  <invalid frame>\n");
			return;
		}
		uint32_t *fp = (uint32_t *)ebp;
		uint32_t next = fp[0];
		uint32_t ret = fp[1];
		panic_write("  ");
		panic_write_hex(ret);
		panic_newline();
		if (next <= ebp) {
			return;
		}
		ebp = next;
	}
}

__attribute__((noreturn))
void panic(const char *msg) {
	__asm__ volatile ("cli");
	panic_clear();
	panic_row = 0;
	panic_col = 0;
	panic_write("KERNEL PANIC\n");
	if (msg && *msg) {
		panic_write(msg);
		panic_newline();
	}
	for (;;) {
		__asm__ volatile ("hlt");
	}
}

__attribute__((noreturn))
void panic_isr(const char *msg,
               uint32_t int_no,
               uint32_t err_code,
               uint32_t eip,
               uint32_t ebp,
               uint32_t esp,
               uint32_t eflags,
               uint32_t cr2) {
	__asm__ volatile ("cli");
	panic_clear();
	panic_row = 0;
	panic_col = 0;

	panic_write("KERNEL PANIC\n");
	if (msg && *msg) {
		panic_write(msg);
		panic_newline();
	}

	panic_write("INT: ");
	panic_write_hex(int_no);
	panic_write("  ERR: ");
	panic_write_hex(err_code);
	panic_newline();

	panic_write("EIP: ");
	panic_write_hex(eip);
	panic_write("  EBP: ");
	panic_write_hex(ebp);
	panic_newline();

	panic_write("ESP: ");
	panic_write_hex(esp);
	panic_write("  EFLAGS: ");
	panic_write_hex(eflags);
	panic_newline();

	panic_write("CR2: ");
	panic_write_hex(cr2);
	panic_newline();

	panic_backtrace(ebp);

	for (;;) {
		__asm__ volatile ("hlt");
	}
}
