#include "file_manager.h"
#include "gfx.h"
#include "memory.h"
#include "cpu.h"

#define FM_MODE_NORMAL 0
#define FM_MODE_RENAME 1
#define FM_MODE_CONFIRM_DELETE 2

static void format_dec(u32 value, char *out, int max) {
    if (max <= 1) return;
    if (value == 0) {
        out[0] = '0';
        out[1] = 0;
        return;
    }
    char temp[16];
    int pos = 0;
    while (value && pos < (int)sizeof(temp)) {
        temp[pos++] = (char)('0' + (value % 10));
        value /= 10;
    }
    int out_pos = 0;
    while (pos > 0 && out_pos < max - 1) {
        out[out_pos++] = temp[--pos];
    }
    out[out_pos] = 0;
}

static void format_size(u32 size, char *out, int max) {
    char num[16];
    if (size >= 1024 * 1024) {
        format_dec(size / (1024 * 1024), num, (int)sizeof(num));
        int len = (int)strlen(num);
        if (len + 2 >= max) len = max - 2;
        memcpy(out, num, (size_t)len);
        out[len++] = 'M';
        out[len++] = 'B';
        out[len] = 0;
    } else if (size >= 1024) {
        format_dec(size / 1024, num, (int)sizeof(num));
        int len = (int)strlen(num);
        if (len + 2 >= max) len = max - 2;
        memcpy(out, num, (size_t)len);
        out[len++] = 'K';
        out[len++] = 'B';
        out[len] = 0;
    } else {
        format_dec(size, num, (int)sizeof(num));
        int len = (int)strlen(num);
        if (len + 1 >= max) len = max - 1;
        memcpy(out, num, (size_t)len);
        out[len++] = 'B';
        out[len] = 0;
    }
}

static void fm_set_status(file_manager_t *fm, const char *msg) {
    if (!msg) return;
    int len = (int)strlen(msg);
    if (len >= (int)sizeof(fm->status)) len = (int)sizeof(fm->status) - 1;
    memcpy(fm->status, msg, (size_t)len);
    fm->status[len] = 0;
    fm->status_until = ticks + PIT_HZ * 2;
}

static int fm_status_active(const file_manager_t *fm) {
    if (fm->status[0] == 0) return 0;
    if (ticks > fm->status_until) return 0;
    return 1;
}

static void fm_clear_status(file_manager_t *fm) {
    fm->status[0] = 0;
    fm->status_until = 0;
}

static void fm_join_path(const char *dir, const char *name, char *out, int max) {
    if (!dir || !name || max <= 1) {
        if (max > 0) out[0] = 0;
        return;
    }
    if (strcmp(dir, "/") == 0) {
        int len = (int)strlen(name);
        if (len + 2 >= max) len = max - 2;
        out[0] = '/';
        memcpy(out + 1, name, (size_t)len);
        out[len + 1] = 0;
        return;
    }
    int dlen = (int)strlen(dir);
    int nlen = (int)strlen(name);
    if (dlen + nlen + 2 >= max) nlen = max - dlen - 2;
    memcpy(out, dir, (size_t)dlen);
    out[dlen] = '/';
    memcpy(out + dlen + 1, name, (size_t)nlen);
    out[dlen + nlen + 1] = 0;
}

static const char *fm_basename(const char *path) {
    const char *p = path;
    const char *last = path;
    while (*p) {
        if (*p == '/') last = p + 1;
        p++;
    }
    return last;
}

static void fm_enter_rename(file_manager_t *fm) {
    if (fm->selection < 0 || fm->selection >= fm->entry_count) return;
    const char *name = fm->entries[fm->selection].name;
    int len = (int)strlen(name);
    if (len >= (int)sizeof(fm->input)) len = (int)sizeof(fm->input) - 1;
    memcpy(fm->input, name, (size_t)len);
    fm->input[len] = 0;
    fm->input_len = len;
    fm->mode = FM_MODE_RENAME;
}

static void fm_enter_delete_confirm(file_manager_t *fm) {
    if (fm->selection < 0 || fm->selection >= fm->entry_count) return;
    fm->mode = FM_MODE_CONFIRM_DELETE;
    fm_set_status(fm, "Delete selected? (y/n)");
}

