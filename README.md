# Crysis Warhead MTO

> [!WARNING]
> This mod has been pretty much entirely generated using AI;
> I won't and will never claim to have enough knowledge or expertise to do this
> kind of reverse-engineering by myself; I've played the first two levels and so far spotted no major issues (on my machine ofc, if you
> have issues feel free to open an Issue).
> I've only tested it with the GOG version of the game so far (1.1.1.711 (gog-5)) and Steam, so no
> guarantees for other exes for now.
> If you're interested and want more (human generated) info, I have a blog post [here](https://slop-blog.enkhayzomachines.net/posts/crysis-warhead-multithreaded-overhaul) :)

A CPU-multithreading mod for Crysis Warhead (2008), ported from Crysis MTO, the same mod for the first Crysis. The game runs its simulation, its scene traversal and every Direct3D call on one thread. This mod moves the renderer's work onto a thread of its own, spreads two of the main thread's jobs (the particle emitters' update and the characters' animation) over worker threads, and fixes two engine defects that cost frame rate. It ships as an ASI plugin, `optiperf.asi`, loaded by [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader); no game file is modified.

In play (the start of the first mission, about 2 minutes each way), against the same 64-bit game with only the allocator, which it needs to keep running: **185 fps on average instead of 109** (×1.7), the slowest 1 % of frames at 109 fps instead of 72, and the time under 100 fps down from 44 % to 2 %. The scripted benchmarks of the game's seven levels show less, **×1.31 on average**, from ×1.05 on cave to ×1.54 on frost (128 fps before, 197 after): play has more of the frames that the main thread holds back.

Current release: **v1.0.0**

## Compatibility

Crysis Warhead 1.1.1.711 for Windows, as sold on Steam and GOG (the same build: their engine files are identical, byte for byte): the 64-bit game (`Bin64\Crysis64.exe`, the one Steam starts; GOG Galaxy starts `Bin64\Crysis.exe`, a copy of it), DX10 and DX9. The render thread and the worker threads need DX10; DX9 gets the particle fix and the allocator. The 64-bit game needs Windows 10 or later. The game picks DX10 on Windows 10 and later; it runs in DX9 only with `r_Driver = "DX9"` in a `system.cfg` in its folder (its `-dx9` switch has no effect, also the one GOG Galaxy's Play button passes).

The Steam release's 32-bit game (`Bin32\Crysis.exe`) runs only through Steam, which starts the 64-bit one, and GOG's release has no 32-bit game, so the mod hasn't run in it. The archives still carry the 32-bit plugin (the particle fix and the allocator); its particle fix passes its test against Warhead's 32-bit renderers outside the game.

| Release | Status |
| --- | --- |
| Steam | Reference build: benchmarked on the seven levels' timedemos, played at the start of the first mission |
| GOG | The same files: the package tested on its install as Galaxy starts the game (both of its tasks) |
| Other releases (EA app, retail) | Not tested |

There are no version or hash checks. Before installing a hook, each part finds the game code it changes by its byte pattern and verifies it: offsets read from the matched code must agree wherever they appear, and the functions it calls are matched by their own patterns. If anything differs, only that part stays off, `optiperf.log` says why, and the game runs as usual. The release notes list the SHA-256 of the reference build's files that the supplied settings change. Mods that replace `CryGame.dll` haven't been tested with this version.

## What it does

| | Game builds | Effect (measured) |
|---|---|---|
| **Render thread** | 64-bit, DX10 | The renderer's main pass, the HUD and the present run on their own thread while the game works on its next frame. Most of the gain: with the parts below, ×1.05 to ×1.54 on the seven levels' benchmarks. |
| **Thread placement** | 64-bit, DX10 | The game's main thread and the render thread each get a physical core of their own, and the game's other threads are kept off those two. Left to Windows, both move between processors and can share a core. |
| **Particle emitters on workers** | 64-bit, DX10 | The 3D engine's particle emitters are updated by worker threads and the main thread at once, instead of one after another on the main thread. About 0.15 ms of the main thread a frame on frost and ambush. |
| **Character animation on workers** | 64-bit, DX10 | The characters' poses (their forward kinematics) are made at once by worker threads at the end of the entity update, instead of in each character's own update. The game's logic reads each pose, and the movement its animation drives, one frame late; what is drawn is current, and the player's own character and weapons are unchanged. Up to about 0.2 ms of the main thread a frame in the benchmarks. |
| **Particle fix** | all | Replaces a quadratic search in the renderer's particle batching. Warhead's levels batch few particles at once (65 objects on average), so the gain is small: about 190 cycles a call instead of 430-540. |
| **Allocator** (mimalloc) | all | Removes the engine allocator's global lock, and keeps the 64-bit game running: the engine cuts some of its pointers to 32 bits, and without the allocator the game crashes once its memory comes from above 4 GB (within the first minutes of a timedemo). |

Measured on 8 cores of an i9-13900K (in a VM) with an RTX 4090, at 1080p and Very High settings (`sys_spec 4`): runs 1-5 of each level's timedemo, each level's two settings back to back.

| Level | Allocator only | With the mod | |
| --- | ---: | ---: | ---: |
| frost | 127.9 fps | 197.3 fps | ×1.54 |
| avalanche | 138.4 fps | 201.4 fps | ×1.46 |
| cargo | 92.4 fps | 125.8 fps | ×1.36 |
| train | 113.0 fps | 148.6 fps | ×1.32 |
| airfield | 89.3 fps | 116.7 fps | ×1.31 |
| ambush | 123.8 fps | 144.6 fps | ×1.17 |
| cave | 131.5 fps | 138.1 fps | ×1.05 |

## Current status

Everything above is on by default in the supplied `optiperf.ini`, and each part can be turned off there on its own. The release package passed 15 timedemo launches through Ultimate ASI Loader with the HUD on: DX10 on five levels, with the frame rate display on, and with the game held to fewer processors; DX9 on two levels, where the render thread and the worker threads stay off by themselves. With the supplied settings and the HUD on, every level's benchmark plays through.

One behaviour differs from the game's own: with the animation on worker threads, the game's logic reads each character's pose one frame late. `[AnimUpdate] Mode=off` restores the game's order.

The main thread's own work still sets the frame rate. What is left to move off it (the scene's objects and terrain, onto workers) is a bigger project, for later.

