#include <unistd.h>

int main(void) {
	write("rmdir: directories not yet supported (flat filesystem)\n",
	      sizeof("rmdir: directories not yet supported (flat filesystem)\n") - 1);
	return 0;
}
