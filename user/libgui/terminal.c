#include <dirent.h>
#include <gui_window.h>
#include <graphics.h>
#include <mouse.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define TERM_MAX_LINES 200
#define TERM_LINE_LEN 128
#define TERM_HISTORY 16
#define TERM_PADDING 4
#define TERM_INPUT_HEIGHT 16
#define TERM_LINE_HEIGHT 10
#define TERM_PROMPT "> "

typedef struct {
    char lines[TERM_MAX_LINES][TERM_LINE_LEN];
    int line_count;
    int scroll_offset;
    char input[TERM_LINE_LEN];
    int input_cursor;
    char history[TERM_HISTORY][TERM_LINE_LEN];
    int history_count;
    int history_index;
    int cols;
    window_t* win;
} terminal_state_t;

static window_t* terminal_window = NULL;
static terminal_state_t terminal_state;

static void term_add_line(terminal_state_t* state, const char* text) {
    if (!state || !text) {
        return;
    }
    if (state->line_count >= TERM_MAX_LINES) {
        for (int i = 1; i < state->line_count; i++) {
            strcpy(state->lines[i - 1], state->lines[i]);
        }
        state->line_count--;
        if (state->scroll_offset > 0) {
            state->scroll_offset--;
        }
    }
    strncpy(state->lines[state->line_count], text, TERM_LINE_LEN - 1);
    state->lines[state->line_count][TERM_LINE_LEN - 1] = '\0';
    state->line_count++;
}

static void term_add_wrapped(terminal_state_t* state, const char* text) {
    if (!state || !text) {
        return;
    }
    int cols = state->cols;
    if (cols < 1) cols = 1;
    if (cols > TERM_LINE_LEN - 1) cols = TERM_LINE_LEN - 1;

    char line[TERM_LINE_LEN];
    int len = 0;
    for (int i = 0; ; i++) {
        char c = text[i];
        if (c == '\0' || c == '\n') {
            line[len] = '\0';
            term_add_line(state, line);
            len = 0;
            if (c == '\0') {
                break;
            }
            continue;
        }
        if (len >= cols) {
            line[len] = '\0';
            term_add_line(state, line);
            len = 0;
        }
        if (len < TERM_LINE_LEN - 1) {
            line[len++] = c;
        }
    }
}

static void term_history_add(terminal_state_t* state, const char* text) {
    if (!state || !text || text[0] == '\0') {
        return;
    }
    if (state->history_count >= TERM_HISTORY) {
        for (int i = 1; i < state->history_count; i++) {
            strcpy(state->history[i - 1], state->history[i]);
        }
        state->history_count--;
    }
    strncpy(state->history[state->history_count], text, TERM_LINE_LEN - 1);
    state->history[state->history_count][TERM_LINE_LEN - 1] = '\0';
    state->history_count++;
    state->history_index = -1;
}

static char* term_skip_spaces(char* s) {
    if (!s) return s;
    while (*s == ' ') {
        s++;
    }
    return s;
}

static char* term_next_token(char** cursor) {
    if (!cursor || !*cursor) {
        return NULL;
    }
    char* s = term_skip_spaces(*cursor);
    if (*s == '\0') {
        *cursor = s;
        return NULL;
    }
    char* start = s;
    while (*s && *s != ' ') {
        s++;
    }
    if (*s) {
        *s = '\0';
        s++;
    }
    *cursor = s;
    return start;
}

static int term_resolve_path(const char* arg, char* out, size_t out_size) {
    if (!out || out_size == 0) {
        return -1;
    }
    if (!arg || arg[0] == '\0') {
        return (getcwd(out, (uint32_t)out_size) < 0) ? -1 : 0;
    }
    if (arg[0] == '/') {
        if (strlen(arg) + 1 > out_size) {
            return -1;
        }
        strcpy(out, arg);
        return 0;
    }
    char cwd[128];
    if (getcwd(cwd, sizeof(cwd)) < 0) {
        return -1;
    }
    size_t cwd_len = strlen(cwd);
    size_t arg_len = strlen(arg);
    if (strcmp(cwd, "/") == 0) {
        if (arg_len + 2 > out_size) {
            return -1;
        }
        out[0] = '/';
        memcpy(out + 1, arg, arg_len);
        out[arg_len + 1] = '\0';
        return 0;
    }
    if (cwd_len + 1 + arg_len + 1 > out_size) {
        return -1;
    }
    memcpy(out, cwd, cwd_len);
    out[cwd_len] = '/';
    memcpy(out + cwd_len + 1, arg, arg_len);
    out[cwd_len + 1 + arg_len] = '\0';
    return 0;
}

