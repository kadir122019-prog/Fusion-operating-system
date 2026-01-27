#include "apps/browser.h"
#include "drivers/gfx.h"
#include "kernel/memory.h"
#include "services/net.h"
#include "kernel/cpu.h"

#define BROWSER_CONTENT_CAP (64 * 1024)

static void browser_set_status(browser_t *br, const char *msg) {
    size_t len = strlen(msg);
    if (len >= sizeof(br->status)) len = sizeof(br->status) - 1;
    memcpy(br->status, msg, len);
    br->status[len] = 0;
}

static int browser_strncasecmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return (unsigned char)ca - (unsigned char)cb;
        if (ca == 0) return 0;
    }
    return 0;
}

static int browser_parse_url(const char *url, char *host, int host_cap,
                             char *path, int path_cap) {
    const char *p = url;
    if (strncmp(p, "http://", 7) == 0) {
        p += 7;
    } else if (strncmp(p, "https://", 8) == 0) {
        return 0;
    }

    if (*p == 0) return 0;
    const char *slash = p;
    while (*slash && *slash != '/') slash++;

    int host_len = (int)(slash - p);
    if (host_len <= 0 || host_len >= host_cap) return 0;
    memcpy(host, p, host_len);
    host[host_len] = 0;

    if (*slash == '/') {
        int path_len = (int)strlen(slash);
        if (path_len >= path_cap) path_len = path_cap - 1;
        memcpy(path, slash, path_len);
        path[path_len] = 0;
    } else {
        if (path_cap < 2) return 0;
        path[0] = '/';
        path[1] = 0;
    }
    return 1;
}

static int browser_find_header(const char *headers, const char *key, const char **out_value) {
    size_t key_len = strlen(key);
    const char *p = headers;
    while (*p) {
        const char *line_end = p;
        while (*line_end && !(line_end[0] == '\r' && line_end[1] == '\n')) line_end++;
        if ((size_t)(line_end - p) >= key_len &&
            browser_strncasecmp(p, key, key_len) == 0 &&
            p[key_len] == ':') {
            const char *val = p + key_len + 1;
            while (*val == ' ' || *val == '\t') val++;
            *out_value = val;
            return 1;
        }
        if (line_end[0] == '\r') line_end += 2;
        else if (*line_end) line_end++;
        p = line_end;
    }
    return 0;
}

static int browser_hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static u32 browser_parse_hex(const char *s) {
    u32 value = 0;
    while (*s) {
        int d = browser_hex_digit(*s++);
        if (d < 0) break;
        value = (value << 4) | (u32)d;
    }
    return value;
}

static void browser_html_to_text(const char *in, char *out, u32 out_cap) {
    u32 o = 0;
    int in_tag = 0;
    char tag[16];
    int tag_len = 0;
    while (*in && o + 1 < out_cap) {
        char c = *in++;
        if (!in_tag) {
            if (c == '<') {
                in_tag = 1;
                tag_len = 0;
            } else if (c == '&') {
                if (strncmp(in, "amp;", 4) == 0) {
                    out[o++] = '&';
                    in += 4;
                } else if (strncmp(in, "lt;", 3) == 0) {
                    out[o++] = '<';
                    in += 3;
                } else if (strncmp(in, "gt;", 3) == 0) {
                    out[o++] = '>';
                    in += 3;
                } else if (strncmp(in, "quot;", 5) == 0) {
                    out[o++] = '"';
                    in += 5;
                } else if (strncmp(in, "apos;", 5) == 0) {
                    out[o++] = '\'';
                    in += 5;
                } else if (strncmp(in, "nbsp;", 5) == 0) {
                    out[o++] = ' ';
                    in += 5;
                } else {
                    out[o++] = c;
                }
            } else {
                out[o++] = c;
            }
        } else {
            if (c == '>') {
                in_tag = 0;
                tag[tag_len] = 0;
                if (tag_len > 0 && tag[0] == '/') {
                    for (int i = 0; tag[i]; i++) tag[i] = tag[i + 1];
                }
                if (strcmp(tag, "br") == 0 ||
                    strcmp(tag, "p") == 0 ||
                    strcmp(tag, "div") == 0 ||
                    strcmp(tag, "li") == 0 ||
                    strcmp(tag, "h1") == 0 ||
                    strcmp(tag, "h2") == 0 ||
                    strcmp(tag, "h3") == 0) {
                    out[o++] = '\n';
                }
            } else if (tag_len < (int)sizeof(tag) - 1) {
                if (c != ' ' && c != '\t' && c != '\r' && c != '\n' && c != '/') {
                    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
                    tag[tag_len++] = c;
                }
            }
        }
    }
    out[o] = 0;
}

