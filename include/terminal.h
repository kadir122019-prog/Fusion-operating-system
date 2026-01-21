#ifndef TERMINAL_H
#define TERMINAL_H

#include "types.h"
#include <limine.h>

#define FONT_WIDTH 8
#define FONT_HEIGHT 16

extern u32 fg_color;
extern u32 bg_color;

void terminal_init(struct limine_framebuffer *fb_info);
void clear_screen(void);
void scroll_up(void);

void putchar(char c);
void putchar_color(char c, u32 color);
void print(const char *s);
void print_color(const char *s, u32 color);
void print_hex(u64 n);
void print_dec(u64 n);

void draw_char(char c, u64 x, u64 y, u32 color);

char read_key(void);

#endif
