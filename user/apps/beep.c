#include <unistd.h>

int main(void) {
	if (beep() < 0) {
		return 1;
	}
	write("*BEEP*\n", sizeof("*BEEP*\n") - 1);
	return 0;
}