static void file_manager_refresh(file_manager_t *fm) {
    fm->entry_count = 0;
    fs_list_dir(fm->path, fm->entries, (int)(sizeof(fm->entries) / sizeof(fm->entries[0])), &fm->entry_count);
    fs_sort_entries(fm->entries, fm->entry_count, fm->sort_mode, fm->sort_desc);
    if (fm->selection >= fm->entry_count) fm->selection = fm->entry_count - 1;
    if (fm->selection < 0) fm->selection = 0;
}

static void file_manager_set_path(file_manager_t *fm, const char *path) {
    int len = (int)strlen(path);
    if (len >= (int)sizeof(fm->path)) len = (int)sizeof(fm->path) - 1;
    memcpy(fm->path, path, (size_t)len);
    fm->path[len] = 0;
    fm->selection = 0;
    file_manager_refresh(fm);
}

static void fm_draw_breadcrumbs(const file_manager_t *fm, int x, int y, int max_w) {
    int cx = x;
    const char *p = fm->path;
    gfx_draw_text("/", cx, y, 0x6FD3FF);
    cx += FONT_WIDTH * 2;
    if (!p || strcmp(p, "/") == 0) return;
    if (*p == '/') p++;
    char part[64];
    while (*p) {
        int len = 0;
        while (p[len] && p[len] != '/' && len < (int)sizeof(part) - 1) {
            part[len] = p[len];
            len++;
        }
        part[len] = 0;
        if (cx + (int)strlen(part) * FONT_WIDTH > x + max_w - 8) break;
        gfx_draw_text(">", cx, y, 0x9BA6B2);
        cx += FONT_WIDTH * 2;
        gfx_draw_text(part, cx, y, 0xE6E6E6);
        cx += (int)strlen(part) * FONT_WIDTH + FONT_WIDTH;
        while (p[len] && p[len] != '/') len++;
        p += len;
        while (*p == '/') p++;
    }
}

void file_manager_init(file_manager_t *fm) {
    fm->path[0] = '/';
    fm->path[1] = 0;
    fm->selection = 0;
    fm->entry_count = 0;
    fm->sort_mode = FS_SORT_NAME;
    fm->sort_desc = 0;
    fm->clipboard[0] = 0;
    fm->clipboard_cut = 0;
    fm->input[0] = 0;
    fm->input_len = 0;
    fm->mode = FM_MODE_NORMAL;
    fm->status[0] = 0;
    fm->status_until = 0;
    file_manager_refresh(fm);
}