static void term_clear_output(terminal_state_t* state) {
    if (!state) return;
    state->line_count = 0;
    state->scroll_offset = 0;
}

static void term_cmd_help(terminal_state_t* state) {
    term_add_wrapped(state, "Commands: help, clear, pwd, cd, ls, cat, echo, exit");
}

static void term_cmd_pwd(terminal_state_t* state) {
    char cwd[128];
    if (getcwd(cwd, sizeof(cwd)) < 0) {
        term_add_wrapped(state, "pwd: failed");
        return;
    }
    term_add_wrapped(state, cwd);
}

static void term_cmd_cd(terminal_state_t* state, const char* arg) {
    char path[128];
    if (term_resolve_path(arg, path, sizeof(path)) < 0) {
        term_add_wrapped(state, "cd: invalid path");
        return;
    }
    if (setcwd(path) < 0) {
        term_add_wrapped(state, "cd: failed");
        return;
    }
}

static void term_cmd_ls(terminal_state_t* state, const char* arg) {
    char path[128];
    if (term_resolve_path(arg, path, sizeof(path)) < 0) {
        term_add_wrapped(state, "ls: invalid path");
        return;
    }
    struct dirent entries[64];
    int count = listdir(path, entries, 64);
    if (count < 0) {
        term_add_wrapped(state, "ls: failed");
        return;
    }
    for (int i = 0; i < count; i++) {
        char line[TERM_LINE_LEN];
        const char* name = entries[i].d_name;
        if (entries[i].d_type == 2) {
            snprintf(line, sizeof(line), "%s/", name);
        } else {
            snprintf(line, sizeof(line), "%s", name);
        }
        term_add_wrapped(state, line);
    }
}

static void term_cmd_cat(terminal_state_t* state, const char* arg) {
    if (!arg || arg[0] == '\0') {
        term_add_wrapped(state, "cat: missing file");
        return;
    }
    char path[128];
    if (term_resolve_path(arg, path, sizeof(path)) < 0) {
        term_add_wrapped(state, "cat: invalid path");
        return;
    }
    int fd = open(path);
    if (fd < 0) {
        term_add_wrapped(state, "cat: open failed");
        return;
    }
    char buf[128];
    char line[TERM_LINE_LEN];
    int line_len = 0;
    while (1) {
        int n = read(fd, buf, sizeof(buf));
        if (n <= 0) break;
        for (int i = 0; i < n; i++) {
            char c = buf[i];
            if (c == '\r') {
                continue;
            }
            if (c == '\n') {
                line[line_len] = '\0';
                term_add_wrapped(state, line);
                line_len = 0;
                continue;
            }
            if (line_len >= TERM_LINE_LEN - 1) {
                line[line_len] = '\0';
                term_add_wrapped(state, line);
                line_len = 0;
            }
            if ((unsigned char)c < 32) {
                c = '.';
            }
            line[line_len++] = c;
        }
    }
    if (line_len > 0) {
        line[line_len] = '\0';
        term_add_wrapped(state, line);
    }
    close(fd);
}

static void term_cmd_echo(terminal_state_t* state, char* rest) {
    if (!rest) {
        return;
    }
    rest = term_skip_spaces(rest);
    if (*rest == '\0') {
        return;
    }
    term_add_wrapped(state, rest);
}

static void term_execute(terminal_state_t* state) {
    char input[TERM_LINE_LEN];
    strncpy(input, state->input, sizeof(input) - 1);
    input[sizeof(input) - 1] = '\0';

    char* cursor = input;
    char* cmd = term_next_token(&cursor);
    if (!cmd || cmd[0] == '\0') {
        return;
    }

    if (strcmp(cmd, "help") == 0) {
        term_cmd_help(state);
    } else if (strcmp(cmd, "clear") == 0) {
        term_clear_output(state);
    } else if (strcmp(cmd, "pwd") == 0) {
        term_cmd_pwd(state);
    } else if (strcmp(cmd, "cd") == 0) {
        char* arg = term_next_token(&cursor);
        term_cmd_cd(state, arg);
    } else if (strcmp(cmd, "ls") == 0) {
        char* arg = term_next_token(&cursor);
        term_cmd_ls(state, arg);
    } else if (strcmp(cmd, "cat") == 0) {
        char* arg = term_next_token(&cursor);
        term_cmd_cat(state, arg);
    } else if (strcmp(cmd, "echo") == 0) {
        term_cmd_echo(state, cursor);
    } else if (strcmp(cmd, "exit") == 0) {
        window_destroy(state->win);
        terminal_window = NULL;
        return;
    } else {
        term_add_wrapped(state, "Unknown command. Type 'help'.");
    }
}

