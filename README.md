# LiveHaptics — live system-audio haptics for the Steam Controller (2026)

Plays your **live Windows system audio** through the Steam Controller (2026)
haptic motors, in real time. No driver installs, no WinUSB/Zadig, no USBIP —
runs on the controller's **stock HID driver** and coexists with Steam.

## How it works
- WASAPI loopback captures the default playback device
- Resamples to 8 kHz stereo µ-law (the controller's native Puck haptic format)
- PCM-stream setup per Pixel1011's TritonLib (DISABLE/ENABLE actuators 2+5, mode Khz8_8Bit_ulaw)
- Report 0x88 over hidapi, paced 3875 µs/packet (128 kbps)

## Use
1. Puck plugged in (or USB cable), controller on. Steam open or closed.
2. Run `LiveHaptics.exe`.
3. Press `y` when you feel the 1-second calibration buzz.
4. Play anything. Enter quits.

## Build (MSYS2 UCRT64)
    pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-hidapi mingw-w64-ucrt-x86_64-spdlog mingw-w64-ucrt-x86_64-fmt
    g++ -O2 -std=gnu++23 haptics.cpp -o LiveHaptics.exe -lhidapi -lspdlog -lfmt -lole32 -luuid -lsetupapi

## Credits
- Protocol reverse-engineering: **Pixel1011** (SteamHapticsPlayer / TritonLib), protocol data by **iczero**.
- µ-law haptics pipeline concepts: **ga2mer** (sc2ds).
