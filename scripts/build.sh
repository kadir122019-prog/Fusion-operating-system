#!/bin/bash

set -e

echo "=== Fusion OS Build Script ==="

if ! command -v gcc &> /dev/null; then
    echo "Error: gcc not found"
    echo "Install a native toolchain (gcc, binutils) first"
    exit 1
fi

if ! command -v nasm &> /dev/null; then
    echo "Error: nasm not found"
    echo "Install with: sudo apt install nasm"
    exit 1
fi

if ! command -v xorriso &> /dev/null; then
    echo "Error: xorriso not found"
    echo "Install with: sudo apt install xorriso"
    exit 1
fi

curl -Lo include/limine.h https://raw.githubusercontent.com/limine-bootloader/limine/v8.x-binary/limine.h

make all

echo ""
echo "=== Build Complete ==="
echo "ISO created: build/fusion.iso"
echo "Run with: make run"
echo "Or boot with: qemu-system-x86_64 -cdrom build/fusion.iso"
