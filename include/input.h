#ifndef INPUT_H
#define INPUT_H

#include "types.h"

typedef enum {
    KEY_NONE = 0,
    KEY_BACKSPACE,
    KEY_ENTER,
    KEY_ESC,
    KEY_TAB,
    KEY_UP,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_WIN,
    KEY_ALT
} keycode_t;

typedef struct {
    u8 pressed;
    u8 ascii;
    keycode_t keycode;
} key_event_t;

typedef struct {
    int x;
    int y;
    int dx;
    int dy;
    u8 buttons;
} mouse_event_t;

void input_init(void);
void input_handle_scancode(u8 scancode);
void input_handle_mouse_byte(u8 data);
int input_poll_key(key_event_t *event);
int input_poll_mouse(mouse_event_t *event);
int input_is_shift_down(void);
int input_is_alt_down(void);

#endif
