.section .trampoline,"ax"
.align 4096
.global kpti_trampoline_start
kpti_trampoline_start:

.global trampoline_kernel_cr3
trampoline_kernel_cr3:
	.long 0
.global trampoline_return_to_user
trampoline_return_to_user:
	.long 0
.global trampoline_user_cr3
trampoline_user_cr3:
	.long 0

.macro KPTI_STUB name, target
	.global \name
\name:
	push %eax
	movl trampoline_kernel_cr3, %eax
	movl %eax, %cr3
	pop %eax
	jmp \target
.endm

KPTI_STUB trampoline_isr_stub_0, isr_stub_0
KPTI_STUB trampoline_isr_stub_1, isr_stub_1
KPTI_STUB trampoline_isr_stub_2, isr_stub_2
KPTI_STUB trampoline_isr_stub_3, isr_stub_3
KPTI_STUB trampoline_isr_stub_4, isr_stub_4
KPTI_STUB trampoline_isr_stub_5, isr_stub_5
KPTI_STUB trampoline_isr_stub_6, isr_stub_6
KPTI_STUB trampoline_isr_stub_7, isr_stub_7
KPTI_STUB trampoline_isr_stub_8, isr_stub_8
KPTI_STUB trampoline_isr_stub_9, isr_stub_9
KPTI_STUB trampoline_isr_stub_10, isr_stub_10
KPTI_STUB trampoline_isr_stub_11, isr_stub_11
KPTI_STUB trampoline_isr_stub_12, isr_stub_12
KPTI_STUB trampoline_isr_stub_13, isr_stub_13
KPTI_STUB trampoline_isr_stub_14, isr_stub_14
KPTI_STUB trampoline_isr_stub_15, isr_stub_15
KPTI_STUB trampoline_isr_stub_16, isr_stub_16
KPTI_STUB trampoline_isr_stub_17, isr_stub_17
KPTI_STUB trampoline_isr_stub_18, isr_stub_18
KPTI_STUB trampoline_isr_stub_19, isr_stub_19
KPTI_STUB trampoline_isr_stub_20, isr_stub_20
KPTI_STUB trampoline_isr_stub_21, isr_stub_21
KPTI_STUB trampoline_isr_stub_22, isr_stub_22
KPTI_STUB trampoline_isr_stub_23, isr_stub_23
KPTI_STUB trampoline_isr_stub_24, isr_stub_24
KPTI_STUB trampoline_isr_stub_25, isr_stub_25
KPTI_STUB trampoline_isr_stub_26, isr_stub_26
KPTI_STUB trampoline_isr_stub_27, isr_stub_27
KPTI_STUB trampoline_isr_stub_28, isr_stub_28
KPTI_STUB trampoline_isr_stub_29, isr_stub_29
KPTI_STUB trampoline_isr_stub_30, isr_stub_30
KPTI_STUB trampoline_isr_stub_31, isr_stub_31

KPTI_STUB trampoline_irq_stub_0, irq_stub_0
KPTI_STUB trampoline_irq_stub_1, irq_stub_1
KPTI_STUB trampoline_irq_stub_2, irq_stub_2
KPTI_STUB trampoline_irq_stub_3, irq_stub_3
KPTI_STUB trampoline_irq_stub_4, irq_stub_4
KPTI_STUB trampoline_irq_stub_5, irq_stub_5
KPTI_STUB trampoline_irq_stub_6, irq_stub_6
KPTI_STUB trampoline_irq_stub_7, irq_stub_7
KPTI_STUB trampoline_irq_stub_8, irq_stub_8
KPTI_STUB trampoline_irq_stub_9, irq_stub_9
KPTI_STUB trampoline_irq_stub_10, irq_stub_10
KPTI_STUB trampoline_irq_stub_11, irq_stub_11
KPTI_STUB trampoline_irq_stub_12, irq_stub_12
KPTI_STUB trampoline_irq_stub_13, irq_stub_13
KPTI_STUB trampoline_irq_stub_14, irq_stub_14
KPTI_STUB trampoline_irq_stub_15, irq_stub_15

