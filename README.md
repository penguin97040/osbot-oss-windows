# osbot-oss-windows

> [!IMPORTANT]
> **Unofficial software.** This is an independent community project. It is not made by,
> affiliated with, endorsed by or supported by OBSBOT. Please don't contact OBSBOT support
> about this app; report problems here on GitHub instead. For official software, use
> OBSBOT Center.

A small, unofficial, free and open-source Windows app for controlling an **OBSBOT Tiny 2 Lite**
webcam (and probably the Tiny 2), without the official OBSBOT Center software.

- **One file.** A single `.exe` of about 1–2 MB. No installer, no runtimes, no admin rights.
- **Private.** Works entirely offline: no account, no telemetry, no internet access, no update checks.
- **Dark mode**, written in New Zealand English.
- **No proprietary code.** It talks to the camera with standard USB video (UVC)
  controls plus the camera's vendor commands, as documented by other open-source projects.

Inspired by [obsbot4linux](https://github.com/vampyren/obsbot4linux), which does the same on Linux.

## Features

| Area | Controls |
|---|---|
| AI tracking | Off, Normal, Upper body, Close-up, Headless, Lower body, Group, Hand, Whiteboard, Desk; Standard/Sport speed |
| Gimbal | Hold-to-move arrows (smooth or stepped), Centre, pan/tilt sliders, 3 presets |
| Zoom | Zoom slider |
| Picture | Brightness, contrast, saturation, sharpness, hue, gamma, white balance, exposure, gain, backlight, focus (with Auto where supported) |
| Camera | HDR, field of view (wide/medium/narrow), anti-flicker (50 Hz default for NZ), sleep/wake |
| Other | Live preview (can be turned off), activity log, *Copy diagnostics*, optional Developer tab for protocol testing |

## Download and run

1. Go to [Releases](../../releases) and download `osbot-oss-windows.exe`.
2. Run it. Windows SmartScreen may warn because the exe isn't code-signed; choose
   *More info → Run anyway*.
3. Plug in the camera. It connects automatically.

Requirements: Windows 10 or 11, 64-bit. Windows "N" editions need the free
*Media Feature Pack* installed.

Tips:
- Only one app can use the camera's **video** at a time. To use Teams, Zoom or OBS
  while this app is open, untick *Camera → Show live preview*. The controls keep working.
- Settings and the log live in `%APPDATA%\osbot-oss-windows\`. Delete that folder to reset.

## Status

This is early software. Tested on a real Tiny 2 Lite: AI tracking, gimbal, HDR, field of view and sleep
all work in v0.1.0. Hardware verification of the v0.1.2 safety fixes is pending.
The Tiny 2 should work too but hasn't been tested. If something doesn't
work on your camera, see [docs/TESTING.md](docs/TESTING.md) and
[docs/PROTOCOL.md](docs/PROTOCOL.md).

## Building from source

On Windows (Visual Studio 2022 or later, with the "Desktop development with C++" workload):

```bat
cmake -S . -B build -A x64
cmake --build build --config Release
build\Release\osbot-oss-windows.exe
```

Cross-compiling from Linux (Debian/Ubuntu):

```sh
sudo apt-get install mingw-w64 cmake ninja-build
cmake -S . -B build-win -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake
cmake --build build-win
```

Protocol and safety regression tests run anywhere: `cmake -S . -B build && cmake --build build && ctest --test-dir build`.

The official release uses MSVC with restricted system DLL loading. MinGW development builds
still resolve static DLL imports before startup, so they retain the risk of a malicious DLL
placed beside the executable.

The OBSBOT SDK is **not** needed and is **not** part of this repository.

## Disclaimer

- "OBSBOT" and "Tiny" are trademarks of their respective owner. They're used here only to
  say which camera this app works with.
- This app is provided as is, with no warranty (see the MIT licence). Use it at your own risk.
- It only changes camera settings. It never updates or modifies the camera's firmware.
  If the camera ends up in an odd state, unplug it and plug it back in, or reset it in
  OBSBOT Center.

## Licence

[MIT](LICENSE). Third-party components and credits: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
