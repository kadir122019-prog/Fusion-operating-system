.PHONY: all clean run

CC = gcc
LD = ld
CFLAGS = -Wall -Wextra -O2 -pipe -ffreestanding -fno-stack-protector -fno-stack-check \
         -fno-lto -fno-pie -fno-pic -m64 -march=x86-64 -mno-80387 -mno-mmx -mno-sse -mno-sse2 \
         -mno-red-zone -mcmodel=kernel -I.
LDFLAGS = -T linker.ld -nostdlib -zmax-page-size=0x1000 -static -no-pie --no-dynamic-linker -ztext

KERNEL = kernel.elf
ISO = fusion.iso
LIMINE_DIR = limine-8.4.0

all: $(ISO)

kernel.o: kernel.c limine.h
	$(CC) $(CFLAGS) -c kernel.c -o kernel.o

$(KERNEL): kernel.o
	$(LD) $(LDFLAGS) kernel.o -o $(KERNEL)

limine:
	@if [ ! -d "$(LIMINE_DIR)" ]; then \
		echo "Downloading Limine binary release..."; \
		curl -Lo limine.tar.gz https://github.com/limine-bootloader/limine/releases/download/v8.4.0/limine-8.4.0.tar.gz; \
		tar -xzf limine.tar.gz; \
		rm limine.tar.gz; \
	fi
	@if [ ! -f "$(LIMINE_DIR)/limine-uefi-cd.bin" ]; then \
		echo "Downloading pre-built Limine binaries..."; \
		cd $(LIMINE_DIR) && \
		curl -Lo limine-uefi-cd.bin https://github.com/limine-bootloader/limine/raw/v8.x-binary/limine-uefi-cd.bin && \
		curl -Lo limine-bios-cd.bin https://github.com/limine-bootloader/limine/raw/v8.x-binary/limine-bios-cd.bin && \
		curl -Lo limine-bios.sys https://github.com/limine-bootloader/limine/raw/v8.x-binary/limine-bios.sys && \
		curl -Lo BOOTX64.EFI https://github.com/limine-bootloader/limine/raw/v8.x-binary/BOOTX64.EFI && \
		curl -Lo BOOTIA32.EFI https://github.com/limine-bootloader/limine/raw/v8.x-binary/BOOTIA32.EFI; \
	fi

$(ISO): $(KERNEL) limine
	rm -rf iso_root
	mkdir -p iso_root/boot iso_root/boot/limine iso_root/EFI/BOOT
	cp $(KERNEL) iso_root/boot/
	cp limine.cfg iso_root/boot/limine/
	cp $(LIMINE_DIR)/limine-bios.sys $(LIMINE_DIR)/limine-bios-cd.bin \
	   $(LIMINE_DIR)/limine-uefi-cd.bin iso_root/boot/limine/
	cp $(LIMINE_DIR)/BOOTX64.EFI iso_root/EFI/BOOT/
	xorriso -as mkisofs -b boot/limine/limine-bios-cd.bin \
	        -no-emul-boot -boot-load-size 4 -boot-info-table \
	        -eltorito-alt-boot -e boot/limine/limine-uefi-cd.bin -no-emul-boot \
	        iso_root -o $(ISO)
	@echo "ISO created successfully: $(ISO)"

run: $(ISO)
	qemu-system-x86_64 -cdrom $(ISO) -m 256M -serial stdio -bios /usr/share/ovmf/x64/OVMF.fd

clean:
	rm -f kernel.o $(KERNEL) $(ISO)
	rm -rf iso_root

distclean: clean
	rm -rf $(LIMINE_DIR)
