[bits 32]

%macro isr_no_err_stub 1
global isr_stub_%1
isr_stub_%1:
    push dword 0
    push dword %1
    jmp isr_common_stub
%endmacro

%macro isr_err_stub 1
global isr_stub_%1
isr_stub_%1:
    push dword %1
    jmp isr_common_stub
%endmacro

extern isr_handler
extern usermode_abort_requested
extern usermode_return_esp
extern usermode_saved_ebx
extern usermode_saved_esi
extern usermode_saved_edi
extern usermode_saved_ebp

section .text

isr_common_stub:
    pushad
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
    call isr_handler
    add esp, 4

    mov eax, [usermode_abort_requested]
    test eax, eax
    jz isr_return

    mov dword [usermode_abort_requested], 0
    mov ebx, [usermode_saved_ebx]
    mov esi, [usermode_saved_esi]
    mov edi, [usermode_saved_edi]
    mov ebp, [usermode_saved_ebp]
    mov eax, 0
    mov esp, [usermode_return_esp]
    sti
    ret

isr_return:
    pop gs
    pop fs
    pop es
    pop ds
    popad
    add esp, 8
    iret

isr_no_err_stub 0
isr_no_err_stub 1
isr_no_err_stub 2
isr_no_err_stub 3
isr_no_err_stub 4
isr_no_err_stub 5
isr_no_err_stub 6
isr_no_err_stub 7
isr_err_stub    8
isr_no_err_stub 9
isr_err_stub    10
isr_err_stub    11
isr_err_stub    12
isr_err_stub    13
isr_err_stub    14
isr_no_err_stub 15
isr_no_err_stub 16
isr_err_stub    17
isr_no_err_stub 18
isr_no_err_stub 19
isr_no_err_stub 20
isr_no_err_stub 21
isr_no_err_stub 22
isr_no_err_stub 23
isr_no_err_stub 24
isr_no_err_stub 25
isr_no_err_stub 26
isr_no_err_stub 27
isr_no_err_stub 28
isr_no_err_stub 29
isr_err_stub    30
isr_no_err_stub 31

global isr_stub_table
isr_stub_table:
%assign i 0
%rep    32
    dd isr_stub_%+i
%assign i i+1
%endrep
