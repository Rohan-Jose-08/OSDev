#include <stdio.h>
#include <stdbool.h>
#include <kernel/interrupt.h>
#include <kernel/tty.h>
#include <kernel/pagings.h>


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
    // page_init();
	idt_init();
    PIC_remap(0x00,0x000);
    
	terminal_initialize();
    printf("MyOS Version 1.0\n");
    printf("Interrupts are:");
    printf(are_interrupts_enabled()?"enabled\n":"disabled\n");

}
    

