#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "io_util.h"
#include "rand_util.h"

static int parse_int(const char *s) {
	int value = 0;
	int sign = 1;

	if (!s || *s == '\0') {
		return 0;
	}
	if (*s == '-') {
		sign = -1;
		s++;
	}
	while (*s >= '0' && *s <= '9') {
		value = value * 10 + (*s - '0');
		s++;
	}
	return value * sign;
}

int main(void) {
	char buf[64];
	rand_seed_from_ticks();
	int target = (int)(rand_next() % 100) + 1;
	int attempts = 0;

	write("\n========== Number Guessing Game ==========\n\n",
	      sizeof("\n========== Number Guessing Game ==========\n\n") - 1);
	write("I'm thinking of a number between 1 and 100.\n",
	      sizeof("I'm thinking of a number between 1 and 100.\n") - 1);
	write("Can you guess it? (Type 'quit' to exit)\n\n",
	      sizeof("Can you guess it? (Type 'quit' to exit)\n\n") - 1);

	while (1) {
		write("Your guess: ", sizeof("Your guess: ") - 1);
		read_line(buf, sizeof(buf));

		if (strcmp(buf, "quit") == 0) {
			write("Game cancelled.\n\n",
			      sizeof("Game cancelled.\n\n") - 1);
			return 0;
		}

		int guess = parse_int(buf);
		attempts++;

		if (guess < 1 || guess > 100) {
			write("Please enter a number between 1 and 100.\n",
			      sizeof("Please enter a number between 1 and 100.\n") - 1);
			continue;
		}

		if (guess == target) {
			write("\n*** CORRECT! ***\n",
			      sizeof("\n*** CORRECT! ***\n") - 1);
			write("You found it in ", sizeof("You found it in ") - 1);
			{
				char tmp[16];
				int i = 0;
				int value = attempts;
				if (value == 0) {
					write("0", 1);
				} else {
					while (value > 0 && i < (int)sizeof(tmp) - 1) {
						tmp[i++] = (char)('0' + (value % 10));
						value /= 10;
					}
					for (int j = i - 1; j >= 0; j--) {
						write(&tmp[j], 1);
					}
				}
			}
			write(" attempts!\n\n", sizeof(" attempts!\n\n") - 1);
			return 0;
		}

		if (guess < target) {
			write("Too low! ", sizeof("Too low! ") - 1);
		} else {
			write("Too high! ", sizeof("Too high! ") - 1);
		}
		int diff = (guess > target) ? (guess - target) : (target - guess);
		if (diff <= 5) {
			write("You're very close!\n", sizeof("You're very close!\n") - 1);
		} else if (diff <= 15) {
			write("You're getting warm!\n", sizeof("You're getting warm!\n") - 1);
		} else {
			write("Try again!\n", sizeof("Try again!\n") - 1);
		}
	}
}
