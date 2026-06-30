# Path of the build folder
BUILD_PATH=./build

# Path to store the Limine binary files
LIMINE_PATH=./build/limine-binary

TARGET=pandora

# Only do this if the patch doesn't exist
if [ ! -d "$LIMINE_PATH" ]; then
    # Download the latest Limine binary release.
    curl -L https://github.com/Limine-Bootloader/Limine/releases/latest/download/limine-binary.tar.gz | tar -xzf - -C "$BUILD_PATH"
    
    # Build "limine" utility.
    make -C $LIMINE_PATH
fi

# Create an empty zeroed-out 64MiB image file.
dd if=/dev/zero bs=1M count=0 seek=64 of=$BUILD_PATH/$TARGET.hdd

# # Create a partition table.
PATH=$PATH:/usr/sbin:/sbin sgdisk $BUILD_PATH/$TARGET.hdd -n 1:2048 -t 1:ef00 -m 1

# # Install the Limine BIOS stages onto the image.
$LIMINE_PATH/limine bios-install $BUILD_PATH/$TARGET.hdd

# Format the partition as FAT32 (-F flag; default mformat picks FAT16 on small images).
mformat -F -i $BUILD_PATH/$TARGET.hdd@@1M

# Make relevant subdirectories.
mmd -i $BUILD_PATH/$TARGET.hdd@@1M ::/EFI ::/EFI/BOOT ::/boot ::/boot/limine ::/bin

# Copy over the relevant files.
mcopy -i build/$TARGET.hdd@@1M $BUILD_PATH/bin/$TARGET ::/boot
mcopy -i build/$TARGET.hdd@@1M limine.conf $LIMINE_PATH/limine-bios.sys ::/boot/limine
mcopy -i build/$TARGET.hdd@@1M $LIMINE_PATH/BOOTX64.EFI ::/EFI/BOOT
mcopy -i build/$TARGET.hdd@@1M $LIMINE_PATH/BOOTIA32.EFI ::/EFI/BOOT
mcopy -i build/$TARGET.hdd@@1M  assets/zap-light16.psf ::/boot
mcopy -i build/$TARGET.hdd@@1M  programs/terminal/build/bin/terminal.bin ::/bin
mcopy -i build/$TARGET.hdd@@1M  programs/ls/build/bin/ls.bin ::/bin



# Create a 128M blank image
# dd if=/dev/zero bs=1M count=128 of=assets/blank.hdd

# # Write an MBR partition table with one FAT32 partition starting at LBA 2048
# parted -s assets/blank.hdd mklabel msdos
# parted -s assets/blank.hdd mkpart primary fat32 1MiB 100%

# # Format the partition (offset 2048 sectors * 512 bytes = 1048576 bytes)
# mformat -F -i assets/blank.hdd@@1M