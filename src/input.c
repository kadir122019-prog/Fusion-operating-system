#include "input.h"
#include "cpu.h"

#define KBD_BUFFER_SIZE 128

static volatile u8 scancode_buf[KBD_BUFFER_SIZE];
static volatile u8 scancode_head = 0;
static volatile u8 scancode_tail = 0;

static int mouse_x = 0;
static int mouse_y = 0;
static u8 mouse_buttons = 0;

static int extended = 0;
static int shift_down = 0;
static int alt_down = 0;

static int ps2_wait_read(void) {
    for (int i = 0; i < 100000; i++) {
        if (inb(0x64) & 0x01) return 1;
        io_wait();
    }
    return 0;
}

static int ps2_wait_write(void) {
    for (int i = 0; i < 100000; i++) {
        if (!(inb(0x64) & 0x02)) return 1;
        io_wait();
    }
    return 0;
}

static void ps2_write_cmd(u8 cmd) {
    if (!ps2_wait_write()) return;
    outb(0x64, cmd);
}

static void ps2_write_data(u8 data) {
    if (!ps2_wait_write()) return;
    outb(0x60, data);
}

static u8 ps2_read_data(void) {
    if (!ps2_wait_read()) return 0;
    return inb(0x60);
}

static void ps2_flush_output(void) {
    while (inb(0x64) & 0x01) {
        (void)inb(0x60);
    }
}

static const char scancode_ascii[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' '
};

static const char scancode_ascii_shift[128] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0, ' '
};

static void push_scancode(u8 scancode) {
    u8 next = (u8)(scancode_head + 1) % KBD_BUFFER_SIZE;
    if (next == scancode_tail) return;
    scancode_buf[scancode_head] = scancode;
    scancode_head = next;
}

static int pop_scancode(u8 *scancode) {
    if (scancode_tail == scancode_head) return 0;
    *scancode = scancode_buf[scancode_tail];
    scancode_tail = (u8)(scancode_tail + 1) % KBD_BUFFER_SIZE;
    return 1;
}

void input_handle_scancode(u8 scancode) {
    push_scancode(scancode);
}

void input_init(void) {
    mouse_x = 20;
    mouse_y = 20;
    mouse_buttons = 0;

    ps2_write_cmd(0xAD);
    ps2_write_cmd(0xA7);
    ps2_flush_output();

    ps2_write_cmd(0x20);
    u8 config = ps2_read_data();
    config |= 0x03;
    config &= (u8)~(1u << 4);
    config &= (u8)~(1u << 5);
    config |= (1u << 6);
    ps2_write_cmd(0x60);
    ps2_write_data(config);

    ps2_write_cmd(0xAE);
    ps2_write_cmd(0xA8);
    ps2_flush_output();

    ps2_write_data(0xF4);
    ps2_read_data();

    ps2_write_cmd(0xD4);
    ps2_write_data(0xF6);
    ps2_read_data();

    ps2_write_cmd(0xD4);
    ps2_write_data(0xF4);
    ps2_read_data();
}

static key_event_t translate_scancode(u8 scancode) {
    key_event_t event = {0, 0, KEY_NONE};
    if (scancode == 0xE0) {
        extended = 1;
        return event;
    }

    if (scancode & 0x80) {
        u8 code = scancode & 0x7F;
        if (code == 0x2A || code == 0x36) shift_down = 0;
        if (code == 0x38) {
            alt_down = 0;
            event.keycode = KEY_ALT;
            return event;
        }
        return event;
    }

    if (scancode == 0x2A || scancode == 0x36) {
        shift_down = 1;
        return event;
    }
    if (scancode == 0x38) {
        alt_down = 1;
        event.pressed = 1;
        event.keycode = KEY_ALT;
        return event;
    }

    event.pressed = 1;

    if (extended) {
        extended = 0;
        switch (scancode) {
            case 0x48: event.keycode = KEY_UP; return event;
            case 0x50: event.keycode = KEY_DOWN; return event;
            case 0x4B: event.keycode = KEY_LEFT; return event;
            case 0x4D: event.keycode = KEY_RIGHT; return event;
            case 0x5B: event.keycode = KEY_WIN; return event;
            default: return event;
        }
    }

    if (scancode == 0x1C) { event.keycode = KEY_ENTER; return event; }
    if (scancode == 0x0E) { event.keycode = KEY_BACKSPACE; return event; }
    if (scancode == 0x01) { event.keycode = KEY_ESC; return event; }
    if (scancode == 0x0F) { event.keycode = KEY_TAB; event.ascii = '\t'; return event; }

    if (scancode < 128) {
        event.ascii = shift_down ? scancode_ascii_shift[scancode] : scancode_ascii[scancode];
    }
    return event;
}

int input_poll_key(key_event_t *event) {
    u8 scancode;
    while (pop_scancode(&scancode)) {
        key_event_t translated = translate_scancode(scancode);
        if (translated.pressed || translated.ascii || translated.keycode != KEY_NONE) {
            *event = translated;
            return 1;
        }
    }
    return 0;
}

static int mouse_read(u8 *data) {
    u8 status = inb(0x64);
    if (!(status & 0x01)) return 0;
    if (!(status & 0x20)) return 0;
    *data = inb(0x60);
    return 1;
}

int input_poll_mouse(mouse_event_t *event) {
    static u8 packet[3];
    static int packet_index = 0;

    u8 data;
    while (mouse_read(&data)) {
        if (packet_index == 0 && !(data & 0x08)) {
            continue;
        }
        packet[packet_index++] = data;
        if (packet_index == 3) {
            packet_index = 0;
            int dx = (int)(int8_t)packet[1];
            int dy = (int)(int8_t)packet[2];
            mouse_buttons = packet[0] & 0x07;

            mouse_x += dx;
            mouse_y -= dy;
            if (mouse_x < 0) mouse_x = 0;
            if (mouse_y < 0) mouse_y = 0;

            event->x = mouse_x;
            event->y = mouse_y;
            event->dx = dx;
            event->dy = dy;
            event->buttons = mouse_buttons;
            return 1;
        }
    }
    return 0;
}

int input_is_shift_down(void) {
    return shift_down;
}

int input_is_alt_down(void) {
    return alt_down;
}
