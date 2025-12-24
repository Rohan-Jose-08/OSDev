#include <unistd.h>

int main(void) {
	if (clear() < 0) {
		return 1;
	}
	return 0;
}
