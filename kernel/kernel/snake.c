#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <kernel/tty.h>
#include <kernel/keyboard.h>
#include <kernel/io.h>
#include <kernel/snake.h>
#include <kernel/graphics.h>

// Game constants (adjusted for 320x200 graphics mode)
#define GAME_WIDTH 39   // 320 / 8 - 2 for borders
#define GAME_HEIGHT 24  // 200 / 8 - 1 for score
#define MAX_SNAKE_LENGTH 500
#define CELL_SIZE 8     // 8x8 pixels per cell

// VGA colors (matching graphics.h colors)
#define COLOR_BLACK         0
#define COLOR_BLUE          1
#define COLOR_GREEN         2
#define COLOR_CYAN          3
#define COLOR_RED           4
#define COLOR_MAGENTA       5
#define COLOR_BROWN         6
#define COLOR_LIGHT_GRAY    7
#define COLOR_DARK_GRAY     8
#define COLOR_LIGHT_BLUE    9
#define COLOR_LIGHT_GREEN   10
#define COLOR_LIGHT_CYAN    11
#define COLOR_LIGHT_RED     12
#define COLOR_LIGHT_MAGENTA 13
#define COLOR_YELLOW        14
#define COLOR_WHITE         15

// Direction enum
typedef enum {
    DIR_UP,
    DIR_DOWN,
    DIR_LEFT,
    DIR_RIGHT
} Direction;

// Position structure
typedef struct {
    int x;
    int y;
} Position;

// Game state
static Position snake[MAX_SNAKE_LENGTH];
static int snake_length;
static Direction direction;
static Direction next_direction;
static Position food;
static int score;
static bool game_over;
static unsigned int rand_seed = 12345;

// Simple random number generator
static unsigned int snake_rand(void) {
    rand_seed = rand_seed * 1103515245 + 12345;
    return (rand_seed / 65536) % 32768;
}

// Draw a filled cell at grid position
static inline void draw_cell(int x, int y, uint8_t color) {
    graphics_fill_rect(x * CELL_SIZE, y * CELL_SIZE, CELL_SIZE, CELL_SIZE, color);
}

static inline void draw_border(void) {
    // Top and bottom borders
    for (int x = 0; x < 40; x++) {
        draw_cell(x, 0, COLOR_LIGHT_CYAN);
        draw_cell(x, 24, COLOR_LIGHT_CYAN);
    }
    
    // Left and right borders
    for (int y = 1; y < 24; y++) {
        draw_cell(0, y, COLOR_LIGHT_CYAN);
        draw_cell(39, y, COLOR_LIGHT_CYAN);
    }
}

static void draw_score(void) {
    // Draw score text at bottom border
    char score_text[30];
    int pos = 0;
    
    const char* label = "Score: ";
    for (int i = 0; label[i]; i++) {
        score_text[pos++] = label[i];
    }
    
    // Convert score to string
    int score_val = score;
    if (score_val == 0) {
        score_text[pos++] = '0';
    } else {
        char temp[10];
        int i = 0;
        while (score_val > 0) {
            temp[i++] = '0' + (score_val % 10);
            score_val /= 10;
        }
        // Reverse
        for (int j = i - 1; j >= 0; j--) {
            score_text[pos++] = temp[j];
        }
    }
    score_text[pos] = '\0';
    
    graphics_print(16, 192, score_text, COLOR_YELLOW, COLOR_BLACK);
}

static void spawn_food(void) {
    bool valid = false;
    
    while (!valid) {
        // Spawn within playable area (avoiding walls at x=0, x=39, y=0, y=24)
        food.x = (snake_rand() % (GAME_WIDTH - 2)) + 1;  // 1 to 37
        food.y = (snake_rand() % (GAME_HEIGHT - 2)) + 1; // 1 to 22
        
        // Check if food spawns on snake
        valid = true;
        for (int i = 0; i < snake_length; i++) {
            if (snake[i].x == food.x && snake[i].y == food.y) {
                valid = false;
                break;
            }
        }
    }
}

static void init_game(void) {
    // Switch to graphics mode
    graphics_set_mode(MODE_13H);
    graphics_clear(COLOR_BLACK);
    
    // Initialize snake in the middle
    snake_length = 3;
    snake[0].x = 20;
    snake[0].y = 12;
    snake[1].x = 19;
    snake[1].y = 12;
    snake[2].x = 18;
    snake[2].y = 12;
    
    direction = DIR_RIGHT;
    next_direction = DIR_RIGHT;
    score = 0;
    game_over = false;
    
    // Spawn initial food
    spawn_food();
    
    // Draw border
    draw_border();
}

