# Technical notes

How the port works and what was found while making it: addresses, mechanisms, measurements and the decisions they led to.

- Addresses are virtual addresses in the US release's `default.xbe`.
- Engine names such as `EXBaseApp::m_MaxFrameRate` or `XMoviePlayer::StopMovie` come from the symbols of the game's GameCube release and are labelled as GameCube names. They were matched to the Xbox code by shared constants and the same control flow, and the Xbox code may differ in details.
- The reference for speed and picture is xemu 0.8.139, not a real console.
- Everything was measured on one PC, with an NVIDIA GeForce RTX 4070 Ti and a 2560x1440 monitor at 164 Hz.
- **Measured** means a run of the game with the number recorded. **Read in code** means the claim comes from the disassembly or the source and was not run. **Not measured** and **hypothesis** mean exactly that.

## Overview

The game's x86 code is statically recompiled to C++ by the XboxXBERecomp SDK in `sdk/`, and built locally into `BatmanBegins.dll` from a copy of the game; the repository holds no game code. The SDK's runtime implements the Xbox kernel and the XDK libraries on Windows and draws with Direct3D 12. The game-specific parts are:

| Where | What |
|---|---|
| `config/game.toml` | XDK functions and data the runtime replaces, functions identified by hand, and `[[hook]]` entries: instructions in the game's code where the port's code is called |
| `src/game/hooks.cpp` | The widescreen option, the frame rate in play and the per-frame step corrections |
| `src/game/movie_probe.cpp` | Logging of each video, hold to skip, and `BB_DEV_MOVIE` for testing videos |
| `src/common/` | The options screen, keyboard input, the frame rate policy (`fps_policy.h`, `frame_step.h`) and the settings |

Recompilation of `default.xbe`, built against XDK 5849:

- Code sections: `.text`, `D3D`, `D3DX`, `XGRPH`, `DSOUND`, `XMV`, `XPP`. `DOLBY` and `.rdata` stay data.
- 12,344 functions, 742,294 instructions, 2,721,500 of 2,960,924 code bytes covered, 469 jump tables and 11 callback entries. Functions reached only through pointers in the data sections are listed in `[functions]` (`tools/find_code_pointers.py` finds them).
- The XDK signatures find 125 of 243 library functions and 12 of 12 data items. Of the misses, Bink and networking are unused (the videos are XMV, and there is no XNET library in the XBE), the CRT math functions run recompiled, and the D3D and DirectSound misses are left as stubs, acceptable as long as nothing calls them. `XMV` has no signatures: the game's own video library runs recompiled.
- The generated code has 1,283 trap sites: 975 are the divide-by-zero check of each `div` and `idiv`, 106 are `int3` and `hlt` padding, and about 200 are unimplemented opcodes where gap filling decoded data as code (at `0x2A60A0` the bytes are pointer tables). They only matter if one fires at run time; the fix would then be `data_ranges` or a `[[function]]` entry in the config.

## Fixes in the SDK for this game

### Functions identified by hand

The SDK implements these, but the 5849 signatures do not find them, so they are declared in `config/game.toml`:

| Address | Name | Evidence |
|---|---|---|
| `0x27AB60` | `D3DDevice_SetDepthClipPlanes` | A jump table on the third argument stores near and far at `CDevice+0x1C08` or `+0x1C10` and pushes NV097 `0x394` and `0x398` |
| `0x27C3B0` | `D3DDevice_Reset` | Called by the game at `0x1FBDBF`: waits for the GPU, recreates buffers, sets the targets and clears. Run recompiled, it would free buffers the host created with the game's own `free` |
| `0x27CB50` | `D3DDevice_SetPalette` | Stores its argument in `CDevice_Palettes[stage]`, two arguments |
| `0x27B800` | `D3DDevice_SetVertexShaderInputDirect` | A table of 16 attributes of 16 bytes and a 12-byte `D3DSTREAM_INPUT`. The SDK did not implement it; it is now in `sdk/runtime/xdk/d3d8.cpp` and `vertex_input.h`. The game's layouts are at `0x3E70AC`. The stream index is not bounded by the count: the game leaves gaps (`0x250340`) and the library reads by index |

**Crash at 0.66 s.** `0x27A4E6`, inside `D3DDevice_SetDepthClipPlanes`, read `[device+0x794]`, the current vertex shader, and faulted at `00000004`. The real D3D initialisation stores the fixed-function shader object there (`0x28023B`); the host's `Direct3D_CreateDevice` only does so when `D3D_FvfShader` is defined. Fix: `D3D_FvfShader = 0x28BE88` in `[data]`.

### Vertex shader program memory

Symptom: the title screen spun with no scenery, and the first level was black with a correct HUD. The camera was never wrong. Measured at `0x28C5DA` (`D3DXMatrixLookAt`) on the title: eye (-0.470, -50.713, -90.454), target (-0.470, -37.857, -75.133), up (0, 0, 1), an orthonormal matrix. The level frame already had 581 world draws.

Cause: the game switches programs with `LoadVertexShader(h, 0)` (`0x27B8D0`, called at `0x21190E` and 35 other places) when `[0x3E8500 + index]` is 1, over the program that `SetVertexShader` left at slot 0. The hardware runs whatever is in that memory; the host kept drawing with the program of the last `SetVertexShader`, the 3-instruction screen-space one (`B0101281`).

Fix: `sdk/runtime/xdk/vertex_program.h`, a program memory with a start slot. A `Load` over the running program rebuilds the bound shader, keeping the inputs in force. `LoadVertexShader` also loads the declared constants, which the library sends in the same commands (`0x27B490`). The spin went away with this fix; its exact mechanism was not traced.

Ruled out with measurements on the way: the clock and vblank, the present interval, `rdtsc` (normalised to 733 MHz), the translation of `fsincos`, `fptan`, `fpatan` and `fprem`, and the register forms of `fxch`, `fld` and `fsubp` in the generated code. `0x28C2B7` is a perspective projection with a constant FOV of 0.3927.

### Depth clip planes

Symptom: no PRESS START, no copyright text and no light strip behind the logo, although their 11 draws existed with the right colour and texture. The title draws into a W-buffer (`ZEnable` 2) at w 17.8, with near 16.8 (or 11.2). The recompiled `SetDepthClipPlanes` (called at `0x200DE5` and `0x1FB34F`) picks the range from the shader at `[device+0x794]`, which on the host is always the fixed-function object, and the renderer read NV097 `0x394` and `0x398` from states nobody wrote. It fell back to a near plane of 256 units, and Direct3D 12 clipped everything in front of it. Fix: an SDK replacement that writes the range into those states.

### Depth buffer read as colour

The pause menu blurs the distant scenery and keeps the floor and Batman sharp. Its final pass (`ps B0103380`) reads the depth buffer's memory as a colour texture (`t1` at `0x80C6C000`, format `0x3F`, `LIN_A8B8G8R8`) and draws the blurred background over the scene with blend `1, 303`. The alpha comes from the high byte of depth: `32 x (byte / 255 - c0.a) + 0.063`, with `c0.a` 0 on both screens that use it. Byte 0 gives alpha 0.06, byte 1 gives 0.19, byte 2 gives 0.31, and from byte 8 up the alpha is 1.

Measured in frame captures of the main menu and the pause menu:

- `D3DRS_ZENABLE` is 2 (`D3DZB_USEW`) in every draw with depth, so the buffer holds w, not z/w.
- Near is 11.1848 in every draw that writes depth (16.7772 only in draws without writes), and far is 16,777,215, the buffer's scale.
- Pause: w from 33,739 to 2,142,087. High byte 0 on 36.8% of the screen, 1 on 49.2%, 2 on 5.7%, and 23 to 32 on 8.4%: the distant windows.
- Main menu: w from 64,372 to 230,560. High byte 1, 2 and 3 on 16.7%, 54.6% and 28.7% of the screen, so alpha 0.19 to 0.44: a light haze, not a full blur.

The host stores reversed depth, proportional to 1/w: `x * (far / w - 1)`, with `x = near / (far - near)`. A first attempt that bound the host's depth directly gave byte 255 everywhere and alpha 1. xemu agrees with this reading: with `z_perspective` it stores the interpolated w in buffer units (`hw/xbox/nv2a/pgraph/glsl/psh.c`), and `copy_zeta_surface_to_texture` hands those bytes to a colour texture.

Fix: `depthbytes` in `sdk/runtime/gpu/shaders.cpp` undoes the inversion, `w = far / (stored / x + 1)`, with the `x` and `far` of the draws that wrote that buffer (`Target::zread`, `DrawConstants::zread`), and returns the bytes of w. For a z buffer it returns `1 - stored`, with no real case measured in this game. If one buffer is written with different ranges, the last one wins; the screens measured have one. Result: high-frequency detail at the top of the main menu was 1.18 before, 0.93 after and 0.99 in xemu, and the pause screen matches xemu. In play, no captured draw reads this format, and the first level's mean brightness is 47.77 against 47.80 before.