## Build

Needs Visual Studio 2022 or later with the C++ x86/x64 tools (CMake and Ninja come with it). CMake fetches mimalloc and MinHook at pinned tags.

```powershell
.\build.ps1 -Arch x86,x64        # build\x86, build\x64: optiperf.asi, optiperf.ini, the dev launcher, the tests
```

[src/README.md](src/README.md) describes the code, the settings and the tests.

## Package

After building and testing the plugin, create the two release archives with:

```powershell
.\package.ps1 -Version 1.0.0
```

The script builds the Release binaries (`-SkipBuild` packages those already in `build\release`), downloads Ultimate ASI Loader v9.7.4 and checks its pinned hashes, and writes to `dist\` the plugin-only archive and the archive with the loader, each with payload hashes (`SHA256SUMS.txt`) and a `.sha256` file beside it. The loader's notice goes only into its archive; the notices of mimalloc and MinHook, which are linked into the plugin, into both. It refuses to overwrite an existing release archive.

## Install

1. Close Crysis Warhead.
2. Take the archive with the loader (`Crysis-Warhead-MTO-v1.0.0-with-Ultimate-ASI-Loader-v9.7.4.zip`), unless the game already has an ASI loader in `Bin64`: then the plugin-only one (`Crysis-Warhead-MTO-v1.0.0-plugin-only.zip`).
3. Extract it into the game's folder (the one that contains `Bin64` and `Game`; on Steam, right-click the game, then Manage, Browse local files; in GOG Galaxy, the game's settings button, then Manage installation, Show folder). GOG's release has no `Bin32`: there, only the archive's `Bin64` is needed. `Bin32` and `Bin64` each get `optiperf.asi` and `optiperf.ini`, and from the archive with the loader `version.dll`. If the game already has a loader, don't overwrite it: copy only `optiperf.asi` and `optiperf.ini`. If another mod's `version.dll` isn't an ASI loader, rename this one to `winmm.dll` (both names work).
4. Start the game as usual: Steam's Play button, or GOG Galaxy's (either of its two tasks), starts the 64-bit game in DX10.

The plugin writes `optiperf.log` beside `optiperf.asi`: what was patched, why a part stayed off, and, if the game crashes or shows an error, where.

## Configuration

Edit `optiperf.ini` (in `Bin64`, and in `Bin32` for the 32-bit game) while the game is closed. The supplied one turns everything on; without a line, a setting is off.

- `[ParticleBatch] Mode=off`, `[Allocator] Mode=off`: the two engine fixes off (without the allocator, the 64-bit game crashes during play).
- `[ShadowPrepass] FlushThread=main`: the render thread off (what prepares the renderer's work stays on); `[ShadowPrepass] Mode=off` leaves the renderer alone entirely.
- `[ShadowPrepass] FlushThreadPlacement=default`: Windows places the main thread and the render thread.
- `[ParticleUpdate] Mode=off`, `[AnimUpdate] Mode=off`: the particle emitters and the animation back on the main thread.
- `[Debug] FrameLog=on`: the frame rate, a line a second in `optiperf_frames.csv`, and the average and the 1 % and 0.1 % lows in `optiperf.log`.

[src/README.md](src/README.md) lists every setting.

## Uninstall

Close the game and remove these files from `Bin32` and `Bin64`:

- `optiperf.asi`
- `optiperf.ini`
- `optiperf.log` (and `optiperf_frames.csv`, if the frame log was on)

Do not remove `version.dll` if it existed before this mod or if another ASI plugin uses it.

## Technical outline

The mod is an ASI plugin for both games. On the 64-bit game with DX10 it runs the renderer's main pass, the HUD and the present on a thread of its own: before the hand-off it prepares, copies or records everything the renderer would otherwise ask the rest of the engine for while drawing, and the game's next frame goes on beside it, its calls into the renderer taken, answered or made to wait. [src/README.md](src/README.md) describes each part, its settings and its tests, and what changed from Crysis MTO for Warhead's build.

The repository contains the mod's source and its build and package scripts. Release archives contain only the built plugin, its configuration, documentation, hashes, notices and, only in the explicitly named archive, the third-party ASI loader. No Crysis Warhead file is distributed.

## License

MIT (see [LICENSE](LICENSE)). The plugin contains mimalloc and MinHook, and the archive with the loader Ultimate ASI Loader, under their own licenses (see each archive's `THIRD-PARTY-NOTICES.txt`).
