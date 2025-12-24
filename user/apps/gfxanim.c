#include <unistd.h>

int main(void) {
	if (gfx_anim() < 0) {
		return 1;
	}
	return 0;
}
