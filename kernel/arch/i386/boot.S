# Declare constants for the multiboot header.
.set ALIGN,    1<<0             # align loaded modules on page boundaries
.set MEMINFO,  1<<1             # provide memory map
.set FLAGS,    ALIGN | MEMINFO  # this is the Multiboot 'flag' field
.set MAGIC,    0x1BADB002       # 'magic number' lets bootloader find the header
.set CHECKSUM, -(MAGIC + FLAGS) # checksum of above, to prove we are multiboot

# Declare a header as in the Multiboot Standard.
.section .multiboot
.align 4
.long MAGIC
.long FLAGS
.long CHECKSUM

# Reserve a stack for the initial thread.
.section .bss
.align 16
stack_bottom:
.skip 16384 # 16 KiB
stack_top:

# Define the GDT
.section .data
gdt_start:
    .quad 0x0000000000000000 # null descriptor
    .quad 0x00CF9A000000FFFF # code segment descriptor
    .quad 0x00CF92000000FFFF # data segment descriptor
gdt_end:

# Define the GDT pointer
gdt_ptr:
    .word gdt_end - gdt_start - 1 # GDT limit
    .long gdt_start # GDT base address


# The kernel entry point.
.section .text
.global _start
.type _start, @function
_start:
	movl $stack_top, %esp
	lgdt gdt_ptr

	# Enable protected mode
    movl %cr0, %eax
    orl $1, %eax
    movl %eax, %cr0

# Set up the segment registers
    movl $0x10, %eax # data segment selector
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss


    # Jump to the protected mode code
    ljmp $0x08, $protected_mode # code segment selector
protected_mode:
	# Call the global constructors.
	call _init
		
	call kernel_main
	# Hang if kernel_main unexpectedly returns.
	cli


1:	hlt
	jmp 1b
.size _start, . - _start