### Free disk space

The save menu said "Your Xbox doesn't have enough free blocks" with every slot empty. `0x20BFF0` calls `GetDiskFreeSpaceExA` (`0x262A60`) at `0x20C064`, and at `0x20C069` the game reads only the low 32 bits of the free bytes. The SDK's kernel answered exactly 8 GiB (`0x200000000`), whose low half is 0: 0 free blocks against the 3 needed. Fix: `sdk/runtime/kernel/volume_size.h` reports the size of the console's data partition (`0x1312D6000`), which leaves about 50,000 blocks in the low half.

### Other runtime fixes

- **Saves committed at once, settings errors, the shader cache and the device-lost check:** see "Saves, settings and logs", "Shader cache" and "Graphics device lost".
- **Non-ASCII folder names:** `settings.toml` was not read in a folder whose name has `ç`. `toml++` treats a plain string as UTF-8, and `path::string()` returns the system code page. Fixed in `sdk/runtime/app/settings.cpp`. A character outside the code page (Japanese, for example) would, by the code, throw at startup in `main.cpp` (`logx::Init`); not measured.
- **Test runs without focus:** with `XBE_NO_ACTIVATE=1` the window is created with `WS_EX_NOACTIVATE`, shown with `SW_SHOWNOACTIVATE` and never brought to the front. Only the test tools set it.

## Frame rate

The game keeps its own rates: 30 frames per second in play, menus included, and 60 while a video plays. That is the validated configuration. 60 in play is offered as an experimental option (see "60 FPS option"). At 60, ten per-frame behaviours were corrected, a reading of the code lists more than 30 others, and measurements after the corrections found two more.

### How the engine keeps time

