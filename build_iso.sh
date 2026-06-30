# Path of the build folder
BUILD_PATH=./build

# Path to store the Limine binary files
LIMINE_PATH=./build/limine-binary

# Only do this if the patch doesn't exist
if [ ! -d "$LIMINE_PATH" ]; then
    # Download the latest Limine binary release.
    curl -L https://github.com/Limine-Bootloader/Limine/releases/latest/download/limine-binary.tar.gz | tar -xzf - -C "$BUILD_PATH"
    
    # Build "limine" utility.
    make -C $LIMINE_PATH
fi

# Create a directory which will be our ISO root.
mkdir -p $BUILD_PATH/iso_root

# Copy the relevant files over.
mkdir -p $BUILD_PATH/iso_root/boot
cp -v $BUILD_PATH/bin/pandora $BUILD_PATH/iso_root/boot/
cp -v assets/zap-light16.psf $BUILD_PATH/iso_root/boot/
mkdir -p $BUILD_PATH/iso_root/boot/limine
cp -v limine.conf $LIMINE_PATH/limine-bios.sys $LIMINE_PATH/limine-bios-cd.bin \
      $LIMINE_PATH/limine-uefi-cd.bin $BUILD_PATH/iso_root/boot/limine/

mkdir -p $BUILD_PATH/iso_root/bin
cp -v programs/terminal/build/bin/terminal.bin $BUILD_PATH/iso_root/bin/
cp -v programs/ls/build/bin/ls.bin $BUILD_PATH/iso_root/bin/

# Create the EFI boot tree and copy Limine's EFI executables over.
mkdir -p $BUILD_PATH/iso_root/EFI/BOOT
cp -v $LIMINE_PATH/BOOTX64.EFI $BUILD_PATH/iso_root/EFI/BOOT/
cp -v $LIMINE_PATH/BOOTIA32.EFI $BUILD_PATH/iso_root/EFI/BOOT/


# Create the bootable ISO.
xorriso -as mkisofs -R -r -J -b boot/limine/limine-bios-cd.bin \
        -no-emul-boot -boot-load-size 4 -boot-info-table -hfsplus \
        -apm-block-size 2048 --efi-boot boot/limine/limine-uefi-cd.bin \
        -efi-boot-part --efi-boot-image --protective-msdos-label \
        $BUILD_PATH/iso_root -o $BUILD_PATH/pandora.iso

# Install Limine stage 1 and 2 for legacy BIOS boot.
$LIMINE_PATH/limine bios-install $BUILD_PATH/pandora.iso