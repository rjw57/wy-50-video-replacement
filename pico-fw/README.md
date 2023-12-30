# Raspberry Pi Pico firmware

## Building

```console
$ mkdir build
$ pushd build
$ PICO_SDK_PATH=$HOME/projects/pico/pico-sdk cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=1 ..
$ popd
$ cmake --build build --parallel
```

The `libtmt` library is originally from https://github.com/deadpixi/libtmt.

The `libtst` library is originally from https://www.freedesktop.org/wiki/Software/kmscon/libtsm/.

The `libvterm` library is originally from https://www.leonerd.org.uk/code/libvterm/.

Connect to serial console via:

```sh
sudo socat -d -d 'stdin,raw,echo=0!!/dev/ttyACM1,b115200,raw,echo=0,cs8' \
    exec:"/bin/login -f $USER TERM=ansi",pty,setsid,setpgid,stderr
```

## Hardware

Connect the following pins of the WY-50 video connector to the pico board. Use any GND connection
which is convenient.

| Pin | Nominal Colour | Actual Colour | Signal | Pico pin  | RP2040 pin |
| --- | -------------- | ------------- | ------ | --------- | ---------- |
| 1   | White          | White         | VIDEO  | 7         | GP5        |
| 2   | Black          | Black         | GND    | Any GND   | GND        |
| 3   | Orange         | Orange        | !DIM   | 6         | GP4        |
| 4   | Yellow         | Yellow        | HSYNC  | 5         | GP3        |
| 5   | Green          | Green         | VSYNC  | 4         | GP2        |
| 8   | Red            | Red           | +5V    | 39 (VSYS) | -          |

Ideally the VIDEO, !DIM, HSYNC and VSYNC signals will be buffered by any convenient 74xx or
4000-series logic. For example, the WY-50 terminal itself uses a 7408 quad AND gate to buffer the
video signals.

One should put a decoupling cap over +5V and GND from the WY-50 terminal close to the buffering IC.

Ideally +5V from the terminal will be connected via a diode to avoid back-driving the power supply.
Similarly, connecting pin 37 (3V3 En) to +5V and to GND via a pull-down resistor causes the pico to
only power up when the WY-50 terminal is powered.

## Picoprobe

Configuration for udev allowing members of the `dialout` group to connect to a picoprobe is provided
in the [udev](./udev/) directory. To enable:

```console
$ sudo cp udev/99-picoprobe.rules /etc/udev/rules.d/
$ sudo udevadm control --reload-rules &&  sudo udevadm trigger
```

One should then be able to start `openocd` as an ordinary user after building and installing it as
per the pico getting started guide:

```sh
openocd -f interface/cmsis-dap.cfg -c "adapter speed 5000" -f target/rp2040.cfg -s tcl
```

If using a RP2040-GEEK, use the appropriate connector cable to connect the SWD port of the
RP2040-GEEK to the SWD pins of the pico. Connect the UART cable according to the following table:

| RP2040-GEEK | Pico pin |
| ----------- | -------- |
| GP5         | 1 (GP0)  |
| GND         | 3        |
| GP4         | 2 (GP1)  |

The serial console can be connected to via, e.g. `minicom`:

```sh
minicom -b 115200 -o -D /dev/ttyACM0
```

Start the debugger via:

```sh
gdb -ix gdbinit ./build/firmware.elf
```

The `bld` and `mri` commands are provided in the `gdbinit` file to rapidly re-build and load and/or
reset the pico.

## Font

Font references taken from
https://dwarffortresswiki.org/index.php/DF2014:Tileset_repository#8.C3.9714 and converted into raw
data via:

```console
$ convert MDA9x14.png -depth 1 -threshold 50% GRAY:mda_font.data
$ xxd -i -n mda_font mda_font.data mda_font.h
```

And similarly for other fonts.
