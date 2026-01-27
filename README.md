# Fusion
Fusion is an open-source operating system project (GPLv2). This repository is focused on kernel bring-up on x86_64 using the Limine bootloader.

## Build dependencies
- `gcc`, `ld`, `nasm`, `make`, `curl`
- `xorriso` (ISO creation)
- `qemu-system-x86_64` (optional, for running)

On Arch:
```
sudo pacman -S --needed base-devel gcc binutils nasm curl xorriso qemu-system-x86
```

## Build
```
make clean
make all
```

Build outputs:
- Kernel: `build/kernel.elf`
- ISO: `build/fusion.iso`

## Run (QEMU)
```
make run
```

Or:
```
qemu-system-x86_64 -cdrom build/fusion.iso -m 256M -smp 2 -serial stdio
```

## Docs
See the `docs/` folder for boot flow, memory assumptions, coding conventions, and the roadmap.
