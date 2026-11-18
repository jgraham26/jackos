CC = x86_64-jackos-gcc
AR = x86_64-jackos-ar
ASM = nasm

C_SRC = $(shell find -type f -name '*.c')
ASM_SRC = $(shell find -type f -name '*.asm')
ASM_FLAGS = -felf64
CFLAGS = -ffreestanding -O2 -Iinclude

OBJ = $(ASM_SRC:.asm=.o) $(C_SRC:.c=.o)


../libc.a: $(OBJ)
	$(AR) rcs $@ $^

%.o: %.asm
	$(ASM) $(ASM_FLAGS) $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@