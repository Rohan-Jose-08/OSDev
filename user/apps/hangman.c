#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "io_util.h"
#include "rand_util.h"

int main(void) {
	const char *words[] = {"KERNEL", "SYSTEM", "MEMORY", "TERMINAL", "COMPUTER", "PROGRAM"};
	char guessed[26] = {0};
	char buf[32];
	int wrong = 0;
	int max_wrong = 6;

	rand_seed_from_ticks();
	const char *word = words[rand_next() % (uint32_t)(sizeof(words) / sizeof(words[0]))];
	int word_len = (int)strlen(word);

	write("\n========== Hangman ==========\n",
	      sizeof("\n========== Hangman ==========\n") - 1);

	while (wrong < max_wrong) {
		write("\nWord: ", sizeof("\nWord: ") - 1);
		int complete = 1;
		for (int i = 0; i < word_len; i++) {
			int found = 0;
			for (int j = 0; j < 26; j++) {
				if (guessed[j] == word[i]) {
					found = 1;
					break;
				}
			}
			if (found) {
				write(&word[i], 1);
				write(" ", 1);
			} else {
				write("_ ", 2);
				complete = 0;
			}
		}

		if (complete) {
			write("\n\nYou won! The word was: ",
			      sizeof("\n\nYou won! The word was: ") - 1);
			write(word, (uint32_t)word_len);
			write("\n\n", 2);
			return 0;
		}

		write("\nWrong guesses: ", sizeof("\nWrong guesses: ") - 1);
		{
			char tmp[4];
			tmp[0] = (char)('0' + wrong);
			tmp[1] = '/';
			tmp[2] = (char)('0' + max_wrong);
			tmp[3] = '\0';
			write(tmp, 3);
		}
		write("\nGuess a letter: ", sizeof("\nGuess a letter: ") - 1);

		read_line(buf, sizeof(buf));
		if (buf[0] == '\0') {
			continue;
		}
		char input = buf[0];
		if (input >= 'a' && input <= 'z') {
			input = (char)(input - 'a' + 'A');
		}
		if (input < 'A' || input > 'Z') {
			write("Invalid input!\n", sizeof("Invalid input!\n") - 1);
			continue;
		}

		int already = 0;
		for (int i = 0; i < 26; i++) {
			if (guessed[i] == input) {
				already = 1;
				break;
			}
		}
		if (already) {
			write("Already guessed that letter!\n",
			      sizeof("Already guessed that letter!\n") - 1);
			continue;
		}

		for (int i = 0; i < 26; i++) {
			if (!guessed[i]) {
				guessed[i] = input;
				break;
			}
		}

		int in_word = 0;
		for (int i = 0; i < word_len; i++) {
			if (word[i] == input) {
				in_word = 1;
				break;
			}
		}

		if (!in_word) {
			wrong++;
			write("Wrong!\n", sizeof("Wrong!\n") - 1);
		} else {
			write("Correct!\n", sizeof("Correct!\n") - 1);
		}
	}

	write("\nYou lost! The word was: ",
	      sizeof("\nYou lost! The word was: ") - 1);
	write(word, (uint32_t)word_len);
	write("\n\n", 2);
	return 0;
}
