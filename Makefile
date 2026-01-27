# Fusion OS Build System
include config.mk

# Source files
C_SOURCES := $(filter-out $(SRCDIR)/wm.c, $(wildcard $(SRCDIR)/*.c))
KERNEL_SOURCES := kernel.c
ALL_SOURCES := $(C_SOURCES) $(KERNEL_SOURCES)
OBJECTS := $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(C_SOURCES)) $(patsubst %.c,$(BUILDDIR)/%.o,$(KERNEL_SOURCES))
ISO_ROOT := $(BUILDDIR)/iso_root

.PHONY: all clean distclean run limine setup

all: setup $(ISO)
	@echo "$(COLOR_GREEN)✓ Fusion OS built successfully!$(COLOR_RESET)"

setup:
	@mkdir -p $(BUILDDIR)
	@mkdir -p $(BOOTDIR)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	@echo "$(COLOR_BLUE)[CC]$(COLOR_RESET) $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/kernel.o: kernel.c
	@echo "$(COLOR_BLUE)[CC]$(COLOR_RESET) $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL): $(OBJECTS)
	@echo "$(COLOR_YELLOW)[LD]$(COLOR_RESET) $@"
	@$(LD) $(LDFLAGS) $(OBJECTS) -o $@

limine:
	@if [ ! -d "$(LIMINE_DIR)" ]; then \
		echo "$(COLOR_YELLOW)Downloading Limine...$(COLOR_RESET)"; \
		curl -Lo limine.tar.gz https://github.com/limine-bootloader/limine/releases/download/v8.4.0/limine-8.4.0.tar.gz; \
		tar -xzf limine.tar.gz; \
		rm limine.tar.gz; \
	fi
	@if [ ! -f "$(LIMINE_DIR)/limine-uefi-cd.bin" ]; then \
		echo "$(COLOR_YELLOW)Downloading Limine binaries...$(COLOR_RESET)"; \
		cd $(LIMINE_DIR) && \
		curl -Lo limine-uefi-cd.bin https://github.com/limine-bootloader/limine/raw/v8.x-binary/limine-uefi-cd.bin && \
		curl -Lo limine-bios-cd.bin https://github.com/limine-bootloader/limine/raw/v8.x-binary/limine-bios-cd.bin && \
		curl -Lo limine-bios.sys https://github.com/limine-bootloader/limine/raw/v8.x-binary/limine-bios.sys && \
		curl -Lo BOOTX64.EFI https://github.com/limine-bootloader/limine/raw/v8.x-binary/BOOTX64.EFI && \
		curl -Lo BOOTIA32.EFI https://github.com/limine-bootloader/limine/raw/v8.x-binary/BOOTIA32.EFI; \
	fi
	@if [ ! -f "limine.h" ]; then \
		echo "$(COLOR_YELLOW)Downloading limine.h...$(COLOR_RESET)"; \
		curl -Lo limine.h https://raw.githubusercontent.com/limine-bootloader/limine/v8.x-binary/limine.h; \
	fi

$(ISO): $(KERNEL) limine
	@echo "$(COLOR_GREEN)[ISO]$(COLOR_RESET) Creating bootable ISO..."
	@rm -rf $(ISO_ROOT)
	@mkdir -p $(ISO_ROOT)/boot $(ISO_ROOT)/boot/limine $(ISO_ROOT)/EFI/BOOT
	@cp $(KERNEL) $(ISO_ROOT)/boot/
	@cp $(BOOTDIR)/limine.conf $(ISO_ROOT)/boot/limine/limine.conf
	@cp $(BOOTDIR)/limine.cfg $(ISO_ROOT)/boot/limine/limine.cfg
	@cp $(BOOTDIR)/limine.conf $(ISO_ROOT)/boot/limine.conf
	@cp $(BOOTDIR)/limine.cfg $(ISO_ROOT)/boot/limine.cfg
	@cp $(BOOTDIR)/limine.conf $(ISO_ROOT)/limine.conf
	@cp $(BOOTDIR)/limine.cfg $(ISO_ROOT)/limine.cfg
	@cp $(LIMINE_DIR)/limine-bios.sys $(LIMINE_DIR)/limine-bios-cd.bin \
	   $(LIMINE_DIR)/limine-uefi-cd.bin $(ISO_ROOT)/boot/limine/
	@cp $(LIMINE_DIR)/BOOTX64.EFI $(ISO_ROOT)/EFI/BOOT/
	@xorriso -as mkisofs -b boot/limine/limine-bios-cd.bin \
	        -no-emul-boot -boot-load-size 4 -boot-info-table \
	        -eltorito-alt-boot -e boot/limine/limine-uefi-cd.bin -no-emul-boot \
	        $(ISO_ROOT) -o $(ISO) 2>&1 | grep -v "xorriso :" || true
	@echo "$(COLOR_GREEN)✓ ISO created: $(ISO)$(COLOR_RESET)"

run: $(ISO)
	@echo "$(COLOR_BLUE)Starting QEMU...$(COLOR_RESET)"
	@./run.sh

clean:
	@echo "$(COLOR_YELLOW)Cleaning build artifacts...$(COLOR_RESET)"
	@rm -f $(OBJECTS) $(KERNEL) $(ISO)
	@rm -rf $(ISO_ROOT) $(BUILDDIR)

distclean: clean
	@echo "$(COLOR_YELLOW)Removing Limine...$(COLOR_RESET)"
	@rm -rf $(LIMINE_DIR) limine.h

info:
	@echo "$(COLOR_BLUE)=== Fusion OS Build Information ===$(COLOR_RESET)"
	@echo "Kernel:  $(KERNEL)"
	@echo "ISO:     $(ISO)"
	@echo "Sources: $(C_SOURCES)"
	@echo "Objects: $(OBJECTS)"
	@echo ""
	@echo "$(COLOR_GREEN)Available targets:$(COLOR_RESET)"
	@echo "  all       - Build everything (default)"
	@echo "  clean     - Remove build artifacts"
	@echo "  distclean - Remove everything including Limine"
	@echo "  run       - Build and run in QEMU"
	@echo "  info      - Show this information"
