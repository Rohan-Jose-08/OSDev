; IRQ handlers for hardware interrupts
%macro irq_stub 1
global irq_stub_%+%1
irq_stub_%+%1:
    pusha
    call irq_handler_%+%1
    popa
    iret
%endmacro

extern keyboard_handler
extern mouse_handler
extern timer_handler
extern PIC_sendEOI

; Timer IRQ (IRQ0)
global irq_stub_0
irq_stub_0:
    pusha
    call timer_handler
    push 0
    call PIC_sendEOI
    add esp, 4
    popa
    iret

; Keyboard IRQ (IRQ1)
global irq_stub_1
irq_stub_1:
    pusha
    call keyboard_handler
    push 1
    call PIC_sendEOI
    add esp, 4
    popa
    iret

; Mouse IRQ (IRQ12)
global irq_stub_12
irq_stub_12:
    pusha
    call mouse_handler
    push 12
    call PIC_sendEOI
    add esp, 4
    popa
    iret

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
