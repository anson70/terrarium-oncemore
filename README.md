# OnceMore

Tape degradation for the PedalPCB Terrarium: from a warm, wobbly cassette to a broken toy recorder.

Firmware for the PedalPCB Terrarium
with an Electrosmith Daisy Seed. How to play it: **[MANUAL.md](MANUAL.md)**.
What changed: [CHANGELOG.md](CHANGELOG.md).

Current version: **1.0**

## Download

Open [Releases](../../releases) and download `terrarium-oncemore-v1.0.bin`
from the newest one.

`terrarium-passthrough.bin` is in every release too: it passes the guitar
through unchanged (knob 1 is volume). Use it to check a new build of the
pedal before trying the effect.

## Flash it (nothing to install)

1. Open the Daisy Web Programmer, <https://flash.daisy.audio>, in **Chrome or
   Edge** (it uses WebUSB, which Firefox and Safari lack).
2. Connect the Seed's USB port to the computer.
3. Put the Seed in its bootloader: hold **BOOT**, press and release
   **RESET**, then release **BOOT** (the two buttons on the Seed itself).
4. In the web programmer, open the **File Upload** tab, choose the `.bin`
   and press **Flash**. When Chrome asks which device to use, pick
   **DFU in FS Mode** (or **STM32 BOOTLOADER**). The Seed restarts with the
   new firmware when done.

**Windows: "no compatible devices found".** The Seed's bootloader needs the
WinUSB driver once. Run [Zadig](https://zadig.akeo.ie/), choose Options →
List All Devices, pick "DFU in FS Mode" (or "STM32 BOOTLOADER"), select
WinUSB and press Install Driver. Then try step 4 again.

## Check what is running (optional)

The firmware reports over USB serial. With the Seed powered from USB (out of
the pedal is fine), open the serial port with any terminal, for example
<https://googlechromelabs.github.io/serial-terminal/>; the speed setting does
not matter. Every 10 seconds it prints a line like

```
terrarium-fx oncemore (oncemore-v1.0) on Daisy Seed 1.1 (WM8731), SDRAM used ... KB
cpu avg ...%  max ...%
```

and every knob, toggle and footswitch change as it happens, which is a quick
way to check the panel wiring.

## Hardware

- PedalPCB Terrarium with a Daisy Seed. Tested on a Seed 1.1; the firmware
  detects later Seed revisions (libDaisy v9), but they are untested
- Knobs, toggles, footswitches and LEDs in the Terrarium's standard positions
- Mono in, mono out, 48 kHz

## Build from source

The DSP is written once, without any hardware code, and runs both on the
pedal and on a desktop, where every behaviour is tested offline.

```
effects/oncemore/   the effect: code, spec (spec.md), tests, approved reference audio
effects/passthrough/  the smallest effect, for checking the pipeline
core/              parameters, smoothing, footswitch gestures, panel mapping
hosts/daisy/       the firmware: the only code that touches libDaisy
hosts/offline/     fx-run: renders a WAV through an effect with a scripted panel
tests/             unit and command-line tests
```

**Firmware** (needs CMake 3.24+, Ninja and `arm-none-eabi-gcc`; libDaisy is
downloaded on first configure):

```sh
cmake -S hosts/daisy -B build-daisy -G Ninja
cmake --build build-daisy      # build-daisy/terrarium-oncemore.bin
```

**Desktop tests and fx-run** (any C++17 compiler):

```sh
cmake -S . -B build -G Ninja
cmake --build build && (cd build && ctest --output-on-failure)
./build/fx-run oncemore --params
./build/fx-run oncemore --in guitar.wav --script effects/oncemore/tests/tour.yaml --out out.wav
```

The tests include a bit-exact comparison against reference audio that was
listened to and approved, so a change that alters the sound is caught.
`spec.md` (in Chinese) holds the design, every formula and its source.
Code in `core/` and `effects/` follows a few real-time rules so that what
passes on the desktop also works on the pedal: no memory allocation, locks or
I/O inside `process()`; float rather than double; nothing that assumes a
sample rate or block size; large buffers only from the allocator given to
`prepare()`; randomness only from the seed. The offline host checks the first
and fails a run that allocates.

This repository is published from a larger private one, so pull requests
are welcome but get applied there and come back with the next release.

## Licence

OnceMore is MIT licensed (LICENSE). The firmware also contains libDaisy (MIT) and
STMicroelectronics and Arm code under their own licences; all of them are in
[LICENSES.txt](LICENSES.txt).

Not affiliated with PedalPCB or Electrosmith.
