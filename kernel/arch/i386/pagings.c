#include <kernel/pagings.h>
#include <kernel/cpu.h>
#include <kernel/kmalloc.h>
#include <string.h>

extern void loadPageDirectory(unsigned int*);
extern void enablePaging();

#define FRAME_POOL_START (HEAP_PHYS_START + HEAP_SIZE)
#define FRAME_POOL_END USER_SPACE_START
#define FRAME_COUNT ((FRAME_POOL_END - FRAME_POOL_START) / PAGE_SIZE)
#define FRAME_BITMAP_SIZE ((FRAME_COUNT + 7) / 8)

static uint8_t frame_bitmap[FRAME_BITMAP_SIZE];
static uint32_t frame_refcount[FRAME_COUNT];
static uint32_t *kernel_page_directory = NULL;

static inline bool page_in_kernel_heap(uint32_t virt) {
	return virt >= HEAP_START && virt < (HEAP_START + HEAP_SIZE);
}

static bool frame_index_from_phys(uint32_t phys, uint32_t *out_idx) {
	if (phys < FRAME_POOL_START || phys >= FRAME_POOL_END) {
		return false;
	}
	if ((phys & (PAGE_SIZE - 1)) != 0) {
		return false;
	}
	uint32_t idx = (phys - FRAME_POOL_START) / PAGE_SIZE;
	if (idx >= FRAME_COUNT) {
		return false;
	}
	if (out_idx) {
		*out_idx = idx;
	}
	return true;
}

static inline bool frame_test(uint32_t idx) {
	return (frame_bitmap[idx / 8] & (1u << (idx % 8))) != 0;
}

static inline void frame_set(uint32_t idx) {
	frame_bitmap[idx / 8] |= (uint8_t)(1u << (idx % 8));
}

static inline void frame_clear(uint32_t idx) {
	frame_bitmap[idx / 8] &= (uint8_t)~(1u << (idx % 8));
}

static void frame_init(void) {
	memset(frame_bitmap, 0, sizeof(frame_bitmap));
	memset(frame_refcount, 0, sizeof(frame_refcount));
}

uint32_t frame_alloc(void) {
	for (uint32_t i = 0; i < FRAME_COUNT; i++) {
		if (!frame_test(i)) {
			frame_set(i);
			frame_refcount[i] = 1;
			return FRAME_POOL_START + i * PAGE_SIZE;
		}
	}
	return 0;
}

void frame_free(uint32_t phys) {
	uint32_t idx = 0;
	if (!frame_index_from_phys(phys, &idx)) {
		return;
	}
	if (frame_refcount[idx] > 1) {
		frame_refcount[idx]--;
		return;
	}
	frame_refcount[idx] = 0;
	frame_clear(idx);
}

void frame_ref_inc(uint32_t phys) {
	uint32_t idx = 0;
	if (!frame_index_from_phys(phys, &idx)) {
		return;
	}
	frame_refcount[idx]++;
}

uint32_t *page_kernel_directory(void) {
	return kernel_page_directory;
}

bool page_map(uint32_t *page_dir, uint32_t virt, uint32_t phys, uint32_t flags) {
	if (!page_dir) {
		return false;
	}
	if (page_in_kernel_heap(virt)) {
		if (page_dir != kernel_page_directory) {
			return false;
		}
		if (flags & PAGE_USER) {
			return false;
		}
	}
	uint32_t pde_idx = virt >> 22;
	uint32_t pte_idx = (virt >> 12) & 0x3FF;
	uint32_t *table = NULL;

	if ((page_dir[pde_idx] & PAGE_PRESENT) == 0) {
		uint32_t table_phys = frame_alloc();
		if (!table_phys) {
			return false;
		}
		table = (uint32_t *)phys_to_virt(table_phys);
		memset(table, 0, PAGE_SIZE);
		uint32_t pde_flags = PAGE_PRESENT | PAGE_RW;
		if (flags & PAGE_USER) {
			pde_flags |= PAGE_USER;
		}
		page_dir[pde_idx] = table_phys | pde_flags;
	} else {
		table = (uint32_t *)phys_to_virt(page_dir[pde_idx] & ~0xFFF);
		if (flags & PAGE_USER) {
			page_dir[pde_idx] |= PAGE_USER;
		}
	}

	if (table[pte_idx] & PAGE_PRESENT) {
		return false;
	}
	table[pte_idx] = (phys & ~0xFFF) | (flags | PAGE_PRESENT);
	return true;
}

bool page_map_alloc(uint32_t *page_dir, uint32_t virt, uint32_t flags, uint32_t *out_phys) {
	uint32_t phys = frame_alloc();
	if (!phys) {
		return false;
	}
	if (!page_map(page_dir, virt, phys, flags)) {
		frame_free(phys);
		return false;
	}
	if (out_phys) {
		*out_phys = phys;
	}
	return true;
}

