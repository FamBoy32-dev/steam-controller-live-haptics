LiveHaptics for Linux - feel your system audio in your Steam Controller (2026)
==============================================================================

WHAT IT DOES
Plays your Linux audio (music, games, videos) through the controller's
haptic motors in real time. No extra drivers; lives happily next to Steam.

1) INSTALL THESE (one time)
Steam Deck / Arch / SteamOS:
    sudo pacman -S base-devel hidapi libpulse qt5-base
Ubuntu / Mint / Debian:
    sudo apt install g++ pkg-config libhidapi-dev libpulse-dev qtbase5-dev
(The qt packages are only needed for the GUI; the terminal version works without them.)

2) LET LINUX TALK TO THE CONTROLLER (one time)
    echo 'SUBSYSTEM=="hidraw", ATTRS{idVendor}=="28de", MODE="0666"' | sudo tee /etc/udev/rules.d/99-livehaptics.rules
    sudo udevadm control --reload-rules && sudo udevadm trigger
    (or just unplug/replug the controller or dongle once)

3) BUILD (about 30 seconds)
    ./build.sh         # terminal version  -> ./livehaptics
    ./build_gui.sh     # window version    -> ./LiveHapticsGUI

4) RUN
GUI (recommended):
    ./LiveHapticsGUI
    Using the wireless Puck dongle? Run it like this instead:
    ./LiveHapticsGUI --dev 0
    Then pick your controller in the dropdown and press Start.

Terminal version:
    ./livehaptics
    Press Y when you feel the calibration buzz (first time only).
    Live keys:  + -  gain   [ ]  bass   , .  latency   q  quit

5) MAKE IT HEAR WHAT YOU HEAR (if the buzz stays silent)
Point capture at your speakers' monitor:
    pactl set-default-source $(pactl info | grep 'Default Sink' | awk '{print $3}').monitor

CONNECTION GUIDE
Wired USB cable : best quality (16-bit 8 kHz), always works.
Puck dongle     : full quality (8 kHz). If ever silent: --dev 0 (above).
Bluetooth       : experimental on Linux (Deck power-save can add lag).
                  Prefer Puck or wired on the Deck.

TROUBLESHOOTING
"No controller found"   -> do step 2, replug.
GUI open but no buzz    -> --dev 0 for Puck, or the pactl line in step 5.
Too weak / too strong   -> Gain slider (GUI) or + / - (terminal).
Feels late              -> raise latency one step (GUI) or , / . (terminal).

Credits: protocol Pixel1011 (SteamHapticsPlayer/TritonLib) + iczero; concepts ga2mer.
MIT license. Full source included.
