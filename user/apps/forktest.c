#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

static char shared_buf[32] = "cow-check";

int main(void) {
	char line[128];
	volatile int local = 123;

	puts("forktest: starting");
	snprintf(line, sizeof(line), "parent before fork: buf=%s local=%d",
	         shared_buf, local);
	puts(line);

	int pid = fork();
	if (pid < 0) {
		puts("forktest: fork failed");
		return 1;
	}

	if (pid == 0) {
		snprintf(line, sizeof(line), "child before write: buf=%s local=%d",
		         shared_buf, local);
		puts(line);
		shared_buf[0] = 'C';
		local = 456;
		snprintf(line, sizeof(line), "child after write: buf=%s local=%d",
		         shared_buf, local);
		puts(line);
		return 0;
	}

	shared_buf[0] = 'P';
	local = 999;
	snprintf(line, sizeof(line), "parent after write: buf=%s local=%d",
	         shared_buf, local);
	puts(line);

	int status = -1;
	int wpid = waitpid(pid, &status);
	snprintf(line, sizeof(line), "parent wait pid=%d status=%d", wpid, status);
	puts(line);

	snprintf(line, sizeof(line), "parent final: buf=%s local=%d",
	         shared_buf, local);
	puts(line);
	return 0;
}