bool page_unmap(uint32_t *page_dir, uint32_t virt, bool free_frame) {
	if (!page_dir) {
		return false;
	}
	uint32_t pde_idx = virt >> 22;
	uint32_t pte_idx = (virt >> 12) & 0x3FF;
	uint32_t pde = page_dir[pde_idx];
	if ((pde & PAGE_PRESENT) == 0) {
		return false;
	}
	uint32_t *table = (uint32_t *)phys_to_virt(pde & ~0xFFF);
	uint32_t pte = table[pte_idx];
	if ((pte & PAGE_PRESENT) == 0) {
		return false;
	}
	uint32_t phys = pte & ~0xFFF;
	table[pte_idx] = 0;
	invlpg(virt);
	if (free_frame) {
		frame_free(phys);
	}
	return true;
}

static bool page_user_present(uint32_t *page_dir, uint32_t addr) {
	uint32_t pde_idx = addr >> 22;
	uint32_t pte_idx = (addr >> 12) & 0x3FF;
	uint32_t pde = page_dir[pde_idx];
	if ((pde & PAGE_PRESENT) == 0 || (pde & PAGE_USER) == 0) {
		return false;
	}
	uint32_t *table = (uint32_t *)phys_to_virt(pde & ~0xFFF);
	uint32_t pte = table[pte_idx];
	if ((pte & PAGE_PRESENT) == 0 || (pte & PAGE_USER) == 0) {
		return false;
	}
	return true;
}

bool page_user_range_mapped(uint32_t *page_dir, uint32_t addr, uint32_t size) {
	if (!page_dir) {
		return false;
	}
	if (size == 0) {
		return true;
	}
	uint32_t end = addr + size - 1;
	if (end < addr) {
		return false;
	}
	uint32_t cur = addr & ~(PAGE_SIZE - 1);
	uint32_t last = end & ~(PAGE_SIZE - 1);
	for (;;) {
		if (!page_user_present(page_dir, cur)) {
			return false;
		}
		if (cur == last) {
			break;
		}
		cur += PAGE_SIZE;
	}
	return true;
}

bool page_handle_cow(uint32_t *page_dir, uint32_t fault_addr) {
	if (!page_dir) {
		return false;
	}
	if (fault_addr < USER_SPACE_START || fault_addr >= USER_SPACE_END) {
		return false;
	}
	uint32_t pde_idx = fault_addr >> 22;
	uint32_t pte_idx = (fault_addr >> 12) & 0x3FF;
	uint32_t pde = page_dir[pde_idx];
	if ((pde & PAGE_PRESENT) == 0) {
		return false;
	}
	uint32_t *table = (uint32_t *)phys_to_virt(pde & ~0xFFF);
	uint32_t pte = table[pte_idx];
	if ((pte & PAGE_PRESENT) == 0 || (pte & PAGE_USER) == 0) {
		return false;
	}
	if ((pte & PAGE_COW) == 0) {
		return false;
	}
	uint32_t phys = pte & ~0xFFF;
	uint32_t flags = pte & 0xFFF;
	uint32_t idx = 0;
	if (!frame_index_from_phys(phys, &idx)) {
		return false;
	}
	if (frame_refcount[idx] <= 1) {
		flags = (flags | PAGE_RW) & ~PAGE_COW;
		table[pte_idx] = phys | flags;
		invlpg(fault_addr);
		return true;
	}
	uint32_t new_phys = frame_alloc();
	if (!new_phys) {
		return false;
	}
	memcpy(phys_to_virt(new_phys), phys_to_virt(phys), PAGE_SIZE);
	flags = (flags | PAGE_RW) & ~PAGE_COW;
	table[pte_idx] = new_phys | flags;
	frame_free(phys);
	invlpg(fault_addr);
	return true;
}

uint32_t *page_directory_create(void) {
	uint32_t phys = frame_alloc();
	if (!phys) {
		return NULL;
	}
	uint32_t *dir = (uint32_t *)phys_to_virt(phys);
	memset(dir, 0, PAGE_SIZE);
	return dir;
}

void page_directory_destroy(uint32_t *page_dir) {
	if (!page_dir || page_dir == kernel_page_directory) {
		return;
	}
	for (uint32_t i = 0; i < 1024; i++) {
		uint32_t pde = page_dir[i];
		if ((pde & PAGE_PRESENT) == 0) {
			continue;
		}
		uint32_t *table = (uint32_t *)phys_to_virt(pde & ~0xFFF);
		for (uint32_t j = 0; j < 1024; j++) {
			uint32_t pte = table[j];
			if ((pte & PAGE_PRESENT) && (pte & PAGE_USER)) {
				uint32_t phys = pte & ~0xFFF;
				frame_free(phys);
			}
		}
		frame_free(virt_to_phys(table));
		page_dir[i] = 0;
	}

	frame_free(virt_to_phys(page_dir));
}

bool page_translate_flags(uint32_t *page_dir, uint32_t virt, uint32_t *out_phys, uint32_t *out_flags) {
	if (!page_dir) {
		return false;
	}
	uint32_t pde = page_dir[virt >> 22];
	if ((pde & PAGE_PRESENT) == 0) {
		return false;
	}
	uint32_t *table = (uint32_t *)phys_to_virt(pde & ~0xFFF);
	uint32_t pte = table[(virt >> 12) & 0x3FF];
	if ((pte & PAGE_PRESENT) == 0) {
		return false;
	}
	uint32_t phys = (pte & ~0xFFF) | (virt & 0xFFF);
	if (out_phys) {
		*out_phys = phys;
	}
	if (out_flags) {
		*out_flags = pte & 0xFFF;
	}
	return true;
}

