#include <kernel/kpti.h>
#include <kernel/gdt.h>
#include <kernel/interrupt.h>
#include <kernel/pagings.h>
#include <kernel/process.h>

typedef struct {
	uint32_t gs, fs, es, ds;
	uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
	uint32_t int_no, err_code;
	uint32_t eip, cs, eflags, useresp, userss;
} __attribute__((packed)) kpti_isr_frame_t;

extern uint32_t trampoline_kernel_cr3;
extern uint32_t trampoline_return_to_user;
extern uint32_t trampoline_user_cr3;
extern char kpti_trampoline_start[];
extern char kpti_trampoline_end[];

static bool kpti_map_page(uint32_t *page_dir, uintptr_t addr) {
	uint32_t phys = 0;
	if (!page_translate(page_kernel_directory(), (uint32_t)addr, &phys)) {
		return false;
	}
	if (page_translate(page_dir, (uint32_t)addr, NULL)) {
		return true;
	}
	return page_map(page_dir, (uint32_t)(addr & ~(PAGE_SIZE - 1)),
	                phys & ~(PAGE_SIZE - 1), PAGE_RW);
}

static bool kpti_map_range(uint32_t *page_dir, uintptr_t start, size_t size) {
	if (!page_dir || size == 0) {
		return false;
	}
	uintptr_t cur = start & ~(uintptr_t)(PAGE_SIZE - 1);
	uintptr_t end = (start + size - 1) & ~(uintptr_t)(PAGE_SIZE - 1);
	for (; cur <= end; cur += PAGE_SIZE) {
		if (!kpti_map_page(page_dir, cur)) {
			return false;
		}
	}
	return true;
}

static bool kpti_map_kernel_stack(uint32_t *page_dir, uint32_t esp) {
	if (!page_dir) {
		return false;
	}
	uint32_t stack_page = esp & ~(PAGE_SIZE - 1);
	return kpti_map_page(page_dir, (uintptr_t)stack_page);
}

void kpti_init(void) {
	uint32_t *kernel_dir = page_kernel_directory();
	if (kernel_dir) {
		trampoline_kernel_cr3 = virt_to_phys(kernel_dir);
	}
}

void kpti_map_kernel_pages(uint32_t *page_dir, struct process *proc) {
	if (!page_dir) {
		return;
	}

	uintptr_t tramp_base = (uintptr_t)kpti_trampoline_start;
	size_t tramp_size = (size_t)(kpti_trampoline_end - kpti_trampoline_start);
	kpti_map_range(page_dir, tramp_base, tramp_size);

	uintptr_t idt_base = 0;
	size_t idt_size = 0;
	idt_get_range(&idt_base, &idt_size);
	kpti_map_range(page_dir, idt_base, idt_size);

	uintptr_t gdt_base = 0;
	size_t gdt_size = 0;
	gdt_get_range(&gdt_base, &gdt_size);
	kpti_map_range(page_dir, gdt_base, gdt_size);

	uintptr_t tss_base = 0;
	size_t tss_size = 0;
	tss_get_range(&tss_base, &tss_size);
	kpti_map_range(page_dir, tss_base, tss_size);

	if (proc && proc->kernel_stack_base) {
		kpti_map_range(page_dir, (uintptr_t)proc->kernel_stack_base,
		               PROCESS_KERNEL_STACK_SIZE);
	}
}

void kpti_prepare_return_trap(trap_frame_t *frame) {
	if (!frame) {
		return;
	}
	if ((frame->cs & 0x3) != 0x3) {
		trampoline_return_to_user = 0;
		return;
	}
	process_t *proc = process_current();
	if (!proc || !proc->page_directory) {
		trampoline_return_to_user = 0;
		return;
	}
	if (!kpti_map_kernel_stack(proc->page_directory, frame->esp)) {
		if (!proc->kernel_stack_base ||
		    !kpti_map_range(proc->page_directory,
		                    (uintptr_t)proc->kernel_stack_base,
		                    PROCESS_KERNEL_STACK_SIZE)) {
			trampoline_return_to_user = 0;
			return;
		}
	}
	trampoline_user_cr3 = virt_to_phys(proc->page_directory);
	trampoline_return_to_user = 1;
}

void kpti_prepare_return_isr(void *frame) {
	kpti_isr_frame_t *isr = (kpti_isr_frame_t *)frame;
	if (!isr) {
		return;
	}
	if ((isr->cs & 0x3) != 0x3) {
		trampoline_return_to_user = 0;
		return;
	}
	process_t *proc = process_current();
	if (!proc || !proc->page_directory) {
		trampoline_return_to_user = 0;
		return;
	}
	if (!kpti_map_kernel_stack(proc->page_directory, isr->esp)) {
		if (!proc->kernel_stack_base ||
		    !kpti_map_range(proc->page_directory,
		                    (uintptr_t)proc->kernel_stack_base,
		                    PROCESS_KERNEL_STACK_SIZE)) {
			trampoline_return_to_user = 0;
			return;
		}
	}
	trampoline_user_cr3 = virt_to_phys(proc->page_directory);
	trampoline_return_to_user = 1;
}
