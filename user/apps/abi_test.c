#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#define BAD_PTR1 ((void *)0x00001000)
#define BAD_PTR2 ((void *)0x00000001)

static void print_line(const char *prefix, const char *name, int value, const char *suffix) {
	char line[128];
	int len = snprintf(line, sizeof(line), "%s%s -> %d%s\n", prefix, name, value, suffix);
	if (len < 0) {
		return;
	}
	if ((uint32_t)len > sizeof(line)) {
		len = (int)sizeof(line);
	}
	write(line, (uint32_t)len);
}

static int expect_fail(const char *name, int res) {
	if (res == -1) {
		print_line("PASS: ", name, res, "");
		return 0;
	}
	print_line("FAIL: ", name, res, " (expected -1)");
	return 1;
}

static int expect_ok(const char *name, int res) {
	if (res >= 0) {
		print_line("PASS: ", name, res, "");
		return 0;
	}
	print_line("FAIL: ", name, res, " (expected >=0)");
	return 1;
}

int main(void) {
	int failures = 0;

	write("abi_test: syscall pointer validation\n", 37);

	failures += expect_fail("write(bad,4)", write(BAD_PTR1, 4));
	failures += expect_ok("write(NULL,0)", write(NULL, 0));
	failures += expect_fail("getcwd(bad,16)", getcwd((char *)BAD_PTR1, 16));
	failures += expect_fail("history_get(bad,16)", history_get(0, (char *)BAD_PTR1, 16));
	failures += expect_fail("alias_get(bad,bad)", alias_get(0, (char *)BAD_PTR1, (char *)BAD_PTR2));
	failures += expect_fail("listdir(/,bad,1)", listdir("/", BAD_PTR1, 1));
	failures += expect_fail("exec(bad args)", exec("/bin/hello.elf", (char *)BAD_PTR1, 4));
	failures += expect_fail("spawn(bad args)", spawn("/bin/hello.elf", (char *)BAD_PTR2, 4));
	failures += expect_fail("waitpid(bad status)", waitpid(-1, (int *)BAD_PTR1));

	if (failures == 0) {
		write("abi_test: all checks passed\n", 30);
		return 0;
	}
	char summary[64];
	int len = snprintf(summary, sizeof(summary), "abi_test: %d checks failed\n", failures);
	if (len > 0) {
		if ((uint32_t)len > sizeof(summary)) {
			len = (int)sizeof(summary);
		}
		write(summary, (uint32_t)len);
	}
	return 1;
}
