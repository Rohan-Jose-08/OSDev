#ifndef _KERNEL_ELF_H
#define _KERNEL_ELF_H

#include <stdint.h>
#include <stdbool.h>

#define ELF_MAGIC 0x464C457F
#define ELF_CLASS_32 1
#define ELF_DATA_LSB 1
#define ELF_TYPE_EXEC 2
#define ELF_MACHINE_386 3

#define PT_LOAD 1

// Minimum allowed user load address to avoid kernel heap.
#define ELF_USER_LOAD_MIN 0x02000000

typedef struct {
	uint8_t ident[16];
	uint16_t type;
	uint16_t machine;
	uint32_t version;
	uint32_t entry;
	uint32_t phoff;
	uint32_t shoff;
	uint32_t flags;
	uint16_t ehsize;
	uint16_t phentsize;
	uint16_t phnum;
	uint16_t shentsize;
	uint16_t shnum;
	uint16_t shstrndx;
} __attribute__((packed)) elf32_ehdr_t;

typedef struct {
	uint32_t type;
	uint32_t offset;
	uint32_t vaddr;
	uint32_t paddr;
	uint32_t filesz;
	uint32_t memsz;
	uint32_t flags;
	uint32_t align;
} __attribute__((packed)) elf32_phdr_t;

typedef struct {
	uint32_t entry;
	uint32_t min_vaddr;
	uint32_t max_vaddr;
} elf_image_t;

bool elf_load_file(const char *path, elf_image_t *image);

#endif
