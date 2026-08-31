# LiveHaptics — Universal Linux Guide

Real-time PC audio -> haptics for the Steam Controller (2026).
Works on any Linux: SteamOS/Steam Deck, Arch/Manjaro, Debian/Ubuntu/Mint,
Fedora, etc. Wired USB / Steam Puck / Bluetooth. Qt GUI + CLI.

============================================================
1. WHAT YOU NEED
============================================================
- A C++ compiler (g++) and make
- hidapi (library + headers)
- Qt5 (for the GUI only; the CLI core needs no Qt)
- PulseAudio or PipeWire with a "*.monitor" source (any normal desktop
  install has one; it is how we hear your audio)
- ffmpeg is NOT required

============================================================
2. INSTALL PER DISTRO
============================================================
Arch / Manjaro / SteamOS:
    sudo pacman -S base-devel qt5-base hidapi

Debian / Ubuntu / Mint / Pop!_OS:
    sudo apt update
    sudo apt install build-essential pkg-config qtbase5-dev \
        libhidapi-dev pulseaudio-utils

Fedora:
    sudo dnf install gcc-c++ make pkgconfig qt5-qtbase-devel \
        hidapi-devel pulseaudio-utils

openSUSE:
    sudo zypper in gcc-c++ make pkgconfig libqt5-qtbase-devel \
        hidapi-devel pulseaudio-utils

Audio capture: PipeWire users need "pipewire-pulse" (usually preinstalled);
PulseAudio users need nothing extra. Check with:
    pactl list short sources      # you should see *.monitor entries

============================================================
3. HID PERMISSIONS (IMPORTANT ON NON-DECK SYSTEMS)
============================================================
On a regular PC the /dev/hidraw* devices may not be writable by your user.
Add a udev rule once:

    sudo sh -c 'printf "SUBSYSTEM==\\"hidraw\\", ATTRS{idVendor}==\\"28de\\", MODE=\\"0666\\"\n" > /etc/udev/rules.d/99-livehaptics.rules'
    sudo udevadm control --reload-rules
    sudo udevadm trigger

(Steam Deck/SteamOS already allows this; skip if everything works.)

============================================================
4. GET IT & BUILD (recommended on all distros)
============================================================
Download LiveHaptics-v1.2-LINUX.tar.gz from Releases (or clone the repo),
then:

    tar -xzf LiveHaptics-v1.2-LINUX.tar.gz
    cd LiveHaptics-v1.2-LINUX
    chmod +x *.sh
    ./build.sh         # core -> ./livehaptics   (needs g++ + hidapi)
    ./build_gui.sh     # GUI  -> ./LiveHapticsGUI (needs Qt5)

The prebuilt binaries in the tar are linked on Arch/SteamOS; on
Debian/Ubuntu/Fedora build from source instead (above) - it takes ~1 min.
If linking complains about "-lhidapi-hidraw", edit build scripts to
"-lhidapi".

============================================================
5. RUN
============================================================
1. CLOSE STEAM if it is running (Steam Input can hold the HID device).
2. Connect: USB cable, Puck dongle, or paired Bluetooth (pair in your
   desktop's Bluetooth settings first).
3. ./LiveHapticsGUI  -> press Start.   (CLI: ./livehaptics)
X11 and Wayland both work; Wayland "requestActivate" warnings are harmless.

============================================================
6. THE GUI
============================================================
- Capture source: lists "*.monitor" sources; haptics follow the chosen
  output. Switching mid-stream restarts the core automatically.
- Controller: auto-picks the haptics-capable interface; sticky.
- Wireless rate: Auto / 4 kHz clean / 8 kHz hi-fi (Puck & BT only).
- Gain / Bass / Latency cap: live sliders.
- Force 16-bit: wired-only mode.
- Tester panel: L/R meters + packet counter.
- Stop/Start is graceful - no replugging the controller.

============================================================
7. CLI
============================================================
./livehaptics [--gain X] [--bass X] [--cap N] [--rate 4000|8000]
              [--16bit] [--dev N] [--pick]
Live keys: +/- gain, [ ] bass, , . latency, q quit.

============================================================
8. TRANSPORTS & QUALITY
============================================================
Wired USB    16-bit 8 kHz (best)
Steam Puck   8 kHz u-law hi-fi (4 kHz clean selectable)
Bluetooth    1 kHz u-law on Linux

============================================================
9. FIRST-RUN PUCK
============================================================
Puck found but silent?
    ./livehaptics --pick
choose the row tagged <haptics>. Saved to livehaptics.cfg.

============================================================
10. TROUBLESHOOTING
============================================================
- "Permission denied" / "controller not found" on a normal PC
                     -> udev rule (section 3), then replug.
- Controller not found -> close Steam, replug dongle, re-pair BT.
- GUI won't start      -> missing Qt5: see section 2; or run ./livehaptics
                          (CLI needs no Qt).
- No "*.monitor" in Capture source
                     -> install pipewire-pulse (or pulseaudio), restart.
- Pops on Puck on busy RF -> Wireless rate -> 4 kHz clean.
- "Permission denied" on the binaries -> chmod +x (section 4).

============================================================
11. CREDITS
============================================================
Protocol research builds on community work: SteamHapticsPlayer / TritonLib
(Ritonton) and the SDL/Valve report structs.
