#ifndef GFX_H
#define GFX_H

#include "types.h"

#define FONT_WIDTH 8
#define FONT_HEIGHT 16

void gfx_init(u32 *fb, u64 width, u64 height, u64 pitch);
void gfx_enable_backbuffer(int enabled);
void gfx_clear(u32 color);
void gfx_draw_rect(int x, int y, int w, int h, u32 color);
void gfx_draw_rect_front(int x, int y, int w, int h, u32 color);
void gfx_draw_char(char c, int x, int y, u32 color);
void gfx_draw_char_clipped(char c, int x, int y, u32 color,
                           int clip_x, int clip_y, int clip_w, int clip_h);
void gfx_draw_text(const char *s, int x, int y, u32 color);
void gfx_draw_text_clipped(const char *s, int x, int y, u32 color,
                           int clip_x, int clip_y, int clip_w, int clip_h);
void gfx_present(void);
void gfx_present_rect(int x, int y, int w, int h);
int gfx_backbuffer_enabled(void);

u64 gfx_width(void);
u64 gfx_height(void);
u64 gfx_pitch(void);

#endif