void file_manager_handle_key(file_manager_t *fm, const key_event_t *event) {
    if (!event->pressed) return;

    if (fm->mode == FM_MODE_RENAME) {
        if (event->keycode == KEY_ESC) {
            fm->mode = FM_MODE_NORMAL;
            fm->input_len = 0;
            fm->input[0] = 0;
            return;
        }
        if (event->keycode == KEY_BACKSPACE) {
            if (fm->input_len > 0) {
                fm->input_len--;
                fm->input[fm->input_len] = 0;
            }
            return;
        }
        if (event->keycode == KEY_ENTER) {
            if (fm->selection < 0 || fm->selection >= fm->entry_count) {
                fm->mode = FM_MODE_NORMAL;
                return;
            }
            char src[160];
            char dst[160];
            fm_join_path(fm->path, fm->entries[fm->selection].name, src, (int)sizeof(src));
            fm_join_path(fm->path, fm->input, dst, (int)sizeof(dst));
            if (fm->input_len == 0) {
                fm_set_status(fm, "Rename failed: empty name");
            } else if (fs_rename(src, dst)) {
                fm_set_status(fm, "Renamed");
                file_manager_refresh(fm);
            } else {
                fm_set_status(fm, "Rename failed");
            }
            fm->mode = FM_MODE_NORMAL;
            fm->input_len = 0;
            fm->input[0] = 0;
            return;
        }
        if (event->ascii >= 32 && event->ascii < 127) {
            if (fm->input_len < (int)sizeof(fm->input) - 1) {
                fm->input[fm->input_len++] = (char)event->ascii;
                fm->input[fm->input_len] = 0;
            }
        }
        return;
    }

    if (fm->mode == FM_MODE_CONFIRM_DELETE) {
        if (event->ascii == 'y' || event->ascii == 'Y') {
            if (fm->selection >= 0 && fm->selection < fm->entry_count) {
                char src[160];
                fm_join_path(fm->path, fm->entries[fm->selection].name, src, (int)sizeof(src));
                if (fs_delete(src)) {
                    fm_set_status(fm, "Deleted");
                    file_manager_refresh(fm);
                } else {
                    fm_set_status(fm, "Delete failed");
                }
            }
            fm->mode = FM_MODE_NORMAL;
            return;
        }
        if (event->ascii == 'n' || event->ascii == 'N' || event->keycode == KEY_ESC) {
            fm->mode = FM_MODE_NORMAL;
            fm_clear_status(fm);
            return;
        }
        return;
    }

    if (event->keycode == KEY_UP) {
        if (fm->selection > 0) fm->selection--;
        return;
    }
    if (event->keycode == KEY_DOWN) {
        if (fm->selection + 1 < fm->entry_count) fm->selection++;
        return;
    }

    if (event->ascii == 's' || event->ascii == 'S') {
        fm->sort_mode = (fs_sort_mode_t)((fm->sort_mode + 1) % 3);
        file_manager_refresh(fm);
        return;
    }
    if (event->ascii == 'r' || event->ascii == 'R') {
        fm->sort_desc = !fm->sort_desc;
        file_manager_refresh(fm);
        return;
    }

    if (event->ascii == 'c' || event->ascii == 'C') {
        if (fm->selection >= 0 && fm->selection < fm->entry_count) {
            fm_join_path(fm->path, fm->entries[fm->selection].name, fm->clipboard, (int)sizeof(fm->clipboard));
            fm->clipboard_cut = 0;
            fm_set_status(fm, "Copied to clipboard");
        }
        return;
    }
    if (event->ascii == 'x' || event->ascii == 'X') {
        if (fm->selection >= 0 && fm->selection < fm->entry_count) {
            fm_join_path(fm->path, fm->entries[fm->selection].name, fm->clipboard, (int)sizeof(fm->clipboard));
            fm->clipboard_cut = 1;
            fm_set_status(fm, "Cut to clipboard");
        }
        return;
    }
    if (event->ascii == 'v' || event->ascii == 'V') {
        if (fm->clipboard[0] == 0) {
            fm_set_status(fm, "Clipboard empty");
            return;
        }
        const char *name = fm_basename(fm->clipboard);
        if (!name || !name[0]) {
            fm_set_status(fm, "Paste failed");
            return;
        }
        char dst[160];
        fm_join_path(fm->path, name, dst, (int)sizeof(dst));
        int ok = 0;
        if (fm->clipboard_cut) {
            ok = fs_move(fm->clipboard, dst);
            if (ok) fm->clipboard[0] = 0;
        } else {
            ok = fs_copy(fm->clipboard, dst);
        }
        if (ok) {
            fm_set_status(fm, fm->clipboard_cut ? "Moved" : "Copied");
            file_manager_refresh(fm);
        } else {
            fm_set_status(fm, "Paste failed");
        }
        return;
    }
    if (event->ascii == 'd' || event->ascii == 'D') {
        fm_enter_delete_confirm(fm);
        return;
    }
    if (event->ascii == 'n' || event->ascii == 'N') {
        fm_enter_rename(fm);
        return;
    }

    if (event->keycode == KEY_ENTER || event->keycode == KEY_RIGHT) {
        if (fm->selection < 0 || fm->selection >= fm->entry_count) return;
        fs_entry_t *entry = &fm->entries[fm->selection];
        if (!entry->is_dir) return;

        char next[128];
        if (strcmp(fm->path, "/") == 0) {
            int len = (int)strlen(entry->name);
            if (len + 2 >= (int)sizeof(next)) return;
            next[0] = '/';
            memcpy(next + 1, entry->name, (size_t)len);
            next[len + 1] = 0;
        } else {
            int len = (int)strlen(fm->path);
            int nlen = (int)strlen(entry->name);
            if (len + nlen + 2 >= (int)sizeof(next)) return;
            memcpy(next, fm->path, (size_t)len);
            next[len] = '/';
            memcpy(next + len + 1, entry->name, (size_t)nlen);
            next[len + nlen + 1] = 0;
        }
        file_manager_set_path(fm, next);
        return;
    }

    if (event->keycode == KEY_BACKSPACE || event->keycode == KEY_LEFT) {
        if (strcmp(fm->path, "/") == 0) return;
        int len = (int)strlen(fm->path);
        while (len > 0 && fm->path[len - 1] != '/') len--;
        if (len <= 1) {
            file_manager_set_path(fm, "/");
            return;
        }
        fm->path[len - 1] = 0;
        file_manager_set_path(fm, fm->path);
    }
}

