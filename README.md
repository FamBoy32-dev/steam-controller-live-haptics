# LiveHaptics v1.2 - Linux / Steam Deck
Real-time audio -> haptics for Steam Controller (2026). Wired / Puck / Bluetooth.
Dark Qt GUI + CLI. Run from Desktop mode.

## Run (prebuilt)
    chmod +x livehaptics LiveHapticsGUI   # only if the archive lost exec bits
    ./LiveHapticsGUI                      # or ./livehaptics for CLI

## What to install (only if something is missing)
SteamOS already ships PipeWire/PulseAudio. The prebuilt GUI needs Qt5 + hidapi:
    sudo pacman -S qt5-base hidapi
To build from source:
    sudo pacman -S base-devel qt5-base hidapi
    ./build.sh        # core  -> ./livehaptics
    ./build_gui.sh    # GUI   -> ./LiveHapticsGUI

## Capture source
The dropdown lists PulseAudio "*.monitor" sources; haptics follow the chosen
output. Switching mid-stream restarts the core automatically.

## Transports
Wired 16-bit 8 kHz | Puck 8 kHz (4 kHz selectable) | Bluetooth 1 kHz (Linux).

## First-run Puck
If found but silent: ./livehaptics --pick  -> choose the <haptics> row.

## Troubleshooting
- "Permission denied" -> chmod +x (archives made on Windows lose exec bits).
- Controller not found? Close Steam, replug the dongle, re-pair BT.
- Pops on Puck? Wireless rate -> 4 kHz clean.
- Nothing here needs sudo except the optional pacman installs.
