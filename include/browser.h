#ifndef BROWSER_H
#define BROWSER_H

#include "types.h"
#include "input.h"

typedef struct {
    char url[128];
    int url_len;
    int scroll;
    int loading;
    char status[64];
    char *content;
    u32 content_len;
    u32 content_cap;
} browser_t;

void browser_init(browser_t *br);
void browser_handle_key(browser_t *br, const key_event_t *event);
void browser_render(browser_t *br, int x, int y, int w, int h);

#endif
