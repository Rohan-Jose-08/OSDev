; IRQ handlers for hardware interrupts
%macro irq_stub 1
global irq_stub_%+%1
irq_stub_%+%1:
    pusha
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    call irq_handler_%+%1
    push esp
    call kpti_prepare_return_trap
    add esp, 4
    jmp trampoline_irq_return
%endmacro

extern keyboard_handler
extern mouse_handler
extern timer_handler
extern PIC_sendEOI
extern kpti_prepare_return_trap
extern trampoline_irq_return

; Timer IRQ (IRQ0)
global irq_stub_0
irq_stub_0:
    pusha
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    push esp
    call timer_handler
    add esp, 4
    push 0
    call PIC_sendEOI
    add esp, 4
    push esp
    call kpti_prepare_return_trap
    add esp, 4
    jmp trampoline_irq_return

; Keyboard IRQ (IRQ1)
global irq_stub_1
irq_stub_1:
    pusha
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    call keyboard_handler
    push 1
    call PIC_sendEOI
    add esp, 4
    push esp
    call kpti_prepare_return_trap
    add esp, 4
    jmp trampoline_irq_return

; Mouse IRQ (IRQ12)
global irq_stub_12
irq_stub_12:
    pusha
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    call mouse_handler
    push 12
    call PIC_sendEOI
    add esp, 4
    push esp
    call kpti_prepare_return_trap
    add esp, 4
    jmp trampoline_irq_return

; Create additional IRQ stubs for future use
irq_stub 2
irq_stub 3
irq_stub 4
irq_stub 5
irq_stub 6
irq_stub 7
irq_stub 8
irq_stub 9
irq_stub 10
irq_stub 11
irq_stub 13
irq_stub 14
irq_stub 15

; Stub table for IRQs
global irq_stub_table
irq_stub_table:
%assign i 0 
%rep    16
    dd irq_stub_%+i
%assign i i+1 
%endrep

; Default IRQ handlers (for non-keyboard/non-mouse/non-timer IRQs)
%assign i 0
%rep 16
%if i != 0 && i != 1 && i != 12
irq_handler_%+i:
    push i
    call PIC_sendEOI
    add esp, 4
    ret
%endif
%assign i i+1
%endrep
