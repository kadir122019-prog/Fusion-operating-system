#include "file_manager.h"
#include "gfx.h"
#include "memory.h"

static void file_manager_refresh(file_manager_t *fm) {
    fm->entry_count = 0;
    fs_list_dir(fm->path, fm->entries, (int)(sizeof(fm->entries) / sizeof(fm->entries[0])), &fm->entry_count);
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

void file_manager_init(file_manager_t *fm) {
    fm->path[0] = '/';
    fm->path[1] = 0;
    fm->selection = 0;
    fm->entry_count = 0;
    file_manager_refresh(fm);
}

void file_manager_handle_key(file_manager_t *fm, const key_event_t *event) {
    if (!event->pressed) return;

    if (event->keycode == KEY_UP) {
        if (fm->selection > 0) fm->selection--;
        return;
    }
    if (event->keycode == KEY_DOWN) {
        if (fm->selection + 1 < fm->entry_count) fm->selection++;
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
    gfx_draw_text(fm->path, x + 8, y + 8, 0x9AD1FF);

    if (!fs_is_ready()) {
        gfx_draw_text("No disk mounted", x + 8, y + 28, 0x9BA6B2);
        return;
    }

    int line_y = y + 28;
    for (int i = 0; i < fm->entry_count; i++) {
        if (line_y + FONT_HEIGHT > y + h) break;
        fs_entry_t *entry = &fm->entries[i];
        u32 color = entry->is_dir ? 0x6FD3FF : 0xE6E6E6;
        if (i == fm->selection) {
            gfx_draw_rect(x + 4, line_y - 2, w - 8, FONT_HEIGHT + 4, 0x1E2A3D);
        }
        gfx_draw_text(entry->name, x + 12, line_y, color);
        line_y += FONT_HEIGHT + 4;
    }
}
