#!/bin/bash

set -e

echo "=== Fusion OS Build Script ==="

if ! command -v x86_64-elf-gcc &> /dev/null; then
    echo "Error: x86_64-elf-gcc not found"
    echo "Install cross-compiler toolchain first"
    echo "On Ubuntu/Debian: sudo apt install gcc-x86-64-linux-gnu binutils-x86-64-linux-gnu"
    echo "Or build from source: https://wiki.osdev.org/GCC_Cross-Compiler"
    exit 1
fi

if ! command -v xorriso &> /dev/null; then
    echo "Error: xorriso not found"
    echo "Install with: sudo apt install xorriso"
    exit 1
fi

curl -Lo limine.h https://raw.githubusercontent.com/limine-bootloader/limine/v8.x-binary/limine.h

make all

echo ""
echo "=== Build Complete ==="
echo "ISO created: fusion.iso"
echo "Run with: make run"
echo "Or boot with: qemu-system-x86_64 -cdrom fusion.iso"
