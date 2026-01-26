#include "shell.h"
#include "memory.h"
#include "cpu.h"
#include "net.h"
#include "fs.h"

static void shell_prompt(shell_t *shell) {
    terminal_print(shell->term, "fusion> ");
}

static void shell_redraw_line(shell_t *shell) {
    terminal_putc(shell->term, '\n');
    shell_prompt(shell);
    terminal_print(shell->term, shell->cmd);
}

static const char *const shell_commands[] = {
    "help",
    "clear",
    "echo",
    "uname",
    "meminfo",
    "heapinfo",
    "malloc",
    "uptime",
    "cpuinfo",
    "color",
    "copy",
    "paste",
    "netinfo",
    "ls",
    "cat",
    "write",
    "append",
    "mkdir",
    "reboot",
    "halt",
    "exit"
};

static void shell_autocomplete(shell_t *shell) {
    for (int i = 0; i < shell->cmd_len; i++) {
        if (shell->cmd[i] == ' ') return;
    }

    int matches = 0;
    const char *first = 0;
    int prefix_len = shell->cmd_len;
    int lcp_len = 0;

    for (u32 i = 0; i < (u32)(sizeof(shell_commands) / sizeof(shell_commands[0])); i++) {
        const char *cmd = shell_commands[i];
        if (strncmp(cmd, shell->cmd, (size_t)prefix_len) == 0) {
            if (matches == 0) {
                first = cmd;
                lcp_len = (int)strlen(cmd);
            } else {
                int j = 0;
                while (j < lcp_len && cmd[j] == first[j]) {
                    j++;
                }
                lcp_len = j;
            }
            matches++;
        }
    }

    if (matches == 0) return;

    if (matches == 1) {
        const char *cmd = first;
        while (shell->cmd_len < MAX_CMD_LEN - 1 && cmd[shell->cmd_len]) {
            char c = cmd[shell->cmd_len];
            shell->cmd[shell->cmd_len++] = c;
            terminal_putc(shell->term, c);
        }
        shell->cmd[shell->cmd_len] = 0;
        return;
    }

    if (lcp_len > prefix_len) {
        while (shell->cmd_len < MAX_CMD_LEN - 1 && shell->cmd_len < lcp_len) {
            char c = first[shell->cmd_len];
            shell->cmd[shell->cmd_len++] = c;
            terminal_putc(shell->term, c);
        }
        shell->cmd[shell->cmd_len] = 0;
        return;
    }

    terminal_putc(shell->term, '\n');
    for (u32 i = 0; i < (u32)(sizeof(shell_commands) / sizeof(shell_commands[0])); i++) {
        const char *cmd = shell_commands[i];
        if (strncmp(cmd, shell->cmd, (size_t)prefix_len) == 0) {
            terminal_print(shell->term, cmd);
            terminal_putc(shell->term, '\n');
        }
    }
    shell_redraw_line(shell);
}

static void print_hex(terminal_t *term, u64 n) {
    char buf[17];
    buf[16] = 0;
    for (int i = 15; i >= 0; i--) {
        int digit = n & 0xF;
        buf[i] = digit < 10 ? '0' + digit : 'a' + digit - 10;
        n >>= 4;
    }
    terminal_print(term, "0x");
    terminal_print(term, buf);
}

static void print_dec(terminal_t *term, u64 n) {
    if (n == 0) {
        terminal_putc(term, '0');
        return;
    }
    char buf[21];
    int i = 20;
    buf[i] = 0;
    while (n > 0) {
        buf[--i] = '0' + (n % 10);
        n /= 10;
    }
    terminal_print(term, &buf[i]);
}

static void print_ip(terminal_t *term, u32 ip) {
    if (ip == 0) {
        terminal_print(term, "0.0.0.0");
        return;
    }
    const u8 *b = (const u8 *)&ip;
    u8 b0 = b[0];
    u8 b1 = b[1];
    u8 b2 = b[2];
    u8 b3 = b[3];
    print_dec(term, b0);
    terminal_putc(term, '.');
    print_dec(term, b1);
    terminal_putc(term, '.');
    print_dec(term, b2);
    terminal_putc(term, '.');
    print_dec(term, b3);
}