static void term_input_insert(terminal_state_t* state, const char* text) {
    if (!state || !text) {
        return;
    }
    char filtered[TERM_LINE_LEN];
    int j = 0;
    for (int i = 0; text[i] && j < TERM_LINE_LEN - 1; i++) {
        if (text[i] == '\n' || text[i] == '\r') {
            continue;
        }
        filtered[j++] = text[i];
    }
    filtered[j] = '\0';
    if (filtered[0] == '\0') {
        return;
    }

    int len = strlen(state->input);
    int cur = state->input_cursor;
    if (cur < 0) cur = 0;
    if (cur > len) cur = len;

    int space = TERM_LINE_LEN - 1 - len;
    int insert_len = strlen(filtered);
    if (space <= 0) {
        return;
    }
    if (insert_len > space) {
        insert_len = space;
    }

    for (int i = len; i >= cur; i--) {
        state->input[i + insert_len] = state->input[i];
    }
    memcpy(state->input + cur, filtered, (size_t)insert_len);
    state->input_cursor = cur + insert_len;
}

static void terminal_on_draw(window_t* win) {
    terminal_state_t* state = (terminal_state_t*)window_get_user_data(win);
    int content_w = window_content_width(win);
    int content_h = window_content_height(win);

    int cols = (content_w - TERM_PADDING * 2) / 8;
    if (cols < 1) cols = 1;
    state->cols = cols;

    int input_y = content_h - TERM_INPUT_HEIGHT;
    int rows = (input_y - TERM_PADDING) / TERM_LINE_HEIGHT;
    if (rows < 1) rows = 1;

    window_clear_content(win, COLOR_BLACK);

    int start = state->line_count - rows - state->scroll_offset;
    if (start < 0) start = 0;
    if (start > state->line_count) start = state->line_count;

    int y = TERM_PADDING;
    for (int i = start; i < state->line_count && y < input_y; i++) {
        window_print(win, TERM_PADDING, y, state->lines[i], COLOR_LIGHT_GRAY);
        y += TERM_LINE_HEIGHT;
    }

    window_fill_rect(win, 0, input_y, content_w, TERM_INPUT_HEIGHT, COLOR_DARK_GRAY);
    window_draw_rect(win, 0, input_y, content_w, TERM_INPUT_HEIGHT, COLOR_BLACK);

    int prompt_x = TERM_PADDING;
    int prompt_y = input_y + 4;
    window_print(win, prompt_x, prompt_y, TERM_PROMPT, COLOR_WHITE);

    int input_x = prompt_x + (int)strlen(TERM_PROMPT) * 8;
    window_print(win, input_x, prompt_y, state->input, COLOR_WHITE);

    int cursor_x = input_x + state->input_cursor * 8;
    if (cursor_x < content_w - 1) {
        window_fill_rect(win, cursor_x, input_y + 2, 2, TERM_INPUT_HEIGHT - 4, COLOR_WHITE);
    }
}

static void terminal_on_mouse_down(window_t* win, int x, int y, int buttons) {
    terminal_state_t* state = (terminal_state_t*)window_get_user_data(win);
    if (!state || !(buttons & MOUSE_LEFT_BUTTON)) {
        return;
    }
    int content_h = window_content_height(win);
    int input_y = content_h - TERM_INPUT_HEIGHT;
    if (y < input_y) {
        return;
    }
    int prompt_x = TERM_PADDING;
    int input_x = prompt_x + (int)strlen(TERM_PROMPT) * 8;
    int col = (x - input_x) / 8;
    int len = strlen(state->input);
    if (col < 0) col = 0;
    if (col > len) col = len;
    state->input_cursor = col;
    state->history_index = -1;
    terminal_on_draw(win);
}

static void terminal_on_scroll(window_t* win, int delta) {
    terminal_state_t* state = (terminal_state_t*)window_get_user_data(win);
    if (!state) return;
    int content_h = window_content_height(win);
    int input_y = content_h - TERM_INPUT_HEIGHT;
    int rows = (input_y - TERM_PADDING) / TERM_LINE_HEIGHT;
    if (rows < 1) rows = 1;
    int max_scroll = state->line_count - rows;
    if (max_scroll < 0) max_scroll = 0;

    state->scroll_offset += delta;
    if (state->scroll_offset < 0) state->scroll_offset = 0;
    if (state->scroll_offset > max_scroll) state->scroll_offset = max_scroll;
    terminal_on_draw(win);
}

static void terminal_on_key(window_t* win, int key) {
    terminal_state_t* state = (terminal_state_t*)window_get_user_data(win);
    if (!state) return;

    if (key == 0x03) {
        uwm_clipboard_set(state->input);
        return;
    } else if (key == 0x18) {
        uwm_clipboard_set(state->input);
        state->input[0] = '\0';
        state->input_cursor = 0;
        state->history_index = -1;
    } else if (key == 0x16) {
        char clip[TERM_LINE_LEN];
        if (uwm_clipboard_get(clip, sizeof(clip)) > 0) {
            term_input_insert(state, clip);
        }
        state->history_index = -1;
    } else if ((uint8_t)key == 0x82) {
        if (state->input_cursor > 0) {
            state->input_cursor--;
        }
        state->history_index = -1;
    } else if ((uint8_t)key == 0x83) {
        int len = strlen(state->input);
        if (state->input_cursor < len) {
            state->input_cursor++;
        }
        state->history_index = -1;
    } else if ((uint8_t)key == 0x80) {
        if (state->history_count > 0) {
            if (state->history_index < 0) {
                state->history_index = state->history_count - 1;
            } else if (state->history_index > 0) {
                state->history_index--;
            }
            strcpy(state->input, state->history[state->history_index]);
            state->input_cursor = strlen(state->input);
        }
    } else if ((uint8_t)key == 0x81) {
        if (state->history_count > 0 && state->history_index >= 0) {
            if (state->history_index < state->history_count - 1) {
                state->history_index++;
                strcpy(state->input, state->history[state->history_index]);
            } else {
                state->history_index = -1;
                state->input[0] = '\0';
            }
            state->input_cursor = strlen(state->input);
        }
    } else if (key == '\n' || key == '\r') {
        char line[TERM_LINE_LEN + 4];
        snprintf(line, sizeof(line), "%s%s", TERM_PROMPT, state->input);
        term_add_wrapped(state, line);
        term_history_add(state, state->input);
        term_execute(state);
        if (!terminal_window || !uwm_window_is_open(terminal_window)) {
            return;
        }
        state->input[0] = '\0';
        state->input_cursor = 0;
        state->history_index = -1;
        state->scroll_offset = 0;
    } else if (key == 8 || key == 127) {
        int len = strlen(state->input);
        if (state->input_cursor > 0 && len > 0) {
            for (int i = state->input_cursor - 1; i < len; i++) {
                state->input[i] = state->input[i + 1];
            }
            state->input_cursor--;
        }
        state->history_index = -1;
    } else if (key >= 32 && key < 127) {
        char insert[2] = {(char)key, '\0'};
        term_input_insert(state, insert);
        state->history_index = -1;
    }

    terminal_on_draw(win);
}

window_t* gui_terminal_create_window(int x, int y) {
    if (terminal_window && uwm_window_is_open(terminal_window)) {
        return terminal_window;
    }
    window_t* win = window_create(x, y, 280, 200, "Terminal");
    if (!win) return NULL;

    memset(&terminal_state, 0, sizeof(terminal_state));
    terminal_state.win = win;
    terminal_state.history_index = -1;
    terminal_state.cols = (window_content_width(win) - TERM_PADDING * 2) / 8;
    if (terminal_state.cols < 1) terminal_state.cols = 1;

    term_add_wrapped(&terminal_state, "RohanOS GUI Terminal");
    term_add_wrapped(&terminal_state, "Type 'help' for commands.");

    window_set_handlers(win, terminal_on_draw, terminal_on_mouse_down, NULL, NULL,
                        terminal_on_scroll, terminal_on_key, &terminal_state);
    terminal_window = win;
    return win;
}
