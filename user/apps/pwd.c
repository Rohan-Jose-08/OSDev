#include <stdio.h>
#include <unistd.h>

int main(void) {
	char cwd[128];

	if (getcwd(cwd, sizeof(cwd)) < 0) {
		puts("pwd: failed to read cwd");
		return 1;
	}

	puts(cwd);
	return 0;
}
