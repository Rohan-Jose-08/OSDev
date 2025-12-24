#include <kernel/cpu.h>
#include <kernel/syscall.h>
#include <stdio.h>

typedef struct {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags, useresp, userss;
} __attribute__((packed)) isr_frame_t;

void isr_handler(isr_frame_t *frame) {
    if (!frame) {
        cpu_halt_forever();
    }

    if (frame->int_no == 14) {
        uint32_t fault_addr = read_cr2();
        bool user = (frame->cs & 0x3) == 0x3;

        printf("Page fault (%s): addr=0x%x eip=0x%x err=0x%x\n",
               user ? "user" : "kernel",
               fault_addr, frame->eip, frame->err_code);

        if (user) {
            usermode_abort_requested = 1;
            return;
        }
    } else {
        printf("Exception %u at eip=0x%x err=0x%x\n",
               frame->int_no, frame->eip, frame->err_code);
    }

    cpu_halt_forever();
}
