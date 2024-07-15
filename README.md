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

## 🚀 Installing Firmware via prebuilt binaries

We provide prebuilt binaries for each of our releases. You can use these to program your microcontroller with the CanOpener firmware.

> [!NOTE]
> We plan to eliminate this flow once the CanOpener website supports flashing
> devices. The aim is to provide an "hardware update" button that will flash
> your device for you. It would present a screen that provides a list of all of
> the released versions and allow the user to select which one they want to
> install or simply install the latest..

Steps to upgrade firmware are:

1. Connect CanOpener device to computer using a USB cable.
2. Download [CanOpener/0.0.0](https://github.com/libhal/can-opener/releases/download/0.0.0/mod-stm32f1-v4-Debug.bin).
3. Program the device by using the [stm-serial-flasher](https://gamadril.github.io/stm-serial-flasher/) website.
   1. To use this website, click "Select Port"
   2. Then click "Pair New Port" and look for a the serial port for
      your device. It should have a name like `COM8`, `/dev/ttyUSB2`,
      `/dev/ttyACM1`, or `/dev/tty.serial-1100`.
   3. Click the "x" to close the modal screen.
   4. Click "Connect".
   5. Click "Open File" and navigate to where the file was downloaded select it.
   6. Now click "Flash" and wait for the board to be flashed.
   7. Click disconnect.
4. Now go to the [CanOpener](https://libhal.github.io/web-tools/can/) website and connect your device to the website.
5. You are done 😄

> [!WARNING]
> If you are not using a CanOpener USB to CAN adaptor board and you are using
> your own board, then make sure to have the can transceiver connected. Without
> it connected, some CAN peripherals may hang at initialization and may hang
> when attempting to transmit a message. If you're device is not working with
> the CanOpener website, consider checking the state of your CAN transceiver IC
> or board.

## 🏗️ Building the Application

libhal applications use the [Conan package manager](https://conan.io/center). To
use libhal you will need to install conan. Conan and libhal will handle
installing the appropriate compiler and build systems for you. No need to
install them yourself.

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
conan build . -pr mod-stm32f1-v4 -pr arm-gcc-12.3 -s build_type=Debug
```

To build for the `lcp40 MicroMod v5`:

```bash
conan build . -pr mod-lcp40-v5  -pr arm-gcc-12.3  -s build_type=Debug
```

> [!CAUTION]
> The `Release` version of the binary doesn't seem to work well so users should
> stick to the `Debug` version until this notice is removed.

## 💾 Flashing your Board via command line

> [!IMPORTANT]
> Make sure to replace the `--device` and port `-p` (examples uses
> `/dev/tty.usbserial-100`) serial device paths and names to the correct
> ones for your platform.

For the lpc40 v5 board:

```bash
nxpprog --device /dev/tty.usbserial-100 --control --cpu lpc4078 --binary build/micromod/mod-lpc40-v5/Debug/app.elf.bin
```

For the stm32f1 v5 board:

```bash
stm32loader -p /dev/tty.usbserial-100 -e -w -v -B build/micromod/mod-stm32f1-v4/Debug/app.elf.bin
```

For the stm32f1 v4 board:

```bash
stm32loader -p /dev/tty.usbserial-100 -e -w -v build/micromod/mod-stm32f1-v4/Debug/app.elf.bin
```
