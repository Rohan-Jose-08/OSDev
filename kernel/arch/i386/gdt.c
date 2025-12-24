#include <kernel/gdt.h>
#include <stdio.h>
#include <string.h>

typedef struct {
	uint16_t limit_low;
	uint16_t base_low;
	uint8_t base_middle;
	uint8_t access;
	uint8_t granularity;
	uint8_t base_high;
} __attribute__((packed)) gdt_entry_t;

typedef struct {
	uint16_t limit;
	uint32_t base;
} __attribute__((packed)) gdt_ptr_t;

typedef struct {
	uint32_t prev_tss;
	uint32_t esp0;
	uint32_t ss0;
	uint32_t esp1;
	uint32_t ss1;
	uint32_t esp2;
	uint32_t ss2;
	uint32_t cr3;
	uint32_t eip;
	uint32_t eflags;
	uint32_t eax, ecx, edx, ebx;
	uint32_t esp, ebp, esi, edi;
	uint32_t es, cs, ss, ds, fs, gs;
	uint32_t ldt;
	uint16_t trap;
	uint16_t iomap_base;
} __attribute__((packed)) tss_entry_t;

static gdt_entry_t gdt_entries[6];
static gdt_ptr_t gdt_ptr;
static tss_entry_t tss_entry;

static uint8_t tss_stack[4096] __attribute__((aligned(16)));

extern void gdt_flush(uint32_t);

static void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
	gdt_entries[num].base_low = (base & 0xFFFF);
	gdt_entries[num].base_middle = (base >> 16) & 0xFF;
	gdt_entries[num].base_high = (base >> 24) & 0xFF;

	gdt_entries[num].limit_low = (limit & 0xFFFF);
	gdt_entries[num].granularity = (limit >> 16) & 0x0F;
	gdt_entries[num].granularity |= (gran & 0xF0);

	gdt_entries[num].access = access;
}

static void tss_write(int num, uint16_t ss0, uint32_t esp0) {
	uint32_t base = (uint32_t)&tss_entry;
	uint32_t limit = sizeof(tss_entry_t) - 1;

	memset(&tss_entry, 0, sizeof(tss_entry));
	tss_entry.ss0 = ss0;
	tss_entry.esp0 = esp0;
	tss_entry.iomap_base = sizeof(tss_entry);

	// 0x89 = present, ring 0, 32-bit TSS (available)
	gdt_set_gate(num, base, limit, 0x89, 0x00);
}

void gdt_init(void) {
	gdt_ptr.limit = sizeof(gdt_entries) - 1;
	gdt_ptr.base = (uint32_t)&gdt_entries;

	gdt_set_gate(0, 0, 0, 0, 0);
	gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);
	gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);
	gdt_set_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xCF);
	gdt_set_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xCF);
	tss_write(5, GDT_KERNEL_DATA, (uint32_t)(tss_stack + sizeof(tss_stack)));

	gdt_flush((uint32_t)&gdt_ptr);

	__asm__ volatile ("ltr %0" : : "r"((uint16_t)GDT_TSS));

	printf("GDT: initialized (user segments + TSS)\n");
}

void tss_set_kernel_stack(uint32_t stack_top) {
	tss_entry.esp0 = stack_top;
}
