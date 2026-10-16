# ClickShare on Linux

Share your **Linux desktop and audio** to a Barco ClickShare room system with a
**ClickShare Button** (USB dongle). Barco ships the ClickShare app for Windows
and macOS only; this project makes the Windows app work under Wine.

> 🇩🇪 Deutsche Fassung: [README.de.md](README.de.md)

| | Status |
|---|---|
| Button detected, pairing with the room | ✅ |
| Desktop video on the room display | ✅ (via the included shim) |
| Audio in the room | ✅ |
| Window sharing (single window) | ❓ untested (thumbnail stays black) |
| Tested on | Pop!_OS 22.04, GNOME on **X11**, Wine 11.0 (WineHQ), ClickShare app 4.36.0, Button USB ID `0600:00ce` |

> **Disclaimer:** This is an unofficial community project. It is not affiliated
> with, endorsed by or supported by Barco. "ClickShare" and "Barco" are
> trademarks of Barco N.V. This repository contains **no Barco software**; you
> need your own copy of the ClickShare app installer.

---

## Contents

- [How it works](#how-it-works)
- [Requirements](#requirements)
- [Installation](#installation)
- [Usage](#usage)
- [Troubleshooting](#troubleshooting)
- [Limitations](#limitations)
- [Uninstall](#uninstall)
- [Technical details](docs/TECHNICAL.md)
- [License](#license)

---

## How it works

```
 Linux desktop (X11)
        │  GDI BitBlt (Wine)
        ▼
 ┌───────────────────┐   DuplicateOutput()   ┌────────────────────────────┐
 │ csdup.dll (shim)  │◀─────────────────────│ clickshare_native.exe      │
 │ fake IDXGIOutput- │──── D3D11 texture ──▶│ (ClickShare app, Wine 11)  │
 │ Duplication       │                       └──────────────┬─────────────┘
 └───────────────────┘                     HID (hidraw)     │ video stream
                                                            ▼
 PipeWire/PulseAudio ── USB audio ─────────────▶ ClickShare Button ── Wi-Fi ──▶ Base unit ──▶ room display
```

1. **The ClickShare app** (`clickshare_native.exe`) is a native Qt/C++ Windows
   program. It is extracted from Barco's `ClickShare_Setup.exe` directly. This
   skips the Squirrel installer, which needs the full .NET Framework and does
   not work under Wine.
2. **Wine 11** gives the app access to the Button's HID interfaces through
   `hidraw`. A udev rule grants your user the necessary permissions.
3. **Screen capture:** the app captures the desktop only through *DXGI Desktop
   Duplication* (`IDXGIOutput1::DuplicateOutput`). Wine has no implementation
   of it, so the room display stays black. The small shim DLL `csdup.dll`
   provides it: it grabs the screen with GDI `BitBlt` (which works in Wine),
   draws the mouse cursor and uploads each frame into a D3D11 texture for the
   app. `inject.exe` starts the app and loads the shim into it.
4. **Audio:** the Button is also a USB sound card. While a share runs, it
   forwards everything played to it into the room. The launcher makes it the
   default audio output.

Full write-up of the analysis: [docs/TECHNICAL.md](docs/TECHNICAL.md).

---

## Requirements

- A **ClickShare Button** that connects to a ClickShare base unit. Tested with a
  Button that enumerates as `0600:00ce` (Barco ClickShare), together with a
  room camera (`0600:0114`).
- `ClickShare_Setup.exe`, the Windows ClickShare app installer (see
  [Installation, step 3](#3-get-clickshare_setupexe)).
- Linux with an **X11 session**. Wayland is untested; see
  [Limitations](#limitations).
- **Wine ≥ 9** (tested: 11.0). Ubuntu 22.04's Wine 6.0 is **too old**: the app
  crashes on start.
- `python3`, `curl`, `pactl` (PipeWire or PulseAudio), optionally `7z` (`p7zip-full`).
- Root access once, for the udev rule (and for Wine if you need to install it).

Lines starting with `sudo` need root.

---

## Installation

### 1. Wine

Check your version:

```sh
wine --version
```

If Wine is older than 9, install the current WineHQ build. On Ubuntu and
derivatives such as Pop!_OS or Mint:

```sh
sudo sh scripts/install-winehq.sh
```

For other distributions, see the
[WineHQ download page](https://gitlab.winehq.org/wine/wine/-/wikis/Download).

### 2. USB permissions (udev)

Wine needs read/write access to the Button's USB and HID device nodes:

```sh
sudo sh scripts/install-udev-rule.sh
```

The rule [`udev/70-clickshare.rules`](udev/70-clickshare.rules) tags all Barco
devices (vendor ID `0600`) with `uaccess`. Only the user logged in at the seat
gets access. **Unplug and re-plug the Button afterwards.**

Verify:

```sh
getfacl /dev/hidraw* 2>/dev/null | grep -B3 "user:$USER"
```

### 3. Get `ClickShare_Setup.exe`

You need the Windows installer of the ClickShare app (tested: 4.36.0). Options:

- Download *ClickShare App for Windows* from Barco's website (barco.com →
  ClickShare → Apps / Downloads).
- Or plug in the Button and run the launcher it contains under Wine:
  `wine /media/$USER/ClickShare/ClickShare_for_Windows.exe`. Accept the EULA
  and click *Install*. The launcher downloads `ClickShare_Setup.exe` to
  `~/Downloads`. **Cancel** the following .NET installation: it hangs under
  Wine, and it is not needed.

### 4. Setup

```sh
sh scripts/setup.sh ~/Downloads/ClickShare_Setup.exe
```

The script does three things:

1. It creates a dedicated Wine prefix in `~/.wine-clickshare`. You can change
   the location with `CLICKSHARE_PREFIX=...`. The Mono and Gecko prompts are
   suppressed.
2. It extracts the app into the prefix at
   `C:\users\<you>\AppData\Local\ClickShare\app-<version>\`, using
   [`scripts/extract-clickshare-app.py`](scripts/extract-clickshare-app.py).
3. It builds `build/csdup.dll` and `build/inject.exe` with
   [`scripts/build.sh`](scripts/build.sh). If no MinGW-w64 is installed, it
   downloads a pinned Zig toolchain into `.toolchain/`, which needs no root.

---

## Usage

1. Plug in the ClickShare Button.
2. Start:
   ```sh
   sh clickshare.sh
   ```
   The launcher does three things:
   - It sets the Button as the default audio output and moves running streams
     to it. Set `CLICKSHARE_NO_AUDIO=1` to skip this.
   - It starts the ClickShare app in the Wine prefix.
   - It injects the shim after 3 seconds.
3. The app shows *Share to room*. Press the Button, or click **Desktop 1**, to
   start sharing. Stop and pause with the buttons on the thumbnail.

When you are done, switch the audio output back to your speakers, for example
in *Settings → Sound*. If you don't, sound keeps going to the Button.

**Optional desktop entry** (`~/.local/share/applications/clickshare.desktop`):

```ini
[Desktop Entry]
Name=ClickShare (Wine)
Exec=sh /path/to/this/repo/clickshare.sh
Icon=video-display
Type=Application
Categories=Network;
```

---

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| App says *"connect a ClickShare Button"* | No HID access. Check the udev rule (step 2), re-plug the Button, and restart with `wineserver -k`. |
| Room shows the ClickShare frame but the **picture is black** | The shim is not loaded. Look at `C:\csdup.log` in the prefix (`$WINEPREFIX/drive_c/csdup.log`): it must contain `patched DuplicateOutput` and `duplication created`. Start the app only via `clickshare.sh`. |
| **No sound** in the room | The default output is not the Button. Check with `pactl get-default-sink`, which should name `…Barco_ClickShare…`. The sink name changes after re-plugging. Audio is only forwarded while sharing. |
| App crashes on start (`RoGetActivationFactory`, `c0000005`) | Wine is too old. Install Wine ≥ 9 (step 1). |
| `.NET Framework Initialization Error` dialog | Harmless. Click OK. It appears when a prefix is created without Mono. |
| `.NET` installer hangs at *"netfx_Full_LDR.mzz"* | You ran `ClickShare_Setup.exe` directly. Don't: use `scripts/setup.sh`. Kill it with `wineserver -k`. |
| App does not see the Button although the HID rule works | Make sure the prefix was created with the current Wine (`wineboot -u` is not enough after an aborted .NET install). Re-create the prefix. |

**Logs:**

- App: `$WINEPREFIX/drive_c/users/$USER/AppData/Local/Barco/ClickShare App/clickshare.log`
- Shim: `$WINEPREFIX/drive_c/csdup.log`
- Wine: `WINEDEBUG=err,+hid sh clickshare.sh`

---

## Limitations

- **X11 only (tested).** Under Wayland, Wine's GDI capture only sees X11/XWayland
  windows; you would most likely share a black or partial desktop.
- **Window sharing** (*share a single window*) uses a different capture path
  (`PrintWindow`). The shim does not cover it, and it is untested; its thumbnail
  stays black in the app. Use desktop sharing.
- **~30 fps, CPU-based capture.** Frames are copied via GDI and uploaded to the
  GPU. That is fine for slides and documents, but not ideal for video.
- **Dirty-rect / move-rect optimisation is not implemented.** Every frame is a
  full frame.
- Tested with a single monitor. The shim duplicates whatever output the app
  requests, but multi-monitor setups are untested.
- Features that need .NET (calendar/Outlook integration, `ClickShare.exe`
  wrapper) and auto-update are not available.
- Network-only sharing (without a Button) needs the base unit to be reachable
  on your network. This is untested, because the test network isolated clients.

---

## Uninstall

```sh
rm -rf ~/.wine-clickshare             # Wine prefix incl. ClickShare app
sudo rm /etc/udev/rules.d/70-clickshare.rules && sudo udevadm control --reload
rm -rf build .toolchain               # build artefacts in this repo
```

---

## Repository layout

```
clickshare.sh                     launcher (audio routing + app start + shim injection)
shim/csdup.c                      DXGI Desktop Duplication implementation (GDI → D3D11)
shim/inject.c                     starts a program and loads a DLL into it
scripts/setup.sh                  one-time setup (prefix, app extraction, build)
scripts/build.sh                  builds the shim (MinGW-w64 or auto-downloaded Zig)
scripts/extract-clickshare-app.py unpacks the app from ClickShare_Setup.exe
scripts/install-udev-rule.sh      installs udev/70-clickshare.rules
scripts/install-winehq.sh         installs current Wine from WineHQ (Ubuntu family)
udev/70-clickshare.rules          device permissions for Barco USB devices
docs/TECHNICAL.md                 detailed analysis and design notes
```

---

## Contributing

Reports from other distributions, Button models (`lsusb | grep 0600`), app
versions and Wayland sessions are very welcome. Please include your Wine
version, the app log and `csdup.log`.

## License

The code in this repository is released under the [MIT License](LICENSE).
The ClickShare software is proprietary software by Barco N.V. and is **not**
covered by this license. Using it is subject to Barco's EULA.