static void execute_command(shell_t *shell, char *cmd) {
    char *args[MAX_ARGS];
    int argc = 0;

    char *p = cmd;
    while (*p && argc < MAX_ARGS) {
        while (*p == ' ') p++;
        if (!*p) break;
        args[argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = 0;
    }

    if (argc == 0) return;

    if (strcmp(args[0], "help") == 0) {
        terminal_print(shell->term, "Available commands:\n");
        terminal_print(shell->term, "  help      - Show this help message\n");
        terminal_print(shell->term, "  clear     - Clear the screen\n");
        terminal_print(shell->term, "  echo      - Print arguments\n");
        terminal_print(shell->term, "  uname     - Show system information\n");
        terminal_print(shell->term, "  meminfo   - Display physical memory information\n");
        terminal_print(shell->term, "  heapinfo  - Display heap allocator statistics\n");
        terminal_print(shell->term, "  malloc    - Test memory allocation (malloc <size>)\n");
        terminal_print(shell->term, "  uptime    - Show system uptime\n");
        terminal_print(shell->term, "  cpuinfo   - Display CPU information\n");
        terminal_print(shell->term, "  color     - Set terminal text color\n");
        terminal_print(shell->term, "  copy      - Copy visible terminal text\n");
        terminal_print(shell->term, "  paste     - Paste copied text\n");
        terminal_print(shell->term, "  netinfo   - Show network status\n");
        terminal_print(shell->term, "  ls        - List directory (ls [path])\n");
        terminal_print(shell->term, "  cat       - Show file contents (cat <path>)\n");
        terminal_print(shell->term, "  write     - Write file (write <path> <text>)\n");
        terminal_print(shell->term, "  append    - Append file (append <path> <text>)\n");
        terminal_print(shell->term, "  mkdir     - Create directory (mkdir <path>)\n");
        terminal_print(shell->term, "  reboot    - Reboot the system\n");
        terminal_print(shell->term, "  halt      - Halt the system\n");
        terminal_print(shell->term, "  exit      - Close the terminal\n");
    } else if (strcmp(args[0], "clear") == 0) {
        terminal_clear(shell->term);
    } else if (strcmp(args[0], "echo") == 0) {
        for (int i = 1; i < argc; i++) {
            terminal_print(shell->term, args[i]);
            if (i < argc - 1) terminal_putc(shell->term, ' ');
        }
        terminal_putc(shell->term, '\n');
    } else if (strcmp(args[0], "uname") == 0) {
        terminal_print(shell->term, "Fusion OS v1.0 x86_64\n");
    } else if (strcmp(args[0], "meminfo") == 0) {
        terminal_print(shell->term, "Physical Memory Information:\n");
        terminal_print(shell->term, "  Total Pages: ");
        print_dec(shell->term, pmm_total_pages);
        terminal_print(shell->term, "\n  Free Pages:  ");
        print_dec(shell->term, pmm_free_pages);
        terminal_print(shell->term, "\n  Used Pages:  ");
        print_dec(shell->term, pmm_used_pages);
        terminal_putc(shell->term, '\n');
    } else if (strcmp(args[0], "heapinfo") == 0) {
        terminal_print(shell->term, "Heap Allocator Statistics:\n");
        terminal_print(shell->term, "  Heap Size:       ");
        print_dec(shell->term, HEAP_SIZE / 1024);
        terminal_print(shell->term, " KB\n  Allocated:       ");
        print_dec(shell->term, heap_allocated);
        terminal_print(shell->term, " bytes\n  Freed:           ");
        print_dec(shell->term, heap_freed);
        terminal_print(shell->term, " bytes\n  Currently Used:  ");
        print_dec(shell->term, heap_allocated - heap_freed);
        terminal_print(shell->term, " bytes\n  Active Blocks:   ");
        print_dec(shell->term, heap_blocks);
        terminal_putc(shell->term, '\n');
    } else if (strcmp(args[0], "malloc") == 0) {
        if (argc < 2) {
            terminal_print(shell->term, "Usage: malloc <size>\n");
        } else {
            u64 size = 0;
            for (char *q = args[1]; *q; q++) {
                if (*q >= '0' && *q <= '9') {
                    size = size * 10 + (*q - '0');
                }
            }
            void *ptr = malloc((size_t)size);
            if (ptr) {
                terminal_print(shell->term, "Allocated ");
                print_dec(shell->term, size);
                terminal_print(shell->term, " bytes at ");
                print_hex(shell->term, (u64)ptr);
                terminal_print(shell->term, "\nMemory freed\n");
                free(ptr);
            } else {
                terminal_print(shell->term, "Allocation failed!\n");
            }
        }
    } else if (strcmp(args[0], "uptime") == 0) {
        terminal_print(shell->term, "System Uptime: ");
        u64 hours = uptime_seconds / 3600;
        u64 minutes = (uptime_seconds % 3600) / 60;
        u64 seconds = uptime_seconds % 60;
        print_dec(shell->term, hours);
        terminal_print(shell->term, "h ");
        print_dec(shell->term, minutes);
        terminal_print(shell->term, "m ");
        print_dec(shell->term, seconds);
        terminal_print(shell->term, "s\nTicks: ");
        print_dec(shell->term, ticks);
        terminal_putc(shell->term, '\n');
    } else if (strcmp(args[0], "cpuinfo") == 0) {
        terminal_print(shell->term, "CPU Information:\n  Vendor: ");
        char vendor[13];
        cpu_get_vendor(vendor);
        terminal_print(shell->term, vendor);
        terminal_putc(shell->term, '\n');
    } else if (strcmp(args[0], "color") == 0) {
        if (argc < 2) {
            terminal_print(shell->term, "Usage: color <white|red|green|blue|cyan|yellow|magenta|orange|pink|lime|gray|reset>\n");
        } else {
            if (strcmp(args[1], "white") == 0) shell->term->fg = COLOR_WHITE;
            else if (strcmp(args[1], "red") == 0) shell->term->fg = COLOR_RED;
            else if (strcmp(args[1], "green") == 0) shell->term->fg = COLOR_GREEN;
            else if (strcmp(args[1], "blue") == 0) shell->term->fg = COLOR_BLUE;
            else if (strcmp(args[1], "cyan") == 0) shell->term->fg = COLOR_CYAN;
            else if (strcmp(args[1], "yellow") == 0) shell->term->fg = COLOR_YELLOW;
            else if (strcmp(args[1], "magenta") == 0) shell->term->fg = COLOR_MAGENTA;
            else if (strcmp(args[1], "orange") == 0) shell->term->fg = COLOR_ORANGE;
            else if (strcmp(args[1], "pink") == 0) shell->term->fg = COLOR_PINK;
            else if (strcmp(args[1], "lime") == 0) shell->term->fg = COLOR_LIME;
            else if (strcmp(args[1], "gray") == 0) shell->term->fg = 0xAAAAAA;
            else if (strcmp(args[1], "reset") == 0) shell->term->fg = TERM_DEFAULT_FG;
            else terminal_print(shell->term, "Unknown color\n");
        }
    } else if (strcmp(args[0], "copy") == 0) {
        terminal_copy_visible(shell->term);
        terminal_print(shell->term, "Copied visible text\n");
    } else if (strcmp(args[0], "paste") == 0) {
        terminal_paste(shell->term);
    } else if (strcmp(args[0], "netinfo") == 0) {
        terminal_print(shell->term, "Network:\n  IP: ");
        print_ip(shell->term, net_get_ip());
        terminal_print(shell->term, "\n  Netmask: ");
        print_ip(shell->term, net_get_netmask());
        terminal_print(shell->term, "\n  Gateway: ");
        print_ip(shell->term, net_get_gateway());
        terminal_print(shell->term, "\n  DNS: ");
        print_ip(shell->term, net_get_dns());
        terminal_putc(shell->term, '\n');
    } else if (strcmp(args[0], "ls") == 0) {
        const char *path = (argc > 1) ? args[1] : "/";
        fs_entry_t entries[64];
        int count = 0;
        if (fs_list_dir(path, entries, 64, &count)) {
            for (int i = 0; i < count; i++) {
                terminal_print(shell->term, entries[i].is_dir ? "[D] " : "    ");
                terminal_print(shell->term, entries[i].name);
                terminal_putc(shell->term, '\n');
            }
        } else {
            terminal_print(shell->term, "ls: failed\n");
        }
    } else if (strcmp(args[0], "cat") == 0) {
        if (argc < 2) {
            terminal_print(shell->term, "Usage: cat <path>\n");
        } else {
            u8 *data = 0;
            u32 len = 0;
            if (fs_read_file(args[1], &data, &len)) {
                terminal_print(shell->term, (char *)data);
                terminal_putc(shell->term, '\n');
                free(data);
            } else {
                terminal_print(shell->term, "cat: failed\n");
            }
        }
    } else if (strcmp(args[0], "write") == 0) {
        if (argc < 3) {
            terminal_print(shell->term, "Usage: write <path> <text>\n");
        } else {
            if (fs_write_file(args[1], (const u8 *)args[2], (u32)strlen(args[2]))) {
                terminal_print(shell->term, "write: ok\n");
            } else {
                terminal_print(shell->term, "write: failed\n");
            }
        }
    } else if (strcmp(args[0], "append") == 0) {
        if (argc < 3) {
            terminal_print(shell->term, "Usage: append <path> <text>\n");
        } else {
            if (fs_append_file(args[1], (const u8 *)args[2], (u32)strlen(args[2]))) {
                terminal_print(shell->term, "append: ok\n");
            } else {
                terminal_print(shell->term, "append: failed\n");
            }
        }
    } else if (strcmp(args[0], "mkdir") == 0) {
        if (argc < 2) {
            terminal_print(shell->term, "Usage: mkdir <path>\n");
        } else {
            if (fs_mkdir(args[1])) {
                terminal_print(shell->term, "mkdir: ok\n");
            } else {
                terminal_print(shell->term, "mkdir: failed\n");
            }
        }
    } else if (strcmp(args[0], "reboot") == 0) {
        terminal_print(shell->term, "Rebooting...\n");
        reboot();
    } else if (strcmp(args[0], "halt") == 0) {
        terminal_print(shell->term, "System halted.\n");
        halt();
    } else if (strcmp(args[0], "exit") == 0) {
        terminal_print(shell->term, "Closing terminal...\n");
        shell->exit_requested = 1;
    } else {
        terminal_print(shell->term, "Unknown command: ");
        terminal_print(shell->term, args[0]);
        terminal_print(shell->term, "\nType 'help' for available commands\n");
    }
}

void shell_init(shell_t *shell, terminal_t *term) {
    shell->term = term;
    shell->cmd_len = 0;
    shell->history_count = 0;
    shell->history_index = 0;
    shell->exit_requested = 0;
    terminal_print(shell->term, "Welcome to Fusion OS\nType 'help' for available commands\n\n");
    shell_prompt(shell);
}

static void shell_set_command(shell_t *shell, const char *cmd) {
    shell->cmd_len = 0;
    shell->cmd[0] = 0;
    terminal_print(shell->term, "\n");
    shell_prompt(shell);
    while (*cmd && shell->cmd_len < MAX_CMD_LEN - 1) {
        shell->cmd[shell->cmd_len++] = *cmd;
        terminal_putc(shell->term, *cmd);
        cmd++;
    }
    shell->cmd[shell->cmd_len] = 0;
}

void shell_handle_key(shell_t *shell, const key_event_t *event) {
    if (!event->pressed) return;

    if (event->ascii == '\t') {
        shell_autocomplete(shell);
        return;
    }

    if (event->keycode == KEY_UP) {
        if (shell->history_count > 0 && shell->history_index > 0) {
            shell->history_index--;
            shell_set_command(shell, shell->history[shell->history_index]);
        }
        return;
    }
    if (event->keycode == KEY_DOWN) {
        if (shell->history_index + 1 < shell->history_count) {
            shell->history_index++;
            shell_set_command(shell, shell->history[shell->history_index]);
        }
        return;
    }
    if (event->keycode == KEY_BACKSPACE) {
        if (shell->cmd_len > 0) {
            shell->cmd_len--;
            shell->cmd[shell->cmd_len] = 0;
            terminal_putc(shell->term, '\b');
        }
        return;
    }
    if (event->keycode == KEY_ENTER) {
        terminal_putc(shell->term, '\n');
        shell->cmd[shell->cmd_len] = 0;
        if (shell->cmd_len > 0 && shell->history_count < MAX_HISTORY) {
            strcpy(shell->history[shell->history_count], shell->cmd);
            shell->history_count++;
        }
        shell->history_index = shell->history_count;
        execute_command(shell, shell->cmd);
        if (shell->exit_requested) return;
        shell->cmd_len = 0;
        shell->cmd[0] = 0;
        shell_prompt(shell);
        return;
    }

    if (event->ascii && shell->cmd_len < MAX_CMD_LEN - 1) {
        shell->cmd[shell->cmd_len++] = event->ascii;
        shell->cmd[shell->cmd_len] = 0;
        terminal_putc(shell->term, event->ascii);
    }
}

int shell_should_exit(const shell_t *shell) {
    return shell->exit_requested;
}
