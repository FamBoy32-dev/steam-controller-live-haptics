# LiveHaptics — live system audio → Steam Controller (2026) haptics
Plays your PC audio through the controller's haptic motors, real time.
No drivers, no Zadig, no USBIP — stock HID, coexists with Steam.

## Downloads (Releases → v1.2)
- **Windows:** `livehaptics-windows-v1.2.zip` — unzip, run `LiveHapticsGUI.exe` (or CLI `LiveHaptics.exe`)
- **Linux:** `livehaptics-linux-v1.2.zip` — follow `README-linux.txt` (`./build.sh`, `./build_gui.sh`)

## Modes
| Mode | Windows | Linux |
|---|---|---|
| Wired USB (16-bit 8 kHz) | ✔ | ✔ |
| Puck dongle (µ-law 8 kHz) | ✔ | ✔ (silent? `./LiveHapticsGUI --dev 0`) |
| Bluetooth (µ-law 4 kHz) | ✔ | ⚠ experimental |

## Credits
Protocol: Pixel1011 (SteamHapticsPlayer/TritonLib) + iczero · Concepts: ga2mer · MIT
