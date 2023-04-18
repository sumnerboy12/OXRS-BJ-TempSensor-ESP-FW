Import("env")

env.AddPostAction(
    "$BUILD_DIR/${PROGNAME}.bin", 
    "esptool.py --chip esp32 merge_bin -o $BUILD_DIR/${PROGNAME}_FLASHER.bin " + 
    " --flash_mode dio --flash_freq 40m --flash_size 4MB " +
    " 0x1000 $BUILD_DIR/bootloader.bin " +
    " 0x8000 $BUILD_DIR/partitions.bin " + 
    " 0xe000 " + 
    " 0x10000 $BUILD_DIR/${PROGNAME}.bin"
)