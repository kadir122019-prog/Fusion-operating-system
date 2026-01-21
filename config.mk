# Fusion OS Build Configuration

# Toolchain
CC := gcc
LD := ld
AS := nasm

# Directories
SRCDIR := src
INCDIR := include
BUILDDIR := build
BOOTDIR := boot

# Compiler Flags
CFLAGS := -Wall -Wextra -O2 -pipe
CFLAGS += -ffreestanding -fno-stack-protector -fno-stack-check
CFLAGS += -fno-lto -fno-pie -fno-pic
CFLAGS += -m64 -march=x86-64
CFLAGS += -mno-80387 -mno-mmx -mno-sse -mno-sse2
CFLAGS += -mno-red-zone -mcmodel=kernel
CFLAGS += -I$(INCDIR) -I.

# Linker Flags
LDFLAGS := -T linker.ld -nostdlib
LDFLAGS += -zmax-page-size=0x1000 -static
LDFLAGS += -no-pie --no-dynamic-linker -ztext

# Output
KERNEL := kernel.elf
ISO := fusion.iso

# Limine
LIMINE_DIR := limine-8.4.0

# Colors for output
COLOR_RESET := \033[0m
COLOR_GREEN := \033[32m
COLOR_YELLOW := \033[33m
COLOR_BLUE := \033[34m
