#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "io_util.h"
#include "rand_util.h"

static int parse_int(const char *s) {
	int value = 0;
	if (!s || *s == '\0') {
		return 0;
	}
	while (*s >= '0' && *s <= '9') {
		value = value * 10 + (*s - '0');
		s++;
	}
	return value;
}

int main(void) {
	char buf[32];
	const char *choices[] = {"", "Rock", "Paper", "Scissors"};

	rand_seed_from_ticks();
	write("\n========== Rock Paper Scissors ==========\n\n",
	      sizeof("\n========== Rock Paper Scissors ==========\n\n") - 1);
	write("1. Rock\n2. Paper\n3. Scissors\n\n",
	      sizeof("1. Rock\n2. Paper\n3. Scissors\n\n") - 1);
	write("Your choice (1-3): ", sizeof("Your choice (1-3): ") - 1);

	read_line(buf, sizeof(buf));
	int player = parse_int(buf);
	if (player < 1 || player > 3) {
		write("Invalid choice!\n\n", sizeof("Invalid choice!\n\n") - 1);
		return 1;
	}

	int computer = (int)(rand_next() % 3) + 1;

	write("You chose: ", sizeof("You chose: ") - 1);
	write(choices[player], (uint32_t)strlen(choices[player]));
	write("\nComputer chose: ", sizeof("\nComputer chose: ") - 1);
	write(choices[computer], (uint32_t)strlen(choices[computer]));
	write("\n\n", 2);

	if (player == computer) {
		write("It's a tie!\n\n", sizeof("It's a tie!\n\n") - 1);
	} else if ((player == 1 && computer == 3) ||
	           (player == 2 && computer == 1) ||
	           (player == 3 && computer == 2)) {
		write("You win!\n\n", sizeof("You win!\n\n") - 1);
	} else {
		write("Computer wins!\n\n", sizeof("Computer wins!\n\n") - 1);
	}

	return 0;
}
