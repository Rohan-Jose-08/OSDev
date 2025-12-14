#include <stdio.h>
#include <stdbool.h>
#include <kernel/interrupt.h>
#include <kernel/tty.h>
#include <kernel/pagings.h>
#include <kernel/keyboard.h>
#include <kernel/shell.h>

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
    keyboard_init();
    

    printf("MyOS Version 1.0\n");
    printf("Interrupts are: ");
    printf(are_interrupts_enabled()?"enabled\n":"disabled\n");
    printf("Keyboard initialized.\n");
    
 
    shell_init();
    

    while(1) {
        __asm__ volatile ("hlt");
    }
}
    

