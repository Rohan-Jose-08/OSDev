#ifndef PIC_H
#define PIC_H
#include <stdint.h>

void PIC_remap(int offset1, int offset2);
void PIC_sendEOI(uint8_t irq);
void IRQ_set_mask(uint8_t IRQline);
void IRQ_clear_mask(uint8_t IRQline);
void pic_disable(void);
 
#endif PIC_H
