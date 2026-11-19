#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="build"
IMAGE="${BUILD_DIR}/disk.img"
STAGE1_BIN="boot/src/bootsector/bootsector.bin"
STAGE2_BIN="boot/src/stage2/stage2.bin"
KERNEL_BIN="${BUILD_DIR}/kernel.bin"
ROOTFS_DIR="src/prog/root"

# Optional external assets (overridable via environment variables)
DOOM_BIN="${DOOM_BIN:-/home/jack/src/doomgeneric/doomgeneric/doom_jackos}"
DOOM_WAD="${DOOM_WAD:-/home/jack/Downloads/DOOM1.WAD}"

mkdir -p "${BUILD_DIR}"

echo "--> Creating 64 MiB partitioned disk image..."
dd if=/dev/zero of="${IMAGE}" bs=1M count=64 status=none

# Partition 1: 20 MiB FAT32 boot (sectors 2048..43007)
# Partition 2: ext2 rootfs      (sectors 43008..end)
parted -s "${IMAGE}" mklabel msdos \
    mkpart primary fat32 1MiB 21MiB \
    set 1 boot on \
    mkpart primary ext2 21MiB 100%

echo "--> Installing custom bootloader (Stage 1 + Stage 2)..."
dd if="${STAGE1_BIN}" of="${IMAGE}" bs=446 count=1 conv=notrunc status=none
dd if="${STAGE1_BIN}" of="${IMAGE}" bs=1 skip=510 seek=510 count=2 conv=notrunc status=none
dd if="${STAGE2_BIN}" of="${IMAGE}" bs=512 seek=1 conv=notrunc status=none

echo "--> Formatting and populating FAT32 boot partition..."
mkfs.fat -F 32 -n "BOOT" --offset=2048 "${IMAGE}" >/dev/null
mmd -i "${IMAGE}@@1M" ::/BOOT
mcopy -i "${IMAGE}@@1M" "${KERNEL_BIN}" ::/BOOT/KERNEL.BIN

echo "--> Building and writing ext2 rootfs partition..."
STAGE_ROOT=$(mktemp -d)
trap 'rm -rf "${STAGE_ROOT}"' EXIT

cp -a "${ROOTFS_DIR}/." "${STAGE_ROOT}/"
[[ -f "${DOOM_BIN}" ]] && cp "${DOOM_BIN}" "${STAGE_ROOT}/doom"
[[ -f "${DOOM_WAD}" ]] && cp "${DOOM_WAD}" "${STAGE_ROOT}/DOOM1.WAD"
mkdir -p "${STAGE_ROOT}/lib" "${STAGE_ROOT}/usr/lib" "${STAGE_ROOT}/etc"
cp -fL "${ROOTFS_DIR}/usr/lib/libc.so" "${STAGE_ROOT}/lib/libc.so"
chmod 755 "${STAGE_ROOT}/lib/libc.so"
ln -sf libc.so "${STAGE_ROOT}/lib/ld-musl-x86_64.so.1"
ln -sf ../../lib/libc.so "${STAGE_ROOT}/usr/lib/libc.so"
printf "/lib\n/usr/lib\n" > "${STAGE_ROOT}/etc/ld-musl-x86_64.path"


# Partition 2 starts at 21 MiB (43 MiB remaining)
EXT2_IMG="${BUILD_DIR}/rootfs.ext2"
dd if=/dev/zero of="${EXT2_IMG}" bs=1M count=43 status=none
mkfs.ext2 -q -F -L "ROOTFS" -d "${STAGE_ROOT}" "${EXT2_IMG}"
dd if="${EXT2_IMG}" of="${IMAGE}" bs=1M seek=21 conv=notrunc status=none
rm -f "${EXT2_IMG}"

echo "--> Success: ${IMAGE} ready."