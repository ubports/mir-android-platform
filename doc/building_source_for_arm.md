Building the Android platform from source for ARM {#building_source_for_arm}
===============================

There are a few ways to compile Mir for an ARM device. You should only need
to choose one of these approaches...

Building on the ARM device
--------------------------

If you have an ARM device you should be able to compile and install directly 
on the device. Although this will usually be significantly slower than using a
desktop. On the armhf or arm64 target device just follow these steps:

       $ mk-build-deps --install --tool "apt-get -y" --build-dep debian/control
       $ cmake .. -DMIR_PLATFORM=android
       $ make
       $ make install

The addional cmake option -DMIR_ENABLE_TESTS=off can be used to avoid building
the test suite to save time.

Building for ARM from a PC (cross compiling)
--------------------------------------------

Using a current Ubuntu (15.04 or later) installation it's very simple to build
binaries for armhf (32-bit) devices. Just follow these steps:

    $ sudo apt-get install g++-arm-linux-gnueabihf g++-4.9-arm-linux-gnueabihf multistrap
    $ cd mir_source_dir
    $ ./cross-compile-chroot.sh
    $ ls -l build-android-arm/*  # binaries to copy to your device as you wish

The special package 'g++-4.9-arm-linux-gnueabihf' above is required if you
are building for vivid (-d vivid).

If you wish to target arm64 (AArch64) then you can use:

    $ sudo apt-get install g++-aarch64-linux-gnu multistrap
    $ cd mir_source_dir
    $ ./cross-compile-chroot.sh -a arm64
    $ ls -l build-arm64-*/bin

Note: If your target device is running an Ubuntu version other than vivid
then you will also need to specify the target distribution for correct
library linkage. For example:

    $ ./cross-compile-chroot.sh -a arm64 -d wily

More architectures like PowerPC are also supported. To see the full list of
options, just run:

    $ ./cross-compile-chroot.sh -h

To speed up the process for future runs of cross-compile-chroot.sh, some files
are saved in ~/.cache/. To flush the cache and download new armhf packages
just add the -u option to cross-compile-chroot.sh.

Building armhf deb packages
---------------------------

"sbuild" is recommended to compile Mir packages with armhf. Information on
setting up sbuild can be found here:

 * <a href="https://wiki.debian.org/sbuild"> https://wiki.debian.org/sbuild</a>
 * <a href="https://wiki.ubuntu.com/SimpleSbuild">
https://wiki.ubuntu.com/SimpleSbuild</a> 

If you do not wish to run the Mir test suite during package generation, set
DEB_BUILD_OPTIONS=nocheck to your environment

Emulated sbuild package generation
----------------------------------

This uses qemu to compile the package. Substitute \<version_string> for the .dsc
file name generated by the debuild command.

       $ cd mir_source_dir
       $ debuild -S -uc -us
       $ cd .. 
       $ sbuild -d vivid --arch armhf mir_<version_string>.dsc

Cross-compile sbuild package generation
---------------------------------------

This uses a cross-compile toolchain to compile the package, and generally 
should be faster than the emulated sbuild package generation.

Substitute \<version_string> for the .dsc file name generated by the debuild 
command.

       $ cd mir_source_dir
       $ debuild -S -uc -us
       $ cd .. 
       $ sbuild -d vivid --host armhf --build amd64 mir_\<version_string>.dsc

