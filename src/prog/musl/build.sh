./configure \
    CROSS_COMPILE=x86_64-jackos- \
    CC=x86_64-jackos-gcc \
    --prefix=/usr \
    --syslibdir=/usr/lib \
    --target=x86_64 \
    --disable-shared \
    --enable-static

make -j$(nproc)
DESTDIR="/home/jack/programming/jackos-v10/src/prog/root" make install
