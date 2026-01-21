#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>

__attribute__((used, section(".requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};

__attribute__((used, section(".requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0
};

__attribute__((used, section(".requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0
};

__attribute__((used, section(".requests_start")))
static volatile LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".requests_end")))
static volatile LIMINE_REQUESTS_END_MARKER;

static volatile struct limine_framebuffer *framebuffer;
static uint32_t *fb;
static uint64_t width, height, pitch;
static uint32_t fg_color = 0x00FF00;
static uint32_t bg_color = 0x000000;
static uint64_t cursor_x = 0;
static uint64_t cursor_y = 0;

#define COLOR_BLACK   0x000000
#define COLOR_RED     0xFF0000
#define COLOR_GREEN   0x00FF00
#define COLOR_YELLOW  0xFFFF00
#define COLOR_BLUE    0x0000FF
#define COLOR_MAGENTA 0xFF00FF
#define COLOR_CYAN    0x00FFFF
#define COLOR_WHITE   0xFFFFFF
#define COLOR_ORANGE  0xFF8800
#define COLOR_PINK    0xFF69B4
#define COLOR_LIME    0x00FF80

#define FONT_WIDTH 8
#define FONT_HEIGHT 16

#define PAGE_SIZE 4096
#define HEAP_SIZE (16 * 1024 * 1024)

typedef struct block {
    size_t size;
    bool free;
    struct block *next;
} block_t;

static uint8_t heap[HEAP_SIZE];
static block_t *heap_start = NULL;
static uint64_t heap_allocated = 0;
static uint64_t heap_freed = 0;
static uint64_t heap_blocks = 0;

static uint64_t pmm_total_pages = 0;
static uint64_t pmm_used_pages = 0;
static uint64_t pmm_free_pages = 0;

static uint64_t ticks = 0;
static uint64_t uptime_seconds = 0;

static const uint8_t font[128][16] = {
    [' '] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['A'] = {0x00, 0x00, 0x18, 0x3C, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['B'] = {0x00, 0x00, 0x7C, 0x66, 0x66, 0x7C, 0x66, 0x66, 0x66, 0x66, 0x7C, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['C'] = {0x00, 0x00, 0x3C, 0x66, 0x60, 0x60, 0x60, 0x60, 0x60, 0x66, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['D'] = {0x00, 0x00, 0x78, 0x6C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x6C, 0x78, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['E'] = {0x00, 0x00, 0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x60, 0x60, 0x7E, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['F'] = {0x00, 0x00, 0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x60, 0x60, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['G'] = {0x00, 0x00, 0x3C, 0x66, 0x60, 0x60, 0x6E, 0x66, 0x66, 0x66, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['H'] = {0x00, 0x00, 0x66, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66, 0x66, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['I'] = {0x00, 0x00, 0x3C, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['J'] = {0x00, 0x00, 0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x6C, 0x6C, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['K'] = {0x00, 0x00, 0x66, 0x6C, 0x78, 0x70, 0x78, 0x6C, 0x66, 0x66, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['L'] = {0x00, 0x00, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x7E, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['M'] = {0x00, 0x00, 0x63, 0x77, 0x7F, 0x6B, 0x63, 0x63, 0x63, 0x63, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['N'] = {0x00, 0x00, 0x66, 0x76, 0x7E, 0x7E, 0x6E, 0x66, 0x66, 0x66, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['O'] = {0x00, 0x00, 0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['P'] = {0x00, 0x00, 0x7C, 0x66, 0x66, 0x7C, 0x60, 0x60, 0x60, 0x60, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['Q'] = {0x00, 0x00, 0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x0E, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['R'] = {0x00, 0x00, 0x7C, 0x66, 0x66, 0x7C, 0x78, 0x6C, 0x66, 0x66, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['S'] = {0x00, 0x00, 0x3C, 0x66, 0x60, 0x3C, 0x06, 0x06, 0x06, 0x66, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['T'] = {0x00, 0x00, 0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['U'] = {0x00, 0x00, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['V'] = {0x00, 0x00, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['W'] = {0x00, 0x00, 0x63, 0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['X'] = {0x00, 0x00, 0x66, 0x66, 0x3C, 0x18, 0x18, 0x3C, 0x66, 0x66, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['Y'] = {0x00, 0x00, 0x66, 0x66, 0x66, 0x3C, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['Z'] = {0x00, 0x00, 0x7E, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x60, 0x60, 0x7E, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['a'] = {0x00, 0x00, 0x00, 0x00, 0x3C, 0x06, 0x3E, 0x66, 0x66, 0x66, 0x3E, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['b'] = {0x00, 0x00, 0x60, 0x60, 0x7C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x7C, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['c'] = {0x00, 0x00, 0x00, 0x00, 0x3C, 0x66, 0x60, 0x60, 0x60, 0x66, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['d'] = {0x00, 0x00, 0x06, 0x06, 0x3E, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3E, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['e'] = {0x00, 0x00, 0x00, 0x00, 0x3C, 0x66, 0x7E, 0x60, 0x60, 0x66, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['f'] = {0x00, 0x00, 0x0E, 0x18, 0x18, 0x3C, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['g'] = {0x00, 0x00, 0x00, 0x00, 0x3E, 0x66, 0x66, 0x66, 0x3E, 0x06, 0x66, 0x3C, 0x00, 0x00, 0x00, 0x00},
    ['h'] = {0x00, 0x00, 0x60, 0x60, 0x6C, 0x76, 0x66, 0x66, 0x66, 0x66, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['i'] = {0x00, 0x00, 0x18, 0x00, 0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['j'] = {0x00, 0x00, 0x06, 0x00, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x66, 0x3C, 0x00, 0x00, 0x00, 0x00},
    ['k'] = {0x00, 0x00, 0x60, 0x60, 0x66, 0x6C, 0x78, 0x6C, 0x66, 0x66, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['l'] = {0x00, 0x00, 0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['m'] = {0x00, 0x00, 0x00, 0x00, 0x66, 0x7F, 0x6B, 0x6B, 0x63, 0x63, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['n'] = {0x00, 0x00, 0x00, 0x00, 0x6C, 0x76, 0x66, 0x66, 0x66, 0x66, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['o'] = {0x00, 0x00, 0x00, 0x00, 0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['p'] = {0x00, 0x00, 0x00, 0x00, 0x7C, 0x66, 0x66, 0x66, 0x7C, 0x60, 0x60, 0x60, 0x00, 0x00, 0x00, 0x00},
    ['q'] = {0x00, 0x00, 0x00, 0x00, 0x3E, 0x66, 0x66, 0x66, 0x3E, 0x06, 0x06, 0x06, 0x00, 0x00, 0x00, 0x00},
    ['r'] = {0x00, 0x00, 0x00, 0x00, 0x6C, 0x76, 0x60, 0x60, 0x60, 0x60, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['s'] = {0x00, 0x00, 0x00, 0x00, 0x3E, 0x60, 0x60, 0x3C, 0x06, 0x06, 0x7C, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['t'] = {0x00, 0x00, 0x18, 0x18, 0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x0E, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['u'] = {0x00, 0x00, 0x00, 0x00, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3E, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['v'] = {0x00, 0x00, 0x00, 0x00, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['w'] = {0x00, 0x00, 0x00, 0x00, 0x63, 0x63, 0x6B, 0x6B, 0x7F, 0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['x'] = {0x00, 0x00, 0x00, 0x00, 0x66, 0x3C, 0x18, 0x18, 0x3C, 0x66, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['y'] = {0x00, 0x00, 0x00, 0x00, 0x66, 0x66, 0x66, 0x66, 0x3E, 0x06, 0x0C, 0x78, 0x00, 0x00, 0x00, 0x00},
    ['z'] = {0x00, 0x00, 0x00, 0x00, 0x7E, 0x0C, 0x18, 0x30, 0x60, 0x60, 0x7E, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['0'] = {0x00, 0x00, 0x3C, 0x66, 0x6E, 0x6E, 0x76, 0x76, 0x66, 0x66, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['1'] = {0x00, 0x00, 0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['2'] = {0x00, 0x00, 0x3C, 0x66, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x60, 0x7E, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['3'] = {0x00, 0x00, 0x3C, 0x66, 0x06, 0x1C, 0x06, 0x06, 0x06, 0x66, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['4'] = {0x00, 0x00, 0x0C, 0x1C, 0x3C, 0x6C, 0x6C, 0x7E, 0x0C, 0x0C, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['5'] = {0x00, 0x00, 0x7E, 0x60, 0x60, 0x7C, 0x06, 0x06, 0x06, 0x66, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['6'] = {0x00, 0x00, 0x3C, 0x60, 0x60, 0x7C, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['7'] = {0x00, 0x00, 0x7E, 0x06, 0x0C, 0x18, 0x30, 0x30, 0x30, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['8'] = {0x00, 0x00, 0x3C, 0x66, 0x66, 0x3C, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['9'] = {0x00, 0x00, 0x3C, 0x66, 0x66, 0x66, 0x3E, 0x06, 0x06, 0x0C, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['.'] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00},
    [','] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x30, 0x00, 0x00, 0x00, 0x00},
    [':'] = {0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00},
    [';'] = {0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x18, 0x18, 0x30, 0x00, 0x00, 0x00, 0x00},
    ['!'] = {0x00, 0x00, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['?'] = {0x00, 0x00, 0x3C, 0x66, 0x06, 0x0C, 0x18, 0x18, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['-'] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['_'] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00},
    ['/'] = {0x00, 0x00, 0x00, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['\\'] = {0x00, 0x00, 0x00, 0x60, 0x30, 0x18, 0x0C, 0x06, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['('] = {0x00, 0x00, 0x0C, 0x18, 0x30, 0x30, 0x30, 0x30, 0x30, 0x18, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00},
    [')'] = {0x00, 0x00, 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x18, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['['] = {0x00, 0x00, 0x3C, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00},
    [']'] = {0x00, 0x00, 0x3C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['>'] = {0x00, 0x00, 0x00, 0x60, 0x30, 0x18, 0x0C, 0x18, 0x30, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['<'] = {0x00, 0x00, 0x00, 0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['='] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['+'] = {0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x7E, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['*'] = {0x00, 0x00, 0x00, 0x66, 0x3C, 0x18, 0x3C, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['#'] = {0x00, 0x00, 0x36, 0x36, 0x7F, 0x36, 0x36, 0x7F, 0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['$'] = {0x00, 0x18, 0x3E, 0x58, 0x58, 0x3C, 0x1A, 0x1A, 0x7C, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['%'] = {0x00, 0x00, 0x62, 0x66, 0x0C, 0x18, 0x30, 0x66, 0x46, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['&'] = {0x00, 0x00, 0x38, 0x6C, 0x6C, 0x38, 0x76, 0x66, 0x66, 0x66, 0x3B, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['\''] = {0x00, 0x00, 0x18, 0x18, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['"'] = {0x00, 0x00, 0x66, 0x66, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['@'] = {0x00, 0x00, 0x3C, 0x66, 0x6E, 0x6A, 0x6E, 0x60, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
};

void *memset(void *s, int c, size_t n) {
    uint8_t *p = s;
    while (n--) *p++ = (uint8_t)c;
    return s;
}

void *memcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = dest;
    const uint8_t *s = src;
    while (n--) *d++ = *s++;
    return dest;
}

size_t strlen(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

char *strcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d++ = *src++));
    return dest;
}

void heap_init(void) {
    heap_start = (block_t *)heap;
    heap_start->size = HEAP_SIZE - sizeof(block_t);
    heap_start->free = true;
    heap_start->next = NULL;
    heap_blocks = 1;
}

void *malloc(size_t size) {
    if (size == 0) return NULL;
    
    size = (size + 7) & ~7;
    
    block_t *current = heap_start;
    while (current) {
        if (current->free && current->size >= size) {
            if (current->size >= size + sizeof(block_t) + 8) {
                block_t *new_block = (block_t *)((uint8_t *)current + sizeof(block_t) + size);
                new_block->size = current->size - size - sizeof(block_t);
                new_block->free = true;
                new_block->next = current->next;
                current->next = new_block;
                current->size = size;
                heap_blocks++;
            }
            current->free = false;
            heap_allocated += current->size;
            return (void *)((uint8_t *)current + sizeof(block_t));
        }
        current = current->next;
    }
    
    return NULL;
}

void free(void *ptr) {
    if (!ptr) return;
    
    block_t *block = (block_t *)((uint8_t *)ptr - sizeof(block_t));
    block->free = true;
    heap_freed += block->size;
    
    block_t *current = heap_start;
    while (current && current->next) {
        if (current->free && current->next->free) {
            current->size += sizeof(block_t) + current->next->size;
            current->next = current->next->next;
            heap_blocks--;
        } else {
            current = current->next;
        }
    }
}

void pmm_init(void) {
    if (memmap_request.response == NULL) return;
    
    struct limine_memmap_response *memmap = memmap_request.response;
    
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        uint64_t pages = entry->length / PAGE_SIZE;
        pmm_total_pages += pages;
        
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            pmm_free_pages += pages;
        } else {
            pmm_used_pages += pages;
        }
    }
}

void draw_char(char c, uint64_t x, uint64_t y, uint32_t color) {
    const uint8_t *glyph = font[(uint8_t)c];
    for (int dy = 0; dy < FONT_HEIGHT; dy++) {
        for (int dx = 0; dx < FONT_WIDTH; dx++) {
            if (glyph[dy] & (1 << (7 - dx))) {
                uint64_t px = x + dx;
                uint64_t py = y + dy;
                if (px < width && py < height) {
                    fb[py * (pitch / 4) + px] = color;
                }
            }
        }
    }
}

void clear_screen(void) {
    for (uint64_t y = 0; y < height; y++) {
        for (uint64_t x = 0; x < width; x++) {
            fb[y * (pitch / 4) + x] = bg_color;
        }
    }
    cursor_x = 0;
    cursor_y = 0;
}

void scroll_up(void) {
    for (uint64_t y = FONT_HEIGHT; y < height; y++) {
        for (uint64_t x = 0; x < width; x++) {
            fb[(y - FONT_HEIGHT) * (pitch / 4) + x] = fb[y * (pitch / 4) + x];
        }
    }
    for (uint64_t y = height - FONT_HEIGHT; y < height; y++) {
        for (uint64_t x = 0; x < width; x++) {
            fb[y * (pitch / 4) + x] = bg_color;
        }
    }
}

void putchar_color(char c, uint32_t color) {
    if (c == '\n') {
        cursor_x = 0;
        cursor_y += FONT_HEIGHT;
        if (cursor_y >= height) {
            scroll_up();
            cursor_y = height - FONT_HEIGHT;
        }
        return;
    }
    if (c == '\b') {
        if (cursor_x >= FONT_WIDTH) {
            cursor_x -= FONT_WIDTH;
            for (uint64_t dy = 0; dy < FONT_HEIGHT; dy++) {
                for (uint64_t dx = 0; dx < FONT_WIDTH; dx++) {
                    fb[(cursor_y + dy) * (pitch / 4) + cursor_x + dx] = bg_color;
                }
            }
        }
        return;
    }
    draw_char(c, cursor_x, cursor_y, color);
    cursor_x += FONT_WIDTH;
    if (cursor_x >= width) {
        cursor_x = 0;
        cursor_y += FONT_HEIGHT;
        if (cursor_y >= height) {
            scroll_up();
            cursor_y = height - FONT_HEIGHT;
        }
    }
}

void putchar(char c) {
    putchar_color(c, fg_color);
}

void print_color(const char *s, uint32_t color) {
    while (*s) {
        putchar_color(*s++, color);
    }
}

void print(const char *s) {
    print_color(s, fg_color);
}

void print_hex(uint64_t n) {
    char buf[17];
    buf[16] = 0;
    for (int i = 15; i >= 0; i--) {
        int digit = n & 0xF;
        buf[i] = digit < 10 ? '0' + digit : 'a' + digit - 10;
        n >>= 4;
    }
    print("0x");
    print(buf);
}

void print_dec(uint64_t n) {
    if (n == 0) {
        putchar('0');
        return;
    }
    char buf[21];
    int i = 20;
    buf[i] = 0;
    while (n > 0) {
        buf[--i] = '0' + (n % 10);
        n /= 10;
    }
    print(&buf[i]);
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline void io_wait(void) {
    outb(0x80, 0);
}

void pit_init(uint32_t frequency) {
    uint32_t divisor = 1193180 / frequency;
    outb(0x43, 0x36);
    outb(0x40, divisor & 0xFF);
    outb(0x40, (divisor >> 8) & 0xFF);
}

void timer_handler(void) {
    ticks++;
    if (ticks % 100 == 0) {
        uptime_seconds++;
    }
}

#define KEYBOARD_DATA_PORT 0x60
#define KEYBOARD_STATUS_PORT 0x64

static char scancode_to_ascii[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' '
};

char read_key(void) {
    while (1) {
        uint8_t status = inb(KEYBOARD_STATUS_PORT);
        if (status & 0x01) {
            uint8_t scancode = inb(KEYBOARD_DATA_PORT);
            if (scancode < 128 && scancode_to_ascii[scancode]) {
                return scancode_to_ascii[scancode];
            }
        }
    }
}

#define MAX_CMD_LEN 256
#define MAX_ARGS 16

void execute_command(char *cmd) {
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
        print_color("Available commands:\n", COLOR_CYAN);
        print_color("  help      ", COLOR_YELLOW);
        print("- Show this help message\n");
        print_color("  clear     ", COLOR_YELLOW);
        print("- Clear the screen\n");
        print_color("  echo      ", COLOR_YELLOW);
        print("- Print arguments\n");
        print_color("  uname     ", COLOR_YELLOW);
        print("- Show system information\n");
        print_color("  meminfo   ", COLOR_YELLOW);
        print("- Display physical memory information\n");
        print_color("  heapinfo  ", COLOR_YELLOW);
        print("- Display heap allocator statistics\n");
        print_color("  malloc    ", COLOR_YELLOW);
        print("- Test memory allocation (malloc <size>)\n");
        print_color("  uptime    ", COLOR_YELLOW);
        print("- Show system uptime\n");
        print_color("  color     ", COLOR_YELLOW);
        print("- Change text color\n");
        print_color("  banner    ", COLOR_YELLOW);
        print("- Display Fusion OS banner\n");
        print_color("  cpuinfo   ", COLOR_YELLOW);
        print("- Display CPU information\n");
        print_color("  reboot    ", COLOR_YELLOW);
        print("- Reboot the system\n");
        print_color("  halt      ", COLOR_YELLOW);
        print("- Halt the system\n");
    }
    else if (strcmp(args[0], "clear") == 0) {
        clear_screen();
    }
    else if (strcmp(args[0], "echo") == 0) {
        for (int i = 1; i < argc; i++) {
            print(args[i]);
            if (i < argc - 1) putchar(' ');
        }
        putchar('\n');
    }
    else if (strcmp(args[0], "uname") == 0) {
        print_color("Fusion OS ", COLOR_CYAN);
        print_color("v1.0 ", COLOR_YELLOW);
        print_color("x86_64\n", COLOR_GREEN);
        print("Bootloader: Limine v8.4.0\n");
        print("Architecture: x86_64\n");
        print("Features: Memory Management, Heap Allocator, Timer\n");
    }
    else if (strcmp(args[0], "meminfo") == 0) {
        print_color("Physical Memory Information:\n", COLOR_CYAN);
        print_color("  Total Pages: ", COLOR_YELLOW);
        print_dec(pmm_total_pages);
        print(" (");
        print_dec((pmm_total_pages * PAGE_SIZE) / 1024 / 1024);
        print(" MB)\n");
        
        print_color("  Free Pages:  ", COLOR_GREEN);
        print_dec(pmm_free_pages);
        print(" (");
        print_dec((pmm_free_pages * PAGE_SIZE) / 1024 / 1024);
        print(" MB)\n");
        
        print_color("  Used Pages:  ", COLOR_RED);
        print_dec(pmm_used_pages);
        print(" (");
        print_dec((pmm_used_pages * PAGE_SIZE) / 1024 / 1024);
        print(" MB)\n");
    }
    else if (strcmp(args[0], "heapinfo") == 0) {
        print_color("Heap Allocator Statistics:\n", COLOR_CYAN);
        print_color("  Heap Size:       ", COLOR_YELLOW);
        print_dec(HEAP_SIZE / 1024);
        print(" KB\n");
        
        print_color("  Allocated:       ", COLOR_GREEN);
        print_dec(heap_allocated);
        print(" bytes\n");
        
        print_color("  Freed:           ", COLOR_BLUE);
        print_dec(heap_freed);
        print(" bytes\n");
        
        print_color("  Currently Used:  ", COLOR_MAGENTA);
        print_dec(heap_allocated - heap_freed);
        print(" bytes\n");
        
        print_color("  Active Blocks:   ", COLOR_ORANGE);
        print_dec(heap_blocks);
        print("\n");
    }
    else if (strcmp(args[0], "malloc") == 0) {
        if (argc < 2) {
            print("Usage: malloc <size>\n");
        } else {
            uint64_t size = 0;
            for (char *p = args[1]; *p; p++) {
                if (*p >= '0' && *p <= '9') {
                    size = size * 10 + (*p - '0');
                }
            }
            
            void *ptr = malloc(size);
            if (ptr) {
                print_color("Allocated ", COLOR_GREEN);
                print_dec(size);
                print(" bytes at ");
                print_hex((uint64_t)ptr);
                print("\n");
                free(ptr);
                print_color("Memory freed\n", COLOR_YELLOW);
            } else {
                print_color("Allocation failed!\n", COLOR_RED);
            }
        }
    }
    else if (strcmp(args[0], "uptime") == 0) {
        print_color("System Uptime: ", COLOR_CYAN);
        uint64_t hours = uptime_seconds / 3600;
        uint64_t minutes = (uptime_seconds % 3600) / 60;
        uint64_t seconds = uptime_seconds % 60;
        
        print_dec(hours);
        print("h ");
        print_dec(minutes);
        print("m ");
        print_dec(seconds);
        print("s\n");
        
        print_color("Ticks: ", COLOR_YELLOW);
        print_dec(ticks);
        print("\n");
    }
    else if (strcmp(args[0], "cpuinfo") == 0) {
        print_color("CPU Information:\n", COLOR_CYAN);
        
        uint32_t eax, ebx, ecx, edx;
        asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0));
        
        print_color("  Vendor: ", COLOR_YELLOW);
        char vendor[13];
        *(uint32_t *)(vendor + 0) = ebx;
        *(uint32_t *)(vendor + 4) = edx;
        *(uint32_t *)(vendor + 8) = ecx;
        vendor[12] = 0;
        print(vendor);
        print("\n");
        
        asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
        
        print_color("  Features: ", COLOR_GREEN);
        if (edx & (1 << 0)) print("FPU ");
        if (edx & (1 << 4)) print("TSC ");
        if (edx & (1 << 5)) print("MSR ");
        if (edx & (1 << 9)) print("APIC ");
        if (edx & (1 << 23)) print("MMX ");
        if (edx & (1 << 25)) print("SSE ");
        if (edx & (1 << 26)) print("SSE2 ");
        if (ecx & (1 << 0)) print("SSE3 ");
        print("\n");
    }
    else if (strcmp(args[0], "color") == 0) {
        if (argc < 2) {
            print("Usage: color <red|green|blue|cyan|yellow|white|pink|orange|lime|magenta>\n");
        } else {
            if (strcmp(args[1], "red") == 0) fg_color = COLOR_RED;
            else if (strcmp(args[1], "green") == 0) fg_color = COLOR_GREEN;
            else if (strcmp(args[1], "blue") == 0) fg_color = COLOR_BLUE;
            else if (strcmp(args[1], "cyan") == 0) fg_color = COLOR_CYAN;
            else if (strcmp(args[1], "yellow") == 0) fg_color = COLOR_YELLOW;
            else if (strcmp(args[1], "white") == 0) fg_color = COLOR_WHITE;
            else if (strcmp(args[1], "pink") == 0) fg_color = COLOR_PINK;
            else if (strcmp(args[1], "orange") == 0) fg_color = COLOR_ORANGE;
            else if (strcmp(args[1], "lime") == 0) fg_color = COLOR_LIME;
            else if (strcmp(args[1], "magenta") == 0) fg_color = COLOR_MAGENTA;
            else {
                print_color("Unknown color!\n", COLOR_RED);
            }
        }
    }
    else if (strcmp(args[0], "banner") == 0) {
        print_color("\n", COLOR_WHITE);
        print_color("  ███████╗██╗   ██╗███████╗██╗ ██████╗ ███╗   ██╗\n", COLOR_CYAN);
        print_color("  ██╔════╝██║   ██║██╔════╝██║██╔═══██╗████╗  ██║\n", COLOR_CYAN);
        print_color("  █████╗  ██║   ██║███████╗██║██║   ██║██╔██╗ ██║\n", COLOR_PINK);
        print_color("  ██╔══╝  ██║   ██║╚════██║██║██║   ██║██║╚██╗██║\n", COLOR_PINK);
        print_color("  ██║     ╚██████╔╝███████║██║╚██████╔╝██║ ╚████║\n", COLOR_YELLOW);
        print_color("  ╚═╝      ╚═════╝ ╚══════╝╚═╝ ╚═════╝ ╚═╝  ╚═══╝\n", COLOR_YELLOW);
        print_color("\n            A minimal x86_64 operating system\n\n", COLOR_GREEN);
    }
    else if (strcmp(args[0], "reboot") == 0) {
        print_color("Rebooting...\n", COLOR_YELLOW);
        uint8_t temp;
        asm volatile("cli");
        do {
            temp = inb(0x64);
            if (temp & 0x01) inb(0x60);
        } while (temp & 0x02);
        outb(0x64, 0xFE);
        while (1) asm volatile("hlt");
    }
    else if (strcmp(args[0], "halt") == 0) {
        print_color("System halted.\n", COLOR_RED);
        while (1) asm volatile("hlt");
    }
    else {
        print_color("Unknown command: ", COLOR_RED);
        print(args[0]);
        print_color("\nType 'help' for available commands\n", COLOR_YELLOW);
    }
}

void shell(void) {
    char cmd[MAX_CMD_LEN];
    int cmd_len = 0;
    
    print_color("\n", COLOR_WHITE);
    print_color("  ███████╗██╗   ██╗███████╗██╗ ██████╗ ███╗   ██╗\n", COLOR_CYAN);
    print_color("  ██╔════╝██║   ██║██╔════╝██║██╔═══██╗████╗  ██║\n", COLOR_CYAN);
    print_color("  █████╗  ██║   ██║███████╗██║██║   ██║██╔██╗ ██║\n", COLOR_PINK);
    print_color("  ██╔══╝  ██║   ██║╚════██║██║██║   ██║██║╚██╗██║\n", COLOR_PINK);
    print_color("  ██║     ╚██████╔╝███████║██║╚██████╔╝██║ ╚████║\n", COLOR_YELLOW);
    print_color("  ╚═╝      ╚═════╝ ╚══════╝╚═╝ ╚═════╝ ╚═╝  ╚═══╝\n", COLOR_YELLOW);
    print_color("\n            A minimal x86_64 operating system\n\n", COLOR_GREEN);
    
    print_color("Welcome to Fusion OS Shell v1.0\n", COLOR_WHITE);
    print_color("Type 'help' for available commands\n\n", COLOR_CYAN);
    
    while (1) {
        print_color("fusion", COLOR_GREEN);
        print_color("> ", COLOR_YELLOW);
        cmd_len = 0;
        
        while (1) {
            char c = read_key();
            
            if (c == '\n') {
                putchar('\n');
                cmd[cmd_len] = 0;
                execute_command(cmd);
                break;
            }
            else if (c == '\b') {
                if (cmd_len > 0) {
                    cmd_len--;
                    putchar('\b');
                }
            }
            else if (cmd_len < MAX_CMD_LEN - 1) {
                cmd[cmd_len++] = c;
                putchar(c);
            }
        }
    }
}

void _start(void) {
    if (framebuffer_request.response == NULL || framebuffer_request.response->framebuffer_count < 1) {
        while (1) asm volatile("hlt");
    }
    
    framebuffer = framebuffer_request.response->framebuffers[0];
    fb = (uint32_t *)framebuffer->address;
    width = framebuffer->width;
    height = framebuffer->height;
    pitch = framebuffer->pitch;
    
    clear_screen();
    
    heap_init();
    pmm_init();
    pit_init(100);
    
    shell();
    
    while (1) asm volatile("hlt");
}
