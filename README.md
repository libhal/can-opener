# 🥫🔓 Can Opener

Can Opener is an open source cross platform firmware application that
implements the [Lawicel slcan (Serial CANBUS) protocol](https://www.canusb.com/files/canusb_manual.pdf) using libhal and its ecosystem.

Currently this project only supports SparkFun's MicroMod boards.

Any MicroMod board should work so long as it implements the following APIs:

- `hal::micromod::v1::can`: Used to communication over CAN.
- `hal::micromod::v1::led`: Used as an indicator in the application.
- `hal::micromod::v1::console`: Used to communicate over serial to a host device
- `hal::micromod::v1::uptime_clock`: Used for telling time
- `hal::micromod::v1::reset`: Used to reset the device

## 🏗️ Building the Application

libhal appications use the [Conan package manager](https://conan.io/center). To
use libhal you will need to install conan. Conan and libhal will handle installing the appropriate compiler and build systems for you. No need to install them yourself.

Follow the "🚀 libhal Getting Started" guide and stop when you reach
"🛠️ Building Demos". Come back to this page to continue the build steps. The
link to the guide is [here](https://libhal.github.io/getting_started/).

Download the MicroMod profiles using:

```bash
conan config install -sf conan/profiles/v1 -tf profiles https://github.com/libhal/libhal-micromod.git
```

Download the ARM GCC profiles:

```bash
conan config install -tf profiles -sf conan/profiles/v1 https://github.com/libhal/arm-gnu-toolchain.git
```

To build for the `stm32f1 MicroMod v4`:

```bash
conan build . -pr mod-stm32f1-v4 -pr arm-gcc-12.3
```

To build for the `lcp40 MicroMod v5`:

```bash
conan build . -pr mod-lcp40-v5  -pr arm-gcc-12.3
```

## 💾 Flashing your MicroMod Board

For the lpc40 v5 board:

```bash
nxpprog --control --cpu lpc4078 --binary build/micromod/mod-lpc40-v5/Release/app.elf.bin --device /dev/tty.usbserial-2110
```

For the stm32f1 v4 board:

```bash
stm32loader -p /dev/tty.usbserial-58690101901 -e -w -v build/micromod/mod-stm32f1-v4/Release/app.elf.bin
```
