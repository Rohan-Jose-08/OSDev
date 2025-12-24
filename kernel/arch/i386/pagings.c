#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <kernel/pagings.h>

uint32_t page_directory[1024] __attribute__((aligned(4096)));
static uint32_t page_tables[16][1024] __attribute__((aligned(4096)));

extern void loadPageDirectory(unsigned int*);
extern void enablePaging();

void page_init(void) {
    for (int i = 0; i < 1024; i++) {
        page_directory[i] = 0x00000002;
    }

    for (int pd = 0; pd < 16; pd++) {
        for (int pt = 0; pt < 1024; pt++) {
            uint32_t addr = (uint32_t)((pd * 1024 + pt) * 0x1000);
            uint32_t flags = 0x3; // present, rw
            if (addr >= 0x02000000) {
                flags |= 0x4; // user
            }
            page_tables[pd][pt] = addr | flags;
        }

        uint32_t pde_flags = 0x3;
        if ((uint32_t)(pd * 0x400000) >= 0x02000000) {
            pde_flags |= 0x4;
        }
        page_directory[pd] = ((uint32_t)page_tables[pd]) | pde_flags;
    }

    loadPageDirectory((unsigned int *)page_directory);
    enablePaging();
}
