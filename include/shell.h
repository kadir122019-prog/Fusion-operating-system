#ifndef SHELL_H
#define SHELL_H

#define MAX_CMD_LEN 256
#define MAX_ARGS 16

void shell_run(void);
void execute_command(char *cmd);

#endif
