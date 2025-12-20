#include <string.h>

char* strrchr(const char* str, int c) {
	const char* last = NULL;
	
	while (*str) {
		if (*str == (char)c) {
			last = str;
		}
		str++;
	}
	
	if ((char)c == '\0') {
		return (char*)str;
	}
	
	return (char*)last;
}
