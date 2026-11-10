<p align="center">
  <img src="docs/images/banner.svg" alt="Batman Begins Recomp" width="100%">
</p>

<p align="center">
  <img src="https://img.shields.io/badge/platform-Windows%2010%20%7C%2011%20x64-0078D6" alt="Platform: Windows 10 and 11, x64">
  <img src="https://img.shields.io/badge/renderer-Direct3D%2012-2d6a4f" alt="Renderer: Direct3D 12">
  <img src="https://img.shields.io/badge/campaign-playable%20to%20the%20credits-c8961e" alt="Campaign: playable to the credits">
  <img src="https://img.shields.io/badge/license-MIT-blue" alt="License: MIT">
</p>

An unofficial native PC port of the original Xbox version of **Batman Begins** (2005), made by static recompilation. The game's x86 executable is translated to C++ ahead of time and built into a 64-bit Windows program. The Xbox kernel, Direct3D 8 and DirectSound calls are implemented on Windows, and everything is drawn with Direct3D 12 and played through XAudio2. There is no emulator and no Xbox BIOS involved.

Built with the [Xbox XBE Recomp](https://github.com/TGP482/XboxXBE-Recomp) tool by TGP482.

> [!NOTE]
> This project does not include any game assets or recompiled game code. You need your own legally acquired copy of the US release of the game to build and play it.

## Contents

- [Installation](#installation)
- [Status](#status)
- [Features](#features)
- [Default controls](#default-controls)
- [System requirements](#system-requirements)
- [Building from source](#building-from-source)
- [FAQ](#faq)
- [How it works](#how-it-works)
- [Credits](#credits)
- [License](#license)

## Installation

1. Download `BatmanBegins.zip` from the [latest release](https://github.com/christiansmmc/BatmanBeginsRecomp/releases/latest) and extract it anywhere.
2. Put your ISO of the US release in the `assets` folder. The files extracted from the disc work too.
3. Run `BatmanBegins.exe`.

The first start prepares the game's code for your PC, behind a small progress window: it recompiles the game from your copy. This happens only once: it took about 20 seconds on the test PC, which compiles 16 files at a time, and a PC with fewer cores takes a few minutes. If you close the window, the next start carries on where it stopped. When it says **The game is ready**, press Start. The same window offers to delete the compiler files, which are only needed for this step.

> [!NOTE]
> The download holds no game code or assets: the program builds the game's code from your copy, on your PC.

The download has no shader cache, since it would be made from the game: the first time each effect appears, the game may stall for a moment while it compiles its shaders, and after that they are kept on disk.

Settings, saves and logs are kept next to the program. The `README.txt` in the download explains the controls, the options screen and where everything is.

## Status

The whole campaign has been played from the first level to the credits on this port, at the experimental 60 FPS option.

| | |
|---|---|
| ✅ Works | Logos, menus and all 30 videos on the disc · the full campaign, including the helicopter and the car chase levels · saving and loading · Alt+Tab · controller hot-plug · keyboard |
| ⚠️ 60 FPS | Offered as experimental. Ten per-frame behaviours were corrected for it, but more remain, and some scenes run too fast, such as the helicopter's flight. |
| ⚠️ 30 FPS | The default, the game's own rate. Most of the campaign was played at 60, so at 30 it has only been played up to the second chapter. |
| ℹ️ Accuracy | Speed and picture were compared against [xemu](https://xemu.app/), not real hardware. |
| ℹ️ Widescreen | At 16:9 the menus and HUD are 33% wider, which is also what the original does on a widescreen console. Choose 4:3 to see them in their original shape. |

Details, measurements and known issues are in [docs/TECHNICAL.md](docs/TECHNICAL.md).

## Features

### Display

All of these are on the in-game options screen, which opens with <kbd>Esc</kbd>, or <kbd>Back</kbd> + <kbd>Start</kbd> on a controller. The game freezes while it is open: picture, sound and clocks.

- **Display Mode**: Windowed, Borderless or Fullscreen.
- **Window Size**: 1280x720, 1600x900, 1920x1080 and 2560x1440, up to what the monitor fits.
- **Internal Resolution**: renders at a whole multiple of the original 640x480 (2x, 3x or 4x), or at the window's own height.
- **Aspect Ratio**: 16:9 turns on the game's own widescreen mode and keeps the option saved in your game in step with it. 4:3 shows the picture with black bars at the sides.
- **Texture Filter**: anisotropic filtering from 2x to 16x, or the game's own filter.
- **VSync**: syncs with the monitor to prevent tearing, without changing the game's speed.
- **Frame Rate**: 30 FPS, the game's own rate, or 60 FPS (experimental). A change takes effect the next time the game starts.

### Input

- **Controller**: any controller SDL3 supports, with the Xbox layout and no setup. Controllers can be connected and disconnected while playing.
- **Keyboard**: always works as player 1, with or without a controller. All 24 actions can be rebound from the KEYBOARD tab.

### General

- **Skip videos**: hold <kbd>A</kbd> (or <kbd>Space</kbd>) for one second to skip a pre-rendered video. Scenes played in the engine stay unskippable, on purpose.
- **Shader cache**: compiled shaders are kept on disk, so an effect only stutters the first time it ever appears.
- **Portable**: saves, settings, logs and the shader cache all live next to the executable.
- **Safe saves**: every write to the save drive reaches the disk before the game says "Game saved".
- **Icon**: the program's icon is made at build time from the title image inside your own copy of the game.
- **Diagnostics**: logs of the last five sessions, crash dumps, and a report that names the failing call if the graphics device is ever lost.

## Default controls

| Action | Keyboard | Controller |
|---|---|---|
| Move | <kbd>W</kbd> <kbd>A</kbd> <kbd>S</kbd> <kbd>D</kbd> | Left stick |
| Camera | <kbd>I</kbd> <kbd>J</kbd> <kbd>K</kbd> <kbd>L</kbd> | Right stick |
| Jump | <kbd>Space</kbd> | A |
| Action (doors and objects) | <kbd>C</kbd> | B |
| Punch | <kbd>E</kbd> | X |
| Kick | <kbd>Q</kbd> | Y |
| Left trigger | <kbd>Left Shift</kbd> | LT |
| Right trigger | <kbd>Left Ctrl</kbd> | RT |
| Start | <kbd>Enter</kbd> | Start |
| Back | <kbd>Backspace</kbd> | Back |
| D-pad | Arrow keys | D-pad |
| Options screen | <kbd>Esc</kbd> | Back + Start |

## System requirements

- **OS**: Windows 10 or 11, 64-bit.
- **CPU**: x86-64 with SSE4.2 and POPCNT (x86-64-v2).
- **GPU**: Direct3D 12.
- **Game**: the US release of Batman Begins for the original Xbox, as an ISO image. Other releases are refused at start with a message.
- **Storage**: about 8.5 GB for the release with the ISO in it and the game's code it builds. Building from source takes about 10 GB, plus 7.5 GB more for a portable folder, which carries its own copy of the ISO.

Tested on one PC, with an NVIDIA GeForce RTX 4070 Ti and a 2560x1440 monitor at 164 Hz.

## Building from source

Only needed to change the code: the [release](#installation) needs none of this.

### 1. Clone the repository

```
git clone https://github.com/christiansmmc/BatmanBeginsRecomp.git
cd BatmanBeginsRecomp
```

### 2. Install the tools

- [Visual Studio 2022](https://visualstudio.microsoft.com/downloads/) with:
  - Desktop development with C++
  - C++ Clang tools for Windows
  - C++ CMake tools for Windows
- [Git](https://git-scm.com/download/win), which the build also uses to fetch SDL, Dear ImGui, toml++ and Zydis.
- [Python](https://www.python.org/downloads/) 3.10 or newer, for the helper scripts.

> [!TIP]
> `build.bat` offers to install the missing Clang and CMake components for you.

### 3. Add the game

Put your ISO in the repository's root folder, then extract it into `assets`:

```
python tools/xiso.py extract "Batman Begins (USA).iso" assets
```

The tool only reads the image and never writes to it.

### 4. Build

Run `build.bat`. The first build also fetches and compiles the libraries, then recompiles the whole game; it takes a few minutes, depending on the machine. Later builds only rebuild what changed.

You can play straight from `out\build\release\BatmanBegins.exe`, which finds the `assets` folder in the repository.

### 5. Make a portable folder (optional)

```
python tools/make_dist.py
```

This writes `dist\Batman Begins`: the program, its runtime libraries, licences, a player README and your ISO, copied and checked. It looks for `Batman Begins (USA).iso` in the repository's root; for another name or place, pass `--iso <path>`. The folder runs on its own, anywhere. Running it again over an existing folder replaces the program and keeps the saves, logs, settings and shader cache.

### Make a release zip (optional)

`release.bat` writes `BatmanBegins.zip`, the download described under [Installation](#installation): the program, the compiler that builds the game's code on the first start, and an empty `assets` folder. It never contains game code. The program in it gets the project's own icon (`tools/release_icon.py`), because the build's icon is made from the game's title image. It needs [7-Zip](https://www.7-zip.org/) on the PATH.

### Tests

```
python -m pip install -r requirements-dev.txt
python -m pytest tests -q
tools\vsenv.cmd ctest --test-dir out\build\release
```

The tests do not need the game: the ones that do are skipped when it is missing.

## FAQ

**Does it use an emulator or the Xbox BIOS?**
No. The game's code is compiled into a native Windows program, and the Xbox system functions it calls are reimplemented.

**Which versions of the game work?**
Only the US release. The program checks the game's executable at start, and refuses any other release with a message.

**Where are my saves?**
In `hdd\E\UDATA`, next to the executable. Settings are in `settings.toml`, and logs in `logs`.

**Why 30 FPS by default?**
It is the game's own rate in play (videos run at 60). The engine handles most of its timing by frame length, but some behaviours were tuned per frame for 30. At 60 some of them still run too fast, so 60 stays an experimental option. See [Frame rate](docs/TECHNICAL.md#frame-rate).

**Can I skip the in-engine cutscenes?**
No, only the pre-rendered videos. The skip borrows the video player's own end path, and scenes played in the engine have no such path to borrow.

**Does it run on Linux or Steam Deck?**
It has not been tried.

## How it works

1. **Recompilation**: [Xbox XBE Recomp](https://github.com/TGP482/XboxXBE-Recomp) decodes the x86 code in `default.xbe` and writes it out as C++, which Clang compiles into `BatmanBegins.dll`. Nothing from the game is stored in this repository: the code is generated on your machine, from your copy.
2. **Runtime**: the SDK's runtime, vendored in [`sdk/`](sdk), stands in for the Xbox kernel and the XDK libraries. It implements Direct3D 8 on Direct3D 12, DirectSound on XAudio2, and the input, file and save APIs on Windows.
3. **Game hooks**: [`config/game.toml`](config/game.toml) lists the places where the recompiled code calls back into the port's own code in [`src/`](src). This covers the widescreen option, the frame rate and its per-frame steps, video skipping and the options screen.

The fixes made along the way, such as reading depth buffers through colour formats, the vertex shader program memory, free disk space and the graphics crash from an out-of-range mip bias, are described in [docs/TECHNICAL.md](docs/TECHNICAL.md).

## Credits

- [Xbox XBE Recomp](https://github.com/TGP482/XboxXBE-Recomp) by TGP482: the recompiler and runtime this port is built on.
- [SDL 3](https://github.com/libsdl-org/SDL), [Dear ImGui](https://github.com/ocornut/imgui), [toml++](https://github.com/marzer/tomlplusplus) and [Zydis](https://github.com/zyantific/zydis), used by the SDK. See [`sdk/THIRD_PARTY.txt`](sdk/THIRD_PARTY.txt).
- The [Michroma](https://github.com/googlefonts/Michroma-font) and [Barlow](https://github.com/jpt/barlow) fonts, under the SIL Open Font License, in the options screen.
- [xemu](https://xemu.app/), used as the reference to compare speed and picture against.

Batman Begins was developed by Eurocom. Batman and all related characters and elements are trademarks of and © DC Comics. This project is not affiliated with or endorsed by Warner Bros., DC, Electronic Arts or Eurocom.

## License

The code in this repository is under the [MIT License](LICENSE). The SDK in [`sdk/`](sdk) is under its own MIT license, and the fonts under the SIL Open Font License 1.1. The game itself is not included and remains the property of its owners.
