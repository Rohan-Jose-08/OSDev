#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <kernel/interrupt.h>
#include <kernel/tty.h>
#include <kernel/pagings.h>
#include <kernel/keyboard.h>
#include <kernel/mouse.h>
#include <kernel/shell.h>
#include <kernel/graphics.h>
#include <kernel/timer.h>
#include <kernel/task.h>
#include <kernel/ata.h>
#include <kernel/fs.h>
#include <kernel/kmalloc.h>

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
    
    // Initialize kernel heap early
    kmalloc_init();
    
	idt_init();
    timer_init(100); // Initialize timer at 100 Hz (10ms per tick)
    scheduler_init(); // Initialize task scheduler
    keyboard_init();
    mouse_init();
    graphics_init();
    ata_init();
    fs_init();

    

    printf("RohanOS Version 0.3\n");
    printf("Interrupts are: ");
    printf(are_interrupts_enabled()?"enabled\n":"disabled\n");
    printf("Keyboard initialized.\n");
    printf("Mouse initialized. (Scroll to navigate history)\n");
    
    // Auto-mount primary disk (drive 0)
    printf("Mounting disk filesystem...\n");
    ata_device_t *drive = ata_get_device(0);
    if (drive) {
        // Try to mount existing filesystem
        if (!fs_mount(0)) {
            // If mount fails, format and mount
            printf("No filesystem found. Formatting disk...\n");
            if (fs_format(0)) {
                if (fs_mount(0)) {
                    printf("Disk formatted and mounted successfully!\n");
                    
                    // Create a welcome file
                    fs_create_file("welcome.txt");
                    const char *welcome = "Welcome to RohanOS!\n\nYour files are now stored on disk and will persist between reboots.\n\nTry these commands:\n  ls - list files\n  cat welcome.txt - read this file\n  write <file> <text> - create a file\n  rm <file> - delete a file\n";
                    fs_write_file("welcome.txt", (const uint8_t*)welcome, strlen(welcome), 0);
                } else {
                    printf("Failed to mount after format\n");
                }
            } else {
                printf("Failed to format disk\n");
            }
        } else {
            printf("Disk mounted successfully!\n");
        }
    } else {
        printf("Warning: No disk drive detected. File operations will be limited.\n");
    }
    

    shell_init();
    

    while(1) {
        __asm__ volatile ("hlt");
    }
}
    

