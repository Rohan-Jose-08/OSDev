#include <kernel/elf.h>
#include <kernel/fs.h>
#include <kernel/kmalloc.h>
#include <stdio.h>
#include <string.h>

static bool elf_check_header(const elf32_ehdr_t *hdr) {
	if (hdr->ident[0] != 0x7F || hdr->ident[1] != 'E' ||
	    hdr->ident[2] != 'L' || hdr->ident[3] != 'F') {
		return false;
	}
	if (hdr->ident[4] != ELF_CLASS_32 || hdr->ident[5] != ELF_DATA_LSB) {
		return false;
	}
	if (hdr->type != ELF_TYPE_EXEC || hdr->machine != ELF_MACHINE_386) {
		return false;
	}
	return true;
}

bool elf_load_file(const char *path, elf_image_t *image) {
	fs_inode_t inode;
	if (!fs_stat(path, &inode)) {
		printf("ELF: file not found: %s\n", path);
		return false;
	}

	if (inode.size < sizeof(elf32_ehdr_t)) {
		printf("ELF: file too small: %s\n", path);
		return false;
	}

	uint8_t *file = kmalloc(inode.size);
	if (!file) {
		printf("ELF: out of memory\n");
		return false;
	}

	int read_bytes = fs_read_file(path, file, inode.size, 0);
	if (read_bytes != (int)inode.size) {
		printf("ELF: read failed (%d/%u)\n", read_bytes, inode.size);
		kfree(file);
		return false;
	}

	elf32_ehdr_t *hdr = (elf32_ehdr_t *)file;
	if (!elf_check_header(hdr)) {
		printf("ELF: invalid header\n");
		kfree(file);
		return false;
	}

	if (hdr->phentsize != sizeof(elf32_phdr_t)) {
		printf("ELF: unexpected program header size\n");
		kfree(file);
		return false;
	}

	uint32_t ph_end = hdr->phoff + hdr->phnum * sizeof(elf32_phdr_t);
	if (ph_end > inode.size) {
		printf("ELF: program headers out of range\n");
		kfree(file);
		return false;
	}

	uint32_t min_vaddr = 0xFFFFFFFF;
	uint32_t max_vaddr = 0;

	elf32_phdr_t *phdrs = (elf32_phdr_t *)(file + hdr->phoff);
	for (uint16_t i = 0; i < hdr->phnum; i++) {
		elf32_phdr_t *ph = &phdrs[i];
		if (ph->type != PT_LOAD) {
			continue;
		}

		if (ph->offset + ph->filesz > inode.size) {
			printf("ELF: segment out of range\n");
			kfree(file);
			return false;
		}

		if (ph->vaddr < ELF_USER_LOAD_MIN) {
			printf("ELF: segment below user range (0x%x)\n", ph->vaddr);
			kfree(file);
			return false;
		}

		memcpy((void *)ph->vaddr, file + ph->offset, ph->filesz);
		if (ph->memsz > ph->filesz) {
			memset((void *)(ph->vaddr + ph->filesz), 0, ph->memsz - ph->filesz);
		}

		if (ph->vaddr < min_vaddr) {
			min_vaddr = ph->vaddr;
		}
		if (ph->vaddr + ph->memsz > max_vaddr) {
			max_vaddr = ph->vaddr + ph->memsz;
		}
	}

	if (min_vaddr == 0xFFFFFFFF) {
		printf("ELF: no loadable segments\n");
		kfree(file);
		return false;
	}

	image->entry = hdr->entry;
	image->min_vaddr = min_vaddr;
	image->max_vaddr = max_vaddr;

	kfree(file);
	return true;
}