static void browser_fetch(browser_t *br) {
    if (!net_is_up()) {
        browser_set_status(br, "Waiting for network...");
        u64 start = ticks;
        while (!net_is_up()) {
            net_poll();
            if (ticks - start > PIT_HZ * 6) {
                browser_set_status(br, "Network down");
                return;
            }
            cpu_sleep_ticks(1);
        }
    }
    char host[96];
    char path[128];
    if (!browser_parse_url(br->url, host, (int)sizeof(host), path, (int)sizeof(path))) {
        browser_set_status(br, "Invalid URL");
        return;
    }

    browser_set_status(br, "Resolving...");
    u32 ip = 0;
    if (!net_dns_resolve(host, &ip)) {
        browser_set_status(br, "DNS failed");
        return;
    }

    browser_set_status(br, "Connecting...");
    if (!net_tcp_connect(ip, 80)) {
        browser_set_status(br, "Connect failed");
        return;
    }
    u64 start = ticks;
    while (!net_tcp_is_established()) {
        net_poll();
        if (ticks - start > PIT_HZ * 5) {
            browser_set_status(br, "Connect timeout");
            return;
        }
        cpu_sleep_ticks(1);
    }

    char req[512];
    int pos = 0;
    const char *p1 = "GET ";
    const char *p2 = " HTTP/1.1\r\nHost: ";
    const char *p3 = "\r\nUser-Agent: FusionBrowser/1.0\r\nConnection: close\r\n\r\n";
    for (int i = 0; p1[i] && pos < (int)sizeof(req) - 1; i++) req[pos++] = p1[i];
    for (int i = 0; path[i] && pos < (int)sizeof(req) - 1; i++) req[pos++] = path[i];
    for (int i = 0; p2[i] && pos < (int)sizeof(req) - 1; i++) req[pos++] = p2[i];
    for (int i = 0; host[i] && pos < (int)sizeof(req) - 1; i++) req[pos++] = host[i];
    for (int i = 0; p3[i] && pos < (int)sizeof(req) - 1; i++) req[pos++] = p3[i];
    req[pos] = 0;

    browser_set_status(br, "Downloading...");
    net_tcp_send((const u8 *)req, (u16)pos);

    char *raw = (char *)malloc(br->content_cap);
    if (!raw) {
        browser_set_status(br, "Out of memory");
        return;
    }
    u32 raw_len = 0;
    u64 last_rx = ticks;
    while (!net_tcp_is_closed()) {
        net_poll();
        u8 tmp[512];
        int got = net_tcp_recv(tmp, sizeof(tmp));
        if (got > 0) {
            if (raw_len + (u32)got < br->content_cap) {
                memcpy(raw + raw_len, tmp, (u32)got);
                raw_len += (u32)got;
            }
            last_rx = ticks;
        }
        if (ticks - last_rx > PIT_HZ * 5) break;
        cpu_sleep_ticks(1);
    }
    net_tcp_close();

    raw[raw_len] = 0;
    const char *body = raw;
    const char *header_end = 0;
    for (u32 i = 0; i + 3 < raw_len; i++) {
        if (raw[i] == '\r' && raw[i + 1] == '\n' &&
            raw[i + 2] == '\r' && raw[i + 3] == '\n') {
            header_end = raw + i + 4;
            break;
        }
    }
    if (header_end) {
        body = header_end;
    }

    int is_chunked = 0;
    int content_length = -1;
    if (header_end) {
        const char *headers = raw;
        const char *val = 0;
        if (browser_find_header(headers, "Transfer-Encoding", &val)) {
            if (browser_strncasecmp(val, "chunked", 7) == 0) {
                is_chunked = 1;
            }
        }
        if (browser_find_header(headers, "Content-Length", &val)) {
            int value = 0;
            while (*val >= '0' && *val <= '9') {
                value = value * 10 + (*val - '0');
                val++;
            }
            content_length = value;
        }
    }

    if (is_chunked) {
        u32 out_len = 0;
        const char *p = body;
        const char *end = raw + raw_len;
        while (p < end && out_len + 1 < br->content_cap) {
            while (p < end && (*p == '\r' || *p == '\n')) p++;
            char size_buf[16];
            int sb = 0;
            while (p < end && *p != '\r' && *p != '\n' && sb < (int)sizeof(size_buf) - 1) {
                size_buf[sb++] = *p++;
            }
            size_buf[sb] = 0;
            u32 chunk = browser_parse_hex(size_buf);
            if (chunk == 0) break;
            while (p < end && (*p == '\r' || *p == '\n')) p++;
            if (p + chunk > end) chunk = (u32)(end - p);
            if (out_len + chunk >= br->content_cap) chunk = br->content_cap - out_len - 1;
            memcpy(br->content + out_len, p, chunk);
            out_len += chunk;
            p += chunk;
        }
        br->content[out_len] = 0;
        br->content_len = out_len;
    } else {
        u32 body_len = (u32)(raw + raw_len - body);
        if (content_length >= 0 && (u32)content_length < body_len) body_len = (u32)content_length;
        if (body_len >= br->content_cap) body_len = br->content_cap - 1;
        memcpy(br->content, body, body_len);
        br->content[body_len] = 0;
        br->content_len = body_len;
    }

    browser_html_to_text(br->content, br->content, br->content_cap);
    free(raw);
    br->scroll = 0;
    browser_set_status(br, "Done");
}

