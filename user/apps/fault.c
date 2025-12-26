#include <stdint.h>

int main(void) {
	volatile uint32_t *ptr = (volatile uint32_t *)0x0;
	*ptr = 0xDEADBEEF;
	return 0;
}
