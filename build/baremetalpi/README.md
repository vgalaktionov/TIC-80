
# Raspberry PI Baremetal build

The following explains how to build TIC-80 for the Raspberry PI boards in baremetal mode, that is, without an operating system (the board boots directly in TIC-80)

# Requirements

You need:

- A Linux machine
- gcc ARM toolchain. You can get it [here](https://developer.arm.com/tools-and-software/open-source-software/developer-tools/gnu-toolchain/gnu-rm/downloads). Successfully tested with version: gcc-arm-none-eabi-8-2018-q4-major
- A standard building environment (make, cmake, gcc, wget, etc)

# Building instructions

Following are all the building steps for building the system.

First, set the path to include the arm toolkit (fix the command with your path):

```
PATH=/home/you/gcc-arm-none-eabi-8-2018-q4-major/bin/:$PATH
```

Get a fresh copy of TIC-80 repository:

```
git clone --recursive https://github.com/nesbox/TIC-80
cd TIC-80
```

Clone the Circle dependency and apply TIC-80's kernel-size configuration before
building it. Replace `4` with your Raspberry Pi model when needed:

```
git clone --recursive https://github.com/smuehlst/circle-stdlib vendor/circle-stdlib
cd vendor/circle-stdlib
git checkout db053a32c165c1b22423a47ed6cb5bddc72b51f2
git submodule update --recursive
cd ../..
git apply build/baremetalpi/circle-kernel-size.patch
cd vendor/circle-stdlib
./configure -r 4
make
```

Make some addon that are not compiled automatically:

```
cd libs/circle/addon/vc4/sound/
make
cd ../vchiq
make
cd ../../linux
make
cd ../../../../../..
```

Build the on-board Wi-Fi driver and download its firmware:

```
cd vendor/circle-stdlib/libs/circle/addon/wlan
./makeall --nosample
cd firmware
make firmware
cd ../../../../../../..
```

Build `tic80studio` for arm with baremetal customizations:

```
git apply build/baremetalpi/circle.patch
cd build
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=baremetalpi/toolchain.cmake -DBUILD_WITH_ALL=ON -DBUILD_PRO=ON ..
make tic80studio
```

Build the kernel:

```
cd baremetalpi
make
```

This generates the final `kernel8-32.img` file (or something similar depending on the RPI version). Copy it into your SD card root.

Now you have to download some bootup files that need to be copied to the SD card root together with your kernel8-32.img. This only need to be done once.

```
cd ../../vendor/circle-stdlib/libs/circle/boot/
make
```

Read the README.md in this folder to see what files needs to be copied to your RPI. For RPi3 should be ok to copy all of them

You can create a `tic80` folder into your SD card to put your carts in.

## Wi-Fi

For Raspberry Pi 4 Wi-Fi, copy these files into `firmware/` on the SD card:

```
brcmfmac43455-sdio.bin
brcmfmac43455-sdio.clm_blob
brcmfmac43455-sdio.txt
```

Copy `build/baremetalpi/wpa_supplicant.conf.example` to the SD card root as
`wpa_supplicant.conf`, then set its `country`, `ssid`, and `psk` values. The
network uses DHCP. If the configuration file is absent, networking is disabled
and TIC-80 continues booting normally. Do not commit the configured file.

## CRT monitor

The Raspberry Pi build renders to a 960x544 framebuffer. With the CRT monitor
disabled, each TIC-80 pixel is copied to an exact 4x4 output block. Enable the
software CRT effect with F6 or with `OPTIONS > CRT MONITOR`; the setting is
saved in the normal Studio options file.

# Thanks

This project is built on two awesome projects, [circle](https://github.com/rsta2/circle) and [circle-stdlib](https://github.com/smuehlst/circle-stdlib). Without them, this version of TIC-80 would not exist.
