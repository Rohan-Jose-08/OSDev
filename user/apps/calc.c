#include <stdint.h>
#include <unistd.h>

#include "path_util.h"

static int parse_int(const char **cursor, int *out) {
	const char *s = *cursor;
	int value = 0;
	int negative = 0;

	if (*s == '-') {
		negative = 1;
		s++;
	}

	if (*s < '0' || *s > '9') {
		return -1;
	}

	while (*s >= '0' && *s <= '9') {
		value = value * 10 + (*s - '0');
		s++;
	}

	if (negative) {
		value = -value;
	}

	*cursor = s;
	*out = value;
	return 0;
}

static void print_uint(uint32_t value) {
	char buf[16];
	int i = 0;

	if (value == 0) {
		write("0", 1);
		return;
	}

	while (value > 0 && i < (int)sizeof(buf) - 1) {
		buf[i++] = (char)('0' + (value % 10));
		value /= 10;
	}

	for (int j = i - 1; j >= 0; j--) {
		write(&buf[j], 1);
	}
}

static void print_int(int value) {
	uint32_t abs_value;

	if (value < 0) {
		write("-", 1);
		abs_value = (uint32_t)(-(value + 1)) + 1;
	} else {
		abs_value = (uint32_t)value;
	}

	print_uint(abs_value);
}

int main(void) {
	char args[128];
	uint32_t total = getargs(args, sizeof(args));

	if (total >= sizeof(args)) {
		total = sizeof(args) - 1;
	}
	args[total] = '\0';

	const char *cursor = skip_spaces(args);
	if (*cursor == '\0') {
		write("Usage: calc <num1> <+|-|*|/> <num2>\n",
		      sizeof("Usage: calc <num1> <+|-|*|/> <num2>\n") - 1);
		return 1;
	}

	int num1 = 0;
	if (parse_int(&cursor, &num1) != 0) {
		write("Error: Invalid expression\n", sizeof("Error: Invalid expression\n") - 1);
		return 1;
	}

	cursor = skip_spaces((char *)cursor);
	char op = *cursor;
	if (op != '+' && op != '-' && op != '*' && op != '/') {
		write("Error: Invalid operator\n", sizeof("Error: Invalid operator\n") - 1);
		return 1;
	}
	cursor++;

	cursor = skip_spaces((char *)cursor);
	int num2 = 0;
	if (parse_int(&cursor, &num2) != 0) {
		write("Error: Invalid expression\n", sizeof("Error: Invalid expression\n") - 1);
		return 1;
	}

	if (op == '/' && num2 == 0) {
		write("Error: Division by zero!\n", sizeof("Error: Division by zero!\n") - 1);
		return 1;
	}

	int result = 0;
	switch (op) {
		case '+':
			result = num1 + num2;
			break;
		case '-':
			result = num1 - num2;
			break;
		case '*':
			result = num1 * num2;
			break;
		case '/':
			result = num1 / num2;
			break;
	}

	print_int(num1);
	write(" ", 1);
	write(&op, 1);
	write(" ", 1);
	print_int(num2);
	write(" = ", 3);
	print_int(result);
	write("\n", 1);
	return 0;
}
