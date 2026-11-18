CC        := x86_64-jackos-gcc
AS        := nasm
LD        := x86_64-jackos-ld
QEMU      := qemu-system-x86_64
AR        := x86_64-jackos-ar

BUILD_DIR := ../../build/libk
TARGET    := $(BUILD_DIR)/libk.a

CFLAGS    := -ffreestanding -O2 -Wall -Wextra -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
             -mcmodel=kernel -fno-pie -fno-pic -fno-stack-protector \
             -I../../include/libk -I../../include/kernel -MMD -MP

ASFLAGS   := -f elf64
LDFLAGS   := -n -T linker.ld -nostdlib -z max-page-size=0x1000

C_SRC     := $(shell find . -type f -name '*.c')
ASM_SRC   := $(shell find . -type f -name '*.asm')
OBJS      := $(patsubst ./%.c,$(BUILD_DIR)/%.c.o,$(C_SRC)) \
             $(patsubst ./%.asm,$(BUILD_DIR)/%.asm.o,$(ASM_SRC))
DEPS      := $(OBJS:.o=.d)

.PHONY: default clean

default: $(TARGET)

clean:
	rm -rf $(BUILD_DIR)

$(TARGET): $(OBJS)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $^

$(BUILD_DIR)/%.asm.o: %.asm
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/%.c.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

-include $(DEPS)