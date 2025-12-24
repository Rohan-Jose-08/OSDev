.section .text
.global syscall_stub

.extern syscall_dispatch
.extern syscall_exit_requested
.extern usermode_return_esp
.extern usermode_saved_ebx
.extern usermode_saved_esi
.extern usermode_saved_edi
.extern usermode_saved_ebp

syscall_stub:
	pusha
	push %ds
	push %es
	push %fs
	push %gs

	movw $0x10, %ax
	movw %ax, %ds
	movw %ax, %es
	movw %ax, %fs
	movw %ax, %gs

	pushl %esp
	call syscall_dispatch
	add $4, %esp

	movl syscall_exit_requested, %eax
	test %eax, %eax
	jnz syscall_exit

	pop %gs
	pop %fs
	pop %es
	pop %ds
	popa
	iret

syscall_exit:
	movl usermode_saved_ebx, %ebx
	movl usermode_saved_esi, %esi
	movl usermode_saved_edi, %edi
	movl usermode_saved_ebp, %ebp
	movl $1, %eax
	movl usermode_return_esp, %esp
	sti
	ret
