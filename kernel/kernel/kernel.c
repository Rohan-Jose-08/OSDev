#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <kernel/interrupt.h>
#include <kernel/tty.h>
#include <kernel/pagings.h>
#include <kernel/keyboard.h>
#include <kernel/mouse.h>
#include <kernel/shell.h>
#include <kernel/vfs.h>
#include <kernel/graphics.h>
#include <kernel/timer.h>
#include <kernel/task.h>

#include <stdint.h>
#include <stddef.h> 


static inline bool are_interrupts_enabled()
{
    unsigned long flags;
    asm volatile ( "pushf\n\t"
                   "pop %0"
                   : "=g"(flags) );
    return flags & (1 << 9);
}

void kernel_main(void) {   

	terminal_initialize();
    
    
	idt_init();
    timer_init(100); // Initialize timer at 100 Hz (10ms per tick)
    scheduler_init(); // Initialize task scheduler
    keyboard_init();
    mouse_init();
    vfs_init();
    graphics_init();

    

    printf("RohanOS Version 0.3\n");
    printf("Interrupts are: ");
    printf(are_interrupts_enabled()?"enabled\n":"disabled\n");
    printf("Keyboard initialized.\n");
    printf("Mouse initialized. (Scroll to navigate history)\n");
    
    // Create some test directories and files in VFS
    vfs_node_t *root = vfs_get_root();
    vfs_mkdir(root, "home");
    vfs_mkdir(root, "bin");
    vfs_mkdir(root, "etc");
    
    vfs_node_t *home = vfs_find_child(root, "home");
    if (home) {
        vfs_node_t *user_dir = vfs_mkdir(home, "user");
        if (user_dir) {
            vfs_node_t *test_file = vfs_create_file(user_dir, "readme.txt", VFS_PERM_READ | VFS_PERM_WRITE);
            if (test_file) {
                const char *content = "Welcome to RohanOS Virtual File System!";
                vfs_write_file(test_file, (const uint8_t*)content, strlen(content));
            }
        }
    }
    

    shell_init();
    

    while(1) {
        __asm__ volatile ("hlt");
    }
}
    

