#ifndef TERMINAL_H
#define TERMINAL_H

#include "types.h"

#define TERM_DEFAULT_FG 0xE6E6E6
#define TERM_DEFAULT_BG 0x0B0D12

typedef struct {
    int x;
    int y;
    int w;
    int h;
    int cols;
    int rows;
    int cursor_x;
    int cursor_y;
    int line_head;
    int line_count;
    int total_lines;
    int view_offset;
    u32 fg;
    u32 bg;
    char *cells;
    char *clipboard;
    size_t clipboard_len;
    size_t clipboard_cap;
} terminal_t;

void terminal_init(terminal_t *term, int x, int y, int w, int h);
void terminal_set_bounds(terminal_t *term, int x, int y, int w, int h);
void terminal_clear(terminal_t *term);
void terminal_putc(terminal_t *term, char c);
void terminal_print(terminal_t *term, const char *s);
void terminal_render(terminal_t *term);
void terminal_scroll_up(terminal_t *term);
void terminal_scroll_down(terminal_t *term);
void terminal_copy_visible(terminal_t *term);
void terminal_paste(terminal_t *term);

#endif
