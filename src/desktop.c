#include "desktop.h"
#include "gfx.h"
#include "input.h"
#include "terminal.h"
#include "shell.h"
#include "file_manager.h"
#include "memory.h"
#include "cpu.h"
#include "net.h"
#include "browser.h"

#define MAX_WINDOWS 8
#define BORDER_SIZE 2
#define TITLE_HEIGHT 24
#define PANEL_HEIGHT 28
#define CLOSE_SIZE 14
#define RESIZE_MARGIN 6
#define MIN_WIN_W 260
#define MIN_WIN_H 160
#define SNAP_MARGIN 20

#define RESIZE_LEFT 1
#define RESIZE_RIGHT 2
#define RESIZE_TOP 4
#define RESIZE_BOTTOM 8

typedef enum {
    APP_TERMINAL = 1,
    APP_FILES = 2,
    APP_SETTINGS = 3,
    APP_ABOUT = 4,
    APP_BROWSER = 5
} app_type_t;

typedef struct {
    u32 bg_top;
    u32 bg_bottom;
    u32 panel;
    u32 panel_border;
    u32 panel_item;
    u32 panel_item_active;
    u32 window_border_active;
    u32 window_border_inactive;
    u32 window_title;
    u32 window_bg;
    u32 text;
    u32 text_muted;
    u32 accent;
} theme_t;

typedef struct {
    int idle_fps;
    int cursor_size;
    int theme_index;
} settings_t;

typedef struct {
    app_type_t type;
    int x;
    int y;
    int w;
    int h;
    int active;
    int minimized;
    char title[32];
    terminal_t terminal;
    shell_t shell;
    file_manager_t file_manager;
    browser_t browser;
} window_t;

typedef struct {
    const char *name;
    app_type_t type;
} app_entry_t;

static window_t windows[MAX_WINDOWS];
static int window_count = 0;
static int active_index = -1;

static int launcher_open = 0;
static char launcher_query[32];
static int launcher_query_len = 0;
static int launcher_selection = 0;
static int dragging_index = -1;
static int drag_offset_x = 0;
static int drag_offset_y = 0;
static int resizing_index = -1;
static int resize_mask = 0;
static int resize_start_x = 0;
static int resize_start_y = 0;
static int resize_start_w = 0;
static int resize_start_h = 0;

static int mouse_x = 20;
static int mouse_y = 20;
static u8 mouse_buttons = 0;
static u8 prev_mouse_buttons = 0;
static int prev_cursor_x = 20;
static int prev_cursor_y = 20;
static int cursor_w = 8;
static int cursor_h = 12;

static const theme_t themes[] = {
    {0x0B0F19, 0x0F1C2C, 0x151A26, 0x2C3142, 0x1E2331, 0x2A5C8A, 0x2A5C8A, 0x2A2E3A, 0x1B2435, 0x0F1218, 0xE6E6E6, 0x9BA6B2, 0x6FD3FF},
    {0x120B0B, 0x2B1616, 0x1C1414, 0x3A2B2B, 0x221919, 0x8A2A2A, 0x8A2A2A, 0x3A2A2A, 0x2A1B1B, 0x140F0F, 0xE6E6E6, 0xB2A09B, 0xFF9A9A},
    {0x0B1411, 0x113024, 0x131E1A, 0x274038, 0x1B2A24, 0x2A8A6D, 0x2A8A6D, 0x2A3A34, 0x1B2A25, 0x0F1412, 0xE6E6E6, 0x9BB2A7, 0x7FFFD4},
    {0x090A0C, 0x0E1115, 0x14171B, 0x2A2F35, 0x1D2228, 0x4A6F6A, 0x4A6F6A, 0x2D3238, 0x1B1F24, 0x0F1113, 0xE3E5E8, 0x9AA1A8, 0x66C7BF}
};

static settings_t settings = {40, 0, 0};

static const app_entry_t apps[] = {
    {"Terminal", APP_TERMINAL},
    {"Files", APP_FILES},
    {"Settings", APP_SETTINGS},
    {"About", APP_ABOUT},
    {"Browser", APP_BROWSER}
};

static int launcher_matches(const char *name);
static void launcher_layout(int *lx, int *ly, int *lw, int *lh,
                            int *list_y, int *list_bottom, int *button_y);

static int theme_count(void) {
    return (int)(sizeof(themes) / sizeof(themes[0]));
}

static void u64_to_dec(char *out, int max, u64 value) {
    if (max <= 0) return;
    char buf[21];
    int i = 20;
    buf[i] = 0;
    if (value == 0) {
        out[0] = '0';
        if (max > 1) out[1] = 0;
        return;
    }
    while (value > 0 && i > 0) {
        buf[--i] = '0' + (value % 10);
        value /= 10;
    }
    int pos = 0;
    while (buf[i] && pos < max - 1) {
        out[pos++] = buf[i++];
    }
    out[pos] = 0;
}

