#include <string.h>

char* strchr(const char* str, int c) {
	while (*str != '\0') {
		if (*str == (char)c) {
			return (char*)str;
		}
		str++;
	}
	
	// Check if we're looking for null terminator
	if ((char)c == '\0') {
		return (char*)str;
	}
	
	return NULL;
}
