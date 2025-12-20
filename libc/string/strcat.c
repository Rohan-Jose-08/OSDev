#include <string.h>

char* strcat(char* dest, const char* src) {
	char* ret = dest;
	
	// Find the end of dest
	while (*dest) {
		dest++;
	}
	
	// Copy src to the end of dest
	while (*src) {
		*dest++ = *src++;
	}
	
	*dest = '\0';
	return ret;
}