static void draw_game(void) {
    // Draw snake
    for (int i = 0; i < snake_length; i++) {
        if (i == 0) {
            draw_cell(snake[i].x, snake[i].y, COLOR_LIGHT_GREEN); // Head
        } else {
            draw_cell(snake[i].x, snake[i].y, COLOR_GREEN); // Body
        }
    }
    
    // Draw food
    draw_cell(food.x, food.y, COLOR_LIGHT_RED);
    
    // Draw score
    draw_score();
}

static void clear_position(int x, int y) {
    draw_cell(x, y, COLOR_BLACK);
}

static bool check_collision(void) {
    Position head = snake[0];
    
    // Check wall collision
    if (head.x <= 0 || head.x >= 39 || head.y <= 0 || head.y >= 24) {
        return true;
    }
    
    // Check self collision
    for (int i = 1; i < snake_length; i++) {
        if (head.x == snake[i].x && head.y == snake[i].y) {
            return true;
        }
    }
    
    return false;
}

static void move_snake(void) {
    // Update direction (can't reverse)
    if (direction == DIR_UP && next_direction != DIR_DOWN) {
        direction = next_direction;
    } else if (direction == DIR_DOWN && next_direction != DIR_UP) {
        direction = next_direction;
    } else if (direction == DIR_LEFT && next_direction != DIR_RIGHT) {
        direction = next_direction;
    } else if (direction == DIR_RIGHT && next_direction != DIR_LEFT) {
        direction = next_direction;
    }
    
    // Calculate new head position
    Position new_head = snake[0];
    
    switch (direction) {
        case DIR_UP:
            new_head.y--;
            break;
        case DIR_DOWN:
            new_head.y++;
            break;
        case DIR_LEFT:
            new_head.x--;
            break;
        case DIR_RIGHT:
            new_head.x++;
            break;
    }
    
    // Check if food is eaten
    bool ate_food = (new_head.x == food.x && new_head.y == food.y);
    
    // Move snake body
    if (!ate_food) {
        // Clear old tail
        clear_position(snake[snake_length - 1].x, snake[snake_length - 1].y);
        
        // Move body segments
        for (int i = snake_length - 1; i > 0; i--) {
            snake[i] = snake[i - 1];
        }
    } else {
        // Grow snake
        if (snake_length < MAX_SNAKE_LENGTH) {
            // Shift all segments
            for (int i = snake_length; i > 0; i--) {
                snake[i] = snake[i - 1];
            }
            snake_length++;
            score += 10;
            spawn_food();
        }
    }
    
    // Update head
    snake[0] = new_head;
    
    // Check collision
    if (check_collision()) {
        game_over = true;
    }
}

static void process_input(void) {
    if (keyboard_has_input()) {
        char c = keyboard_getchar();
        
        // Arrow keys or WASD
        switch (c) {
            case 'w':
            case 'W':
                if (direction != DIR_DOWN) {
                    next_direction = DIR_UP;
                }
                break;
            case 's':
            case 'S':
                if (direction != DIR_UP) {
                    next_direction = DIR_DOWN;
                }
                break;
            case 'a':
            case 'A':
                if (direction != DIR_RIGHT) {
                    next_direction = DIR_LEFT;
                }
                break;
            case 'd':
            case 'D':
                if (direction != DIR_LEFT) {
                    next_direction = DIR_RIGHT;
                }
                break;
            case 'q':
            case 'Q':
            case 27: // ESC
                game_over = true;
                break;
        }
    }
}

static void delay(unsigned int ms) {
    // Simple delay loop (approximate)
    for (unsigned int i = 0; i < ms * 10000; i++) {
        __asm__ volatile ("nop");
    }
}

static void show_game_over(void) {
    const char* msg1 = "GAME OVER!";
    const char* msg2 = "Press any key...";
    
    // Draw messages centered
    graphics_print(120, 80, msg1, COLOR_YELLOW, COLOR_BLACK);
    graphics_print(104, 96, msg2, COLOR_WHITE, COLOR_BLACK);
    
    // Wait for key press
    keyboard_clear_buffer();
    while (!keyboard_has_input()) {
        delay(10);
    }
    keyboard_getchar();
}

void snake_game(void) {
    // Initialize game
    init_game();
    
    // Clear keyboard buffer to prevent leftover input
    keyboard_clear_buffer();
    
    // Main game loop
    while (!game_over) {
        // Process input
        process_input();
        
        // Move snake
        if (!game_over) {
            move_snake();
        }
        
        // Draw everything
        draw_game();
        
        // Delay for game speed (slower = more playable)
        delay(3000);
    }
    
    // Show game over screen
    show_game_over();
    
    // Return to text mode
    graphics_set_mode(MODE_TEXT);
}
