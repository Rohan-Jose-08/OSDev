.section .text

# CPUID instruction wrapper
# void cpuid(uint32_t code, uint32_t* eax, uint32_t* ebx, uint32_t* ecx, uint32_t* edx)
.global cpuid
.type cpuid, @function
cpuid:
	pushl %ebp
	movl %esp, %ebp
	pushl %ebx
	pushl %edi
	pushl %esi
	
	# Get parameters
	movl 8(%ebp), %eax      # code in eax
	movl 12(%ebp), %edi     # eax pointer
	movl 16(%ebp), %esi     # ebx pointer
	
	cpuid
	
	# Store results
	movl %eax, (%edi)       # Store eax
	movl %ebx, (%esi)       # Store ebx
	
	movl 20(%ebp), %edi     # ecx pointer
	movl 24(%ebp), %esi     # edx pointer
	movl %ecx, (%edi)       # Store ecx
	movl %edx, (%esi)       # Store edx
	
	popl %esi
	popl %edi
	popl %ebx
	popl %ebp
	ret

# Read Time-Stamp Counter
# uint64_t rdtsc(void)
.global rdtsc
.type rdtsc, @function
rdtsc:
	rdtsc
	ret

# Read from Model Specific Register
# uint64_t rdmsr(uint32_t msr)
.global rdmsr
.type rdmsr, @function
rdmsr:
	pushl %ebp
	movl %esp, %ebp
	
	movl 8(%ebp), %ecx
	rdmsr
	
	popl %ebp
	ret

# Write to Model Specific Register
# void wrmsr(uint32_t msr, uint64_t value)
.global wrmsr
.type wrmsr, @function
wrmsr:
	pushl %ebp
	movl %esp, %ebp
	
	movl 8(%ebp), %ecx      # MSR number
	movl 12(%ebp), %eax     # Low 32 bits
	movl 16(%ebp), %edx     # High 32 bits
	wrmsr
	
	popl %ebp
	ret

# Read CR0 register
.global read_cr0
.type read_cr0, @function
read_cr0:
	movl %cr0, %eax
	ret

# Write CR0 register
.global write_cr0
.type write_cr0, @function
write_cr0:
	pushl %ebp
	movl %esp, %ebp
	
	movl 8(%ebp), %eax
	movl %eax, %cr0
	
	popl %ebp
	ret

# Read CR2 register (page fault linear address)
.global read_cr2
.type read_cr2, @function
read_cr2:
	movl %cr2, %eax
	ret

# Read CR3 register (page directory base)
.global read_cr3
.type read_cr3, @function
read_cr3:
	movl %cr3, %eax
	ret

# Write CR3 register
.global write_cr3
.type write_cr3, @function
write_cr3:
	pushl %ebp
	movl %esp, %ebp
	
	movl 8(%ebp), %eax
	movl %eax, %cr3
	
	popl %ebp
	ret

# Read CR4 register
.global read_cr4
.type read_cr4, @function
read_cr4:
	movl %cr4, %eax
	ret

# Write CR4 register
.global write_cr4
.type write_cr4, @function
write_cr4:
	pushl %ebp
	movl %esp, %ebp
	
	movl 8(%ebp), %eax
	movl %eax, %cr4
	
	popl %ebp
	ret

# Invalidate TLB entry
# void invlpg(uint32_t addr)
.global invlpg
.type invlpg, @function
invlpg:
	pushl %ebp
	movl %esp, %ebp
	
	movl 8(%ebp), %eax
	invlpg (%eax)
	
	popl %ebp
	ret

# Halt until next interrupt
.global cpu_hlt
.type cpu_hlt, @function
cpu_hlt:
	hlt
	ret

# Disable interrupts and halt
.global cpu_halt_forever
.type cpu_halt_forever, @function
cpu_halt_forever:
	cli
1:	hlt
	jmp 1b

# Enable interrupts
.global cpu_sti
.type cpu_sti, @function
cpu_sti:
	sti
	ret

# Disable interrupts
.global cpu_cli
.type cpu_cli, @function
cpu_cli:
	cli
	ret

# Read flags register
.global read_eflags
.type read_eflags, @function
read_eflags:
	pushfl
	popl %eax
	ret

# Write flags register
.global write_eflags
.type write_eflags, @function
write_eflags:
	pushl %ebp
	movl %esp, %ebp
	
	movl 8(%ebp), %eax
	pushl %eax
	popfl
	
	popl %ebp
	ret

# Atomic compare and swap
# int atomic_cmpxchg(int* ptr, int old_val, int new_val)
.global atomic_cmpxchg
.type atomic_cmpxchg, @function
atomic_cmpxchg:
	pushl %ebp
	movl %esp, %ebp
	
	movl 8(%ebp), %ecx      # ptr
	movl 12(%ebp), %eax     # old_val
	movl 16(%ebp), %edx     # new_val
	
	lock cmpxchgl %edx, (%ecx)
	
	popl %ebp
	ret

# Atomic increment
# void atomic_inc(int* ptr)
.global atomic_inc
.type atomic_inc, @function
atomic_inc:
	pushl %ebp
	movl %esp, %ebp
	
	movl 8(%ebp), %eax
	lock incl (%eax)
	
	popl %ebp
	ret

# Atomic decrement
# void atomic_dec(int* ptr)
.global atomic_dec
.type atomic_dec, @function
atomic_dec:
	pushl %ebp
	movl %esp, %ebp
	
	movl 8(%ebp), %eax
	lock decl (%eax)
	
	popl %ebp
	ret

# Memory barrier
.global memory_barrier
.type memory_barrier, @function
memory_barrier:
	lock addl $0, (%esp)
	ret
