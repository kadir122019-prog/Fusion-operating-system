# Fusion OS Features

This document explains how each feature works and how to use it.

## Desktop and Window Manager
- **Panel**: bottom bar with launcher button, task buttons, and clock.
- **Launcher**: press the Win key or click the "Fusion" button to open.
- **Windows**: click a window to focus. Drag the title bar to move.
- **Resize**: grab window edges/corners to resize.
- **Snap**: drag to screen edges to snap left/right/top.
- **Minimize**: click a task button for a focused window to minimize it.
- **Alt+Tab**: switch to next window. **Shift+Alt+Tab** switches backward.
- **Scheduler**: desktop runs as a task with background networking.

## Launcher
- Full-height popout with app list and search.
- Type to filter apps. Use Up/Down to move selection. Enter to open.
- Buttons at the bottom:
  - **Shutdown**: halts the system.
  - **Sleep**: currently behaves like Shutdown (halts).
  - **Reboot**: reboots the system.

## Settings App
- **Idle FPS**: cycles between 30/40/60 for idle rendering.
- **Cursor Size**: Small/Large toggle.
- **Theme**: Blue/Red/Teal/Dark.

## About App
Shows:
- OS version
- Uptime
- Resolution
- Memory totals
- Heap usage
- CPU vendor

## Terminal + Shell
- **Tab**: command autocomplete.
- **Up/Down**: command history.
- **Shift+Up/Down**: terminal scrollback.
- **exit**: closes the terminal window.

### Shell Commands
- `help`
- `clear`
- `echo <text>`
- `uname`
- `meminfo`
- `heapinfo`
- `malloc <size>`
- `uptime`
- `cpuinfo`
- `color <white|red|green|blue|cyan|yellow|magenta|orange|pink|lime|gray|reset>`
- `copy` / `paste`
- `netinfo`
- `ls [path]`
- `ls [-s|-t|-r] [path]` (size/type/reverse sorting)
- `cat <path>`
- `write <path> <text>`
- `append <path> <text>`
- `mkdir <path>`
- `reboot`
- `halt`
- `exit`

## File Manager
- Displays real filesystem contents (FAT32).
- **Up/Down**: move selection.
- **Enter/Right**: open a directory.
- **Backspace/Left**: go up.
- **S**: cycle sort mode (Name/Size/Type).
- **R**: toggle descending sort.

## Browser
- Open from the launcher.
- URL bar at top. Type a URL and press Enter.
- HTTP only (no HTTPS yet).
- Basic HTML to text conversion.
- Up/Down scrolls the page.

## Networking
- e1000 PCI NIC driver.
- IPv4 + ARP + UDP + DHCP + DNS + TCP.
- Requires a DHCP lease to show IP info.
- `netinfo` shows IP/netmask/gateway/DNS.

## Storage and Filesystem
- VirtIO-blk driver (QEMU).
- FAT32 read/write with long filename support.
- Shell commands: `ls`, `cat`, `write`, `append`, `mkdir`.

### Disk Image Setup (QEMU)
Create a FAT32 disk image named `disk.img` in the repo root:
```
dd if=/dev/zero of=disk.img bs=1M count=64
mkfs.fat -F 32 disk.img
```
When `disk.img` exists, `run.sh` auto-attaches it as VirtIO-blk.

## Build and Run
- Build: `make all`
- Run: `make run`

`make run` launches QEMU with:
- e1000 networking
- VirtIO-blk if `disk.img` exists
