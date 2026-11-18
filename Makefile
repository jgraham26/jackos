BUILD_DIR := build
DISK_IMG  := $(BUILD_DIR)/disk.img
KERNEL    := $(BUILD_DIR)/kernel.bin
BOOT_DIR   := boot
STAGE1_BIN := $(BOOT_DIR)/src/bootsector/bootsector.bin
STAGE2_BIN := $(BOOT_DIR)/src/stage2/stage2.bin


QEMU       := qemu-system-x86_64
QEMU_FLAGS := -m 256M -drive format=raw,file=$(DISK_IMG) -serial stdio -vga std

.PHONY: all boot kernel prog disk qemu qemu_dbg bochs clean

all: disk

boot:
	$(MAKE) -C boot BUILD_DIR=$(abspath $(BUILD_DIR)/boot)

libk:
	$(MAKE) -C src/libk

kernel: libk
	$(MAKE) -C src/kernel

prog:
	$(MAKE) -C src/prog

$(DISK_IMG): boot kernel prog scripts/build_disk.sh
	./scripts/build_disk.sh

disk: $(DISK_IMG)

qemu: $(DISK_IMG)
	$(QEMU) $(QEMU_FLAGS) -enable-kvm

qemu_dbg: $(DISK_IMG)
	$(QEMU) $(QEMU_FLAGS) -monitor stdio -s -S -d int -no-reboot

bochs: $(DISK_IMG)
	nm -n $(KERNEL) | awk '{print $$1, $$3}' > $(BUILD_DIR)/kernel.sym
	bochs -q -f bochsrc.txt -dbg_gui

clean:
	rm -rf $(BUILD_DIR)
	$(MAKE) -C boot clean
	$(MAKE) -C src/kernel clean
	$(MAKE) -C src/libk clean
	$(MAKE) -C src/prog clean