void browser_init(browser_t *br) {
    memset(br, 0, sizeof(*br));
    br->content_cap = BROWSER_CONTENT_CAP;
    br->content = (char *)malloc(br->content_cap);
    if (br->content) {
        br->content[0] = 0;
        br->content_len = 0;
    }
    strcpy(br->url, "http://example.com");
    br->url_len = (int)strlen(br->url);
    browser_set_status(br, "Ready");
}

void browser_handle_key(browser_t *br, const key_event_t *event) {
    if (!event->pressed) return;
    if (event->keycode == KEY_ENTER) {
        browser_fetch(br);
        return;
    }
    if (event->keycode == KEY_BACKSPACE) {
        if (br->url_len > 0) {
            br->url_len--;
            br->url[br->url_len] = 0;
        }
        return;
    }
    if (event->keycode == KEY_UP) {
        if (br->scroll > 0) br->scroll--;
        return;
    }
    if (event->keycode == KEY_DOWN) {
        br->scroll++;
        return;
    }
    if (event->ascii >= 32 && event->ascii < 127) {
        if (br->url_len < (int)sizeof(br->url) - 1) {
            br->url[br->url_len++] = event->ascii;
            br->url[br->url_len] = 0;
        }
    }
}

static void browser_draw_text_lines(const char *text, int x, int y, int w, int h, int scroll) {
    int max_lines = h / FONT_HEIGHT;
    int max_chars = w / FONT_WIDTH;
    if (max_chars < 1) max_chars = 1;
    int line = 0;
    int drawn = 0;
    const char *p = text;
    char line_buf[256];
    while (*p && drawn < max_lines) {
        int lb = 0;
        while (*p && *p != '\n' && lb < (int)sizeof(line_buf) - 1) {
            line_buf[lb++] = *p++;
            if (lb >= max_chars) break;
        }
        line_buf[lb] = 0;
        if (line >= scroll) {
            gfx_draw_text(line_buf, x, y + drawn * FONT_HEIGHT, 0xE6E6E6);
            drawn++;
        }
        line++;
        if (*p == '\n') p++;
    }
}

void browser_render(browser_t *br, int x, int y, int w, int h) {
    if (!br->content) return;
    int bar_h = FONT_HEIGHT + 6;
    int status_h = FONT_HEIGHT + 6;
    gfx_draw_rect(x, y, w, h, 0x0F1218);
    gfx_draw_rect(x, y, w, bar_h, 0x1E2331);
    gfx_draw_text(br->url, x + 8, y + 4, 0xE6E6E6);

    int content_y = y + bar_h + 4;
    int content_h = h - bar_h - status_h - 8;
    if (content_h < 0) content_h = 0;
    browser_draw_text_lines(br->content, x + 8, content_y, w - 16, content_h, br->scroll);

    gfx_draw_rect(x, y + h - status_h, w, status_h, 0x1E2331);
    gfx_draw_text(br->status, x + 8, y + h - status_h + 4, 0x9BA6B2);
}
