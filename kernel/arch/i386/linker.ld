/* The bootloader will look at this image and start execution at the symbol
   designated at the entry point. */
ENTRY(_start)

KERNEL_VIRT_BASE = 0xC0000000;
KERNEL_PHYS_BASE = 0x00100000;

/* Tell where the various sections of the object files will be put in the final
   kernel image. */
SECTIONS
{
	/* Begin putting sections at 1 MiB, a conventional place for kernels to be
	   loaded at by the bootloader. */
	. = KERNEL_PHYS_BASE;

	/* Multiboot header and early boot code live in low memory. */
	.multiboot : {
		*(.multiboot)
	}

	.boot BLOCK(4K) : ALIGN(4K)
	{
		*(.boot)
		*(.boot.text)
		*(.boot.data)
	}

	.boot.bss (NOLOAD) : ALIGN(4K)
	{
		*(.boot.bss)
	}

	. = ALIGN(4K);
	boot_end = .;

	/* High-half kernel image. */
	. = KERNEL_VIRT_BASE + boot_end;

	.trampoline ALIGN(4K) : AT(ADDR(.trampoline) - KERNEL_VIRT_BASE)
	{
		*(.trampoline)
		*(.trampoline.*)
	}

	.text ALIGN(4K) : AT(ADDR(.text) - KERNEL_VIRT_BASE)
	{
		*(.text)
		*(.text.*)
		*(.init)
		*(.fini)
	}

	/* Read-only data. */
	.rodata ALIGN(4K) : AT(ADDR(.rodata) - KERNEL_VIRT_BASE)
	{
		*(.rodata)
		*(.rodata.*)
		*(.eh_frame)
	}

	/* Read-write data (initialized) */
	.data ALIGN(4K) : AT(ADDR(.data) - KERNEL_VIRT_BASE)
	{
		*(.data)
		*(.data.*)
		*(.init_array)
		*(.fini_array)
		*(.ctors)
		*(.dtors)
	}

	/* Read-write data (uninitialized) and stack */
	.bss ALIGN(4K) : AT(ADDR(.bss) - KERNEL_VIRT_BASE)
	{
		*(COMMON)
		*(.bss)
		*(.bss.*)
	}

	/DISCARD/ : {
		*(.comment)
		*(.note*)
	}

	/* The compiler may produce other sections, put them in the proper place in
	   in this file, if you'd like to include them in the final kernel. */
}
