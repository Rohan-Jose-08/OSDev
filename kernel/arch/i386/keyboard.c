#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <kernel/tty.h>
#include <kernel/io.h>

#define KEYBOARD_DATA_PORT 0x60
#define KEYBOARD_STATUS_PORT 0x64

#define KEY_BUFFER_SIZE 256

// Special key codes
#define KEY_UP_ARROW 0x80
#define KEY_DOWN_ARROW 0x81
#define KEY_LEFT_ARROW 0x82
#define KEY_RIGHT_ARROW 0x83
#define KEY_PAGE_UP 0x84
#define KEY_PAGE_DOWN 0x85

static char key_buffer[KEY_BUFFER_SIZE];
static int key_buffer_head = 0;
static int key_buffer_tail = 0;
static bool shift_pressed = false;
static bool caps_lock = false;

// US QWERTY keyboard layout scancode to ASCII mapping
static unsigned char scancode_to_ascii[] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, // Control
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, // Left shift
    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',
    0, // Right shift
    '*',
    0,   // Alt
    ' ', // Space
    0,   // Caps lock
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // F1-F10
    0, // Num lock
    0, // Scroll lock
    0, // Home
    0, // Up arrow
    0, // Page up
    '-',
    0, // Left arrow
    0,
    0, // Right arrow
    '+',
    0, // End
    0, // Down arrow
    0, // Page down
    0, // Insert
    0, // Delete
    0, 0, 0,
    0, 0 // F11, F12
};

static unsigned char scancode_to_ascii_shifted[] = {
    0,  27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, // Control
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, // Left shift
    '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?',
    0, // Right shift
    '*',
    0,   // Alt
    ' ', // Space
    0,   // Caps lock
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // F1-F10
    0, // Num lock
    0, // Scroll lock
    0, // Home
    0, // Up arrow
    0, // Page up
    '-',
    0, // Left arrow
    0,
    0, // Right arrow
    '+',
    0, // End
    0, // Down arrow
    0, // Page down
    0, // Insert
    0, // Delete
    0, 0, 0,
    0, 0 // F11, F12
};

void keyboard_init(void) {
    key_buffer_head = 0;
    key_buffer_tail = 0;
    shift_pressed = false;
    caps_lock = false;
}

void keyboard_handler(void) {
    uint8_t scancode = inb(KEYBOARD_DATA_PORT);
    io_wait();
    
    // Check if key release (bit 7 set)
    if (scancode & 0x80) {
        scancode &= 0x7F;
        // Handle shift release
        if (scancode == 0x2A || scancode == 0x36) {
            shift_pressed = false;
        }
    } else {
        // Key press
        // Handle shift press
        if (scancode == 0x2A || scancode == 0x36) {
            shift_pressed = true;
            return;
        }
        
        // Handle caps lock
        if (scancode == 0x3A) {
            caps_lock = !caps_lock;
            return;
        }
        
        // Handle arrow keys for history navigation
        if (scancode == 0x48) { // Up arrow
            int next_head = (key_buffer_head + 1) % KEY_BUFFER_SIZE;
            if (next_head != key_buffer_tail) {
                key_buffer[key_buffer_head] = KEY_UP_ARROW;
                key_buffer_head = next_head;
            }
            return;
        }
        if (scancode == 0x50) { // Down arrow
            int next_head = (key_buffer_head + 1) % KEY_BUFFER_SIZE;
            if (next_head != key_buffer_tail) {
                key_buffer[key_buffer_head] = KEY_DOWN_ARROW;
                key_buffer_head = next_head;
            }
            return;
        }
        if (scancode == 0x4B) { // Left arrow
            int next_head = (key_buffer_head + 1) % KEY_BUFFER_SIZE;
            if (next_head != key_buffer_tail) {
                key_buffer[key_buffer_head] = KEY_LEFT_ARROW;
                key_buffer_head = next_head;
            }
            return;
        }
        if (scancode == 0x4D) { // Right arrow
            int next_head = (key_buffer_head + 1) % KEY_BUFFER_SIZE;
            if (next_head != key_buffer_tail) {
                key_buffer[key_buffer_head] = KEY_RIGHT_ARROW;
                key_buffer_head = next_head;
            }
            return;
        }
        
        // Convert scancode to ASCII
        char ascii = 0;
        if (scancode < sizeof(scancode_to_ascii)) {
            if (shift_pressed) {
                ascii = scancode_to_ascii_shifted[scancode];
            } else {
                ascii = scancode_to_ascii[scancode];
                // Apply caps lock to letters
                if (caps_lock && ascii >= 'a' && ascii <= 'z') {
                    ascii = ascii - 'a' + 'A';
                }
            }
            
            // Add to buffer if valid character
            if (ascii != 0) {
                int next_head = (key_buffer_head + 1) % KEY_BUFFER_SIZE;
                if (next_head != key_buffer_tail) {
                    key_buffer[key_buffer_head] = ascii;
                    key_buffer_head = next_head;
                }
            }
        }
    }
}

bool keyboard_has_input(void) {
    return key_buffer_head != key_buffer_tail;
}

char keyboard_getchar(void) {
    if (key_buffer_head == key_buffer_tail) {
        return 0; // No input available
    }
    
    char c = key_buffer[key_buffer_tail];
    key_buffer_tail = (key_buffer_tail + 1) % KEY_BUFFER_SIZE;
    return c;
}

void keyboard_clear_buffer(void) {
    key_buffer_head = 0;
    key_buffer_tail = 0;
}
