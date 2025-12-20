#include <string.h>

char* strncpy(char* dest, const char* src, size_t n) {
	char* d = dest;
	
	while (n > 0 && *src) {
		*d++ = *src++;
		n--;
	}
	
	while (n > 0) {
		*d++ = '\0';
		n--;
	}
	
	return dest;
}
