# Window-Aware Terminal Rendering Changes

## Summary
Modified the Fusion OS terminal rendering to respect window bounds, inspired by dwm (Dynamic Window Manager). When `clear` is executed, it now only clears the content area inside a window's borders and title bar, preserving the window decorations. This creates the proper window manager behavior.

## Changes Made

### 1. Updated `clear_screen()` Function
**File**: `src/kernel/main.c` (lines ~208-244)

**Before**: Cleared entire framebuffer, destroying window borders
**After**: 
- Checks if window manager is initialized (`wm.window_count == 0`)
- If not initialized, clears full screen (early boot phase)
- If initialized, clears only the active window's content area
- Content area = window bounds minus border width and title height
- Window borders and title bar remain intact

**Key Code**:
```c
// Calculate content area (inside borders and title bar)
uint64_t content_x = win->x + BORDER_WIDTH;
uint64_t content_y = win->y + BORDER_WIDTH + TITLE_HEIGHT;
uint64_t content_width = win->width - (2 * BORDER_WIDTH);
uint64_t content_height = win->height - (2 * BORDER_WIDTH) - TITLE_HEIGHT;
```

### 2. Updated `scroll_up()` Function  
**File**: `src/kernel/main.c` (lines ~235-265)

**Before**: Scrolled entire screen
**After**:
- Scrolls only within active window's content area
- Preserves all window decorations during scroll
- Text wrapping respects window width

### 3. Updated `putchar_color()` Function
**File**: `src/kernel/main.c` (lines ~278-320)

**Before**: 
- Cursor position was global (0 to screen width/height)
- Text could wrap anywhere on screen
- Line wrapping used full screen width

**After**:
- Calculates window content bounds
- Cursor position is relative to window's content area
- Text wraps within window width (not screen width)
- Newline moves cursor to content start (not screen start)
- Scrolling triggered when content height exceeded

**Key Changes**:
```c
// Window content boundaries
uint64_t content_x = win->x + BORDER_WIDTH;
uint64_t content_y = win->y + BORDER_WIDTH + TITLE_HEIGHT;
uint64_t content_width = win->width - (2 * BORDER_WIDTH);
uint64_t content_height = win->height - (2 * BORDER_WIDTH) - TITLE_HEIGHT;

// Text wraps at content_width, not screen width
if (cursor_x >= content_x + content_width) {
    cursor_x = content_x;  // Back to window start
    cursor_y += FONT_HEIGHT;
}
```

## Design Philosophy (from dwm)

dwm uses the concept that:
- Each window has its own drawable area (content)
- Window decorations are separate from content
- Terminal operations (like `clear`) only affect content
- Window manager maintains frame/border integrity

This prevents the visual chaos of terminal operations destroying window layout in multi-window environments.

## Impact

✓ `clear` command now only clears active window  
✓ Window borders/titles remain visible after clear  
✓ Text stays within window bounds  
✓ Tiling layout remains intact during terminal operations  
✓ Smooth integration with dwm-style window manager  

## Testing

Build and test with:
```bash
make
qemu-system-x86_64 -cdrom fusion.iso -m 256M -display sdl
```

In QEMU:
1. Windows will display with borders and titles
2. Run `clear` - borders/titles stay intact
3. Type text - wraps within window boundaries
4. Output stays confined to window content area
