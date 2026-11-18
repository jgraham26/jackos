CC      := x86_64-jackos-gcc
LD      := x86_64-jackos-ld
ASM     := nasm

SYSROOT := $(abspath root)
CFLAGS  := -ffreestanding -O2 --sysroot=$(SYSROOT)
LDFLAGS := -T user.ld -L$(SYSROOT)/usr/lib -lc

.PHONY: all clean musl busybox init rootfs

all: rootfs

musl:
	$(MAKE) -C musl DESTDIR="$(SYSROOT)" install -j$$(nproc)

busybox: musl
	$(MAKE) -C busybox -j$$(nproc)

init: musl
	mkdir -p $(SYSROOT)/bin $(SYSROOT)/dev $(SYSROOT)/etc $(SYSROOT)/lib $(SYSROOT)/usr/bin
	$(CC) -o $(SYSROOT)/bin/init init/main.c $(CFLAGS) $(LDFLAGS)

rootfs: musl busybox init
	cp busybox/busybox $(SYSROOT)/usr/bin/busybox

clean:
	rm -rf $(SYSROOT)
	$(MAKE) -C busybox clean