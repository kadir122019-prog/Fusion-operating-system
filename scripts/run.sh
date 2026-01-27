if [ -f disk.img ]; then
  DISK_ARGS="-drive file=disk.img,if=none,id=drive0,format=raw -device virtio-blk-pci,drive=drive0"
else
  DISK_ARGS=""
fi

qemu-system-x86_64 -cdrom build/fusion.iso -m 256M -smp 2 -serial stdio \
  -netdev user,id=net0 -device e1000,netdev=net0 $DISK_ARGS
