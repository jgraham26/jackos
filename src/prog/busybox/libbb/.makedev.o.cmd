cmd_libbb/makedev.o := x86_64-jackos-gcc -Wp,-MD,libbb/.makedev.o.d  -std=gnu99 -Iinclude -Ilibbb  -include include/autoconf.h -D_GNU_SOURCE -DNDEBUG  -DBB_VER='"1.36.1"' -malign-data=abi -Wall -Wshadow -Wwrite-strings -Wundef -Wstrict-prototypes -Wunused -Wunused-parameter -Wunused-function -Wunused-value -Wmissing-prototypes -Wmissing-declarations -Wno-format-security -Wdeclaration-after-statement -Wold-style-definition -finline-limit=0 -fno-builtin-strlen -fomit-frame-pointer -ffunction-sections -fdata-sections -fno-guess-branch-probability -funsigned-char -static-libgcc -falign-functions=1 -falign-jumps=1 -falign-labels=1 -falign-loops=1 -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-builtin-printf -Oz --sysroot="/home/jack/programming/jackos-v10/src/prog/root"    -DKBUILD_BASENAME='"makedev"'  -DKBUILD_MODNAME='"makedev"' -c -o libbb/makedev.o libbb/makedev.c

deps_libbb/makedev.o := \
  libbb/makedev.c \
  include/platform.h \
    $(wildcard include/config/werror.h) \
    $(wildcard include/config/big/endian.h) \
    $(wildcard include/config/little/endian.h) \
    $(wildcard include/config/nommu.h) \
  /usr/local/lib/gcc/x86_64-jackos/12.2.0/include-fixed/limits.h \
  /usr/local/lib/gcc/x86_64-jackos/12.2.0/include-fixed/syslimits.h \
  /home/jack/programming/jackos-v10/src/prog/root/usr/include/limits.h \
  /home/jack/programming/jackos-v10/src/prog/root/usr/include/features.h \
  /home/jack/programming/jackos-v10/src/prog/root/usr/include/bits/alltypes.h \
  /home/jack/programming/jackos-v10/src/prog/root/usr/include/bits/limits.h \
  /home/jack/programming/jackos-v10/src/prog/root/usr/include/byteswap.h \
  /usr/local/lib/gcc/x86_64-jackos/12.2.0/include/stdint.h \
  /home/jack/programming/jackos-v10/src/prog/root/usr/include/endian.h \
  /usr/local/lib/gcc/x86_64-jackos/12.2.0/include/stdbool.h \
  /home/jack/programming/jackos-v10/src/prog/root/usr/include/unistd.h \
  /home/jack/programming/jackos-v10/src/prog/root/usr/include/sys/sysmacros.h \

libbb/makedev.o: $(deps_libbb/makedev.o)

$(deps_libbb/makedev.o):
