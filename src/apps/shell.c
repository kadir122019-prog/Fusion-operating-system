#include "apps/shell.h"
#include "kernel/memory.h"
#include "kernel/cpu.h"
#include "services/net.h"
#include "services/fs.h"

static void shell_prompt(shell_t *shell) {
    terminal_print(shell->term, "fusion");
    terminal_print(shell->term, shell->cwd[0] ? shell->cwd : "/");
    terminal_print(shell->term, "> ");
}

static void shell_redraw_line(shell_t *shell) {
    terminal_putc(shell->term, '\n');
    shell_prompt(shell);
    terminal_print(shell->term, shell->cmd);
}

static const char *const shell_commands[] = {
    "help",
    "clear",
    "cls",
    "echo",
    "uname",
    "version",
    "whoami",
    "meminfo",
    "mem",
    "heapinfo",
    "malloc",
    "uptime",
    "time",
    "date",
    "ticks",
    "cpuinfo",
    "color",
    "copy",
    "paste",
    "netinfo",
    "ip",
    "ls",
    "dir",
    "pwd",
    "cd",
    "cat",
    "type",
    "write",
    "append",
    "touch",
    "truncate",
    "mkdir",
    "rmdir",
    "cp",
    "mv",
    "rm",
    "del",
    "rename",
    "stat",
    "exists",
    "size",
    "wc",
    "head",
    "tail",
    "hexdump",
    "hex",
    "sum",
    "cmp",
    "grep",
    "lower",
    "upper",
    "reverse",
    "len",
    "repeat",
    "sleep",
    "rand",
    "ascii",
    "basename",
    "dirname",
    "history",
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

static void print_hex_byte(terminal_t *term, u8 v) {
    const char *hex = "0123456789abcdef";
    char buf[3];
    buf[0] = hex[(v >> 4) & 0xF];
    buf[1] = hex[v & 0xF];
    buf[2] = 0;
    terminal_print(term, buf);
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

static void join_args(char *out, int max, char **args, int start, int argc) {
    if (max <= 0) return;
    int pos = 0;
    for (int i = start; i < argc; i++) {
        const char *s = args[i];
        while (*s && pos < max - 1) {
            out[pos++] = *s++;
        }
        if (i + 1 < argc && pos < max - 1) {
            out[pos++] = ' ';
        }
    }
    out[pos] = 0;
}

static void str_copy_limit(char *dst, const char *src, int max) {
    if (max <= 0) return;
    int i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static int memcmp_local(const void *a, const void *b, size_t n) {
    const u8 *pa = (const u8 *)a;
    const u8 *pb = (const u8 *)b;
    for (size_t i = 0; i < n; i++) {
        if (pa[i] != pb[i]) return (int)pa[i] - (int)pb[i];
    }
    return 0;
}

static char *strstr_local(char *hay, const char *needle) {
    if (!needle || !needle[0]) return hay;
    int nlen = (int)strlen(needle);
    for (int i = 0; hay[i]; i++) {
        int j = 0;
        while (needle[j] && hay[i + j] == needle[j]) j++;
        if (j == nlen) return hay + i;
    }
    return 0;
}

static int is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static void resolve_path(shell_t *shell, const char *in, char *out, int max) {
    if (!in || !in[0]) {
        str_copy_limit(out, shell->cwd[0] ? shell->cwd : "/", max);
        return;
    }
    char temp[256];
    if (in[0] == '/') {
        int tlen = (int)strlen(in);
        if (tlen >= (int)sizeof(temp)) tlen = (int)sizeof(temp) - 1;
        memcpy(temp, in, (size_t)tlen);
        temp[tlen] = 0;
    } else {
        const char *cwd = shell->cwd[0] ? shell->cwd : "/";
        int clen = (int)strlen(cwd);
        if (clen >= (int)sizeof(temp) - 2) clen = (int)sizeof(temp) - 2;
        memcpy(temp, cwd, (size_t)clen);
        int pos = clen;
        if (pos == 0 || temp[pos - 1] != '/') {
            temp[pos++] = '/';
        }
        int ilen = (int)strlen(in);
        if (pos + ilen >= (int)sizeof(temp)) ilen = (int)sizeof(temp) - pos - 1;
        memcpy(temp + pos, in, (size_t)ilen);
        pos += ilen;
        temp[pos] = 0;
    }

    int out_len = 0;
    out[out_len++] = '/';
    out[out_len] = 0;
    int seg_pos[32];
    int seg_count = 0;

    char *p = temp;
    if (*p == '/') p++;
    while (*p) {
        char *seg = p;
        while (*p && *p != '/') p++;
        if (*p) *p++ = 0;
        if (seg[0] == 0 || (seg[0] == '.' && seg[1] == 0)) continue;
        if (seg[0] == '.' && seg[1] == '.' && seg[2] == 0) {
            if (seg_count > 0) {
                out_len = seg_pos[--seg_count];
                out[out_len] = 0;
            }
            continue;
        }
        if (out_len > 1 && out_len < max - 1) out[out_len++] = '/';
        if (seg_count < (int)(sizeof(seg_pos) / sizeof(seg_pos[0]))) {
            seg_pos[seg_count++] = out_len;
        }
        for (int i = 0; seg[i] && out_len < max - 1; i++) {
            out[out_len++] = seg[i];
        }
        out[out_len] = 0;
    }
    if (out_len == 0) {
        out[0] = '/';
        out[1] = 0;
    }
}

static void dirname_from(const char *path, char *out, int max) {
    if (!path || !path[0]) {
        str_copy_limit(out, "/", max);
        return;
    }
    int len = (int)strlen(path);
    int i = len - 1;
    while (i > 0 && path[i] == '/') i--;
    while (i > 0 && path[i] != '/') i--;
    if (i == 0) {
        out[0] = '/';
        out[1] = 0;
        return;
    }
    int out_len = i;
    if (out_len >= max) out_len = max - 1;
    memcpy(out, path, (size_t)out_len);
    out[out_len] = 0;
}

static void basename_from(const char *path, char *out, int max) {
    if (!path || !path[0]) {
        out[0] = 0;
        return;
    }
    int len = (int)strlen(path);
    int i = len - 1;
    while (i > 0 && path[i] == '/') i--;
    int end = i + 1;
    while (i > 0 && path[i - 1] != '/') i--;
    int blen = end - i;
    if (blen >= max) blen = max - 1;
    memcpy(out, path + i, (size_t)blen);
    out[blen] = 0;
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
        terminal_print(shell->term, "  clear/cls, echo, uname/version, whoami\n");
        terminal_print(shell->term, "  meminfo/mem, heapinfo, malloc, cpuinfo\n");
        terminal_print(shell->term, "  uptime/time/date, ticks\n");
        terminal_print(shell->term, "  color, copy, paste, netinfo/ip\n");
        terminal_print(shell->term, "  ls/dir, pwd, cd, cat/type\n");
        terminal_print(shell->term, "  write, append, touch, truncate\n");
        terminal_print(shell->term, "  mkdir, rmdir, rm/del, cp, mv/rename\n");
        terminal_print(shell->term, "  stat, exists, size, wc, head, tail\n");
        terminal_print(shell->term, "  hexdump/hex, sum, cmp, grep\n");
        terminal_print(shell->term, "  lower, upper, reverse, len, repeat\n");
        terminal_print(shell->term, "  sleep, rand, ascii, basename, dirname\n");
        terminal_print(shell->term, "  history, reboot, halt, exit\n");
    } else if (strcmp(args[0], "clear") == 0 || strcmp(args[0], "cls") == 0) {
        terminal_clear(shell->term);
    } else if (strcmp(args[0], "echo") == 0) {
        for (int i = 1; i < argc; i++) {
            terminal_print(shell->term, args[i]);
            if (i < argc - 1) terminal_putc(shell->term, ' ');
        }
        terminal_putc(shell->term, '\n');
    } else if (strcmp(args[0], "uname") == 0) {
        terminal_print(shell->term, "Fusion OS v1.0 x86_64\n");
    } else if (strcmp(args[0], "version") == 0) {
        terminal_print(shell->term, "Fusion OS v1.0\n");
    } else if (strcmp(args[0], "whoami") == 0) {
        terminal_print(shell->term, "root\n");
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
    } else if (strcmp(args[0], "mem") == 0) {
        terminal_print(shell->term, "Total: ");
        print_dec(shell->term, pmm_total_pages * 4096 / 1024);
        terminal_print(shell->term, " KB  Free: ");
        print_dec(shell->term, pmm_free_pages * 4096 / 1024);
        terminal_print(shell->term, " KB\n");
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
    } else if (strcmp(args[0], "time") == 0 || strcmp(args[0], "date") == 0) {
        terminal_print(shell->term, "Uptime: ");
        print_dec(shell->term, uptime_seconds);
        terminal_print(shell->term, "s\n");
    } else if (strcmp(args[0], "ticks") == 0) {
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
    } else if (strcmp(args[0], "netinfo") == 0 || strcmp(args[0], "ip") == 0) {
        terminal_print(shell->term, "Network:\n  IP: ");
        print_ip(shell->term, net_get_ip());
        terminal_print(shell->term, "\n  Netmask: ");
        print_ip(shell->term, net_get_netmask());
        terminal_print(shell->term, "\n  Gateway: ");
        print_ip(shell->term, net_get_gateway());
        terminal_print(shell->term, "\n  DNS: ");
        print_ip(shell->term, net_get_dns());
        terminal_putc(shell->term, '\n');
    } else if (strcmp(args[0], "ls") == 0 || strcmp(args[0], "dir") == 0) {
        const char *path = NULL;
        fs_sort_mode_t mode = FS_SORT_NAME;
        int desc = 0;
        for (int i = 1; i < argc; i++) {
            if (args[i][0] == '-') {
                for (char *f = args[i] + 1; *f; f++) {
                    if (*f == 's') mode = FS_SORT_SIZE;
                    else if (*f == 't') mode = FS_SORT_TYPE;
                    else if (*f == 'r') desc = 1;
                }
            } else {
                path = args[i];
            }
        }
        char resolved[256];
        resolve_path(shell, path ? path : shell->cwd, resolved, (int)sizeof(resolved));
        fs_entry_t entries[64];
        int count = 0;
        if (fs_list_dir(resolved, entries, 64, &count)) {
            fs_sort_entries(entries, count, mode, desc);
            for (int i = 0; i < count; i++) {
                terminal_print(shell->term, entries[i].is_dir ? "[D] " : "    ");
                terminal_print(shell->term, entries[i].name);
                terminal_putc(shell->term, '\n');
            }
        } else {
            terminal_print(shell->term, "ls: failed\n");
        }
    } else if (strcmp(args[0], "pwd") == 0) {
        terminal_print(shell->term, shell->cwd[0] ? shell->cwd : "/");
        terminal_putc(shell->term, '\n');
    } else if (strcmp(args[0], "cd") == 0) {
        const char *path = argc > 1 ? args[1] : "/";
        char resolved[256];
        resolve_path(shell, path, resolved, (int)sizeof(resolved));
        fs_entry_t st;
        if (fs_stat(resolved, &st) && st.is_dir) {
            str_copy_limit(shell->cwd, resolved, (int)sizeof(shell->cwd));
        } else {
            terminal_print(shell->term, "cd: not a directory\n");
        }
    } else if (strcmp(args[0], "cat") == 0) {
        if (argc < 2) {
            terminal_print(shell->term, "Usage: cat <path>\n");
        } else {
            char resolved[256];
            resolve_path(shell, args[1], resolved, (int)sizeof(resolved));
            u8 *data = 0;
            u32 len = 0;
            if (fs_read_file(resolved, &data, &len)) {
                terminal_print(shell->term, (char *)data);
                terminal_putc(shell->term, '\n');
                free(data);
            } else {
                terminal_print(shell->term, "cat: failed\n");
            }
        }
    } else if (strcmp(args[0], "type") == 0) {
        if (argc < 2) {
            terminal_print(shell->term, "Usage: type <path>\n");
        } else {
            char resolved[256];
            resolve_path(shell, args[1], resolved, (int)sizeof(resolved));
            u8 *data = 0;
            u32 len = 0;
            if (fs_read_file(resolved, &data, &len)) {
                terminal_print(shell->term, (char *)data);
                terminal_putc(shell->term, '\n');
                free(data);
            } else {
                terminal_print(shell->term, "type: failed\n");
            }
        }
    } else if (strcmp(args[0], "write") == 0) {
        if (argc < 3) {
            terminal_print(shell->term, "Usage: write <path> <text>\n");
        } else {
            char resolved[256];
            char text[192];
            resolve_path(shell, args[1], resolved, (int)sizeof(resolved));
            join_args(text, (int)sizeof(text), args, 2, argc);
            if (fs_write_file(resolved, (const u8 *)text, (u32)strlen(text))) {
                terminal_print(shell->term, "write: ok\n");
            } else {
                terminal_print(shell->term, "write: failed\n");
            }
        }
    } else if (strcmp(args[0], "append") == 0) {
        if (argc < 3) {
            terminal_print(shell->term, "Usage: append <path> <text>\n");
        } else {
            char resolved[256];
            char text[192];
            resolve_path(shell, args[1], resolved, (int)sizeof(resolved));
            join_args(text, (int)sizeof(text), args, 2, argc);
            if (fs_append_file(resolved, (const u8 *)text, (u32)strlen(text))) {
                terminal_print(shell->term, "append: ok\n");
            } else {
                terminal_print(shell->term, "append: failed\n");
            }
        }
    } else if (strcmp(args[0], "touch") == 0) {
        if (argc < 2) {
            terminal_print(shell->term, "Usage: touch <path>\n");
        } else {
            char resolved[256];
            resolve_path(shell, args[1], resolved, (int)sizeof(resolved));
            if (fs_write_file(resolved, (const u8 *)"", 0)) {
                terminal_print(shell->term, "touch: ok\n");
            } else {
                terminal_print(shell->term, "touch: failed\n");
            }
        }
    } else if (strcmp(args[0], "truncate") == 0) {
        if (argc < 2) {
            terminal_print(shell->term, "Usage: truncate <path>\n");
        } else {
            char resolved[256];
            resolve_path(shell, args[1], resolved, (int)sizeof(resolved));
            if (fs_write_file(resolved, (const u8 *)"", 0)) {
                terminal_print(shell->term, "truncate: ok\n");
            } else {
                terminal_print(shell->term, "truncate: failed\n");
            }
        }
    } else if (strcmp(args[0], "mkdir") == 0) {
        if (argc < 2) {
            terminal_print(shell->term, "Usage: mkdir <path>\n");
        } else {
            char resolved[256];
            resolve_path(shell, args[1], resolved, (int)sizeof(resolved));
            if (fs_mkdir(resolved)) {
                terminal_print(shell->term, "mkdir: ok\n");
            } else {
                terminal_print(shell->term, "mkdir: failed\n");
            }
        }
    } else if (strcmp(args[0], "rmdir") == 0) {
        if (argc < 2) {
            terminal_print(shell->term, "Usage: rmdir <path>\n");
        } else {
            char resolved[256];
            resolve_path(shell, args[1], resolved, (int)sizeof(resolved));
            if (fs_delete(resolved)) {
                terminal_print(shell->term, "rmdir: ok\n");
            } else {
                terminal_print(shell->term, "rmdir: failed\n");
            }
        }
    } else if (strcmp(args[0], "cp") == 0) {
        if (argc < 3) {
            terminal_print(shell->term, "Usage: cp <src> <dst>\n");
        } else {
            char src[256];
            char dst[256];
            resolve_path(shell, args[1], src, (int)sizeof(src));
            resolve_path(shell, args[2], dst, (int)sizeof(dst));
            if (fs_copy(src, dst)) {
                terminal_print(shell->term, "cp: ok\n");
            } else {
                terminal_print(shell->term, "cp: failed\n");
            }
        }
    } else if (strcmp(args[0], "mv") == 0 || strcmp(args[0], "rename") == 0) {
        if (argc < 3) {
            terminal_print(shell->term, "Usage: mv <src> <dst>\n");
        } else {
            char src[256];
            char dst[256];
            resolve_path(shell, args[1], src, (int)sizeof(src));
            resolve_path(shell, args[2], dst, (int)sizeof(dst));
            if (fs_move(src, dst)) {
                terminal_print(shell->term, "mv: ok\n");
            } else {
                terminal_print(shell->term, "mv: failed\n");
            }
        }
    } else if (strcmp(args[0], "rm") == 0 || strcmp(args[0], "del") == 0) {
        if (argc < 2) {
            terminal_print(shell->term, "Usage: rm <path>\n");
        } else {
            char resolved[256];
            resolve_path(shell, args[1], resolved, (int)sizeof(resolved));
            if (fs_delete(resolved)) {
                terminal_print(shell->term, "rm: ok\n");
            } else {
                terminal_print(shell->term, "rm: failed\n");
            }
        }
    } else if (strcmp(args[0], "stat") == 0) {
        if (argc < 2) {
            terminal_print(shell->term, "Usage: stat <path>\n");
        } else {
            char resolved[256];
            resolve_path(shell, args[1], resolved, (int)sizeof(resolved));
            fs_entry_t st;
            if (fs_stat(resolved, &st)) {
                terminal_print(shell->term, st.is_dir ? "Directory\n" : "File\n");
                terminal_print(shell->term, "Name: ");
                terminal_print(shell->term, st.name);
                terminal_print(shell->term, "\nSize: ");
                print_dec(shell->term, st.size);
                terminal_print(shell->term, " bytes\n");
            } else {
                terminal_print(shell->term, "stat: failed\n");
            }
        }
    } else if (strcmp(args[0], "exists") == 0) {
        if (argc < 2) {
            terminal_print(shell->term, "Usage: exists <path>\n");
        } else {
            char resolved[256];
            resolve_path(shell, args[1], resolved, (int)sizeof(resolved));
            terminal_print(shell->term, fs_exists(resolved) ? "yes\n" : "no\n");
        }
    } else if (strcmp(args[0], "size") == 0) {
        if (argc < 2) {
            terminal_print(shell->term, "Usage: size <path>\n");
        } else {
            char resolved[256];
            resolve_path(shell, args[1], resolved, (int)sizeof(resolved));
            fs_entry_t st;
            if (fs_stat(resolved, &st) && !st.is_dir) {
                print_dec(shell->term, st.size);
                terminal_putc(shell->term, '\n');
            } else {
                terminal_print(shell->term, "size: failed\n");
            }
        }
    } else if (strcmp(args[0], "wc") == 0) {
        if (argc < 2) {
            terminal_print(shell->term, "Usage: wc <path>\n");
        } else {
            char resolved[256];
            resolve_path(shell, args[1], resolved, (int)sizeof(resolved));
            u8 *data = 0;
            u32 len = 0;
            if (fs_read_file(resolved, &data, &len)) {
                u32 lines = 0;
                u32 words = 0;
                int in_word = 0;
                for (u32 i = 0; i < len; i++) {
                    char c = (char)data[i];
                    if (c == '\n') lines++;
                    if (is_space(c)) {
                        if (in_word) in_word = 0;
                    } else {
                        if (!in_word) {
                            in_word = 1;
                            words++;
                        }
                    }
                }
                print_dec(shell->term, lines);
                terminal_print(shell->term, " ");
                print_dec(shell->term, words);
                terminal_print(shell->term, " ");
                print_dec(shell->term, len);
                terminal_putc(shell->term, '\n');
                free(data);
            } else {
                terminal_print(shell->term, "wc: failed\n");
            }
        }
    } else if (strcmp(args[0], "head") == 0 || strcmp(args[0], "tail") == 0) {
        if (argc < 2) {
            terminal_print(shell->term, "Usage: head/tail <path> [n]\n");
        } else {
            int n = 10;
            if (argc >= 3) {
                n = 0;
                for (char *q = args[2]; *q; q++) if (*q >= '0' && *q <= '9') n = n * 10 + (*q - '0');
                if (n <= 0) n = 10;
            }
            char resolved[256];
            resolve_path(shell, args[1], resolved, (int)sizeof(resolved));
            u8 *data = 0;
            u32 len = 0;
            if (fs_read_file(resolved, &data, &len)) {
                if (strcmp(args[0], "head") == 0) {
                    u32 lines = 0;
                    for (u32 i = 0; i < len && lines < (u32)n; i++) {
                        terminal_putc(shell->term, (char)data[i]);
                        if (data[i] == '\n') lines++;
                    }
                    if (lines == 0) terminal_putc(shell->term, '\n');
                } else {
                    int count = 0;
                    for (u32 i = 0; i < len; i++) if (data[i] == '\n') count++;
                    int start_line = count - n;
                    if (start_line < 0) start_line = 0;
                    int line = 0;
                    for (u32 i = 0; i < len; i++) {
                        if (line >= start_line) terminal_putc(shell->term, (char)data[i]);
                        if (data[i] == '\n') line++;
                    }
                    terminal_putc(shell->term, '\n');
                }
                free(data);
            } else {
                terminal_print(shell->term, "head/tail: failed\n");
            }
        }
    } else if (strcmp(args[0], "hexdump") == 0 || strcmp(args[0], "hex") == 0) {
        if (argc < 2) {
            terminal_print(shell->term, "Usage: hexdump <path>\n");
        } else {
            char resolved[256];
            resolve_path(shell, args[1], resolved, (int)sizeof(resolved));
            u8 *data = 0;
            u32 len = 0;
            if (fs_read_file(resolved, &data, &len)) {
                for (u32 i = 0; i < len; i += 16) {
                    print_hex(shell->term, i);
                    terminal_print(shell->term, ": ");
                    for (u32 j = 0; j < 16; j++) {
                        if (i + j < len) {
                            print_hex_byte(shell->term, data[i + j]);
                            terminal_putc(shell->term, ' ');
                        } else {
                            terminal_print(shell->term, "   ");
                        }
                    }
                    terminal_putc(shell->term, '\n');
                }
                free(data);
            } else {
                terminal_print(shell->term, "hexdump: failed\n");
            }
        }
    } else if (strcmp(args[0], "sum") == 0) {
        if (argc < 2) {
            terminal_print(shell->term, "Usage: sum <path>\n");
        } else {
            char resolved[256];
            resolve_path(shell, args[1], resolved, (int)sizeof(resolved));
            u8 *data = 0;
            u32 len = 0;
            if (fs_read_file(resolved, &data, &len)) {
                u32 sum = 0;
                for (u32 i = 0; i < len; i++) sum += data[i];
                print_dec(shell->term, sum);
                terminal_putc(shell->term, '\n');
                free(data);
            } else {
                terminal_print(shell->term, "sum: failed\n");
            }
        }
    } else if (strcmp(args[0], "cmp") == 0) {
        if (argc < 3) {
            terminal_print(shell->term, "Usage: cmp <a> <b>\n");
        } else {
            char a[256];
            char b[256];
            resolve_path(shell, args[1], a, (int)sizeof(a));
            resolve_path(shell, args[2], b, (int)sizeof(b));
            u8 *da = 0;
            u8 *db = 0;
            u32 la = 0;
            u32 lb = 0;
            if (fs_read_file(a, &da, &la) && fs_read_file(b, &db, &lb)) {
                int same = (la == lb) && (memcmp_local(da, db, la) == 0);
                terminal_print(shell->term, same ? "equal\n" : "different\n");
                free(da);
                free(db);
            } else {
                terminal_print(shell->term, "cmp: failed\n");
                if (da) free(da);
                if (db) free(db);
            }
        }
    } else if (strcmp(args[0], "grep") == 0) {
        if (argc < 3) {
            terminal_print(shell->term, "Usage: grep <text> <path>\n");
        } else {
            char resolved[256];
            resolve_path(shell, args[2], resolved, (int)sizeof(resolved));
            u8 *data = 0;
            u32 len = 0;
            if (fs_read_file(resolved, &data, &len)) {
                const char *needle = args[1];
                u32 start = 0;
                for (u32 i = 0; i <= len; i++) {
                    if (i == len || data[i] == '\n') {
                        data[i] = 0;
                        if (strstr_local((char *)(data + start), needle)) {
                            terminal_print(shell->term, (char *)(data + start));
                            terminal_putc(shell->term, '\n');
                        }
                        start = i + 1;
                    }
                }
                free(data);
            } else {
                terminal_print(shell->term, "grep: failed\n");
            }
        }
    } else if (strcmp(args[0], "lower") == 0 || strcmp(args[0], "upper") == 0 ||
               strcmp(args[0], "reverse") == 0 || strcmp(args[0], "len") == 0 ||
               strcmp(args[0], "repeat") == 0) {
        if (argc < 2) {
            terminal_print(shell->term, "Usage: lower/upper/reverse/len/repeat <text> [n]\n");
        } else {
            char text[192];
            int text_argc = argc;
            if (strcmp(args[0], "len") == 0) {
                join_args(text, (int)sizeof(text), args, 1, argc);
                print_dec(shell->term, (u64)strlen(text));
                terminal_putc(shell->term, '\n');
            } else if (strcmp(args[0], "reverse") == 0) {
                join_args(text, (int)sizeof(text), args, 1, argc);
                int l = (int)strlen(text);
                for (int i = l - 1; i >= 0; i--) terminal_putc(shell->term, text[i]);
                terminal_putc(shell->term, '\n');
            } else if (strcmp(args[0], "upper") == 0) {
                join_args(text, (int)sizeof(text), args, 1, argc);
                for (int i = 0; text[i]; i++) {
                    char c = text[i];
                    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
                    terminal_putc(shell->term, c);
                }
                terminal_putc(shell->term, '\n');
            } else if (strcmp(args[0], "lower") == 0) {
                join_args(text, (int)sizeof(text), args, 1, argc);
                for (int i = 0; text[i]; i++) {
                    char c = text[i];
                    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
                    terminal_putc(shell->term, c);
                }
                terminal_putc(shell->term, '\n');
            } else if (strcmp(args[0], "repeat") == 0) {
                int count = 2;
                if (argc >= 3) {
                    count = 0;
                    for (char *q = args[argc - 1]; *q; q++) if (*q >= '0' && *q <= '9') count = count * 10 + (*q - '0');
                    if (count <= 0) count = 2;
                    text_argc = argc - 1;
                }
                join_args(text, (int)sizeof(text), args, 1, text_argc);
                for (int i = 0; i < count; i++) {
                    terminal_print(shell->term, text);
                    terminal_putc(shell->term, '\n');
                }
            }
        }
    } else if (strcmp(args[0], "sleep") == 0) {
        if (argc < 2) {
            terminal_print(shell->term, "Usage: sleep <seconds>\n");
        } else {
            u64 secs = 0;
            for (char *q = args[1]; *q; q++) if (*q >= '0' && *q <= '9') secs = secs * 10 + (*q - '0');
            if (secs == 0) secs = 1;
            cpu_sleep_ticks(secs * PIT_HZ);
        }
    } else if (strcmp(args[0], "rand") == 0) {
        static u32 seed = 0x12345678;
        if (seed == 0) seed = (u32)ticks;
        seed = seed * 1103515245u + 12345u;
        u32 val = seed;
        if (argc >= 2) {
            u32 mod = 0;
            for (char *q = args[1]; *q; q++) if (*q >= '0' && *q <= '9') mod = mod * 10 + (*q - '0');
            if (mod) val %= mod;
        }
        print_dec(shell->term, val);
        terminal_putc(shell->term, '\n');
    } else if (strcmp(args[0], "ascii") == 0) {
        for (int c = 32; c <= 126; c++) {
            terminal_putc(shell->term, (char)c);
            if ((c - 31) % 16 == 0) terminal_putc(shell->term, '\n');
        }
        terminal_putc(shell->term, '\n');
    } else if (strcmp(args[0], "basename") == 0) {
        if (argc < 2) {
            terminal_print(shell->term, "Usage: basename <path>\n");
        } else {
            char out[128];
            basename_from(args[1], out, (int)sizeof(out));
            terminal_print(shell->term, out);
            terminal_putc(shell->term, '\n');
        }
    } else if (strcmp(args[0], "dirname") == 0) {
        if (argc < 2) {
            terminal_print(shell->term, "Usage: dirname <path>\n");
        } else {
            char out[128];
            dirname_from(args[1], out, (int)sizeof(out));
            terminal_print(shell->term, out);
            terminal_putc(shell->term, '\n');
        }
    } else if (strcmp(args[0], "history") == 0) {
        for (int i = 0; i < shell->history_count; i++) {
            print_dec(shell->term, (u64)i);
            terminal_print(shell->term, ": ");
            terminal_print(shell->term, shell->history[i]);
            terminal_putc(shell->term, '\n');
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
    shell->cwd[0] = '/';
    shell->cwd[1] = 0;
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
