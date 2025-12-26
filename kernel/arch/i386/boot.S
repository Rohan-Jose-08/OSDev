# Declare constants for the multiboot header.
.set ALIGN,    1<<0             # align loaded modules on page boundaries
.set MEMINFO,  1<<1             # provide memory map
.set FLAGS,    ALIGN | MEMINFO  # this is the Multiboot 'flag' field
.set MAGIC,    0x1BADB002       # 'magic number' lets bootloader find the header
.set CHECKSUM, -(MAGIC + FLAGS) # checksum of above, to prove we are multiboot

.set KERNEL_VIRT_BASE, 0xC0000000
.set KERNEL_PDE_INDEX, (KERNEL_VIRT_BASE >> 22)
.set PAGE_TABLES, 8
.set PAGE_SIZE, 4096

# Declare a header as in the Multiboot Standard.
.section .multiboot,"a",@progbits
.align 4
.long MAGIC
.long FLAGS
.long CHECKSUM

# Define the GDT
.section .boot.data,"aw",@progbits
gdt_start:
    .quad 0x0000000000000000 # null descriptor
    .quad 0x00CF9A000000FFFF # code segment descriptor
    .quad 0x00CF92000000FFFF # data segment descriptor
gdt_end:

# Define the GDT pointer
gdt_ptr:
    .word gdt_end - gdt_start - 1 # GDT limit
    .long gdt_start # GDT base address

# Bootstrap page directory, tables, and stack.
.section .boot.bss,"aw",@nobits
.align PAGE_SIZE
bootstrap_page_dir:
.skip PAGE_SIZE

.align PAGE_SIZE
bootstrap_page_tables:
.skip PAGE_TABLES * PAGE_SIZE

.align 16
stack_bottom:
.skip 16384 # 16 KiB
stack_top:

# The kernel entry point.
.section .boot,"ax",@progbits
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

    # Clear the bootstrap page directory.
    movl $bootstrap_page_dir, %edi
    xorl %eax, %eax
    movl $1024, %ecx
    rep stosl

    # Build page tables mapping 0..32MB identity and high-half.
    movl $bootstrap_page_tables, %ebx
    xorl %eax, %eax
    xorl %esi, %esi

page_table_loop:
    movl $1024, %ecx
    movl %ebx, %edi
pte_loop:
    movl %eax, %edx
    orl $0x3, %edx
    movl %edx, (%edi)
    addl $0x1000, %eax
    addl $4, %edi
    loop pte_loop

    movl %ebx, %edx
    orl $0x3, %edx

    movl $bootstrap_page_dir, %edi
    movl %esi, %ecx
    shll $2, %ecx
    movl %edx, (%edi,%ecx)

    movl %esi, %ecx
    addl $KERNEL_PDE_INDEX, %ecx
    shll $2, %ecx
    movl %edx, (%edi,%ecx)

    addl $PAGE_SIZE, %ebx
    incl %esi
    cmpl $PAGE_TABLES, %esi
    jl page_table_loop

    movl $bootstrap_page_dir, %eax
    movl %eax, %cr3

    movl %cr0, %eax
    orl $0x80000000, %eax
    movl %eax, %cr0

    lea higher_half_entry, %eax
    addl $KERNEL_VIRT_BASE, %eax
    jmp *%eax

higher_half_entry:
    movl $(stack_top + KERNEL_VIRT_BASE), %esp
    movl $kernel_main, %eax
    call *%eax

    cli

1:	hlt
	jmp 1b
.size _start, . - _start
