#include "ui/terminal.h"
#include "drivers/gfx.h"
#include "kernel/memory.h"

#define SCROLLBACK_LINES 200

static int terminal_max_offset(const terminal_t *term) {
    int available = term->line_count - term->rows;
    if (available < 0) available = 0;
    if (available > term->total_lines) available = term->total_lines;
    return available;
}

static int terminal_line_index(const terminal_t *term, int line) {
    int total = term->total_lines;
    if (total <= 0) return 0;
    while (line < 0) line += total;
    return line % total;
}

static char *terminal_line_ptr(terminal_t *term, int line) {
    return term->cells + (size_t)terminal_line_index(term, line) * (size_t)term->cols;
}

static void terminal_clear_line(terminal_t *term, int line) {
    char *row = terminal_line_ptr(term, line);
    for (int i = 0; i < term->cols; i++) {
        row[i] = ' ';
    }
}

static void terminal_alloc_cells(terminal_t *term) {
    int cols = term->w / FONT_WIDTH;
    int rows = term->h / FONT_HEIGHT;
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;

    if (cols == term->cols && rows == term->rows && term->cells) {
        return;
    }

    if (term->cells) {
        free(term->cells);
    }

    term->cols = cols;
    term->rows = rows;
    term->total_lines = rows + SCROLLBACK_LINES;
    term->cells = (char *)malloc((size_t)(cols * term->total_lines));
    if (term->cells) {
        for (int i = 0; i < cols * term->total_lines; i++) {
            term->cells[i] = ' ';
        }
    }
    term->cursor_x = 0;
    term->cursor_y = 0;
    term->line_head = 0;
    term->line_count = 1;
    term->view_offset = 0;
}

void terminal_init(terminal_t *term, int x, int y, int w, int h) {
    term->x = x;
    term->y = y;
    term->w = w;
    term->h = h;
    term->fg = TERM_DEFAULT_FG;
    term->bg = TERM_DEFAULT_BG;
    term->cells = 0;
    term->cols = 0;
    term->rows = 0;
    term->cursor_x = 0;
    term->cursor_y = 0;
    term->line_head = 0;
    term->line_count = 0;
    term->total_lines = 0;
    term->view_offset = 0;
    term->clipboard = 0;
    term->clipboard_len = 0;
    term->clipboard_cap = 0;
    terminal_alloc_cells(term);
}

void terminal_set_bounds(terminal_t *term, int x, int y, int w, int h) {
    term->x = x;
    term->y = y;
    term->w = w;
    term->h = h;
    terminal_alloc_cells(term);
}

void terminal_clear(terminal_t *term) {
    if (!term->cells) return;
    for (int i = 0; i < term->cols * term->total_lines; i++) {
        term->cells[i] = ' ';
    }
    term->cursor_x = 0;
    term->cursor_y = 0;
    term->line_head = 0;
    term->line_count = 1;
    term->view_offset = 0;
}

void terminal_putc(terminal_t *term, char c) {
    if (!term->cells) return;

    if (c == '\n') {
        term->cursor_x = 0;
        term->line_head = (term->line_head + 1) % term->total_lines;
        terminal_clear_line(term, term->line_head);
        if (term->line_count < term->total_lines) {
            term->line_count++;
        }
        if (term->view_offset > 0) {
            term->view_offset++;
            int max_offset = terminal_max_offset(term);
            if (term->view_offset > max_offset) term->view_offset = max_offset;
        }
        return;
    }
    if (c == '\b') {
        if (term->cursor_x > 0) {
            term->cursor_x--;
            char *row = terminal_line_ptr(term, term->line_head);
            row[term->cursor_x] = ' ';
        }
        return;
    }

    char *row = terminal_line_ptr(term, term->line_head);
    row[term->cursor_x] = c;
    term->cursor_x++;
    if (term->cursor_x >= term->cols) {
        term->cursor_x = 0;
        term->line_head = (term->line_head + 1) % term->total_lines;
        terminal_clear_line(term, term->line_head);
        if (term->line_count < term->total_lines) {
            term->line_count++;
        }
        if (term->view_offset > 0) {
            term->view_offset++;
            int max_offset = terminal_max_offset(term);
            if (term->view_offset > max_offset) term->view_offset = max_offset;
        }
    }
}

void terminal_print(terminal_t *term, const char *s) {
    while (*s) {
        terminal_putc(term, *s++);
    }
}

void terminal_render(terminal_t *term) {
    if (!term->cells) return;
    gfx_draw_rect(term->x, term->y, term->w, term->h, term->bg);
    int clip_x = term->x;
    int clip_y = term->y;
    int clip_w = term->w;
    int clip_h = term->h;

    int max_offset = terminal_max_offset(term);
    if (term->view_offset > max_offset) term->view_offset = max_offset;
    int top_line = term->line_head - (term->rows - 1) - term->view_offset;

    for (int row = 0; row < term->rows; row++) {
        char *line = terminal_line_ptr(term, top_line + row);
        for (int col = 0; col < term->cols; col++) {
            char c = line[col];
            int px = term->x + col * FONT_WIDTH;
            int py = term->y + row * FONT_HEIGHT;
            if (c != ' ') {
                gfx_draw_char_clipped(c, px, py, term->fg, clip_x, clip_y, clip_w, clip_h);
            }
        }
    }

    if (term->view_offset == 0) {
        int cursor_row = term->rows - 1;
        int cursor_px = term->x + term->cursor_x * FONT_WIDTH;
        int cursor_py = term->y + cursor_row * FONT_HEIGHT + FONT_HEIGHT - 2;
        gfx_draw_rect(cursor_px, cursor_py, FONT_WIDTH, 2, 0xFFFFFF);
    }
}

void terminal_scroll_up(terminal_t *term) {
    int max_offset = terminal_max_offset(term);
    if (term->view_offset < max_offset) {
        term->view_offset++;
    }
}

void terminal_scroll_down(terminal_t *term) {
    if (term->view_offset > 0) {
        term->view_offset--;
    }
}

void terminal_copy_visible(terminal_t *term) {
    if (!term->cells) return;
    int max_offset = terminal_max_offset(term);
    if (term->view_offset > max_offset) term->view_offset = max_offset;
    int top_line = term->line_head - (term->rows - 1) - term->view_offset;

    size_t needed = 0;
    for (int row = 0; row < term->rows; row++) {
        char *line = terminal_line_ptr(term, top_line + row);
        int end = term->cols;
        while (end > 0 && line[end - 1] == ' ') end--;
        needed += (size_t)end + 1;
    }

    if (needed == 0) return;
    if (term->clipboard) {
        free(term->clipboard);
        term->clipboard = 0;
        term->clipboard_cap = 0;
    }

    term->clipboard = (char *)malloc(needed + 1);
    if (!term->clipboard) return;
    term->clipboard_cap = needed + 1;

    size_t pos = 0;
    for (int row = 0; row < term->rows; row++) {
        char *line = terminal_line_ptr(term, top_line + row);
        int end = term->cols;
        while (end > 0 && line[end - 1] == ' ') end--;
        for (int col = 0; col < end; col++) {
            term->clipboard[pos++] = line[col];
        }
        term->clipboard[pos++] = '\n';
    }
    term->clipboard[pos] = 0;
    term->clipboard_len = pos;
}

void terminal_paste(terminal_t *term) {
    if (!term->clipboard || term->clipboard_len == 0) return;
    terminal_print(term, term->clipboard);
}
