#!/bin/bash

set -e

BINUTILS_VER=2.41
GCC_VER=14.2.0
TARGET=x86_64-elf

TOOLCHAIN_PREFIX=$PWD/toolchain/$TARGET
PATH="$TOOLCHAIN_PREFIX/bin:$PATH"

JOBS=$(nproc)

# -------------------------
# Commandline Options
# -------------------------
BUILD_GCC=1
BUILD_BINUTILS=1
PARALLEL=0

for arg in "$@"; do
    case "$arg" in
        gcc)
            BUILD_GCC=1
            BUILD_BINUTILS=0
            ;;
        binutils)
            BUILD_GCC=0
            BUILD_BINUTILS=1
            ;;
        all)
            BUILD_GCC=1
            BUILD_BINUTILS=1
            ;;
        parallel)
            PARALLEL=1
            ;;
        serial)
            PARALLEL=0
            ;;
        *)
            echo "Unknown option: $arg"
            exit 1
            ;;
    esac
done

# -------------------------
# Helpers
# -------------------------
MAKE_CMD="make"
if [[ $PARALLEL -eq 1 ]]; then
    MAKE_CMD="make -j$JOBS"
fi

echo "Build config:"
echo "  GCC: $BUILD_GCC"
echo "  Binutils: $BUILD_BINUTILS"
echo "  Parallel: $PARALLEL ($JOBS jobs)"
echo "  Toolchain Prefix: $TOOLCHAIN_PREFIX"

mkdir -p toolchain
cd toolchain

# -------------------------
# Build binutils
# -------------------------
build_binutils() {
    echo "==> Downloading binutils $BINUTILS_VER"
    wget https://ftp.gnu.org/gnu/binutils/binutils-$BINUTILS_VER.tar.xz

    echo "==> Extracting binutils"
    tar -xf binutils-$BINUTILS_VER.tar.xz

    echo "==> Building binutils"

    mkdir -p build-binutils
    cd build-binutils

    ../binutils-$BINUTILS_VER/configure \
        --target=$TARGET				\
        --prefix="$TOOLCHAIN_PREFIX" 	\
        --with-sysroot					\
        --disable-nls					\
        --disable-werror

    $MAKE_CMD
    $MAKE_CMD install

    cd ..
    rm -rf binutils-$BINUTILS_VER.tar.xz
    rm -rf binutils-$BINUTILS_VER/
    rm -rf build-binutils/
}

# -------------------------
# Build GCC
# -------------------------
build_gcc() {
    echo "==> Downloading gcc $GCC_VER"
    wget https://ftp.gnu.org/gnu/gcc/gcc-$GCC_VER/gcc-$GCC_VER.tar.xz

    echo "==> Extracting binutils"
    tar -xf gcc-$GCC_VER.tar.xz

    echo "==> Building GCC"

    mkdir -p build-gcc

    cd gcc-$GCC_VER
        ./contrib/download_prerequisites
    cd ..

    export CC=gcc-14
    export CXX=g++-14

    cd build-gcc
    ../gcc-$GCC_VER/configure \
        --target=$TARGET \
        --prefix="$TOOLCHAIN_PREFIX" \
        --disable-nls \
        --enable-languages=c,c++ \
        --without-headers \
        --disable-multilib \
        --disable-bootstrap \
        --disable-libsanitizer \
        --disable-libssp \
        --disable-libquadmath \
        --disable-libgomp \
        --disable-libatomic \
        --disable-libvtv \
        --disable-libstdcxx \
        --disable-c++-modules

    $MAKE_CMD all-gcc
    $MAKE_CMD install-gcc

    cd ..
    rm -rf gcc-$GCC_VER.tar.xz
    rm -rf gcc-$GCC_VER/
    rm -rf build-gcc/
}

# -------------------------
# Execute selected builds
# -------------------------
if [[ $BUILD_BINUTILS -eq 1 ]]; then
    build_binutils
fi

if [[ $BUILD_GCC -eq 1 ]]; then
    build_gcc

    echo "Checking if GCC is compiled correctly."
    echo "If you get a successful version output, it is done correctly"
    echo " "
    $TOOLCHAIN_PREFIX/bin/$TARGET-gcc --version
fi