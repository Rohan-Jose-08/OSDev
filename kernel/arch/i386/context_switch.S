.section .text
.global context_switch

# void context_switch(registers_t *old_regs, registers_t *new_regs)
# Context switch between tasks
context_switch:
    # Get parameters from stack
    movl 4(%esp), %eax    # old_regs
    movl 8(%esp), %ebx    # new_regs
    
    # Save old context if old_regs is not NULL
    testl %eax, %eax
    jz load_new_context
    
save_old_context:
    # Save general purpose registers
    movl %ebx, 4(%eax)    # Save EBX (we're using it)
    movl %ecx, 8(%eax)    # Save ECX
    movl %edx, 12(%eax)   # Save EDX
    movl %esi, 16(%eax)   # Save ESI
    movl %edi, 20(%eax)   # Save EDI
    
    # Save stack pointers
    movl %esp, 24(%eax)   # Save ESP
    movl %ebp, 28(%eax)   # Save EBP
    
    # Save EIP (return address)
    movl (%esp), %ecx     # Get return address from stack
    movl %ecx, 32(%eax)   # Save EIP
    
    # Save EFLAGS
    pushfl
    popl %ecx
    movl %ecx, 36(%eax)   # Save EFLAGS
    
    # Save segment registers
    movw %cs, 40(%eax)
    movw %ds, 44(%eax)
    movw %es, 48(%eax)
    movw %fs, 52(%eax)
    movw %gs, 56(%eax)
    movw %ss, 60(%eax)
    
    # Now save EAX (we saved EBX earlier)
    movl 4(%esp), %ecx    # Get old_regs from stack again
    movl %eax, 0(%ecx)    # Save EAX's original value

load_new_context:
    # Load new context from new_regs (in EBX)
    movl 8(%esp), %ebx    # Reload new_regs (in case we skipped save)
    
    # Restore segment registers
    movw 44(%ebx), %ax
    movw %ax, %ds
    movw 48(%ebx), %ax
    movw %ax, %es
    movw 52(%ebx), %ax
    movw %ax, %fs
    movw 56(%ebx), %ax
    movw %ax, %gs
    movw 60(%ebx), %ax
    movw %ax, %ss
    
    # Restore EFLAGS
    movl 36(%ebx), %eax
    pushl %eax
    popfl
    
    # Restore stack pointers
    movl 24(%ebx), %esp   # Restore ESP
    movl 28(%ebx), %ebp   # Restore EBP
    
    # Restore general purpose registers
    movl 0(%ebx), %eax
    movl 8(%ebx), %ecx
    movl 12(%ebx), %edx
    movl 16(%ebx), %esi
    movl 20(%ebx), %edi
    
    # Push new EIP onto stack for ret
    movl 32(%ebx), %ecx
    pushl %ecx
    
    # Restore EBX last
    movl 4(%ebx), %ebx
    
    # Return to new task's EIP
    ret