bool page_translate(uint32_t *page_dir, uint32_t virt, uint32_t *out_phys) {
	return page_translate_flags(page_dir, virt, out_phys, NULL);
}

bool page_update_flags(uint32_t *page_dir, uint32_t virt, uint32_t set, uint32_t clear) {
	if (!page_dir) {
		return false;
	}
	uint32_t pde = page_dir[virt >> 22];
	if ((pde & PAGE_PRESENT) == 0) {
		return false;
	}
	uint32_t *table = (uint32_t *)phys_to_virt(pde & ~0xFFF);
	uint32_t idx = (virt >> 12) & 0x3FF;
	uint32_t pte = table[idx];
	if ((pte & PAGE_PRESENT) == 0) {
		return false;
	}
	uint32_t phys = pte & ~0xFFF;
	uint32_t flags = pte & 0xFFF;
	flags |= set;
	flags &= ~clear;
	flags |= PAGE_PRESENT;
	table[idx] = phys | flags;
	invlpg(virt);
	return true;
}

bool page_copy_from_user(uint32_t *page_dir, void *dst, uint32_t src, uint32_t len) {
	if (!page_dir || !dst) {
		return false;
	}
	uint32_t remaining = len;
	uint32_t offset = 0;
	while (remaining > 0) {
		uint32_t addr = src + offset;
		uint32_t phys = 0;
		if (!page_translate(page_dir, addr, &phys)) {
			return false;
		}
		uint32_t page_off = addr & (PAGE_SIZE - 1);
		uint32_t chunk = PAGE_SIZE - page_off;
		if (chunk > remaining) {
			chunk = remaining;
		}
		memcpy((uint8_t *)dst + offset, (uint8_t *)phys_to_virt(phys), chunk);
		remaining -= chunk;
		offset += chunk;
	}
	return true;
}

bool page_copy_to_user(uint32_t *page_dir, uint32_t dst, const void *src, uint32_t len) {
	if (!page_dir || !src) {
		return false;
	}
	uint32_t remaining = len;
	uint32_t offset = 0;
	while (remaining > 0) {
		uint32_t addr = dst + offset;
		uint32_t phys = 0;
		uint32_t flags = 0;
		if (!page_translate_flags(page_dir, addr, &phys, &flags)) {
			return false;
		}
		if ((flags & PAGE_RW) == 0) {
			if ((flags & PAGE_COW) == 0 || !page_handle_cow(page_dir, addr)) {
				return false;
			}
			if (!page_translate_flags(page_dir, addr, &phys, &flags) ||
			    (flags & PAGE_RW) == 0) {
				return false;
			}
		}
		uint32_t page_off = addr & (PAGE_SIZE - 1);
		uint32_t chunk = PAGE_SIZE - page_off;
		if (chunk > remaining) {
			chunk = remaining;
		}
		memcpy((uint8_t *)phys_to_virt(phys), (const uint8_t *)src + offset, chunk);
		remaining -= chunk;
		offset += chunk;
	}
	return true;
}

bool page_memset_user(uint32_t *page_dir, uint32_t dst, int value, uint32_t len) {
	if (!page_dir) {
		return false;
	}
	uint32_t remaining = len;
	uint32_t offset = 0;
	while (remaining > 0) {
		uint32_t addr = dst + offset;
		uint32_t phys = 0;
		uint32_t flags = 0;
		if (!page_translate_flags(page_dir, addr, &phys, &flags)) {
			return false;
		}
		if ((flags & PAGE_RW) == 0) {
			if ((flags & PAGE_COW) == 0 || !page_handle_cow(page_dir, addr)) {
				return false;
			}
			if (!page_translate_flags(page_dir, addr, &phys, &flags) ||
			    (flags & PAGE_RW) == 0) {
				return false;
			}
		}
		uint32_t page_off = addr & (PAGE_SIZE - 1);
		uint32_t chunk = PAGE_SIZE - page_off;
		if (chunk > remaining) {
			chunk = remaining;
		}
		memset((uint8_t *)phys_to_virt(phys), value, chunk);
		remaining -= chunk;
		offset += chunk;
	}
	return true;
}

void page_init(void) {
	frame_init();

	uint32_t dir_phys = frame_alloc();
	if (!dir_phys) {
		return;
	}
	kernel_page_directory = (uint32_t *)phys_to_virt(dir_phys);
	memset(kernel_page_directory, 0, PAGE_SIZE);

	for (uint32_t phys = 0; phys < USER_SPACE_START; phys += PAGE_SIZE) {
		uint32_t virt = phys + KERNEL_VIRT_BASE;
		if (!page_map(kernel_page_directory, virt, phys, PAGE_RW)) {
			return;
		}
	}

	loadPageDirectory((unsigned int *)dir_phys);
	enablePaging();
}
