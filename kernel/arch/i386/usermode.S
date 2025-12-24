.section .text
.global enter_user_mode
.global usermode_run_elf

.extern usermode_return_esp
.extern usermode_saved_ebx
.extern usermode_saved_esi
.extern usermode_saved_edi
.extern usermode_saved_ebp
.extern usermode_run_elf_impl

// Save return ESP for syscall exit path, then jump to C helper.
usermode_run_elf:
	movl %ebx, usermode_saved_ebx
	movl %esi, usermode_saved_esi
	movl %edi, usermode_saved_edi
	movl %ebp, usermode_saved_ebp
	movl %esp, %eax
	movl %eax, usermode_return_esp
	jmp usermode_run_elf_impl

// void enter_user_mode(uint32_t entry, uint32_t user_stack);
enter_user_mode:
	movl 4(%esp), %edx    # entry
	movl 8(%esp), %eax    # user_stack

	movw $0x23, %cx
	movw %cx, %ds
	movw %cx, %es
	movw %cx, %fs
	movw %cx, %gs

	pushl $0x23           # user SS
	pushl %eax            # user ESP
	pushfl
	popl %ecx
	orl $0x200, %ecx      # enable IF
	pushl %ecx
	pushl $0x1B           # user CS
	pushl %edx            # entry point
	iret
