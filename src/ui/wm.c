#include "ui/wm.h"
#include "ui/terminal.h"
#include "kernel/memory.h"

wm_t wm;

void draw_rect(u64 x, u64 y, u64 w, u64 h, u32 color) {
    for (u64 py = y; py < y + h && py < wm.screen_height; py++) {
        for (u64 px = x; px < x + w && px < wm.screen_width; px++) {
            wm.framebuffer[py * (wm.pitch / 4) + px] = color;
        }
    }
}

void draw_window_border(window_t *win) {
    if (!(win->flags & WINDOW_VISIBLE)) return;
    
    u32 border = (win->flags & WINDOW_ACTIVE) ? 0x00FFFF : 0x888888;
    
    // Top border
    draw_rect(win->x, win->y, win->width, BORDER_WIDTH, border);
    // Bottom border
    draw_rect(win->x, win->y + win->height - BORDER_WIDTH, win->width, BORDER_WIDTH, border);
    // Left border
    draw_rect(win->x, win->y, BORDER_WIDTH, win->height, border);
    // Right border
    draw_rect(win->x + win->width - BORDER_WIDTH, win->y, BORDER_WIDTH, win->height, border);
    
    // Title bar
    draw_rect(win->x + BORDER_WIDTH, win->y + BORDER_WIDTH, 
              win->width - 2*BORDER_WIDTH, TITLE_HEIGHT - BORDER_WIDTH, 0x0066CC);
}

void wm_init(u32 *fb, u64 width, u64 height, u64 pitch) {
    wm.framebuffer = fb;
    wm.screen_width = width;
    wm.screen_height = height;
    wm.pitch = pitch;
    wm.window_count = 0;
    wm.active_window = 0;
}

window_t *wm_create_window(u64 w, u64 h, const char *title) {
    if (wm.window_count >= MAX_WINDOWS) return NULL;
    
    window_t *win = &wm.windows[wm.window_count];
    win->width = w;
    win->height = h;
    win->bg_color = 0x000000;
    win->border_color = 0x00FFFF;
    win->flags = WINDOW_VISIBLE;
    if (wm.window_count == 0) win->flags |= WINDOW_ACTIVE;
    strcpy(win->title, title);
    
    wm.window_count++;
    wm_tile_windows();
    
    return win;
}

void wm_tile_windows(void) {
    if (wm.window_count == 0) return;
    
    u64 usable_width = wm.screen_width;
    u64 usable_height = wm.screen_height;
    
    if (wm.window_count == 1) {
        // Full screen
        wm.windows[0].x = 0;
        wm.windows[0].y = 0;
        wm.windows[0].width = usable_width;
        wm.windows[0].height = usable_height;
    }
    else if (wm.window_count == 2) {
        // Split vertically 50/50
        u64 w = usable_width / 2;
        wm.windows[0].x = 0;
        wm.windows[0].y = 0;
        wm.windows[0].width = w;
        wm.windows[0].height = usable_height;
        
        wm.windows[1].x = w;
        wm.windows[1].y = 0;
        wm.windows[1].width = usable_width - w;
        wm.windows[1].height = usable_height;
    }
    else if (wm.window_count == 3) {
        // Master on left, 2 slaves on right
        u64 mw = usable_width / 2;
        u64 sh = usable_height / 2;
        
        wm.windows[0].x = 0;
        wm.windows[0].y = 0;
        wm.windows[0].width = mw;
        wm.windows[0].height = usable_height;
        
        wm.windows[1].x = mw;
        wm.windows[1].y = 0;
        wm.windows[1].width = usable_width - mw;
        wm.windows[1].height = sh;
        
        wm.windows[2].x = mw;
        wm.windows[2].y = sh;
        wm.windows[2].width = usable_width - mw;
        wm.windows[2].height = usable_height - sh;
    }
    else {
        // Grid layout
        u64 cols = 2;
        u64 rows = (wm.window_count + cols - 1) / cols;
        u64 w = usable_width / cols;
        u64 h = usable_height / rows;
        
        for (int i = 0; i < wm.window_count; i++) {
            int col = i % cols;
            int row = i / cols;
            wm.windows[i].x = col * w;
            wm.windows[i].y = row * h;
            wm.windows[i].width = w;
            wm.windows[i].height = h;
        }
    }
}

void wm_draw_all_windows(void) {
    for (int i = 0; i < wm.window_count; i++) {
        if (wm.windows[i].flags & WINDOW_VISIBLE) {
            draw_rect(wm.windows[i].x + BORDER_WIDTH, 
                     wm.windows[i].y + TITLE_HEIGHT,
                     wm.windows[i].width - 2*BORDER_WIDTH,
                     wm.windows[i].height - TITLE_HEIGHT - BORDER_WIDTH,
                     wm.windows[i].bg_color);
            draw_window_border(&wm.windows[i]);
        }
    }
}

void wm_focus_next(void) {
    if (wm.window_count == 0) return;
    wm.windows[wm.active_window].flags &= ~WINDOW_ACTIVE;
    wm.active_window = (wm.active_window + 1) % wm.window_count;
    wm.windows[wm.active_window].flags |= WINDOW_ACTIVE;
}

void wm_focus_prev(void) {
    if (wm.window_count == 0) return;
    wm.windows[wm.active_window].flags &= ~WINDOW_ACTIVE;
    wm.active_window = (wm.active_window - 1 + wm.window_count) % wm.window_count;
    wm.windows[wm.active_window].flags |= WINDOW_ACTIVE;
}

void wm_close_active_window(void) {
    if (wm.window_count == 0) return;
    
    for (int i = wm.active_window; i < wm.window_count - 1; i++) {
        wm.windows[i] = wm.windows[i + 1];
    }
    wm.window_count--;
    
    if (wm.active_window >= wm.window_count && wm.window_count > 0) {
        wm.active_window = wm.window_count - 1;
    }
    
    if (wm.window_count > 0) {
        wm.windows[wm.active_window].flags |= WINDOW_ACTIVE;
        wm_tile_windows();
    }
}
