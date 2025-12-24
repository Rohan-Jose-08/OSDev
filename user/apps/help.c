#include <stdint.h>
#include <string.h>
#include <unistd.h>

static void write_line(const char *line) {
	write(line, (uint32_t)strlen(line));
}

int main(void) {
	write_line("\n=== MyOS Shell Commands (user mode) ===\n\n");
	write_line("General:\n");
	write_line("  help, about, banner, clear, echo, color, colors\n");
	write_line("  calc, reverse, strlen, upper, lower, rainbow, draw\n");
	write_line("  randcolor, fortune, animate, matrix\n\n");

	write_line("Files:\n");
	write_line("  ls, cat, stat, touch, write, rm, mkdir, cd, pwd\n\n");

	write_line("Games:\n");
	write_line("  guess, rps, tictactoe, hangman\n\n");

	write_line("Shell:\n");
	write_line("  alias, unalias, aliases, theme, history, timer\n\n");

	write_line("System:\n");
	write_line("  sysinfo, uptime, beep, halt, run\n\n");

	write_line("Graphics/GUI:\n");
	write_line("  gfx, gfxanim, gfxpaint, gui, guipaint, guicalc, guifilemgr, desktop\n\n");
	return 0;
}
