#ifndef WM_H
#define WM_H

#include "types.h"

#define MAX_WINDOWS 10
#define BORDER_WIDTH 2
#define TITLE_HEIGHT 20

typedef struct {
    u64 x, y, width, height;
    char title[64];
    u32 border_color;
    u32 bg_color;
    u32 flags;
} window_t;

#define WINDOW_ACTIVE  1
#define WINDOW_VISIBLE 2

typedef struct {
    window_t windows[MAX_WINDOWS];
    int window_count;
    int active_window;
    u64 screen_width;
    u64 screen_height;
    u32 *framebuffer;
    u64 pitch;
} wm_t;

extern wm_t wm;

void wm_init(u32 *fb, u64 width, u64 height, u64 pitch);
window_t *wm_create_window(u64 w, u64 h, const char *title);
void wm_tile_windows(void);
void wm_draw_all_windows(void);
void wm_focus_next(void);
void wm_focus_prev(void);
void wm_close_active_window(void);

#endif