KPTI_STUB trampoline_syscall_stub, syscall_stub

.global trampoline_syscall_return
trampoline_syscall_return:
	movl trampoline_return_to_user, %eax
	test %eax, %eax
	jz 1f
	movl trampoline_user_cr3, %eax
	movl %eax, %cr3
1:
	movl $0, trampoline_return_to_user
	pop %gs
	pop %fs
	pop %es
	pop %ds
	popa
	iret

.global trampoline_irq_return
trampoline_irq_return:
	movl trampoline_return_to_user, %eax
	test %eax, %eax
	jz 1f
	movl trampoline_user_cr3, %eax
	movl %eax, %cr3
1:
	movl $0, trampoline_return_to_user
	pop %gs
	pop %fs
	pop %es
	pop %ds
	popa
	iret

.global trampoline_isr_return
trampoline_isr_return:
	movl trampoline_return_to_user, %eax
	test %eax, %eax
	jz 1f
	movl trampoline_user_cr3, %eax
	movl %eax, %cr3
1:
	movl $0, trampoline_return_to_user
	pop %gs
	pop %fs
	pop %es
	pop %ds
	popa
	add $8, %esp
	iret

.global trampoline_enter_user_mode
trampoline_enter_user_mode:
	movl 4(%esp), %ecx    # user_cr3
	movl 8(%esp), %edi    # kernel_esp (iret frame)

	movw $0x23, %ax
	movw %ax, %ds
	movw %ax, %es
	movw %ax, %fs
	movw %ax, %gs

	movl %edi, %esp
	movl %ecx, %cr3
	iret

.global trampoline_isr_stub_table
trampoline_isr_stub_table:
	.long trampoline_isr_stub_0
	.long trampoline_isr_stub_1
	.long trampoline_isr_stub_2
	.long trampoline_isr_stub_3
	.long trampoline_isr_stub_4
	.long trampoline_isr_stub_5
	.long trampoline_isr_stub_6
	.long trampoline_isr_stub_7
	.long trampoline_isr_stub_8
	.long trampoline_isr_stub_9
	.long trampoline_isr_stub_10
	.long trampoline_isr_stub_11
	.long trampoline_isr_stub_12
	.long trampoline_isr_stub_13
	.long trampoline_isr_stub_14
	.long trampoline_isr_stub_15
	.long trampoline_isr_stub_16
	.long trampoline_isr_stub_17
	.long trampoline_isr_stub_18
	.long trampoline_isr_stub_19
	.long trampoline_isr_stub_20
	.long trampoline_isr_stub_21
	.long trampoline_isr_stub_22
	.long trampoline_isr_stub_23
	.long trampoline_isr_stub_24
	.long trampoline_isr_stub_25
	.long trampoline_isr_stub_26
	.long trampoline_isr_stub_27
	.long trampoline_isr_stub_28
	.long trampoline_isr_stub_29
	.long trampoline_isr_stub_30
	.long trampoline_isr_stub_31

.global trampoline_irq_stub_table
trampoline_irq_stub_table:
	.long trampoline_irq_stub_0
	.long trampoline_irq_stub_1
	.long trampoline_irq_stub_2
	.long trampoline_irq_stub_3
	.long trampoline_irq_stub_4
	.long trampoline_irq_stub_5
	.long trampoline_irq_stub_6
	.long trampoline_irq_stub_7
	.long trampoline_irq_stub_8
	.long trampoline_irq_stub_9
	.long trampoline_irq_stub_10
	.long trampoline_irq_stub_11
	.long trampoline_irq_stub_12
	.long trampoline_irq_stub_13
	.long trampoline_irq_stub_14
	.long trampoline_irq_stub_15

.global kpti_trampoline_end
kpti_trampoline_end:
