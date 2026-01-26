#ifndef SHELL_H
#define SHELL_H

#include "terminal.h"
#include "input.h"

#define MAX_CMD_LEN 256
#define MAX_ARGS 16
#define MAX_HISTORY 10

typedef struct {
    terminal_t *term;
    char cmd[MAX_CMD_LEN];
    int cmd_len;
    char history[MAX_HISTORY][MAX_CMD_LEN];
    int history_count;
    int history_index;
    int exit_requested;
} shell_t;

void shell_init(shell_t *shell, terminal_t *term);
void shell_handle_key(shell_t *shell, const key_event_t *event);
int shell_should_exit(const shell_t *shell);

#endif