The engine (Eurocom's EngineX) was built for a variable frame rate:

| Xbox | GameCube name | What it is | Value |
|---|---|---|---|
| `0x1FA550` | | Vblank callback, counts in `[[0x400418]+0x988]` | |
| `0x1A8410` | | Frame update, called through a pointer: waits for the minimum vblanks, reads and clears the counter at `0x1A8512`, derives the rate | |
| `[app+0x8C]` | | Vblanks per second | 60 |
| `0x182970` | `EXBaseApp::UpdateFrameRate` | Takes the whole-number rate and recomputes everything below | |
| `0x30FF58`, `0x30FF5C` | `m_NominalGameFrameRate`, `m_NominalEngineFrameRate` | The rate the engine's constants were written for | 60 |
| `0x30FF64` | `m_MinFrameRate` | Below this the game runs in slow motion | 20 in play |
| `0x30FF68` | `m_MaxFrameRate` | The target. `0xAC706` writes 30 at start, `0xE0A4C` writes 60 while a video plays, `0xE1722` restores it | 30 or 60 |
| `0x30FF74` | `m_FrameLength` | `1 / rate`, 334 references in the code | 1/30 |
| `0x30FF7C` | `m_GameFrameRateMul` | `60 / rate`, 54 references | 2 at 30, 1 at 60 |
| `0x30FF88` | `m_RenderFrameScale` | The game's own slow motion, `0x182A90` | 1 |
| `0x30FF90` | `m_GameRenderFrameRateMul` | The same, with slow motion applied | |
| `0x30B464` | `gBlendFrameSpeed` | Copy of the above for `pow(k, x)` in the camera, written at `0x32C0A` | |

Each frame, rate = vblanks per second / vblanks elapsed, floored at `m_MinFrameRate`. At a target of 30 the game waits for 2 vblanks; at 60, for 1. At `0x1FBD20` the target also picks the present interval (1 at 60, 2 at 30), which the host ignores. The Dolphin wiki's 60 FPS code for the US GameCube release, `043FB5AC 0000003C`, writes 60 into this same variable; the wiki does not say whether the speed stays right.

Asking for another rate is therefore writing `m_MaxFrameRate`, which the hook at `0xAC71C` does. The host's clock is a fixed 60 Hz vblank, unrelated to the monitor, in `VerticalBlank` (`sdk/runtime/xdk/d3d8.cpp`).

Measured: the title and menus run at 30, and only videos at 60. Capping the output at 30 does not put anything in slow motion: with `fps_cap = 30` the logos last 3.26, 7.37 and 5.07 s, against 3.22, 7.30 and 4.97 s uncapped. `kMaxValidatedFps` in `src/common/fps_policy.h` is a ceiling of 60 because that is what the videos ask for.

### How measurements were made

- **Hero position:** `tools/hero_walk.py` reads `[0x3EF3C4]` (`gpPlayerItem` in the GameCube names, identified by its position among the pointers) `+ 0x38C` from outside the process, in the port and in xemu, without stopping either. In xemu the guest memory is at `address + 0xAF000` in its RAM, which the tool finds by the vblank callback's code. Speed then no longer depends on the camera or the route being the same in both runs. `+0x38C` does not rise during a jump: it measures ground position, not height.
- **The corridor:** save BATMAN A in chapter 2, the forward key held. Times from `z = -42.5` to `-35` (start-up) and from `-35` to `-10` (straight run).
- **Camera projection:** `0x400000` holds the projection of the last camera computed by `0x2010F0`. `tools/camera_probe.py xemu` stops the console at the end of that function (`0x201F77`) and reads that call's camera; `tools/camera_probe.py port` reads the running port's memory from outside, about 150,000 times a second, and lists each projection seen. The ratio `xScale / yScale` tells which shape the frame is made for: 0.75 for 4:3, 0.5625 for 16:9.
- **Films:** each run records a video of its output. `tools/frame_timeline.py cuts` finds scene cuts, `align` matches frames. `tools/walk_time.py compare` finds the same moment in two films every 0.1 s and fits a line; the slope is port time over xemu time. The pass mark is 3%.
- **Voice:** `tools/voice_sync.py`. A run's film and sound start at unrelated instants. The opening video, whose picture and sound are locked in the file, serves as a clapperboard; then the same offset is measured in the scene.
- **Monitors:** `fps_cap` holds each frame for the time a slower monitor would.
- **Slow seconds:** `tools/perf_report.py` lists each second below 29 FPS and says whether shader compilation explains it.
- **Frame captures:** F3, with `XBE_CAPTURE_DIR` set, writes every draw of one frame with its states, constants and vertices.

### Speed against xemu at 30

- **Real-time scene after the opening video:** from the first cut to the control camera, 21.80 s in xemu and 21.73 s in the port (-0.3%). Frame by frame with the port's middle 4:3, slope 1.0008, 54 of 54 frames on the line.
- **Corridor walk and run, the same checkpoint in both:** forward from 0.3 to 8.0 s, slopes 1.0014 and 1.0012 (77 of 77 moments on the line); running back from 0.3 to 10.3 s, 0.9883 and 0.9982 (100 of 100); the whole run back, 1.0023 and 1.0041.
- **The spread comes from xemu:** port against port over the run back gives 1.0004; xemu against xemu gives 1.0102, one xemu film falling about 0.1 s behind in the first 10 s. Why xemu varied was not measured. From 10 to 15 s into the run back the two programs' camera sits in different places and only 8 to 10 of 50 moments fit, so that stretch is not timed; the cause was not measured.

### At 60 without corrections

With `m_MaxFrameRate = 60` and nothing else, the engine obeys: `m_FrameRateInt` 60 and `m_FrameLength` 1/60 in 12,894 of 12,894 samples, 60.0 FPS throughout.

| Measure | xemu | Port at 30 | Port at 60, uncorrected |
|---|---:|---:|---:|
| Corridor, start-up | 1.666 s | 1.665 s | 1.481 s (-11%) |
| Corridor, straight run | 4.503 s | 4.498 s | 4.503 s |
| Whole corridor | 6.169 s | 6.163 s | 5.984 s (-3.0%) |
| Thug's EKG in the tutorial | 106 | 106 | 85 |

The steady run was already right, because it comes from the animation, which advances by time; the start-up was not. The real-time scene was already right (slope 0.9982 against the port at 30). The thug's EKG is `fear x 120 / 99 + 60` (`XCtrlTriggerHighLight::UpdateEKG`), and fear drops 1/60 per frame, so at 60 it fell twice as fast.

### Per-frame steps that were corrected

334 places read `m_FrameLength` and 54 read `m_GameFrameRateMul`, and the camera mostly uses `pow(k, m_GameFrameRateMul)`. What remains are steps with constants tuned for the 30 FPS the game shipped at, which run twice as fast at 60. The list comes from a static reading of the GameCube binary, linked to the Xbox by the same constants and flow; about 200 of 278 candidate lines were false positives.

The corrections are in `src/game/hooks.cpp` and `src/common/frame_step.h`, with the hooks in `config/game.toml`. A lerp `x += (target - x) * k` gets the `k` that closes the same share of the gap per second; a fixed step is scaled by the frame's length; a counter counts once per 1/30 s of time. On frames of 1/30 s or longer the hooks change nothing, to the bit, so the game at its own rate is unchanged.

| # | Behaviour | GameCube name | Xbox sites | Form | Measured? |
|---|---|---|---|---|---|
| 1 | Hero speed factor (start-up) | `Hero::MoveNormal` and the other `Move*`, field `0x49C` (`0x470` on Xbox) | `0x6E641`, `0x6E6A5`, `0x6E874`, `0x6E8E6`, `0x6EA56`, `0x72C7F`, `0x72F45`, `0x730C6`, `0x731BF`, `0x93E37`, `0x782E7`, `0x786BD`, `0x732E4` | Lerp, k from 0.05 to 0.5 | Yes, corridor |
| 2 | Hero turn and the second rotation lerp | `Hero::MoveNormal` | `0x7832C`, `0x78388` | Lerp | Partly: film of the run back |
| 3 | Enemy fear drop, 1/60 per frame | `XFearFactor::Update` | `0x67B98` | Fixed step | Yes, EKG 85 back to 106 |
| 4 | Turn recovery and air control | `Hero::CheckTurn` | `0x717A7`, `0x717DB` | Lerp | No |
| 5 | Smoothed danger distance, feeds fight mode | `Player::DoFindHeadLookAt` | `0x827DB`, `0x82809` | Lerp | No |
| 6 | Hero collision capsule | `Hero::SetHeroHeightWidth`, `Hero::DoLedgeJump` | `0x728C7`, `0x728CF`, `0x729A9`, `0x729B1`, `0x8AA50` | Lerp and step | No |
| 7 | Fixed distance per frame: kick lunge, sideways combat jump, climbing across, pipe, ladder and rope slides, pipe drop | `Hero::DoKickAttack1`, `DoCombatJumpX`, `DoClimbAcross`, `DoPipeSlideDown`, `DoLadderSlideDown`, `DoRope*`, `DoPipeDrop` | `0x88BC1`, `0x8C26B`, `0x91D2D`, `0x93CFB`, `0x9BC1B`, `0x93EA7`, `0x93F77`, `0x94187`, `0x942C7`, `0x93E4B` | Fixed step | No |
| 8 | Hero counters: combat button buffer (12 frames), regrab lockout (20 and 16), remembered object (60), fight mode (300), gas pipe turn (50), platform (10) | `Hero::DoCounters`, `UpdateBtnBuffer`, `ProcessFightMode`, `MoveGasPipe`, `UpdateTempPlatform` | `0x74560`, `0x744B5`, `0x744C6`, `0x7DA66`, `0x6E945`, `0x84575` | Counter | No |
| 9 | Enemy alert meter | `XEnemyAI::DetermineStatus` | `0x668B0`, `0x668C3` | `x 0.99 + 0.05` per frame | No |
| 10 | Area fear drop | `XFearFactor::UpdateAreaFear` | `0x66D5B` | Fixed step | No |

### Per-frame steps not corrected

At 60 each runs twice as fast unless noted. The addresses come only from reading the code:

| System | What | Xbox | Effect |
|---|---|---|---|
| Hero | Stealth stance blend | `0x6E272` | Visual |
| Hero | Head turn | `0x82452`, `0x8263B` | Visual |
| Hero | Lean against a wall | `0x9B5EF`, `0x9B72F` | Visual |
| Hero | Damping when hit in the air | `0x8D6F6` | Visual |
| Hero | 2-frame timers | `0x744F0`, `0x744A3` | Negligible |
| Enemy | Thug's stance when close: in the tutorial he is already on guard at 60 and still walking at 30 | Not located | Visible, cause not found |
| Enemy | Physics frame counter, limit 10 | Not located (GameCube `XEnemyAIGeneric::Update` `0x5CC`) | Hits |
| Enemy | Fade out and in | `0x49BC1`, `0x629CE` | Visual |
| World | Flash grenade life, 400 frames | `0xFF880` | May affect play; not read further |
| World | Smoke, ripple, shock wave, footprints | `0xFF5DB`, `0xFF6B0`, `0xFF72A`, `0xFF732`, `0x129AD8` | Visual |
| World | Animator particle life | Not located (`XItemAnimator_Particle` `0x120`) | Visual |
| World | Rope follows the hero; FOV zone; trigger highlight; pushable object | `0x114E75`, `0x50D24`, `0x3DE2F`, `0x107BDE`, `0x107BE6` | Small |
| Camera | FOV, return to rest, distance, floor and ceiling, edge, climbing, stick: 13 sites | `0x25EC2`, `0xADCE7`, `0x2BEE0`, `0x2C36B`, `0x2C38E`, `0x369C0`, `0x370D9`, `0x370E3`, `0x33E93`, `0x34BF6`, `0x34C2D`, `0x28C4D`, `0x271F8` | Camera feel |
| HUD and menus | Fade, flash, banner, wave, script button, menu | `0xC0F4B`, `0xCDB20`, `0xDD194`, `0xDD18F`, `0xDDC2D`, `0xDD3FA`, `0xED23A`, `0xED275`, `0xED327` | Visual |
| Menus | "Saving BATMAN A" stays about half as long (measured) | Not located | A new game reaches the opening video 6.6 s sooner |
| Helicopter | Flight along the path and acceleration | `0x1718D2`, `0x171D8A`, `0x171BED`, `0x171C54`, `0x1720AE`, `0x172115`, `0x171B39`, `0x171FEF` | Flies twice as fast |
| Cars | Slope pull, with no time factor | `0x157B55` | Physics: the slope pulls twice as hard per second |
| Cars | Wheel spin, engine revs, race manager's smoothed values, chasing car's offset | `0x15F0A2`, `0x15FCE6`, `0x15C15A`, `0x15C17D`, `0x16D07A`, `0x16D0D4`, `0x16D225`, `0x14DF82` | Small to medium |
| Cars | Counters: object removal, robot car state, car height, rumble | `0x17020A`, `0x150E29`, `0x1662CC`, `0x16BCD5` | Half the time |

Not reviewed: the engine range `0x1B0000` to `0x25FFFF`, the `EXBaseParticleSys` particles, `GrappleLine::StretchLine`, the Tracking, Path, LookAt and Cutscene camera modes, and the cape beyond what already uses `m_MaxFrameRate`. The search also misses constants held in registers and long x87 sequences. Car physics itself follows the frame time (read in code): `DoAcceleration`, `DoBraking`, `DoSteering`, `OrientateCar`, `MoveCarOnwards` and `ApplyBodyRoll` read `m_GameRenderFrameRateMul`; the exceptions are in the table.

### Speed in the final version

| Measure | xemu | Port at 30 | Port at 60 |
|---|---:|---:|---:|
| Real-time scene, 1st to 5th cut | 9.73 s | 9.73 s (+0.0%) | 9.74 s (+0.1%) |
| Scene, 28 s frame by frame against xemu, middle 4:3 | | Slope 0.9999, 140 of 140 | 0.9998, 139 of 140 |
| Scene, 30 s against the port at 30 | | | 0.9996, 148 of 150 |
| Corridor, start-up | 1.666 s | 1.663 s | 1.655 s |
| Corridor, straight run | 4.503 s, 5.552 u/s | 4.504 s, 5.550 u/s | 4.503 s, 5.552 u/s |
| Whole corridor | 6.169 s | 6.167 s (-0.0%) | 6.157 s (-0.2%) |
| Film of the run, 0.3 to 10.3 s, against xemu | | 1.0007, 100 of 100 | 0.9978, 100 of 100 |
| Run and jump twice, to `z = -8` | 7.463 s | 7.461 s (-0.0%) | 7.315 s (-2.0%) |
| Film of the jumps, 0.3 to 9.5 s, against xemu | | 0.9999, 92 of 92 | 0.9806, 91 of 92 |
| Voice: picture against sound in the scene, against the port at 30 | Not measured | +0.012 to +0.015 s between two runs | +0.014 to +0.098 s |
| EA, WB, DC logos and opening video (last packets 3.16, 7.23, 4.97, 27.20 s) | | 3.22, 7.30, 4.97, 27.28 s | 3.22, 7.30, 4.97, 27.28 s |
| Thug's EKG in the tutorial | 106 | 106 | 106 |

- **Voice:** two runs at 30 already differ by +0.012 to +0.030 s on this measure. At 60, sounds from 15 s into the scene start about 0.09 s earlier relative to the picture, and the gap does not grow. The probable cause is the sound latency counted in frames, shorter at 60; not measured. xemu's sound could not be recorded (a WASAPI loopback capture of the process was nearly silent), so the picture is checked against xemu and the sound against the port at 30.
- **Jump:** within 3% (-2.0%), but a new problem the static list did not have: each jump takes about 0.1 s less at 60, so the hero reaches `z = -17` after two jumps 0.2 s early. More jumps in a row would pass 3%. The cause was not looked for.
- **Tutorial thug:** with fear and the capsule corrected, the frame at the "To punch press X" stop is the same at 60 and 30, with the same EKG and camera, except the thug: at 30 and in xemu he is still walking, at 60 already on guard. Cause not found.
- **Fight:** with the same button pattern, the same prompts appear and the hit and the second thug come at the same time at 30 and 60. Not a speed measurement.

### Averaging, slow frames and monitors

At `0x1A8655` the engine averages the rate with the previous frame's and rounds it. At a steady rate this changes nothing. With a mix of frame lengths, game time runs slower than real time: with a target of 60 and 20 ms frames, the run is 7.9% slow (5.117 u/s), against -0.3% (5.534 u/s) with each frame's own rate. So when a rate above 30 is asked for, the hook at `0x1A869F` uses the rate of that frame, without the average. At 30 the average stays, because the game was tuned with it, and because without it a machine that cannot hold 30 runs even slower:

| 40 ms frames, target 30 | Rate | Frame lengths | Straight run |
|---|---|---|---:|
| The engine's average, as the game does | 24 to 29 | 1/29 to 1/24 | 5.205 u/s, -6.2% |
| Each frame's rate from the vblanks (attempt, reverted) | 30 or 20 | 1/30 and 1/20 | 4.963 u/s, -10.7% |

Below 30 the run is slower than real time, and that is the game's own behaviour, left as is. In the reverted attempt game time matched real time (0.0400 s per frame) and the run was still slower, so long frames do not move in proportion to time; the cause was not looked for.

The game's clock is the host's fixed vblank, so the monitor only changes how many frames are shown and when. Only the 164 Hz monitor was available, and its mode was not changed; slower monitors were simulated with `fps_cap`. Whole corridor, 6.169 s in xemu:

| Case | Target | Frames the engine used | Corridor | Straight run |
|---|---:|---|---:|---:|
| 164 Hz, vsync on | 30 | All 1/30 | 6.167 s | 5.550 u/s |
| Vsync off | 30 | All 1/30 | 6.164 s | 5.552 u/s |
| `fps_cap = 50` | 30 | All 1/30 | 6.165 s | 5.557 u/s |
| 164 Hz, vsync on | 60 | All 1/60 | 6.157 s | 5.552 u/s |
| Vsync off | 60 | All 1/60 | 6.157 s | 5.550 u/s |
| `fps_cap = 50` | 60 | 80% 1/60, 20% 1/30 | 6.176 s | 5.534 u/s |

A real 50 Hz monitor at a target of 30 should behave like the `fps_cap = 50` row: the frame waits 2 vblanks anyway, and `Present` never takes longer than those 33 ms. Not tested: any real monitor other than this one, in particular 60 Hz, where `Present` with vsync blocks a whole period, and 50, 75, 120 and 144 Hz. With the host clock at exactly 60.0 Hz and a 59.94 Hz monitor there should be one 2-vblank frame about every 16 s; that is arithmetic, not a measurement.

120 and 144 were not tried. They would need a faster host vblank in `VerticalBlank` and `[app+0x8C]` equal to it; the per-frame step arithmetic would still hold.

### Why 30 stays the validated rate

The engine accepts 60, and everything measurable in chapters 1 and 2 is within 3% after the corrections. Still:

- Ten behaviours were corrected, and the code reading lists more than 30 others. Measurements after the corrections turned up two new ones, the jump and the thug's guard, so the list is not complete.
- Most of the rest cannot be measured with the available saves and test scripts: helicopter, cars, ropes, pipes and ladders.
- Car physics follows the frame time, but the slope and the helicopter do not. Fixing them takes more hooks of the same kind, none of them written or tested.

So `kMaxValidatedPlayFps` stays 30, and at 30 none of the frame rate hooks changes any value. `BB_DEV_PLAY_FPS=<n>` forces any rate in play, for measuring; it is off by default.

## 60 FPS option

The campaign was played to the end at 60. Every session with a log kept after the mip bias fix (see "Graphics device lost") says `play frame rate 60` and ends in `Exit (0)`, with no crash. In that playthrough the helicopter seemed to fly fast without affecting play. That matches the code reading (`0x1718D2` and the others in the table above) but was not measured.

The options screen offers FRAME RATE: 30, the default, or `60 · EXPERIMENTAL`, with the line "Experimental and may be unstable: some scenes, like the helicopter, run too fast." 60 is offered on the strength of that playthrough, not of new measurements; the speed measurements did not change, so `kMaxValidatedPlayFps` is still 30. The choice is stored as `experimental_play_fps` in `settings.toml`, written only when it is 60.

**The choice applies at the next start, not live.** The game itself changes rate around every video: the video player's constructor (`0xE09D0`) saves `m_MaxFrameRate` at `[player+0x2F0]` and writes 60 (`0xE0A4C`), and its destructor (`0xE16A0`) restores the saved value (`0xE1722`). A first version used this as a precedent and switched live, with three hooks: one per frame at `0x1A8410` and two in the video player, so that videos stayed at 60 and their end restored the current choice. It worked as far as the logs show: the title switched between 30 and 60 on closing the screen, and a change during a video took effect when the video closed.

A code review found the flaw, confirmed in the generated code: the game derives values from the rate when it creates objects and keeps the result. At `0x1E1B0` it divides per-second values by the rate and stores per-frame steps at `[obj+0x3C4]` and `[obj+0x3CC]`; `0x8234A` and `0x84C16` also read the rate to set up counters. A switch in the middle of a level leaves those objects at the old rate, in a state nobody played. From 60 to 30 that breaks the rule that at 30 the game stays as shipped. The game's own switch around videos proves nothing, because it always returns to the same rate.

So the live switch was removed, with its three hooks. `PlayRateSetting` (`src/common/fps_policy.h`) keeps the rate in force, read at start, and the chosen rate, which goes to the file. While they differ, the line says "Takes effect the next time the game starts." `CounterClock` stays: without a mid-session switch it changes nothing. Measured in the final version: starting at 30 and choosing 60, the title stays at 30 and the file gains `experimental_play_fps = 60`; the next start logs `play frame rate 60` and the title runs at 60; choosing 30 then keeps 60 until exit, and the file loses the key. Tests: `tests/native/fps_policy_test.cpp`.

## Videos

The recompiled XMV library plays all 30 videos with no correction, at the right speed, picture and sound. No native decoder was needed.

| Address | What it is |
|---|---|
| `0x30EA70` | Movie table (`XMovieList` in the GameCube names): 32 entries of 44 bytes. Byte `+4` says the video is widescreen (`IsMovieWidescreen`), byte `+5` that a press skips it, and from `+12` a name per language (`PSS_US\EUROCOM`, `PSS_US\01_001US`, ...) |
| `0x1FD870` | `EXWnd::MovieOpen` (GameCube name): builds `D:\...\<name>.xmv` in a `0x200`-byte stack buffer and calls `0x23E380` |
| `0x23E380` | Opens the video. `[0x427AD0]` is the state: 0 stopped, 1 closing, 2 opening, 3 playing |
| `0x23DE60` | Advances the state machine each frame; `0x2B5247` asks the library for the next frame |
| `0x23E1E3` | The `rep stosd` that clears the state on close |

`src/game/movie_probe.cpp` hooks `0x23E398` and `0x23E1E5` to log `movie open <name> <t>` and `movie close <name> <t>`. With `BB_DEV_MOVIE=<file.xmv>`, off by default, that file plays in place of the first video and the program exits when it ends; `tools/movie_sweep.py` uses it to play each video in turn.

### Hold to skip

Holding A on a controller, or the jump key on the keyboard (Space unless rebound), for 1 s ends any of the 30 videos. The notice "HOLD A TO SKIP" (or the key's name) and a filling bar appear while it is held. The game's own skip path, read in the Xbox code with the GameCube names:

| Xbox | GameCube name | What it does |
|---|---|---|
| `0xE1910` | `XMoviePlayer::Update` | Acts only with `[+0x2A4]` (playing) set and `[+0x2A5]` clear |
| `0xE1962` to `0xE198A` | `UpdateMovie`, `IsMovieFinished` (`0xE0C40`) | With `[+0x8E]` set the video has ended, and it calls `0xE1480`; otherwise it advances the video |
| `0xE1480` | `StopMovie` | Closes the video through virtual function `+0xC0` (`0x1FD9A0`, which calls `0x23E350`) and clears the state |
| `0xE1996` to `0xE19B0` | `UpdateSkipMovie` | With `[+0x2B8]` set and `0xDF600(1)` true, calls `0xE1480` |
| `0xE129F` | `EnableMovieSkip` | `[+0x2B8]` comes from byte `+5` of the video's table entry |
| `0xDF600` | `XLenny::StartButtonPressed` | Tests bits 18 and 13 of each controller's state; which buttons those are was not checked |

So the original game already skips on a press, but only for the videos of the extras menu (`TT`, `TR1`, `TR2`, `DLOOP`, `INT1` to `INT5`); the logos and the 18 story videos have 0. The port's hook at `0xE1972` (`MovieSkipCheck`) sets `[+0x8E]`, the flag a video's natural end sets, once the button has been held for 1 s. From there the game takes its normal end path: it reads the same flag and calls the same `StopMovie` its own skip uses. A tap still does what the game does: nothing where the table has 0, a skip where it has 1 (read in code; the videos with 1 are reachable only from the extras menu and were not tested there).

The button is counted after the input gate of the options screen, so a button already down when the video starts counts only after it is released. When a video is skipped, whatever is still held is withheld from the game until released: with the key held, A disappeared for the game at the skip and returned only on a new press.

**Real-time scenes are not skippable.** The skip borrows the movie player's own end flag, so it exists only inside `XMoviePlayer::Update`. Scenes played in the engine never pass through that code, and there is no end path to borrow (read in code). Measured on the scene after the opening video: Space held for 8 s in the middle of it logged no `movie hold`, and the cuts came at 1.90, 5.07, 7.84, 9.74 and 13.10 s after the first, the same as with nothing held.

### The 30 videos

The expected length is the last packet's time, not the XMV header's: they differ in almost every file, by up to 3.25 s in `ea_US.xmv`, whose header says 6.408 s while its packets end at 3.16 s, where both FFmpeg and the game stop. Each video was played in the first logo's slot and checked against the file decoded by FFmpeg:

- **Speed:** from `movie open` to `movie close`, at most 0.09 s past the last packet (tolerance 2% or 0.5 s), 1,751 s played in all.
- **Sound:** correlation of the mixer's recorded waveform with the file's, at 8 kHz. The lowest is 0.963. Warping the recording of `ea_US.xmv` by 3% drops it from 0.99 to 0.08, so this also checks pitch and speed. The level is always 1.3 to 2.1 dB below the file's. `DC_US.xmv` has no audio track, and the game is silent in it.
- **Picture:** three captures per video, at 25%, 50% and 75%, against every frame of the file within 3 s. Every capture with a picture matches with a correlation of 0.986 or more, at most 0.42 s late, which is the capture's own delay. The 11 black captures fall on black stretches of the file, each checked by its instant.
- **Hold:** Space pressed 2 s after opening and held 1.5 s. Each video closed 0.98 to 1.00 s after the key started to count, with the sound after the close at -41.9 dBFS or below (the game itself plays something at about -47 dBFS there, also at a normal end; the limit was -40 dBFS).

| Video | 16:9 | Press skips | Header (s) | Last packet (s) | Played (s) | Waveform | Held: closed after (s) |
|---|---|---|---:|---:|---:|---:|---:|
| `01_001us` | Yes | No | 27.48 | 27.20 | 27.27 | 0.994 | 0.98 |
| `01_095us` | Yes | No | 10.40 | 7.77 | 7.83 | 0.995 | 1.00 |
| `02_005us` | Yes | No | 30.96 | 29.40 | 29.47 | 0.996 | 1.00 |
| `02_190us` | Yes | No | 17.12 | 15.37 | 15.42 | 0.963 | 1.00 |
| `03_001us` | Yes | No | 20.84 | 19.80 | 19.87 | 0.998 | 1.00 |
| `03_230us` | Yes | No | 31.92 | 29.97 | 30.03 | 0.995 | 1.00 |
| `03b001us` | Yes | No | 21.72 | 20.80 | 20.85 | 0.998 | 0.98 |
| `03b200us` | Yes | No | 22.44 | 20.27 | 20.32 | 0.995 | 0.98 |
| `04_005us` | Yes | No | 12.84 | 11.17 | 11.23 | 0.997 | 0.98 |
| `04_165us` | Yes | No | 15.08 | 13.97 | 14.03 | 0.993 | 0.98 |
| `05_005us` | Yes | No | 30.44 | 26.67 | 26.73 | 0.997 | 0.98 |
| `05_200us` | Yes | No | 11.36 | 9.73 | 9.78 | 0.992 | 1.00 |
| `05b001us` | Yes | No | 15.44 | 13.10 | 13.15 | 0.996 | 1.00 |
| `05b100us` | Yes | No | 21.56 | 19.83 | 19.90 | 0.990 | 0.98 |
| `06_005us` | Yes | No | 25.92 | 24.70 | 24.77 | 0.995 | 0.99 |
| `06_060us` | Yes | No | 23.22 | 21.27 | 21.32 | 0.994 | 0.98 |
| `07_005us` | Yes | No | 17.96 | 16.83 | 16.90 | 0.992 | 1.00 |
| `07_080us` | Yes | No | 31.32 | 30.63 | 30.70 | 0.997 | 0.98 |
| `tt_us` | Yes | Yes | 69.76 | 68.83 | 68.88 | 0.987 | 0.98 |
| `tr1_us` | Yes | Yes | 74.28 | 72.47 | 72.53 | 1.000 | 1.00 |
| `tr2_us` | Yes | Yes | 30.04 | 28.83 | 28.90 | 0.987 | 0.98 |
| `ea_US` | No | No | 6.41 | 3.16 | 3.22 | 0.992 | 1.00 (own slot) |
| `wb_US` | No | No | 7.28 | 7.23 | 7.28 | 0.969 | 0.98 |
| `DC_US` | No | No | 4.97 | 4.97 | 4.97 | No audio | 1.00 |
| `dloop_us` | No | Yes | 78.99 | 78.14 | 78.20 | 0.986 | 0.98 |
| `int1_us` | No | Yes | 228.93 | 227.52 | 227.61 | 0.998 | 0.98 |
| `int2_us` | No | Yes | 207.04 | 206.56 | 206.62 | 0.997 | 1.00 |
| `int3_us` | No | Yes | 219.88 | 219.48 | 219.54 | 0.997 | 1.00 |
| `int4_us` | No | Yes | 218.36 | 217.76 | 217.82 | 0.998 | 0.98 |
| `int5_us` | No | Yes | 257.20 | 255.88 | 255.94 | 0.998 | 1.00 |

In their own slots, the EA, WB and DC logos and the opening video `01_001US` were skipped 0.98 to 1.00 s after the key counted. The next logo opened 0.38 and 0.35 s after the skip, against 0.40 and 0.37 s after a normal end; the sound after the close was the normal end's, at the same instant; and after `01_001US` the real-time scene ran with the same cuts as without a skip. A 0.15 s tap on the EA and WB logos let them play their usual 3.22 and 7.30 s.

Limits: the other 26 videos were measured in the first logo's slot, not their own, so their framing and subtitles in the real slot were not checked this way. The videos were not compared with xemu.

## Options screen

Esc on the keyboard, or Back + Start on a controller, opens a screen drawn with Dear ImGui over the game at the window's real resolution (`src/common/options_screen.cpp`; the rules without ImGui are in `options_model.cpp`, tested in `tests/native/options_model_test.cpp`). It has two tabs:

- **Video:** DISPLAY MODE (windowed, borderless, fullscreen); WINDOW SIZE (1280x720, 1600x900, 1920x1080, 2560x1440, up to what fits the monitor; a larger size set by hand in `settings.toml` still works and is listed; 3840x2160 was left out because no available monitor could test it); INTERNAL RESOLUTION; ASPECT RATIO (16:9 or 4:3, see "Aspect ratio"); TEXTURE FILTER (OFF, which is the game's own filter, or 2x, 4x, 8x or 16x anisotropic); VSYNC; FRAME RATE (see "60 FPS option").
- **Keyboard:** every action can be rebound. The map is stored in `settings.toml` as `key_*` virtual-key codes. A map with a repeated or reserved key falls back to the default, with a warning in the log. Esc, F3 and the Windows keys are reserved.

### Freezing the game

With the screen open, the game's thread is held inside `D3DDevice_Swap`, presenting the last frame again with the screen on top. Held with it:

| What | Where | Why |
|---|---|---|
| Vblank | `VerticalBlank` in `sdk/runtime/xdk/d3d8.cpp` | It is the game's clock. Otherwise the next frame would see every vblank that passed |
| Sound | `Render` in `sdk/runtime/xdk/dsound.cpp` | Voices stop, and the game's streams wait with them |
| Clocks the game reads | `sdk/runtime/core/guest_clock.h`: `rdtsc`, `KeTickCount`, `KeQueryInterruptTime`, `KeQueryPerformanceCounter` | The pause menu's timer uses `KeTickCount` (read in code: `0xEDC19` calls `0x20BA10`, which subtracts from `0x262A20`) |

`KeQuerySystemTime` stays real: it is the PC's date and time. The freeze starts at the next `Swap`, so the game never waits for a vblank that would not come; a long load with no frames shows the screen only at its next `Swap` (read in code). Repeated frames do not count towards ageing render targets and textures (`held_frames_` in `sdk/runtime/gpu/d3d12.cpp`). A scale change made while frozen takes effect on the first frame after closing, because it discards the targets, the one on screen included. The game's other threads keep running with nothing to do: its streams wait for the sound, and its D3D calls wait for the lock the main thread holds.

Measured with the screen open for 30 s in the middle of the real-time scene (the freeze logged as 30.001 s): with the freeze taken out, the times from the first scene cut to the 2nd through the 6th were 1.90, 5.10, 7.83, 9.73 and 13.13 s, against 1.90, 5.07, 7.83, 9.77 and 13.10 s in a run without the screen, within -0.35% to +0.66%. Each cut comes from a 30 FPS film, so each end has up to 1/30 s of error. The sound is silent (-inf dBFS) during the freeze, against -20.6 dBFS before it, and the same 30 s of sound after it arrives 29.98 s later than without the screen. The pause timer does not jump: paused at `00:00:50`, it still read `00:00:50` after 27 s with the screen open over the pause. Videos stop and resume: `WB_US` with the screen open for 5.00 s took 12.30 s, that is 7.30 s of video, as always.

Input while the screen is open: an input gate at the end of the pad path, after the keyboard, gives the game a neutral but connected pad. Anything held when the screen closes reaches the game only after release (a stick counts as released below 8000 of 32767), and with Back held, Start never reaches the game.

### Internal resolution

INTERNAL RESOLUTION offers the whole scales 2x, 3x and 4x of the game's 640x480 (1280x960, 1920x1440, 2560x1920), plus AUTO, the window's own height, when that is not a whole scale. A value set by hand (`render_percent`, a percentage of the output's height) still works and is listed.

2x, 3x and 4x were each run at 1280x720 windowed and at 2560x1440 borderless, alongside percentage settings of 100, 200 and 300:

- **Frame rate:** a minimum of 29.5 FPS or more and an average of 30.0 in every run, on the RTX 4070 Ti, in the first room and the corridor only. Explosions, smoke and later levels were not measured at 4x, and 4x may be heavy on a slower GPU.
- **Picture:** the main menu, the HUD in the tutorial and the pause menu were compared with the 720p output at 100%, already checked against xemu. At 3x and 4x every correlation is 0.985 or more. Two runs (2x at 720p, 200% at 1440p) scored lower on the HUD, 0.543 and 0.565, because the camera in the first room stopped at another framing; the HUD sits in the same pixels. Sharpness rises with the scale: 2.16 at 100%, 2.28 at 2x, 2.43 at 3x, 2.64 at 4x in a 720p window. The shadows on the floor get more defined, with no new edges or bands, and the pause blur and the gold strip's blend look the same.
- **Speed:** the whole corridor took 6.165 to 6.169 s at 100%, 3x and 4x in the 720p window and at 3x, 4x and 300% at 1440p, against 6.169 s in xemu.
- **Switching:** changing the scale 10 times while a level runs hung nothing (minimum 29.9 and 29.5 FPS), and each change showed in the sharpness.

Whole scales are what the row offers because a whole scale is the same internal picture in any window (arithmetic, not a run), so what was checked in one window holds in another; a percentage changes with the window's height. The texture filter at 2x, 4x, 8x and 16x held 30.0 FPS with correlations of 0.984 or more. Not run in these tests: FULLSCREEN, any other monitor, and 1600x900 and 1920x1080 beyond windowed at the default scale. In borderless test runs the window stays behind others (`XBE_NO_ACTIVATE`); capture and 30 FPS were normal, but borderless in front was not tested.

## Aspect ratio

The game has its own widescreen mode. With its option on, it builds the picture for 16:9 and `xScale / yScale` is 0.5625; with it off, for 4:3, 0.75. What decides is the option stored in the save, not what the console reports. The 3D is right for the shape chosen and, in 16:9, shows more at the sides.

### Where the game reads the option

`0x4006A4` is the byte the GameCube names call `EXBaseRenderEnv::ms_WideScreen`, and the option is code `0x3E` (`0x4200003E`) in the option list at `0x3F4CFC`.

| Address | What it does |
|---|---|
| `0x2626FE` | `XGetVideoFlags`, reads `ExQueryNonVolatileSetting 8`. The SDK answers widescreen when the window is 3:2 or wider |
| `0x1FABFD` | If the console says widescreen: `D3DPRESENTFLAG_WIDESCREEN`, `[[0x3F56F0]+0x4C] = 1` (that is `0x3EE4CC`, measured) and the byte set to 1 |
| `0xF8FF0` | Returns `0x3EE4CC`; matches `Config_GetDefault_WidescreenSetting`, which on GameCube always returns 0 |
| `0x13B860`, `0xADE70` | New game: the option takes that default, and the byte takes the option (measured) |
| `0xF73D0` | Builds the (code, value) pairs saved in the save file, the option as code `0x3E` |
| `0xF7160` | On load, the byte takes the option; what the console said is only a default if the option is missing (measured) |
| `0xF1915`, `0xEFCBF` | Looks like a menu item that toggles the option and publishes `PauseOptions-WideScreen` (text at `0x2EB8D0`, referenced at `0xEFD13` and `0xF197D`). It does not appear in the game's SCREEN SETTINGS and was not triggered |

What uses it, read in code:

| Address | Effect with the option on |
|---|---|
| `0x2010F0` | Camera: `xs = (640/480) x k x cot(fov/2)`, with `k` 0.75 when `[camera+0x6D]` is non-zero and 1 when it is 0. The option multiplies `k` by 0.75. `[camera+0x6D]` was 1 in every reading of the main camera |
| `0xC9550` | Another camera, at `+0x1E0` in its object, computed with the byte cleared and then restored, so it is the same either way. Not identified |
| `0xEE0F0`, `0xF1DE5`, `0xF250C` (`0xE96E0`) | Objects with bit `0x40` in `[+0x9168]`: x scaled by 1.33. Not identified |
| `0xB9630` | A smaller centred window: width 0.375 instead of 1/2, x0.75. Not seen on screen |
| `0xC6470` | On some screens, width x0.7615. Not seen |
| `0xE1320` | Videos, below |

The other uses of 4/3, at `0x394B5`, `0x3A303` and `0x3AF60`, are an inverted 0.75 s time, not a shape.

### Measured, before the port kept the option in step

| Program, window | Console says | Game | Byte | Place | xScale | yScale | xScale / yScale |
|---|---:|---|---:|---|---:|---:|---:|
| xemu 4:3 | 0 | New game | 0 | First room | 1.8296 | 2.4395 | 0.7500 |
| xemu 4:3 | 0 | Save with the option at 1 | 1 | First room | 1.3722 | 2.4395 | 0.5625 |
| xemu 4:3 | 0 | The same save, option at 0 | 0 | First room | 1.8296 | 2.4395 | 0.7500 |
| xemu 4:3 | 0 | Corridor save, option at 1 | 1 | Corridor | 0.9002 | 1.6003 | 0.5625 |
| xemu 4:3 | 0 | The same save, option at 0 | 0 | Corridor | 1.2003 | 1.6003 | 0.7500 |
| Port 1280x720 | 1 | New game | 1 | First room | 1.3722 | 2.4395 | 0.5625 |
| Port 1280x720 | 1 | Save with the option at 0 | 0 | First room | 1.8296 | 2.4395 | 0.7500 |
| Port 960x720 | 0 | New game | 0 | First room | 1.8296 | 2.4395 | 0.7500 |
| Port 960x720 | 0 | Save with the option at 1 | 1 | First room | 1.3722 | 2.4395 | 0.5625 |

In the same place yScale never changes, only xScale. The port and xemu read the same numbers in each combination, and the value follows the byte, which follows the save. Changing the value in a save and re-signing it is enough: the game accepts the file and obeys. The draws agree: in F3 captures, the matrix in `c0` to `c3` of the 3D draws, corrected for the 640 width (the game multiplies x and y by the same 240, `c-38`, around a centre at 320), gives 0.562 in 40 of 42 draws of the first room in 16:9 and 0.750 in 31 of 31 in 4:3.

So a save made in 16:9 came out squeezed in a 4:3 window, and a save with the option off came out stretched 33% in 16:9. An earlier "x0.75" difference against xemu was this: a port save loaded in xemu turns the option on, and xemu draws 16:9 and shows it in 4:3.

### Keeping the option in step with the window

The `aspect` key in `settings.toml` takes `auto` (the default, following the window's shape), `16:9` or `4:3`. `app::Widescreen()` obeys it, which sets what the console reports and the picture's size in `BlitFrame`: 4:3 in a 16:9 window gets bars at the sides, 16:9 in a 4:3 window bars above and below.

That alone is not enough, because the save decides. So `KeepWidescreenOption` in `src/game/hooks.cpp`, hooked at `0x1A8410` (the start of each frame, on the game's thread), makes the game's option follow the choice: the byte `0x4006A4` that the camera reads, the option `0x3E` in the list at `0x3F4CFC` that `0xB9630`, `0xC6470` and `0xE12FB` read and the next save writes, and the new-game default at `0x3EE4CC`. When they already agree, nothing changes.

Measured in the first room with a save whose option was 1, and the same save with it at 0, in seven combinations of window (1280x720 or 960x720), `aspect` (`auto`, `16:9` or `4:3`) and option in the save: `xScale / yScale` was 0.5625 whenever the chosen shape was 16:9 and 0.75 whenever it was 4:3, whatever the save said, with the picture whole or with bars as expected. Switching on the options screen while the level ran gave 0.5625, 0.75, 0.5625, 0.75 in turn. A new game in 4:3 saves 0 and in 16:9 saves 1, and saving a checkpoint to a new slot from a save with 1 writes 0 in 4:3 and 1 in 16:9. The byte reads 0 in about 1 of 10,000 samples with the option on, which matches the camera at `0xC9550`. Only the first room had its camera read this way.

### Menus and HUD 33% wider at 16:9

The HUD and the menus do not move in the game's 640x480 image when the option changes: in captures of 16:9 and 4:3, the menu text sits on the same pixels (`OPTIONS` and `CREDITS` 1.000, `NEW GAME` 0.996) and the HUD regions have no offset. The port shows those 640x480 stretched to 16:9, so the HUD and the menus are 33% wider. Only the 3D compensates, through xScale (747 draws in the first room at 16:9 against 677 at 4:3; that the wider view brings more objects into frame is the probable reason, not checked).

The original game does the same. In xemu, with no port code and no save, and the console set to widescreen (an EEPROM copy with `AV_FLAGS_WIDESCREEN`, `0x00010000`, at `0x94` and the checksum at `0x60` redone, which is the bit `XGetVideoFlags` returns and `0x1FABFD` tests), the byte and `0x3EE4CC` are 1, `xScale / yScale` is 0.5625, xemu shows 16:9 by itself, and the menu's A and B buttons turn oval. Horizontal scale of each HUD piece against the 4:3 picture, by best fit:

| Piece | Original in 16:9 | Fit | Port in 16:9 | Fit |
|---|---:|---:|---:|---:|
| Prompt text | x1.33 | 0.907 | x1.33 | 0.811 |
| Enemy name | x1.33 | 0.692 | x1.33 | 0.580 |
| Meter | x1.33 | 0.985 | x1.33 | 0.961 |
| Bottom icon | x1.33 | 0.995 | x1.25 | 0.730 |

The port's x1.25 on the small icon comes from a weak fit; enlarged, the icon has the same width and place in both. So the wider HUD is the original game's behaviour on a widescreen console (seen in xemu, not on a real console and TV), and the port keeps it. 4:3 on the options screen gives the original proportions.

For reference, read in code and in frame captures, nothing implemented: all the text and most of the HUD are quads (prim 7) drawn by one function, `0x204550` (`EXWnd::DrawSprite2DData` in the GameCube names), called only from `0x2004EA`, with three `D3DDevice_DrawVerticesUP` calls (`0x204A79`, `0x2050F2`, `0x2052E7`) on one buffer at `0x420B80`. Squeezing quads narrower than 560 by 0.75 around x = 320 there would move the text and HUD pieces together, into the middle 4:3 of the screen. The crosshair, prompts and some plates are fans (prim 6) at `0x205C10` (called from `0x2004A9`) placed by a matrix in `c0` to `c3`, and the menus' button icons go through the model path (shader `B0101281`) from a place not located. The objects the game already compensates (`0xEE0F0` and the others above) would be squeezed twice.

### Videos in each shape

The video code at `0xE1320` also follows the option. A 4:3 video with the option on is drawn at width x0.6923 x 1.2: 531 of 640 columns, or 1062 of 1280; the EA logo measured 1061 columns, about 10% wider than 4:3. In a 4:3 window it fills the 960 columns. A widescreen video with the option off is drawn at height x0.6923: the film carries its own bars, and fills 535 of 720 lines in the port at 16:9 (2.39:1) and 495 of 960 in xemu at 4:3 (2.59:1); `960 x 0.6923 x 535/720` gives 494, so the 2.59:1 is the game's own 4:3 drawing.

Not verified: a real console on a widescreen TV; anything beyond the first room, the chapter 2 corridor, the main menu and the EA logo; the pause screen, the `0xC6470` screens and the bit `0x40` objects; whether the game ever writes the option on its own.

## Graphics device lost

Before, the result of `swap_->Present` was discarded, so a log could not show a lost device. Now, when `Present` returns `DXGI_ERROR_DEVICE_REMOVED`, `DEVICE_RESET` or `DEVICE_HUNG` (`sdk/runtime/gpu/device_lost.h`), the game writes the line to the log, stops the sound, shows "A graphics error occurred and the game will close..." and exits with code 1. Any other `Present` failure is logged once and the game goes on (that path is read in code; the decision has a test, `tests/native/device_lost_test.cpp`).

Simulated with `BB_DEV_REMOVE_DEVICE=<seconds>`, off by default, which calls `ID3D12Device5::RemoveDevice`: after the logos and at the start of the first level, the line `Present failed with 0x887A0005, device removed reason 0x887A0005` came 21 and 14 ms after the removal, then the message, `Exit (1)` and no crash dump. Nothing hung: fence waits pending before the removal, and new ones after it, return at once, the fence value becomes `UINT64_MAX`, and creating a resource fails with `0x887A0005`. Not provoked: a real driver or hardware failure. By the code, `CreatePixelTexture` and `Upload`'s own buffer use a new resource without checking, so a removal in the frame that creates one could crash, with a dump, before the message.

### The "graphics error" crashes

Three crashes during the campaign, all with `render_percent = 134` (4x on a 1440-line output, by the formula in `options_model.cpp`), the 16x filter, vsync off and 60 FPS: two at the lockpick gate on the docks (7:03 and 8:55 into a session) and one with no apparent trigger (1:48). The same gate had opened without a crash earlier. Each log had `Present failed with 0x887A0005, device removed reason 0x887A0001` (`DXGI_ERROR_INVALID_CALL`), with no driver reset event (4101), DRED in an unknown state and no command list in flight.

A separate probe called Direct3D 12 on this PC with illegal values, one case per process. The same signature comes from the creation calls that return nothing: `CreateSampler` with a `MipLODBias` outside -16 to 15.99, or NaN, and `CreateShaderResourceView` or `CreateRenderTargetView` with an invalid format, dimension or mip. An inverted rectangle in a clear or scissor, a NaN depth and a NaN viewport do not remove the device. Injecting a bad value into the game gives exactly the crashes' line, DRED state and message for a sampler and for a view.

**Hypothesis, then confirmed for one value.** The only sampler field the port passed through unbounded was the game's `D3DTSS_MIPMAPLODBIAS`, raw, in all four stages, in use or not. The bias is now clamped to Direct3D 12's range, -16 to 15.99, NaN becoming 0, with a log line (`sdk/runtime/gpu/sampler_bias.h`). A later session of 44 min with the same settings passed the gate without a crash, but with no bias line either, so it proved nothing. In the next, of 1 h 4 min, the log had `d3d12: stage 0 mip bias 51.039726 (424C28AE) is out of range, texture bound` 16 times at the same instant (the line limit), during an area change (textures fell from 2025 to 361 the next minute), and the game ran on to a normal exit. Without the clamp that value would have reached `CreateSampler`, and the probe shows that any bias of 16 or more removes the device with the crashes' exact signature. So the cause is proven for that value, and fixed. That the three earlier crashes were the same event fits everything, but cannot be proven for each one. The clamp acted twice more in later sessions, with 37.418655 and 2129820.25, and both times the game went on.

Unknown: whether the settings matter. There is no crash with the defaults, but also no long session with the defaults in that part of the game. One illegal use found in review remains: the sampler heap is reused without waiting for the GPU. That needs 2048 sampler entries; the sessions measured reached 64, 132 and 184.

### Diagnostics on device removal

- After each creation call, the port checks whether the device is gone and logs the call and its values: `device removed, first seen after a sampler` (or `a texture view`).
- DRED auto-breadcrumbs and page-fault reporting are switched on before the device is created.
- The log has one line a minute with resource use (textures, targets, pipelines, samplers, descriptor ring wraps, video memory against its budget). On removal it gets the DRED report, that line, and the port's last steps, oldest first.
- Once during testing the test PC hung with a black screen and was powered off. Windows recorded only an unexpected shutdown (events 41 and 6008): no bugcheck, no hardware error (WHEA), no display driver reset (4101). The cause was not found, and nothing shows the game was not the trigger. The `Present` check above came from this.

## Shader cache

**Measured cause of stutter:** shaders were compiled at draw time. `D3DCompile` takes 45 to 90 ms per shader on this PC, and the game asks for each shader the first time its effect appears. The explosion in the first room needs 7 at once, about 500 ms with the picture stopped.

`sdk/runtime/gpu/shader_cache.{h,cpp}` keeps each compiled shader in `shaders.cache`, next to the program, keyed by the source, entry point and profile, and loads them at start. Each record has its own check: from a cut or damaged record on, the rest is discarded and compiled again, so nothing invalid reaches the driver. The header's version includes the compile options. A folder that cannot be written runs without the cache. The stored shaders are the bytes the compiler returns, 61 of 61 identical between two cold runs.

| Moment | Cold: FPS | Cold: compile (ms per frame) | Cold: shaders | Warm: FPS | Warm: shaders |
|---|---:|---:|---:|---:|---:|
| Logos | 5.1 | 159.14 | 17 | 42.0 | 0 |
| Title | 25.2 | 6.08 | 5 | 30.0 | 0 |
| Start of the level | 4.5 | 182.53 | 10 | 30.1 | 0 |
| Explosion | 16.0 | 35.55 | 7 | 30.0 | 0 |
| Whole run | 4 seconds slowed by compiling | 2,608 ms in all | 61 | No second slowed by compiling | 0 |

The warm run's remaining seconds below 29 FPS (25.8 to 28.5) all fall on file loads between scenes. Pipelines are not the cause: creating them took 29 ms in the whole cold run and 25 ms warm. With the driver's own cache cold (forced by a temporary build whose compile options changed every shader's bytes), pipeline creation took 225 ms in all and 65.5 ms in the worst second, against 2,507 ms of compiling, so there is no pipeline cache. A cache cut in half loaded 35 shaders, discarded 1,812 bytes and compiled 18; one with a flipped byte loaded 20, discarded 281,928 bytes and compiled 33; both reached the level with no crash. `tests/native/shader_cache_test.cpp` cuts the file at every length and flips every byte.

The cache is built from the game's own shaders, so it stays out of Git. `tools/make_dist.py` gives a new portable folder every shader of the build's cache and of any cache passed with `--cache`, without mixing cache versions; an existing folder keeps its own. The portable folder used for the campaign started with 62 shaders (424,932 bytes), and no shader compiled in runs through the logos, title, menus, options screen, a new game, saving, the opening video, the first room's fight and explosion, and the start of chapter 2 with its corridor and pause. The rest of the campaign compiles each new shader once, on first sight, and keeps it. The code is DXBC from `D3DCompile`, which does not depend on the GPU (general knowledge, not measured on another GPU).

## Saves, settings and logs

### Saves

Saves are under `hdd\E\UDATA\45410389\`, next to the program, and the port signs them with a zero key (`tools/save_sign.py` re-signs a save, which is how saves were moved into xemu). Measured over a session of menus, a new game, the opening video, the first room and a save, the game writes `TitleMeta.xbx` (54 bytes), `TitleImage.xbx` (10,240) and `SaveImage.xbx` (4,096) only when they are missing, `SaveMeta.xbx` (32 bytes) in each new slot, and the save itself, `BATMAN A` to `F`, 2,560 bytes in two writes. Nothing is written to `hdd\C`, `X`, `Y` or `Z`, and the game never calls `NtFlushBuffersFile`. A save opens its file when "Saving" appears, writes 6.1 s later, and closes at 10.27 s; "Game saved." is on screen 7 s after the start. Since the game never asks for a flush, the close used to be what took the save to disk.

Now a file on `hdd\E` written since its last commit goes to disk on `NtFlushBuffersFile`, on close, and on every write, in `Transfer` itself (`sdk/runtime/kernel/save_commit.h`, tested in `tests/native/save_commit_test.cpp`). The other drives do not. `CloseHandle` now releases the object after freeing the handle table entry, so the commit runs on the last reference (read in code). The calls now take 2.9 to 7.5 ms against about 0.3 ms before, the save takes the same 10.27 s, and the FPS stays at 30 around it. `kernel::Shutdown` also flushes, on a separate thread with a 2 s limit, because `_flushall()` without a limit hangs if another thread holds the stream.

Not testable here: that the bytes survive a power cut; the longer calls suggest the request reaches the disk but do not prove it. Saving over a slot erases the previous save as soon as "Saving" appears, so a power cut in those 10 s loses that slot. Only the manual Save Last Checkpoint was measured; whether the game also saves on its own at checkpoints was not.

### Settings

`settings.toml` sits next to the program. A missing file is silent, as on first start. A broken one (a repeated key, for example) logs an `error` line with the message and the line number, is copied to `settings.toml.bad` before anything can overwrite it, and the game runs with the defaults (`tests/native/settings_file_test.cpp`). Keys include `display_mode`, `width`, `height`, `render_percent`, `aspect`, `anisotropic_filtering`, `vsync`, `fps_cap` (0 for no cap, 1 for the monitor's refresh rate, otherwise frames per second, never above `kMaxValidatedFps`; not on the options screen, and rewritten as it was in the file), `experimental_play_fps` and the `key_*` map.

### Logs

Each start moves `batman.log` to `batman.1.log`, and so on up to `batman.4.log`; the oldest is deleted. A relaunch into another image continues the same session, and a file that cannot be moved gets the new lines appended (`tests/native/log_test.cpp`). In normal play only `Report` and `Error` lines exist, and each leaves the program at once. With `XBE_PERF`, which only the tests use, `Info` lines wait in a buffer of about 4 KB, about 5 s of perf lines. Nothing in the logger calls `FlushFileBuffers`, so a crash of the game loses nothing, but a PC hang or power cut can lose what is still in the Windows cache (read in code; the window was not measured).

### Input and other details

- **Keyboard:** keys are read from the window's messages and applied to controller port 0 (`src/common/keyboard_pad.cpp`), only while the window has focus; losing focus releases them all. The game does not pause on losing focus (tested with focus messages, not a real Alt+Tab).
- **ACTION (B):** the B button's action is named ACTION because the game says "Interact with objects by pressing" with the second button's icon.
- **Icon:** made at build time from the XBE's `$$XTIMAGE` section (10,240 bytes, identical to the `TitleImage.xbx` the game writes: XPR0 with DXT1 at 128x128, the bat on opaque white), with the white reaching the edges made transparent. On a dark taskbar the thin dark bat nearly vanishes at 48 px and below, so the icon uses it at 256, 128 and 64 and the save image (`$$XSIMAGE`, 64x64, the bat on grey) at 48, 32, 24 and 16. SDL takes the program's first icon for the window class, so the title bar and taskbar match.
- **Portable folder:** `tools/make_dist.py`. The ISO copy is checked by SHA-256. A missing ISO, or one from another release, shows a message and exits with code 1. The program embeds the build machine's `.pdb` path.

## Not verified

- A real console. xemu is the reference, and it varied by 1.0% over 10 s between two runs.
- At 30 FPS, measurements cover chapters 1 and 2; the campaign sessions with logs ran at 60.
- Per-frame corrections 4 to 10 are measured only as far as they enter the run, the jump and the fight. Ropes, pipes, ladders, the kick and the combat counters have no measurement of their own.
- The static reading may have the sense of some field wrong, and three Xbox stretches have no GameCube counterpart.
- Video framing and subtitles in each video's own slot, except the logos and the opening video. The tap skip in the extras menu.
- FULLSCREEN, other monitors and refresh rates, other GPUs, and 4x in heavy scenes.
- A real graphics driver failure, and a PC without the Visual C++ runtime installed.
- That saves survive a power cut; automatic checkpoint saves.
- The cause of the test PC's hang.
