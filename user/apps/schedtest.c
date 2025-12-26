#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

static void busy_worker(int id, uint32_t duration_ticks) {
	uint32_t start = get_ticks();
	uint32_t next_report = start;
	uint32_t counter = 0;
	char line[96];

	while ((uint32_t)(get_ticks() - start) < duration_ticks) {
		counter++;
		if ((counter & 0x3FFFF) == 0) {
			uint32_t now = get_ticks();
			if (now >= next_report) {
				snprintf(line, sizeof(line), "spin %d tick %u", id, now);
				puts(line);
				next_report = now + 5;
			}
		}
	}

	snprintf(line, sizeof(line), "spin %d done", id);
	puts(line);
}

static void sleeper_task(void) {
	char line[64];
	for (int i = 0; i < 5; i++) {
		snprintf(line, sizeof(line), "sleeper round %d", i);
		puts(line);
		sleep_ms(100);
	}
	puts("sleeper done");
}

int main(void) {
	puts("schedtest: starting");

	int worker_count = 3;
	int role = 0;
	for (int i = 0; i < worker_count; i++) {
		int pid = fork();
		if (pid == 0) {
			role = i + 1;
			break;
		}
		if (pid < 0) {
			puts("schedtest: fork failed");
		}
	}

	if (role != 0) {
		busy_worker(role, 50);
		return 0;
	}

	int sleeper_pid = fork();
	if (sleeper_pid == 0) {
		sleeper_task();
		return 0;
	}
	if (sleeper_pid < 0) {
		puts("schedtest: sleeper fork failed");
	}

	int status = -1;
	int total = worker_count + ((sleeper_pid > 0) ? 1 : 0);
	int completed = 0;
	char line[96];

	while (completed < total) {
		int pid = wait(&status);
		if (pid < 0) {
			break;
		}
		snprintf(line, sizeof(line), "schedtest: child %d exit %d", pid, status);
		puts(line);
		completed++;
	}

	puts("schedtest: done");
	return 0;
}
