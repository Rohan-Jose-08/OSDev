#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "io_util.h"

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

static void draw_board(const char board[9]) {
	write("\n", 1);
	for (int i = 0; i < 9; i += 3) {
		write("  ", 2);
		write(&board[i], 1);
		write(" | ", 3);
		write(&board[i + 1], 1);
		write(" | ", 3);
		write(&board[i + 2], 1);
		write("\n", 1);
		if (i < 6) {
			write(" -----------\n", sizeof(" -----------\n") - 1);
		}
	}
	write("\n", 1);
}

int main(void) {
	char board[9] = {'1','2','3','4','5','6','7','8','9'};
	int player = 1;
	int moves = 0;
	char buf[32];

	write("\n========== Tic-Tac-Toe ==========\n",
	      sizeof("\n========== Tic-Tac-Toe ==========\n") - 1);

	while (moves < 9) {
		draw_board(board);

		int win_lines[8][3] = {
			{0,1,2},{3,4,5},{6,7,8},{0,3,6},
			{1,4,7},{2,5,8},{0,4,8},{2,4,6}
		};
		for (int i = 0; i < 8; i++) {
			if (board[win_lines[i][0]] == board[win_lines[i][1]] &&
			    board[win_lines[i][1]] == board[win_lines[i][2]]) {
				write("\nPlayer ", sizeof("\nPlayer ") - 1);
				char ch = (player == 1) ? '2' : '1';
				write(&ch, 1);
				write(" wins!\n\n", sizeof(" wins!\n\n") - 1);
				return 0;
			}
		}

		char mark = (player == 1) ? 'X' : 'O';
		write("Player ", sizeof("Player ") - 1);
		char pch = (char)('0' + player);
		write(&pch, 1);
		write(" (", 2);
		write(&mark, 1);
		write("), enter position: ", sizeof("), enter position: ") - 1);

		read_line(buf, sizeof(buf));
		int pos = parse_int(buf);
		if (pos < 1 || pos > 9) {
			write("Invalid input! Use 1-9.\n", sizeof("Invalid input! Use 1-9.\n") - 1);
			continue;
		}
		int idx = pos - 1;
		if (board[idx] == 'X' || board[idx] == 'O') {
			write("Position already taken!\n", sizeof("Position already taken!\n") - 1);
			continue;
		}
		board[idx] = mark;
		moves++;
		player = (player == 1) ? 2 : 1;
	}

	write("\nGame Over - It's a draw!\n\n",
	      sizeof("\nGame Over - It's a draw!\n\n") - 1);
	return 0;
}
