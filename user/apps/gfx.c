#include <unistd.h>

int main(void) {
	if (gfx_demo() < 0) {
		return 1;
	}
	return 0;
}
