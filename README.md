# LiveHaptics

**Real-time PC audio -> haptics for the Steam Controller (2026).**
Windows · Linux · Wired USB · Steam Puck · Bluetooth.

LiveHaptics captures your system audio, runs it through a per-channel DSP
chain (gain, bass shelf, soft-knee limiter), and streams it as haptic PCM to
the controller's actuators - feel music, movies and games on both motors,
in stereo, on every way the controller connects.

- **Windows:** fully static binaries, no install, dark Win11-style GUI.
- **Linux:** works on any distro (SteamOS/Arch, Debian/Ubuntu, Fedora, ...),
  dark Qt GUI + build scripts. See `LINUX-GUIDE.md` for the Linux deep-dive.

---

## Supported hardware

| Connection | PID | Haptics stream |
|---|---|---|
| Wired USB | `0x1302` | 16-bit 8 kHz stereo (256 kbps) - best |
| Steam Puck (2.4 GHz dongle) | `0x1304` | 8 kHz µ-law stereo (128 kbps), 4 kHz clean selectable |
| Bluetooth | `0x1303` | 4 kHz µ-law (Windows) / 1 kHz µ-law (Linux) |

The controller firmware rejects 16-bit over wireless (bandwidth), so
wireless transports use µ-law. The GUI's **Wireless rate** selector switches
between 8 kHz hi-fi and 4 kHz clean (the pop-free zone on busy RF).

## Downloads

Grab the two archives from **Releases**:
- `LiveHaptics-v1.2-WINDOWS.zip` - `LiveHapticsGUI.exe`, `core.exe`, `LiveHaptics.exe` (CLI), `README.txt`
- `LiveHaptics-v1.2-LINUX.tar.gz` - prebuilt binaries + sources + build scripts

---

## Windows

**Install: none.** Unzip, double-click `LiveHapticsGUI.exe` (Win10/11,
statically linked). `core.exe` must stay next to it; `LiveHaptics.exe` is the
standalone CLI.

1. **Close Steam** - Steam Input can hold the controller's HID device.
2. Connect: cable, Puck dongle, or paired Bluetooth.
3. Start. Pick your output under **Capture device** if it isn't the default.

SmartScreen may flag an unknown publisher - false positive, *Run anyway*.

### Build from source (MSYS2 UCRT64)
    pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-hidapi
    g++ -O2 -std=gnu++23 haptics.cpp -o LiveHaptics.exe -static \
        -lhidapi -lspdlog -lfmt -lole32 -luuid -lsetupapi -lwinmm
    g++ -O2 -std=gnu++23 gui.cpp -o LiveHapticsGUI.exe -mwindows -static \
        -lcomctl32 -lhidapi -lole32 -luuid -ldwmapi

---

## Linux (any distro)

Quick version - full guide in **LINUX-GUIDE.md**.

Packages:
    # Arch / Manjaro / SteamOS
    sudo pacman -S base-devel qt5-base hidapi
    # Debian / Ubuntu / Mint
    sudo apt install build-essential pkg-config qtbase5-dev libhidapi-dev pulseaudio-utils
    # Fedora
    sudo dnf install gcc-c++ make pkgconfig qt5-qtbase-devel hidapi-devel pulseaudio-utils
    # openSUSE
    sudo zypper in gcc-c++ make pkgconfig libqt5-qtbase-devel hidapi-devel pulseaudio-utils

Run (prebuilt binaries are Arch-linked; elsewhere build from source):
    chmod +x livehaptics LiveHapticsGUI *.sh
    ./build.sh && ./build_gui.sh
    ./LiveHapticsGUI          # or ./livehaptics (CLI, no Qt needed)

On a regular PC (not Deck) add the HID udev rule once:
    SUBSYSTEM=="hidraw", ATTRS{idVendor}=="28de", MODE="0666"
(in /etc/udev/rules.d/, then `udevadm control --reload-rules && udevadm trigger`)

---

## The GUI (both platforms)

- **Capture device / source** - Windows render devices or PulseAudio
  `*.monitor` sources; switching mid-stream restarts and follows.
- **Controller** - auto-picks the haptics-capable interface; sticky.
- **Wireless rate** - Auto / 4 kHz clean / 8 kHz hi-fi (Puck & BT only).
- **Gain / Bass / Latency cap** - live sliders with readouts.
- **Force 16-bit** - wired-only mode.
- **Tester panel** - L/R meters + packet counter.
- **Presets** (Windows) - Add / Save / Delete.
- Stop/Start is graceful; unplug/replug auto-reconnects.

## CLI

    LiveHaptics [--gain X] [--bass X] [--cap N] [--rate 4000|8000]
                [--16bit] [--dev N] [--pick]
Live keys: `+/-` gain, `[ ]` bass, `, .` latency, `q` quit.

`--pick` interactively chooses the haptics interface and saves it to
`livehaptics.cfg` - use it if a Puck is found but silent.

---

## How it works

Capture (WASAPI loopback / PulseAudio monitor) -> gain + bass shelf +
soft-knee limiter -> decimation to the transport rate -> µ-law or 16-bit
stereo encoding -> HID report `0x88` (31 x 8-bit or 15 x 16-bit samples per
channel) paced at packet rate (3.875 ms at 8 kHz). Streaming is armed with
`0x86` PCM_MODE enable on actuator channels 2+5. A jitter buffer with
proportional clock recovery plus near-freeze gap concealment keeps wireless
clean; the send path mirrors SteamHapticsPlayer's discipline (one pacer, one
writer, no HID/COM traffic during playback).

## Troubleshooting

- **Controller not found** - close Steam, replug dongle, re-pair BT;
  Linux: udev rule above.
- **Puck found but silent** - `--pick`, choose the `<haptics>` row.
- **Pops on Puck** - Wireless rate -> 4 kHz clean.
- **No haptics after changing audio output** - pick it under Capture device/source.
- **Linux "Permission denied"** - `chmod +x`.
- **Linux GUI won't start** - `sudo pacman -S qt5-base hidapi` (or apt/dnf equivalent).

## Credits & license

Protocol research builds on community work: **SteamHapticsPlayer / TritonLib**
(Ritonton) and the **SDL / Valve** report structs (zlib license).
LiveHaptics is MIT licensed.
