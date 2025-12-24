#include <stdint.h>
#include <unistd.h>

int main(void) {
	write("\n", 1);
	write(" __  __       ___   ____  \n", sizeof(" __  __       ___   ____  \n") - 1);
	write("|  \\/  |_   _ / _ \\ |  _ \\ \n", sizeof("|  \\/  |_   _ / _ \\ |  _ \\ \n") - 1);
	write("| |\\/| | | | | | | | |_) |\n", sizeof("| |\\/| | | | | | | | |_) |\n") - 1);
	write("| |  | | |_| | |_| |  _ < \n", sizeof("| |  | | |_| | |_| |  _ < \n") - 1);
	write("|_|  |_|\\__, |\\___/|_| \\_\\\n", sizeof("|_|  |_|\\__, |\\___/|_| \\_\\\n") - 1);
	write("        |___/                   \n", sizeof("        |___/                   \n") - 1);
	write("\n        Operating System v1.0\n", sizeof("\n        Operating System v1.0\n") - 1);
	write("\nType 'help' for available commands.\n\n",
	      sizeof("\nType 'help' for available commands.\n\n") - 1);
	return 0;
}
