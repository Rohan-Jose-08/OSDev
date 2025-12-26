#ifndef _PAGINGS_H
#define _PAGINGS_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <kernel/memory.h>

#define PAGE_SIZE 4096
#define PAGE_PRESENT 0x1
#define PAGE_RW 0x2
#define PAGE_USER 0x4
#define PAGE_COW 0x200

#define USER_SPACE_START 0x02000000
#define USER_SPACE_END   0x04000000

void page_init(void);
uint32_t *page_kernel_directory(void);

uint32_t *page_directory_create(void);
void page_directory_destroy(uint32_t *page_dir);

bool page_map(uint32_t *page_dir, uint32_t virt, uint32_t phys, uint32_t flags);
bool page_map_alloc(uint32_t *page_dir, uint32_t virt, uint32_t flags, uint32_t *out_phys);
bool page_unmap(uint32_t *page_dir, uint32_t virt, bool free_frame);
bool page_handle_cow(uint32_t *page_dir, uint32_t fault_addr);
bool page_user_range_mapped(uint32_t *page_dir, uint32_t addr, uint32_t size);
bool page_translate(uint32_t *page_dir, uint32_t virt, uint32_t *out_phys);
bool page_translate_flags(uint32_t *page_dir, uint32_t virt, uint32_t *out_phys, uint32_t *out_flags);
bool page_update_flags(uint32_t *page_dir, uint32_t virt, uint32_t set, uint32_t clear);
bool page_copy_from_user(uint32_t *page_dir, void *dst, uint32_t src, uint32_t len);
bool page_copy_to_user(uint32_t *page_dir, uint32_t dst, const void *src, uint32_t len);
bool page_memset_user(uint32_t *page_dir, uint32_t dst, int value, uint32_t len);

uint32_t frame_alloc(void);
void frame_free(uint32_t phys);
void frame_ref_inc(uint32_t phys);

#endif
