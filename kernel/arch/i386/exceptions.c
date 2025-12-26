#include <kernel/cpu.h>
#include <kernel/process.h>
#include <kernel/syscall.h>
#include <kernel/trap_frame.h>
#include <kernel/pagings.h>
#include <kernel/graphics.h>
#include <kernel/panic.h>
#include <stdio.h>

typedef struct {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags, useresp, userss;
} __attribute__((packed)) isr_frame_t;

static void log_user_fault(const isr_frame_t *frame, uint32_t fault_addr) {
	process_t *proc = process_current();
	if (frame->int_no == 14) {
		if (proc) {
			printf("Page fault (user): pid=%u name=%s addr=0x%x eip=0x%x err=0x%x\n",
			       proc->pid, proc->name, fault_addr, frame->eip, frame->err_code);
		} else {
			printf("Page fault (user): addr=0x%x eip=0x%x err=0x%x\n",
			       fault_addr, frame->eip, frame->err_code);
		}
		return;
	}
	if (proc) {
		printf("Exception %u (user): pid=%u name=%s eip=0x%x err=0x%x\n",
		       frame->int_no, proc->pid, proc->name, frame->eip, frame->err_code);
	} else {
		printf("Exception %u (user): eip=0x%x err=0x%x\n",
		       frame->int_no, frame->eip, frame->err_code);
	}
}

static void trap_from_isr(trap_frame_t *out, const isr_frame_t *in) {
	out->gs = in->gs;
	out->fs = in->fs;
	out->es = in->es;
	out->ds = in->ds;
	out->edi = in->edi;
	out->esi = in->esi;
	out->ebp = in->ebp;
	out->esp = in->esp;
	out->ebx = in->ebx;
	out->edx = in->edx;
	out->ecx = in->ecx;
	out->eax = in->eax;
	out->eip = in->eip;
	out->cs = in->cs;
	out->eflags = in->eflags;
	out->useresp = in->useresp;
	out->userss = in->userss;
}

static void isr_from_trap(isr_frame_t *out, const trap_frame_t *in) {
	out->gs = in->gs;
	out->fs = in->fs;
	out->es = in->es;
	out->ds = in->ds;
	out->edi = in->edi;
	out->esi = in->esi;
	out->ebp = in->ebp;
	out->esp = in->esp;
	out->ebx = in->ebx;
	out->edx = in->edx;
	out->ecx = in->ecx;
	out->eax = in->eax;
	out->eip = in->eip;
	out->cs = in->cs;
	out->eflags = in->eflags;
	out->useresp = in->useresp;
	out->userss = in->userss;
}

static void recover_user_graphics_mode(void) {
	if (graphics_get_mode() == MODE_TEXT) {
		return;
	}
	if (graphics_is_double_buffered()) {
		graphics_disable_double_buffer();
	}
	graphics_return_to_text();
}

void isr_handler(isr_frame_t *frame) {
    if (!frame) {
        cpu_halt_forever();
    }

    bool user = (frame->cs & 0x3) == 0x3;
    uint32_t fault_addr = 0;
    if (frame->int_no == 14) {
        fault_addr = read_cr2();
        if (user && (frame->err_code & 0x7) == 0x7) {
            process_t *proc = process_current();
            if (proc && proc->page_directory) {
                if (page_handle_cow(proc->page_directory, fault_addr)) {
                    return;
                }
            }
        }
    }

    if (user) {
        recover_user_graphics_mode();
        log_user_fault(frame, fault_addr);
        trap_frame_t tf;
        trap_from_isr(&tf, frame);
        int code = 128 + (int)frame->int_no;
        if (process_exit_current(&tf, code)) {
            isr_from_trap(frame, &tf);
            return;
        }
        syscall_exit_code = (uint32_t)code;
        usermode_abort_requested = 1;
        return;
    }

    if (frame->int_no == 14) {
        printf("Page fault (kernel): addr=0x%x eip=0x%x err=0x%x\n",
               fault_addr, frame->eip, frame->err_code);
    } else {
        printf("Exception %u at eip=0x%x err=0x%x\n",
               frame->int_no, frame->eip, frame->err_code);
    }

    panic_isr("Kernel exception",
              frame->int_no,
              frame->err_code,
              frame->eip,
              frame->ebp,
              frame->esp,
              frame->eflags,
              fault_addr);
}
