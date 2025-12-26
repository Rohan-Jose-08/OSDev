#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static int parse_number(const char** format) {
	int num = 0;
	while (**format >= '0' && **format <= '9') {
		num = num * 10 + (**format - '0');
		(*format)++;
	}
	return num;
}

// Simple snprintf implementation
int snprintf(char* buffer, size_t size, const char* format, ...) {
	va_list parameters;
	va_start(parameters, format);
	
	size_t written = 0;
	
	while (*format != '\0' && written < size - 1) {
		if (format[0] != '%' || format[1] == '%') {
			if (format[0] == '%')
				format++;
			buffer[written++] = *format;
			format++;
			continue;
		}
		
		format++;
		
		// Parse flags
		bool left_justify = false;
		if (*format == '-') {
			left_justify = true;
			format++;
		}
		
		// Parse width
		int width = 0;
		if (*format >= '0' && *format <= '9') {
			width = parse_number(&format);
		}
		
		if (*format == 'c') {
			format++;
			char c = (char) va_arg(parameters, int);
			if (written < size - 1) buffer[written++] = c;
		}
		else if (*format == 's') {
			format++;
			const char* str = va_arg(parameters, const char*);
			size_t len = strlen(str);
			
			if (!left_justify && width > 0 && (int)len < width) {
				// Right justify - add padding first
				for (int i = 0; i < width - (int)len && written < size - 1; i++) {
					buffer[written++] = ' ';
				}
			}
			
			// Copy string
			for (size_t i = 0; i < len && written < size - 1; i++) {
				buffer[written++] = str[i];
			}
			
			if (left_justify && width > 0 && (int)len < width) {
				// Left justify - add padding after
				for (int i = 0; i < width - (int)len && written < size - 1; i++) {
					buffer[written++] = ' ';
				}
			}
		}
		else if (*format == 'd' || *format == 'i') {
			format++;
			int value = va_arg(parameters, int);
			char num_buf[32];
			int pos = 0;
			bool is_negative = false;
			
			if (value < 0) {
				is_negative = true;
				unsigned int uvalue;
				if (value == INT32_MIN) {
					uvalue = (unsigned int)(-(value + 1)) + 1;
				} else {
					uvalue = (unsigned int)(-value);
				}
				value = uvalue;
			}
			
			unsigned int uval = (unsigned int)value;
			if (uval == 0) {
				num_buf[pos++] = '0';
			} else {
				while (uval > 0) {
					num_buf[pos++] = '0' + (uval % 10);
					uval /= 10;
				}
			}
			
			int num_len = pos + (is_negative ? 1 : 0);
			
			// Handle width padding
			if (!left_justify && width > num_len) {
				for (int i = 0; i < width - num_len && written < size - 1; i++) {
					buffer[written++] = ' ';
				}
			}
			
			if (is_negative && written < size - 1) {
				buffer[written++] = '-';
			}
			
			for (int i = pos - 1; i >= 0 && written < size - 1; i--) {
				buffer[written++] = num_buf[i];
			}
			
			if (left_justify && width > num_len) {
				for (int i = 0; i < width - num_len && written < size - 1; i++) {
					buffer[written++] = ' ';
				}
			}
		}
		else if (*format == 'u') {
			format++;
			unsigned int value = va_arg(parameters, unsigned int);
			char num_buf[32];
			int pos = 0;
			
			if (value == 0) {
				num_buf[pos++] = '0';
			} else {
				while (value > 0) {
					num_buf[pos++] = '0' + (value % 10);
					value /= 10;
				}
			}
			
			// Handle width padding
			if (!left_justify && width > pos) {
				for (int i = 0; i < width - pos && written < size - 1; i++) {
					buffer[written++] = ' ';
				}
			}
			
			for (int i = pos - 1; i >= 0 && written < size - 1; i--) {
				buffer[written++] = num_buf[i];
			}
			
			if (left_justify && width > pos) {
				for (int i = 0; i < width - pos && written < size - 1; i++) {
					buffer[written++] = ' ';
				}
			}
		}
		else if (*format == 'x' || *format == 'X') {
			format++;
			unsigned int value = va_arg(parameters, unsigned int);
			char num_buf[32];
			int pos = 0;
			char base = (*(format - 1) == 'X') ? 'A' : 'a';

			if (value == 0) {
				num_buf[pos++] = '0';
			} else {
				while (value > 0) {
					int digit = value % 16;
					if (digit < 10) {
						num_buf[pos++] = '0' + digit;
					} else {
						num_buf[pos++] = base + (digit - 10);
					}
					value /= 16;
				}
			}

			// Handle width padding
			if (!left_justify && width > pos) {
				for (int i = 0; i < width - pos && written < size - 1; i++) {
					buffer[written++] = ' ';
				}
			}

			for (int i = pos - 1; i >= 0 && written < size - 1; i--) {
				buffer[written++] = num_buf[i];
			}

			if (left_justify && width > pos) {
				for (int i = 0; i < width - pos && written < size - 1; i++) {
					buffer[written++] = ' ';
				}
			}
		}
		else {
			format++;
		}
	}
	
	buffer[written] = '\0';
	va_end(parameters);
	return (int)written;
}