static int text_width(const char *s) {
    return (int)strlen(s) * FONT_WIDTH;
}

static void launcher_layout(int *lx, int *ly, int *lw, int *lh,
                            int *list_y, int *list_bottom, int *button_y) {
    int margin = 8;
    int panel_y = (int)gfx_height() - PANEL_HEIGHT;
    int max_h = panel_y - margin * 2;
    if (max_h < 140) max_h = 140;

    int max_w = (int)gfx_width() - margin * 2;
    int width = 280;
    if (width > max_w) width = max_w;
    if (width < 160) width = max_w;

    int x = margin;
    int y = margin;

    int pad = 12;
    int header_y = y + pad;
    int search_y = header_y + FONT_HEIGHT + 6;
    int list_start = search_y + FONT_HEIGHT + 10;
    int button_h = 24;
    int button_pos = y + max_h - pad - button_h;
    int list_end = button_pos - 8;
    if (list_end < list_start + FONT_HEIGHT) list_end = list_start + FONT_HEIGHT;

    if (lx) *lx = x;
    if (ly) *ly = y;
    if (lw) *lw = width;
    if (lh) *lh = max_h;
    if (list_y) *list_y = list_start;
    if (list_bottom) *list_bottom = list_end;
    if (button_y) *button_y = button_pos;
}

static void window_content_bounds(window_t *win, int *x, int *y, int *w, int *h) {
    *x = win->x + BORDER_SIZE;
    *y = win->y + TITLE_HEIGHT;
    *w = win->w - (BORDER_SIZE * 2);
    *h = win->h - TITLE_HEIGHT - BORDER_SIZE;
}

static void desktop_focus_window(int index) {
    if (index < 0 || index >= window_count) return;
    for (int i = 0; i < window_count; i++) windows[i].active = 0;
    if (windows[index].minimized) {
        active_index = -1;
        return;
    }
    windows[index].active = 1;
    active_index = index;
}

static void desktop_focus_next(int dir) {
    if (window_count == 0) return;
    int start = active_index;
    if (start < 0 || start >= window_count) start = 0;
    int idx = start;
    for (int i = 0; i < window_count; i++) {
        idx = (idx + dir + window_count) % window_count;
        if (!windows[idx].minimized) {
            desktop_focus_window(idx);
            return;
        }
    }
}

static void desktop_create_window(app_type_t type) {
    if (window_count >= MAX_WINDOWS) return;
    window_t *win = &windows[window_count];
    win->type = type;
    win->w = (int)((gfx_width() * 55) / 100);
    win->h = (int)((gfx_height() * 55) / 100);
    if (win->w < MIN_WIN_W) win->w = MIN_WIN_W;
    if (win->h < MIN_WIN_H) win->h = MIN_WIN_H;
    win->x = (int)(gfx_width() - win->w) / 2 + (window_count * 18);
    win->y = (int)(gfx_height() - win->h) / 2 + (window_count * 18);
    win->active = 0;
    win->minimized = 0;

    if (type == APP_TERMINAL) {
        strcpy(win->title, "Terminal");
        int cx, cy, cw, ch;
        window_content_bounds(win, &cx, &cy, &cw, &ch);
        terminal_init(&win->terminal, cx + 6, cy + 6, cw - 12, ch - 12);
        shell_init(&win->shell, &win->terminal);
    } else if (type == APP_FILES) {
        strcpy(win->title, "Files");
        file_manager_init(&win->file_manager);
    } else if (type == APP_SETTINGS) {
        strcpy(win->title, "Settings");
    } else if (type == APP_ABOUT) {
        strcpy(win->title, "About");
    } else if (type == APP_BROWSER) {
        strcpy(win->title, "Browser");
        browser_init(&win->browser);
    } else {
        strcpy(win->title, "App");
    }

    window_count++;
    desktop_focus_window(window_count - 1);
}

static void desktop_close_window(int index) {
    if (index < 0 || index >= window_count) return;
    if (index != window_count - 1) {
        windows[index] = windows[window_count - 1];
    }
    window_count--;
    if (window_count == 0) {
        active_index = -1;
    } else {
        desktop_focus_window(window_count - 1);
    }
}

static const theme_t *current_theme(void) {
    int count = theme_count();
    int idx = settings.theme_index % count;
    if (idx < 0) idx = 0;
    return &themes[idx];
}

static void draw_background(void) {
    const theme_t *theme = current_theme();
    gfx_draw_rect(0, 0, (int)gfx_width(), (int)gfx_height(), theme->bg_top);
    gfx_draw_rect(0, 0, (int)gfx_width(), (int)((gfx_height() * 45) / 100), theme->bg_bottom);
}

static void draw_panel(void) {
    int panel_y = (int)gfx_height() - PANEL_HEIGHT;
    const theme_t *theme = current_theme();
    gfx_draw_rect(0, panel_y, (int)gfx_width(), PANEL_HEIGHT, theme->panel);
    gfx_draw_rect(0, panel_y, (int)gfx_width(), 1, theme->panel_border);

    gfx_draw_rect(8, panel_y + 4, 72, PANEL_HEIGHT - 8, launcher_open ? theme->panel_item_active : theme->panel_item);
    gfx_draw_text("Fusion", 16, panel_y + 8, theme->text);

    int task_x = 90;
    for (int i = 0; i < window_count; i++) {
        int w = 90;
        u32 color = windows[i].active ? theme->panel_item_active : theme->panel_item;
        if (windows[i].minimized) color = theme->panel_item;
        gfx_draw_rect(task_x, panel_y + 4, w, PANEL_HEIGHT - 8, color);
        gfx_draw_text(windows[i].title, task_x + 8, panel_y + 8, theme->text);
        task_x += w + 6;
    }

    u64 hours = uptime_seconds / 3600;
    u64 minutes = (uptime_seconds % 3600) / 60;
    u64 seconds = uptime_seconds % 60;
    char clock[16];
    clock[0] = '0' + (hours / 10) % 10;
    clock[1] = '0' + (hours % 10);
    clock[2] = ':';
    clock[3] = '0' + (minutes / 10);
    clock[4] = '0' + (minutes % 10);
    clock[5] = ':';
    clock[6] = '0' + (seconds / 10);
    clock[7] = '0' + (seconds % 10);
    clock[8] = 0;
    int clock_x = (int)gfx_width() - 80;
    gfx_draw_text(clock, clock_x, panel_y + 8, theme->text);
}

static void draw_launcher(void) {
    if (!launcher_open) return;
    int lx, ly, lw, lh, list_y, list_bottom, button_y;
    launcher_layout(&lx, &ly, &lw, &lh, &list_y, &list_bottom, &button_y);
    const theme_t *theme = current_theme();
    gfx_draw_rect(lx, ly, lw, lh, theme->panel);
    gfx_draw_rect(lx, ly, lw, 1, theme->panel_border);

    int pad = 12;
    int header_y = ly + pad;
    int search_y = header_y + FONT_HEIGHT + 6;

    gfx_draw_text("Apps", lx + pad, header_y, theme->accent);
    gfx_draw_text("Search:", lx + pad, search_y, theme->text_muted);
    gfx_draw_text(launcher_query_len > 0 ? launcher_query : "...", lx + pad + 60, search_y, theme->text);

    int line_y = list_y;
    int match_index = 0;
    for (u32 i = 0; i < (u32)(sizeof(apps) / sizeof(apps[0])); i++) {
        const char *name = apps[i].name;
        if (!launcher_matches(name)) continue;
        if (line_y + FONT_HEIGHT > list_bottom) break;
        if (match_index == launcher_selection) {
            gfx_draw_rect(lx + pad - 2, line_y - 2, lw - (pad * 2) + 4, FONT_HEIGHT + 4, theme->panel_item_active);
        }
        gfx_draw_text(name, lx + pad, line_y, theme->text);
        line_y += FONT_HEIGHT + 6;
        match_index++;
    }

    int button_h = 24;
    int button_gap = 8;
    int button_w = (lw - pad * 2 - button_gap * 2) / 3;
    int button_x = lx + pad;
    gfx_draw_rect(button_x, button_y, button_w, button_h, theme->panel_item);
    gfx_draw_rect(button_x + button_w + button_gap, button_y, button_w, button_h, theme->panel_item);
    gfx_draw_rect(button_x + (button_w + button_gap) * 2, button_y, button_w, button_h, theme->panel_item);
    gfx_draw_text("Shutdown", button_x + 6, button_y + 4, theme->text);
    gfx_draw_text("Sleep", button_x + button_w + button_gap + 12, button_y + 4, theme->text);
    gfx_draw_text("Reboot", button_x + (button_w + button_gap) * 2 + 12, button_y + 4, theme->text);
}

static int point_in_rect(int x, int y, int rx, int ry, int rw, int rh) {
    return (x >= rx && x < rx + rw && y >= ry && y < ry + rh);
}

static int launcher_matches(const char *name) {
    if (launcher_query_len == 0) return 1;
    for (int j = 0; name[j]; j++) {
        int k = 0;
        while (launcher_query[k]) {
            char nc = name[j + k];
            char qc = launcher_query[k];
            if (nc >= 'A' && nc <= 'Z') nc = (char)(nc - 'A' + 'a');
            if (qc >= 'A' && qc <= 'Z') qc = (char)(qc - 'A' + 'a');
            if (nc == 0 || nc != qc) break;
            k++;
        }
        if (launcher_query[k] == 0) return 1;
    }
    return 0;
}

static int launcher_match_count(void) {
    int count = 0;
    for (u32 i = 0; i < (u32)(sizeof(apps) / sizeof(apps[0])); i++) {
        if (launcher_matches(apps[i].name)) count++;
    }
    return count;
}

static app_type_t launcher_match_type(int index) {
    int match = 0;
    for (u32 i = 0; i < (u32)(sizeof(apps) / sizeof(apps[0])); i++) {
        if (!launcher_matches(apps[i].name)) continue;
        if (match == index) return apps[i].type;
        match++;
    }
    return APP_TERMINAL;
}

static void launcher_reset_query(void) {
    launcher_query_len = 0;
    launcher_query[0] = 0;
    launcher_selection = 0;
}

static void apply_cursor_settings(void) {
    if (settings.cursor_size == 0) {
        cursor_w = 8;
        cursor_h = 12;
    } else {
        cursor_w = 12;
        cursor_h = 18;
    }
}

static int settings_handle_click(window_t *win, int mx, int my) {
    int cx, cy, cw, ch;
    window_content_bounds(win, &cx, &cy, &cw, &ch);
    int sx = cx + 12;
    int sy = cy + 12 + FONT_HEIGHT + 8;

    if (point_in_rect(mx, my, sx + 90, sy - 2, 40, FONT_HEIGHT + 4)) {
        if (settings.idle_fps == 30) settings.idle_fps = 40;
        else if (settings.idle_fps == 40) settings.idle_fps = 60;
        else settings.idle_fps = 30;
        return 1;
    }
    sy += FONT_HEIGHT + 10;

    if (point_in_rect(mx, my, sx + 90, sy - 2, 80, FONT_HEIGHT + 4)) {
        settings.cursor_size = settings.cursor_size ? 0 : 1;
        apply_cursor_settings();
        return 1;
    }
    sy += FONT_HEIGHT + 10;

    if (point_in_rect(mx, my, sx + 90, sy - 2, 80, FONT_HEIGHT + 4)) {
        int count = theme_count();
        settings.theme_index = (settings.theme_index + 1) % count;
        return 1;
    }
    return 0;
}

static void draw_window(window_t *win) {
    if (win->minimized) return;
    const theme_t *theme = current_theme();
    u32 border = win->active ? theme->window_border_active : theme->window_border_inactive;
    gfx_draw_rect(win->x, win->y, win->w, win->h, border);
    gfx_draw_rect(win->x + BORDER_SIZE, win->y + BORDER_SIZE,
                  win->w - BORDER_SIZE * 2, TITLE_HEIGHT - BORDER_SIZE, theme->window_title);
    gfx_draw_text(win->title, win->x + 10, win->y + 6, theme->text);

    int close_x = win->x + win->w - CLOSE_SIZE - 6;
    int close_y = win->y + 5;
    gfx_draw_rect(close_x, close_y, CLOSE_SIZE, CLOSE_SIZE, 0xA03B3B);
    gfx_draw_text("x", close_x + 4, close_y + 1, theme->text);

    int cx, cy, cw, ch;
    window_content_bounds(win, &cx, &cy, &cw, &ch);
    gfx_draw_rect(cx, cy, cw, ch, theme->window_bg);

    if (win->type == APP_TERMINAL) {
        terminal_set_bounds(&win->terminal, cx + 6, cy + 6, cw - 12, ch - 12);
        terminal_render(&win->terminal);
    } else if (win->type == APP_FILES) {
        file_manager_render(&win->file_manager, cx + 6, cy + 6, cw - 12, ch - 12);
    } else if (win->type == APP_BROWSER) {
        browser_render(&win->browser, cx + 6, cy + 6, cw - 12, ch - 12);
    } else if (win->type == APP_SETTINGS) {
        int sx = cx + 12;
        int sy = cy + 12;
        gfx_draw_text("Settings", sx, sy, theme->text);
        sy += FONT_HEIGHT + 8;

        gfx_draw_text("Idle FPS:", sx, sy, theme->text);
        char fps[8];
        fps[0] = '0' + (settings.idle_fps / 10);
        fps[1] = '0' + (settings.idle_fps % 10);
        fps[2] = 0;
        gfx_draw_rect(sx + 90, sy - 2, 40, FONT_HEIGHT + 4, theme->panel_item);
        gfx_draw_text(fps, sx + 100, sy, theme->text);
        sy += FONT_HEIGHT + 10;

        gfx_draw_text("Cursor:", sx, sy, theme->text);
        gfx_draw_rect(sx + 90, sy - 2, 80, FONT_HEIGHT + 4, theme->panel_item);
        gfx_draw_text(settings.cursor_size == 0 ? "Small" : "Large", sx + 98, sy, theme->text);
        sy += FONT_HEIGHT + 10;

        gfx_draw_text("Theme:", sx, sy, theme->text);
        gfx_draw_rect(sx + 90, sy - 2, 80, FONT_HEIGHT + 4, theme->panel_item);
        int idx = settings.theme_index % theme_count();
        if (idx < 0) idx = 0;
        const char *theme_name = "Blue";
        if (idx == 1) theme_name = "Red";
        else if (idx == 2) theme_name = "Teal";
        else if (idx == 3) theme_name = "Dark";
        gfx_draw_text(theme_name, sx + 100, sy, theme->text);
    } else if (win->type == APP_ABOUT) {
        int sx = cx + 12;
        int sy = cy + 12;
        gfx_draw_text("Fusion OS", sx, sy, theme->text);
        sy += FONT_HEIGHT + 8;

        gfx_draw_text("Version:", sx, sy, theme->text);
        gfx_draw_text("1.0", sx + 120, sy, theme->text);
        sy += FONT_HEIGHT + 6;

        gfx_draw_text("Uptime (s):", sx, sy, theme->text);
        char uptime_buf[24];
        u64_to_dec(uptime_buf, (int)sizeof(uptime_buf), uptime_seconds);
        gfx_draw_text(uptime_buf, sx + 120, sy, theme->text);
        sy += FONT_HEIGHT + 6;

        gfx_draw_text("Resolution:", sx, sy, theme->text);
        char wbuf[12];
        char hbuf[12];
        u64_to_dec(wbuf, (int)sizeof(wbuf), gfx_width());
        u64_to_dec(hbuf, (int)sizeof(hbuf), gfx_height());
        int rx = sx + 120;
        gfx_draw_text(wbuf, rx, sy, theme->text);
        rx += text_width(wbuf);
        gfx_draw_text("x", rx, sy, theme->text_muted);
        rx += FONT_WIDTH;
        gfx_draw_text(hbuf, rx, sy, theme->text);
        sy += FONT_HEIGHT + 6;

        gfx_draw_text("Memory Total (MB):", sx, sy, theme->text);
        u64 total_mb = (pmm_total_pages * 4096) / (1024 * 1024);
        char total_buf[16];
        u64_to_dec(total_buf, (int)sizeof(total_buf), total_mb);
        gfx_draw_text(total_buf, sx + 180, sy, theme->text);
        sy += FONT_HEIGHT + 6;

        gfx_draw_text("Memory Free (MB):", sx, sy, theme->text);
        u64 free_mb = (pmm_free_pages * 4096) / (1024 * 1024);
        char free_buf[16];
        u64_to_dec(free_buf, (int)sizeof(free_buf), free_mb);
        gfx_draw_text(free_buf, sx + 180, sy, theme->text);
        sy += FONT_HEIGHT + 6;

        gfx_draw_text("Heap Used (KB):", sx, sy, theme->text);
        u64 heap_used_kb = (heap_allocated - heap_freed) / 1024;
        char heap_buf[16];
        u64_to_dec(heap_buf, (int)sizeof(heap_buf), heap_used_kb);
        gfx_draw_text(heap_buf, sx + 180, sy, theme->text);
        sy += FONT_HEIGHT + 6;

        gfx_draw_text("CPU Vendor:", sx, sy, theme->text);
        char vendor[13];
        cpu_get_vendor(vendor);
        gfx_draw_text(vendor, sx + 120, sy, theme->text);
    }
}

static void draw_mouse_cursor_front(void) {
    const theme_t *theme = current_theme();
    gfx_draw_rect_front(mouse_x, mouse_y, cursor_w, cursor_h, theme->text);
    gfx_draw_rect_front(mouse_x + 2, mouse_y + 2, cursor_w - 4, cursor_h - 4, theme->bg_top);
}

static int handle_mouse_click(void) {
    int changed = 0;
    int panel_y = (int)gfx_height() - PANEL_HEIGHT;
    int left_pressed = (mouse_buttons & 0x1) && !(prev_mouse_buttons & 0x1);
    int left_released = !(mouse_buttons & 0x1) && (prev_mouse_buttons & 0x1);

    if (left_pressed) {
        if (point_in_rect(mouse_x, mouse_y, 8, panel_y + 4, 72, PANEL_HEIGHT - 8)) {
            launcher_open = !launcher_open;
            if (launcher_open) launcher_reset_query();
            return 1;
        }

        int task_x = 90;
        for (int i = 0; i < window_count; i++) {
            int w = 90;
            if (point_in_rect(mouse_x, mouse_y, task_x, panel_y + 4, w, PANEL_HEIGHT - 8)) {
                if (windows[i].minimized) {
                    windows[i].minimized = 0;
                    desktop_focus_window(i);
                } else if (active_index == i) {
                    windows[i].minimized = 1;
                    windows[i].active = 0;
                    active_index = -1;
                } else {
                    desktop_focus_window(i);
                }
                return 1;
            }
            task_x += w + 6;
        }

        if (launcher_open) {
            int lx, ly, lw, lh, list_y, list_bottom, button_y;
            launcher_layout(&lx, &ly, &lw, &lh, &list_y, &list_bottom, &button_y);
            int pad = 12;
            int button_h = 24;
            int button_gap = 8;
            int button_w = (lw - pad * 2 - button_gap * 2) / 3;
            int button_x = lx + pad;

            if (point_in_rect(mouse_x, mouse_y, button_x, button_y, button_w, button_h)) {
                halt();
                return 1;
            }
            if (point_in_rect(mouse_x, mouse_y, button_x + button_w + button_gap, button_y, button_w, button_h)) {
                halt();
                return 1;
            }
            if (point_in_rect(mouse_x, mouse_y, button_x + (button_w + button_gap) * 2, button_y, button_w, button_h)) {
                reboot();
                return 1;
            }

            if (mouse_y >= list_y && mouse_y < list_bottom) {
                int index = (mouse_y - list_y) / (FONT_HEIGHT + 6);
                int matches = launcher_match_count();
                if (index >= 0 && index < matches) {
                    app_type_t type = launcher_match_type(index);
                    desktop_create_window(type);
                    launcher_open = 0;
                    return 1;
                }
            }
        }

        for (int i = window_count - 1; i >= 0; i--) {
            window_t *win = &windows[i];
            if (win->minimized) continue;
            if (!point_in_rect(mouse_x, mouse_y, win->x, win->y, win->w, win->h)) {
                continue;
            }
            if (active_index != i) {
                desktop_focus_window(i);
                changed = 1;
            }

            int close_x = win->x + win->w - CLOSE_SIZE - 6;
            int close_y = win->y + 5;
            if (point_in_rect(mouse_x, mouse_y, close_x, close_y, CLOSE_SIZE, CLOSE_SIZE)) {
                desktop_close_window(i);
                return 1;
            }

            int mask = 0;
            if (mouse_x - win->x < RESIZE_MARGIN) mask |= RESIZE_LEFT;
            if ((win->x + win->w) - mouse_x < RESIZE_MARGIN) mask |= RESIZE_RIGHT;
            if (mouse_y - win->y < RESIZE_MARGIN) mask |= RESIZE_TOP;
            if ((win->y + win->h) - mouse_y < RESIZE_MARGIN) mask |= RESIZE_BOTTOM;
            if (mask) {
                resizing_index = i;
                resize_mask = mask;
                resize_start_x = win->x;
                resize_start_y = win->y;
                resize_start_w = win->w;
                resize_start_h = win->h;
                return 1;
            }

            if (point_in_rect(mouse_x, mouse_y, win->x, win->y, win->w, TITLE_HEIGHT)) {
                dragging_index = i;
                drag_offset_x = mouse_x - win->x;
                drag_offset_y = mouse_y - win->y;
                return 1;
            }

            if (win->type == APP_SETTINGS) {
                if (settings_handle_click(win, mouse_x, mouse_y)) return 1;
            }
            return changed;
        }
    }

    if (left_released) {
        if (dragging_index >= 0) {
            window_t *win = &windows[dragging_index];
            if (mouse_x < SNAP_MARGIN) {
                win->x = 0;
                win->y = 0;
                win->w = (int)gfx_width() / 2;
                win->h = (int)gfx_height() - PANEL_HEIGHT;
                changed = 1;
            } else if (mouse_x > (int)gfx_width() - SNAP_MARGIN) {
                win->x = (int)gfx_width() / 2;
                win->y = 0;
                win->w = (int)gfx_width() - win->x;
                win->h = (int)gfx_height() - PANEL_HEIGHT;
                changed = 1;
            } else if (mouse_y < SNAP_MARGIN) {
                win->x = 0;
                win->y = 0;
                win->w = (int)gfx_width();
                win->h = (int)gfx_height() - PANEL_HEIGHT;
                changed = 1;
            }
        }
        dragging_index = -1;
        resizing_index = -1;
        resize_mask = 0;
    }

    if (dragging_index >= 0 && (mouse_buttons & 0x1)) {
        window_t *win = &windows[dragging_index];
        int old_x = win->x;
        int old_y = win->y;
        win->x = mouse_x - drag_offset_x;
        win->y = mouse_y - drag_offset_y;
        if (win->x < 4) win->x = 4;
        if (win->y < 4) win->y = 4;
        if (win->x + win->w > (int)gfx_width() - 4) win->x = (int)gfx_width() - win->w - 4;
        if (win->y + win->h > (int)gfx_height() - PANEL_HEIGHT - 4) {
            win->y = (int)gfx_height() - PANEL_HEIGHT - win->h - 4;
        }
        if (win->x != old_x || win->y != old_y) {
            changed = 1;
        }
    }

    if (resizing_index >= 0 && (mouse_buttons & 0x1)) {
        window_t *win = &windows[resizing_index];
        int right = resize_start_x + resize_start_w;
        int bottom = resize_start_y + resize_start_h;
        if (resize_mask & RESIZE_LEFT) {
            win->x = mouse_x;
            win->w = right - win->x;
        }
        if (resize_mask & RESIZE_RIGHT) {
            win->w = mouse_x - resize_start_x;
        }
        if (resize_mask & RESIZE_TOP) {
            win->y = mouse_y;
            win->h = bottom - win->y;
        }
        if (resize_mask & RESIZE_BOTTOM) {
            win->h = mouse_y - resize_start_y;
        }

        if (win->w < MIN_WIN_W) win->w = MIN_WIN_W;
        if (win->h < MIN_WIN_H) win->h = MIN_WIN_H;
        if (win->x < 0) win->x = 0;
        if (win->y < 0) win->y = 0;
        if (win->x + win->w > (int)gfx_width()) win->x = (int)gfx_width() - win->w;
        if (win->y + win->h > (int)gfx_height() - PANEL_HEIGHT) {
            win->y = (int)gfx_height() - PANEL_HEIGHT - win->h;
        }
        changed = 1;
    }
    return changed;
}

void desktop_init(void) {
    window_count = 0;
    active_index = -1;
    launcher_open = 0;
    launcher_reset_query();
    apply_cursor_settings();
    desktop_create_window(APP_TERMINAL);
}

void desktop_loop(void) {
    u64 last_tick = 0;
    u64 last_uptime = uptime_seconds;
    u32 idle_accum = 0;

    int dirty_full = 1;
    int dirty_panel = 1;
    int dirty_window = -1;
    int cursor_dirty = 1;
    while (1) {
        int activity = 0;

        key_event_t key;
        while (input_poll_key(&key)) {
            if (key.pressed && key.keycode == KEY_TAB && input_is_alt_down()) {
                int dir = input_is_shift_down() ? -1 : 1;
                desktop_focus_next(dir);
                dirty_full = 1;
                dirty_panel = 1;
                cursor_dirty = 1;
                activity = 1;
                continue;
            }

            if (key.pressed && key.keycode == KEY_WIN) {
                launcher_open = !launcher_open;
                if (launcher_open) launcher_reset_query();
                dirty_full = 1;
                dirty_panel = 1;
                cursor_dirty = 1;
                activity = 1;
                continue;
            }

            if (launcher_open) {
                if (key.pressed && key.keycode == KEY_ESC) {
                    launcher_open = 0;
                    dirty_full = 1;
                    cursor_dirty = 1;
                    activity = 1;
                    continue;
                }
                if (key.pressed && key.keycode == KEY_BACKSPACE) {
                    if (launcher_query_len > 0) {
                        launcher_query_len--;
                        launcher_query[launcher_query_len] = 0;
                        launcher_selection = 0;
                        dirty_panel = 1;
                        activity = 1;
                    }
                    continue;
                }
                if (key.pressed && (key.keycode == KEY_UP || key.keycode == KEY_DOWN)) {
                    int matches = launcher_match_count();
                    if (matches > 0) {
                        if (key.keycode == KEY_UP && launcher_selection > 0) {
                            launcher_selection--;
                        } else if (key.keycode == KEY_DOWN && launcher_selection + 1 < matches) {
                            launcher_selection++;
                        }
                    }
                    dirty_panel = 1;
                    activity = 1;
                    continue;
                }
                if (key.pressed && key.keycode == KEY_ENTER) {
                    int matches = launcher_match_count();
                    if (matches > 0) {
                        app_type_t type = launcher_match_type(launcher_selection);
                        desktop_create_window(type);
                    }
                    launcher_open = 0;
                    dirty_full = 1;
                    dirty_panel = 1;
                    cursor_dirty = 1;
                    activity = 1;
                    continue;
                }
                if (key.ascii >= 32 && key.ascii < 127) {
                    if (launcher_query_len < (int)sizeof(launcher_query) - 1) {
                        launcher_query[launcher_query_len++] = key.ascii;
                        launcher_query[launcher_query_len] = 0;
                        launcher_selection = 0;
                        dirty_panel = 1;
                        activity = 1;
                    }
                    continue;
                }
                continue;
            }

            if (active_index >= 0) {
                window_t *win = &windows[active_index];
                if (win->type == APP_FILES) {
                    file_manager_handle_key(&win->file_manager, &key);
                    dirty_window = active_index;
                    cursor_dirty = 1;
                    activity = 1;
                    continue;
                }
                if (win->type == APP_BROWSER) {
                    browser_handle_key(&win->browser, &key);
                    dirty_window = active_index;
                    cursor_dirty = 1;
                    activity = 1;
                    continue;
                }
                if (win->type == APP_TERMINAL) {
                    if (input_is_shift_down() &&
                        (key.keycode == KEY_UP || key.keycode == KEY_DOWN)) {
                        if (key.keycode == KEY_UP) terminal_scroll_up(&win->terminal);
                        else terminal_scroll_down(&win->terminal);
                        dirty_window = active_index;
                        cursor_dirty = 1;
                        activity = 1;
                        continue;
                    }
                    shell_handle_key(&win->shell, &key);
                    dirty_window = active_index;
                    cursor_dirty = 1;
                    activity = 1;
                    if (shell_should_exit(&win->shell)) {
                        desktop_close_window(active_index);
                        dirty_full = 1;
                        dirty_panel = 1;
                        cursor_dirty = 1;
                        activity = 1;
                    }
                }
            }
        }

        mouse_event_t mouse;
        while (input_poll_mouse(&mouse)) {
            mouse_x = mouse.x;
            mouse_y = mouse.y;
            if (mouse_x > (int)gfx_width() - 1) mouse_x = (int)gfx_width() - 1;
            if (mouse_y > (int)gfx_height() - 1) mouse_y = (int)gfx_height() - 1;
            mouse_buttons = mouse.buttons;
            if (mouse_x != prev_cursor_x || mouse_y != prev_cursor_y) {
                cursor_dirty = 1;
            }
            activity = 1;
        }

        if (ticks == last_tick) {
            asm volatile("hlt");
            continue;
        }
        last_tick = ticks;
        net_poll();

        if (uptime_seconds != last_uptime) {
            last_uptime = uptime_seconds;
            dirty_panel = 1;
        }

        if (handle_mouse_click()) {
            dirty_full = 1;
            dirty_panel = 1;
            cursor_dirty = 1;
            activity = 1;
        }
        prev_mouse_buttons = mouse_buttons;

        int need_render = dirty_full || dirty_panel || dirty_window >= 0 || cursor_dirty;
        if (!activity && !need_render) {
            idle_accum += (u32)settings.idle_fps;
            if (idle_accum < PIT_HZ) {
                continue;
            }
            idle_accum -= PIT_HZ;
        } else {
            idle_accum = 0;
        }

        int did_full = 0;

        if (dirty_full) {
            draw_background();
            for (int i = 0; i < window_count; i++) {
                if (i == active_index) continue;
                draw_window(&windows[i]);
            }
            if (active_index >= 0) {
                draw_window(&windows[active_index]);
            }
            draw_panel();
            draw_launcher();
            gfx_present();
            dirty_full = 0;
            dirty_panel = 0;
            dirty_window = -1;
            did_full = 1;
        } else if (dirty_window >= 0 && dirty_window < window_count) {
            window_t *win = &windows[dirty_window];
            draw_window(win);
            gfx_present_rect(win->x, win->y, win->w, win->h);
            dirty_window = -1;
        }

        if (!did_full && dirty_panel) {
            int panel_y = (int)gfx_height() - PANEL_HEIGHT;
            draw_panel();
            gfx_present_rect(0, panel_y, (int)gfx_width(), PANEL_HEIGHT);
            if (launcher_open) {
                int lx, ly, lw, lh;
                launcher_layout(&lx, &ly, &lw, &lh, 0, 0, 0);
                draw_launcher();
                gfx_present_rect(lx, ly, lw, lh);
            }
            dirty_panel = 0;
        }

        if (cursor_dirty) {
            gfx_present_rect(prev_cursor_x, prev_cursor_y, cursor_w, cursor_h);
            draw_mouse_cursor_front();
            prev_cursor_x = mouse_x;
            prev_cursor_y = mouse_y;
            cursor_dirty = 0;
        }
    }
}
