CC      := x86_64-jackos-gcc
AS      := nasm
LD      := x86_64-jackos-ld

BUILD_DIR := ../../build/kernel
LIBK      := ../../build/libk/libk.a
TARGET    := ../../build/kernel.bin

CFLAGS  := -ffreestanding -O2 -Wall -Wextra -mno-red-zone -mcmodel=kernel \
           -fno-pie -fno-pic -fno-stack-protector -fno-omit-frame-pointer \
           -mno-omit-leaf-frame-pointer -I../../include/libk -I../../include/kernel \
           -MMD -MP
ASFLAGS := -f elf64
LIBGCC  := $(shell $(CC) $(CFLAGS) -print-libgcc-file-name)
LDFLAGS := -n -T linker.ld -nostdlib -z max-page-size=0x1000 \
           -L../../build/libk -lk $(LIBGCC)

C_SRC   := $(shell find . -type f -name '*.c')
ASM_SRC := $(shell find . -type f -name '*.asm')
OBJS    := $(patsubst ./%.c,$(BUILD_DIR)/%.c.o,$(C_SRC)) \
           $(patsubst ./%.asm,$(BUILD_DIR)/%.asm.o,$(ASM_SRC))
DEPS    := $(OBJS:.o=.d)

.PHONY: all libk clean

all: $(TARGET)

libk:
	$(MAKE) -C ../libk

$(LIBK): libk

$(BUILD_DIR)/%.asm.o: %.asm
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/%.c.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS) linker.ld $(LIBK)
	@mkdir -p $(dir $@)
	$(LD) -o $@ $(OBJS) $(LDFLAGS)

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

-include $(DEPS)