void file_manager_render(file_manager_t *fm, int x, int y, int w, int h) {
    gfx_draw_rect(x, y, w, h, 0x0F1218);
    fm_draw_breadcrumbs(fm, x + 8, y + 8, w - 16);

    if (!fs_is_ready()) {
        gfx_draw_text("No disk mounted", x + 8, y + 28, 0x9BA6B2);
        return;
    }

    const char *mode_label = "Name";
    if (fm->sort_mode == FS_SORT_SIZE) mode_label = "Size";
    else if (fm->sort_mode == FS_SORT_TYPE) mode_label = "Type";
    char sort_label[24];
    int pos = 0;
    const char *prefix = "Sort:";
    for (int i = 0; prefix[i] && pos < (int)sizeof(sort_label) - 1; i++) sort_label[pos++] = prefix[i];
    sort_label[pos++] = ' ';
    for (int i = 0; mode_label[i] && pos < (int)sizeof(sort_label) - 1; i++) sort_label[pos++] = mode_label[i];
    if (fm->sort_desc && pos < (int)sizeof(sort_label) - 2) {
        sort_label[pos++] = ' ';
        sort_label[pos++] = 'v';
    }
    sort_label[pos] = 0;
    int sort_x = x + w - 8 - (int)strlen(sort_label) * FONT_WIDTH;
    if (sort_x < x + 8) sort_x = x + 8;
    gfx_draw_text(sort_label, sort_x, y + 8, 0x9BA6B2);

    int line_y = y + 28;
    for (int i = 0; i < fm->entry_count; i++) {
        if (line_y + FONT_HEIGHT > y + h) break;
        fs_entry_t *entry = &fm->entries[i];
        u32 color = entry->is_dir ? 0x6FD3FF : 0xE6E6E6;
        if (i == fm->selection) {
            gfx_draw_rect(x + 4, line_y - 2, w - 8, FONT_HEIGHT + 4, 0x1E2A3D);
        }
        gfx_draw_text(entry->name, x + 12, line_y, color);
        if (!entry->is_dir) {
            char size_buf[16];
            format_size(entry->size, size_buf, (int)sizeof(size_buf));
            int size_x = x + w - 8 - (int)strlen(size_buf) * FONT_WIDTH;
            if (size_x < x + 8) size_x = x + 8;
            gfx_draw_text(size_buf, size_x, line_y, 0x9BA6B2);
        }
        line_y += FONT_HEIGHT + 4;
    }

    int footer_y = y + h - FONT_HEIGHT - 6;
    if (fm_status_active(fm)) {
        gfx_draw_rect(x + 4, footer_y - 4, w - 8, FONT_HEIGHT + 6, 0x141B26);
        gfx_draw_text(fm->status, x + 8, footer_y, 0xE6E6E6);
    } else {
        fm_clear_status(fm);
    }

    int hint_y = y + h - (FONT_HEIGHT * 2) - 10;
    gfx_draw_text("C Copy  X Cut  V Paste  N Rename  D Delete", x + 8, hint_y, 0x9BA6B2);

    if (fm->mode == FM_MODE_RENAME) {
        int ry = y + h - (FONT_HEIGHT * 3) - 12;
        gfx_draw_rect(x + 4, ry - 4, w - 8, FONT_HEIGHT + 6, 0x1B2435);
        gfx_draw_text("Rename:", x + 8, ry, 0xE6E6E6);
        gfx_draw_text(fm->input[0] ? fm->input : "...", x + 78, ry, 0xE6E6E6);
    }
}
