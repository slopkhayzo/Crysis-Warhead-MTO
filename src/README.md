# optiperf

Runtime performance patches for Crysis Warhead (2008), shipped as an **ASI plugin** (`optiperf.asi`) loaded by Ultimate ASI Loader. Nothing in the game's files is modified. The code is Crysis MTO's (the same plugin for Crysis 1.2.1, build 6156), with build 711's patterns, offsets and slots, and the changes Warhead's engine needed ([below](#ported-from-crysis-mto-what-build-711-changed)).

This file and the code's comments refer to `research/` (the port's plan, measurements, test scripts): the author's working notes, which are kept out of this repository. Notes named `research/docs/<part>.md` other than the port plan are Crysis MTO's, whose findings this code shares.

| Patch | What | Status |
|---|---|---|
| Particle batch fix | O(N²) search in `EF_AddParticlesToScene` replaced by a hash index with identical results | x86 + x64, DX9 + DX10 |
| Allocator | CrySystem's `CryMalloc`/`CryFree`/`CryRealloc`/`CryGetMemSize` replaced by mimalloc (no global lock); on x64, engine memory kept below 4 GB, which keeps the 64-bit game running | x86 + x64 |
| Diagnostics | Logs crash-type exceptions, error boxes and `ExitProcess` with the engine call chain; records the engine's thread names | x86 + x64 |
| Call tracer (developer tool, off by default) | Counts calls through the engine's vtables per method, caller, call site and thread (x86: also times them); used to map the render-thread boundary | renderer: x86 DX10 + x64; all modules: x64 |
| Shadow pre-pass (Phase 2) | Renders the shadow casters before the renderer's flush instead of inside it, and replays their output into each shadow map; the first step towards a render thread, no speed-up on its own | x64 DX10 |
| Reflections before the flush (Phase 2) | Renders the ocean reflection's scene before the flush too, and replays it where the game draws it; part of the shadow pre-pass's settings | x64 DX10 |
| Far trees before the flush (Phase 2) | Generates the distant-vegetation sprites before the flush, and decides there how they are drawn; builds on the reflections | x64 DX10 |
| Render thread | The flush, then the HUD and the present, on a thread of their own while the main thread goes on with its next frame, its scene traversal included (`FlushThread=overlap`, `FlushThreadBarrier`, `FlushThreadRecord=beside`, `FlushThreadPresent`); the parts above and the snapshots below prepare and answer the flush's calls | x64 DX10 |
| Thread placement | The main thread and the render thread on a physical core each, the process's other threads kept off both (`FlushThreadPlacement=apart`) | x64 DX10 |
| Particle update | The 3D engine's particle emitters updated at once by worker threads and the main thread (`[ParticleUpdate]`) | x64 DX10 |
| Animation update | The characters' forward kinematics made at once by worker threads at the end of the entity update, one step late for the game's logic (`[AnimUpdate]`) | x64 DX10 |
| Frame log (off by default) | The frame rate a second, the average and the lows (`[Debug] FrameLog`) | x64 (x86 untested) |

Everything from the shadow pre-pass down, except the frame log, is off in the code's defaults and turned on by the package's `optiperf.ini`.

**Supported:** Crysis Warhead 1.1.1.711 (build 711): the Steam release, whose `Bin64\Crysis64.exe` is the game Steam starts, and the GOG release, whose engine files are the same byte for byte (no `Bin32`; Galaxy runs `CrysisWHLauncher.exe`, which starts `Bin64\Crysis.exe`, a copy of `Crysis64.exe`, with the task's argument: `-dx9`, which the game ignores, or none). There are **no version or hash checks.** Each patch finds its target by byte pattern and verifies it once at hook time:

- member offsets are read from the matched code and must agree everywhere they appear;
- callees are matched by their own patterns;
- the allocator patch also checks that `CryGetMemSize` encodes the layout it relies on.

If anything differs, only that patch stays off, and `optiperf.log` says why.

## Install (players)

Run `package.ps1`, or take a release archive, and extract it into the Crysis Warhead folder. `Bin32` and `Bin64` each get `optiperf.asi` and `optiperf.ini`, and from the archive with the loader `version.dll` (Ultimate ASI Loader v9.7.4); the plugin-only archive is for a game that already has an ASI loader. Both archives' `README.txt` comes from [dist/README.txt](dist/README.txt).

UAL is installed as `version.dll` because `CrySystem.dll` imports that name in both `Bin` folders (so does `winmm.dll`), and the 32-bit exe imports it itself, so the loader comes in with the first engine DLL. In practice UAL loads plugins after CrySystem and the renderer are mapped (it waits for game code to run). The plugin therefore patches whatever is already loaded and watches for later loads; the allocator copes with blocks allocated before it was installed.

The Steam release's 32-bit exe is wrapped with Steam's DRM: started directly it shows "Application load error 5:0000065434", and Steam starts the 64-bit exe. The test scripts refuse to start it.

## Build and test (developers)

Needs Visual Studio 2022+ with the C++ x86/x64 tools (CMake and Ninja come with VS). mimalloc v2.5.2 and MinHook v1.3.4 are fetched by CMake at pinned tags.

```powershell
.\build.ps1 -Arch x86,x64                  # build\<arch>\: optiperf.asi, optiperf.ini, optiperf_launch.exe, tests
.\build\x86\particle_batch_test.exe "<Warhead>\Bin32"   # differential test vs the game's own renderer code (x64: build\x64\..., Bin64)
.\build\x64\allocator_test.exe             # allocator outside the game: layout, realloc, x64 below-4 GB guarantee
.\build\x86\calltrace_test.exe             # call tracer outside the game (x64: build\x64\...)
.\build\x64\shadow_prepass_test.exe        # Phase 2: list handling, vtable counter, its code in Bin64's renderer and 3D engine
.\package.ps1 -Version 1.0.0               # Release build + dist\Crysis-Warhead-MTO-v1.0.0-plugin-only.zip and
                                           # -with-Ultimate-ASI-Loader-v9.7.4.zip (-SkipBuild: the binaries in build\release)
```

- `optiperf_launch.exe "<Warhead>\Bin64\Crysis64.exe" -dx10` injects the plugin without installing UAL, for development. Use the matching architecture's launcher. Warhead's 64-bit game ignores `-dx9` (and `+r_Driver DX9`, applied after the renderer is made): `r_Driver = "DX9"` in a `system.cfg` in the game's root selects DX9 (the sandbox's, for tests).
- `research/profiling/run_bench.ps1 -Launcher ...` runs the timedemos with it (through the demo recorder's level chain: `+map X +demo` quits before the level loads in Warhead). It puts the user's `game.cfg` back after each run: the game saves the command line's settings into it.
- `research/profiling/sandbox.ps1` creates a sandbox game root (copied `BinXX` plus a junction to `Game`) for testing with the real UAL without touching the install.

**Settings** live in `optiperf.ini`. Test scripts can override keys with environment variables:

| Key | Values | Environment variable |
|---|---|---|
| `[ParticleBatch] Mode` | `fast` · `verify` · `off` | `OPTIPERF_PARTICLE_BATCH` |
| `[ParticleUpdate] Mode` | `off` (default) · `on` (x64: the 3D engine's particle emitters updated at once by worker threads and the main thread, at the particle manager's first update call; its liveness checks and octree registrations go on in its order; the sound system, physical entities' creation and changes, ray tests, an emitter's first vis-area lookup, shared meshes' and characters' `AddRef` and the profiler's registration made one at a time, their `Release` on the main thread after the batch; a worker's `GetCamera` answered with the main thread's; needs `[ShadowPrepass] Mode` other than `off`, not with `[Debug] TraversalCensus`) · `serial` (the same batches on the main thread alone) · `time` (the game's own update, timed in the log) | `OPTIPERF_PARTICLE_UPDATE` |
| `[ParticleUpdate] Workers` | `auto` (default: the processors `FlushThreadPlacement=apart` leaves to the other threads less one, else half the process's less one; at most 4) · `1`-`8` | `OPTIPERF_PARTICLE_UPDATE_WORKERS` |
| `[ParticleUpdate] Priority` | `normal` (default) · `above` (the workers' thread priority, a test) | `OPTIPERF_PARTICLE_UPDATE_PRIORITY` |
| `[AnimUpdate] Mode` | `off` (default) · `on` (x64: the characters' forward kinematics wait for the end of the entity system's update, then made at once by worker threads and the main thread, the post-processes after them on the main thread; the game's logic reads a pose one step old; not the local player's character or first-person weapons) · `serial` (the same on the main thread alone, for comparisons) · `time` (as in the game, its times logged) · `verify` (`on`, each pose made again on the main thread and compared, a test) | `OPTIPERF_ANIM_UPDATE` |
| `[AnimUpdate] Workers` | `auto` (default: as `[ParticleUpdate] Workers`) · a number, 1-8 | `OPTIPERF_ANIM_UPDATE_WORKERS` |
| `[Allocator] Mode` | `mimalloc` · `off` | `OPTIPERF_ALLOCATOR` |
| `[Allocator] LowMemoryMB` | x64 reservation below 4 GB, default 1536 | |
| `[Allocator] Owners` | `off` · `on` (x64, a diagnostic: each block keeps the code that allocated it; the pointer census needs it) | `OPTIPERF_ALLOCATOR_OWNERS` |
| `[Debug] Diagnostics` | | `OPTIPERF_DIAGNOSTICS` |
| `[Debug] NoDialogs` | log and auto-answer error boxes (set by `run_bench.ps1`) | `OPTIPERF_NO_DIALOGS` |
| `[Debug] FramePhases` | `off` · `on` (x64, a diagnostic: the main thread's frame timed by phase, and marked in call traces) | `OPTIPERF_FRAME_PHASES` |
| `[Debug] FrameLog` | `off` (default) · `on`: the frame rate, in any mode: a line a second in `optiperf_frames.csv` next to the log (fps, the longest frame, paused frames), the average, the 1 % and 0.1 % lows and the longest frame in the log | `OPTIPERF_FRAME_LOG` |
| `[Debug] RLogAt` | empty (default) · a counted frame (x64, a diagnostic with `FrameLog=on`: the renderer's own log, `r_Log` set through the console at that frame's start, for `RLogFrames` frames (2) at `RLogLevel` (4: every pass, state and shader), in `Direct3DLog.txt` in the game's folder; `run_bench.ps1` moves it into the run's) | `OPTIPERF_RLOG_AT`, `OPTIPERF_RLOG_FRAMES`, `OPTIPERF_RLOG_LEVEL` |
| `[Debug] PostWatch` | empty (default) · text (x64, a diagnostic with `FrameLog=on`: the 3D engine's `SetPostEffectParam` calls for a parameter whose name contains the text, with the frame, the thread and the caller; and CryVision's parameters, logged when they change) | `OPTIPERF_POST_WATCH` |
| `[Debug] ProducerTiming` | `off` · `on` (x64, a diagnostic: the main thread's calls into the renderer's producer methods timed, per frame phase with `FramePhases=on`; not with the call tracer nor `FlushThread=overlap`) | `OPTIPERF_PRODUCER_TIMING` |
| `[Debug] TraversalCensus` | `off` (default) · `casters` · `fields` · `particles` (x64, a diagnostic for moving work off the main thread: on a few frames, what the main thread reads and writes of the engine's memory in the 3D engine's shadow casters' walk and in the rest of its scene drawing, page by page with the code and call chain, into `traversal_census.tsv` next to the log; `TraversalCensusEvery` (400), `TraversalCensusMax` (3); needs `[Allocator] Owners=on`; run with `FlushThreadRecord=off` and `FlushThreadPresent=off`) | `OPTIPERF_TRAVERSAL_CENSUS`, `_EVERY`, `_MAX` |
| `[Debug] ProcessAffinity` | empty (default: the mask is left as the launcher set it, and logged) · a hex mask, e.g. `FE` (a test: the process's processors set at the plugin's start) | `OPTIPERF_PROCESS_AFFINITY` |
| `[Log] File` | | `OPTIPERF_LOG` |
| `[Trace] Modules` | empty (off) · `renderer` · `all` · DLL list | `OPTIPERF_TRACE` |
| `[Trace] Timing` | `1` (x86) · `0` | `OPTIPERF_TRACE_TIMING` |
| `[Trace] MarkFile` | file whose size each snapshot records (`run_bench.ps1` passes the timedemo log) | `OPTIPERF_TRACE_MARK` |
| `[Trace] InternalSites` | empty · one traced DLL's name: its calls from itself are counted per call site too (slower) | `OPTIPERF_TRACE_INTERNAL_SITES` |
| `[ShadowPrepass] Mode` | `off` · `on` · `verify` (on, plus a check of every caster call's inputs) · `compare` (renders twice, compares outputs) | `OPTIPERF_SHADOW_PREPASS` |
| `[ShadowPrepass] At` | `flush` (before the flush: the goal) · `shadows` (right before the shadow maps, for comparison) | `OPTIPERF_SHADOW_PREPASS_AT` |
| `[ShadowPrepass] Reflections` | `off` · `observe` (log only) · `on` · `verify` (on, plus a check of each replayed reflection's inputs); needs `Mode` other than `off` | `OPTIPERF_SHADOW_PREPASS_REFLECTIONS` |
| `[ShadowPrepass] FarTrees` | `off` · `on` · `verify` (on, plus generating them in the flush again to compare); needs `Reflections=on` or `verify` | `OPTIPERF_SHADOW_PREPASS_FARTREES` |
| `[ShadowPrepass] DynTextures` | `off` · `on` · `verify` (on, plus a check of each update's inputs at the flush's call); needs `Reflections=on` or `verify` | `OPTIPERF_SHADOW_PREPASS_DYNTEXTURES` |
| `[ShadowPrepass] Environment` | `off` · `on` · `verify` (on, plus each answer checked against the 3D engine's); needs `Mode` other than `off`, meant for `Reflections=on` or `verify` | `OPTIPERF_SHADOW_PREPASS_ENVIRONMENT` |
| `[ShadowPrepass] Particles` | `off` · `on` · `verify` (on, plus the flush's own `SetVertices` call recorded and compared); needs `Mode` other than `off` | `OPTIPERF_SHADOW_PREPASS_PARTICLES` |
| `[ShadowPrepass] Skinning` | `off` · `on` · `verify` (on, plus vanilla's call into scratch outputs, compared with the record); needs `Mode` other than `off` | `OPTIPERF_SHADOW_PREPASS_SKINNING` |
| `[ShadowPrepass] Materials` | `off` · `on` · `verify` (on, plus each answer checked against the 3D engine's); needs `Mode` other than `off` | `OPTIPERF_SHADOW_PREPASS_MATERIALS` |
| `[ShadowPrepass] Cvars` | `off` · `on` · `verify` (on, plus each answer checked against CrySystem's); needs `Mode` other than `off` | `OPTIPERF_SHADOW_PREPASS_CVARS` |
| `[ShadowPrepass] System` | `off` · `on` · `verify` (on, plus each answer checked against CrySystem's); needs `Mode` other than `off` | `OPTIPERF_SHADOW_PREPASS_SYSTEM` |
| `[ShadowPrepass] Lights` | `off` · `on` · `verify` (on, plus a search for pointers to the originals left, and a check that none changes during the flush); needs `Mode` other than `off` | `OPTIPERF_SHADOW_PREPASS_LIGHTS` |
| `[ShadowPrepass] EngineData` | `off` · `on` · `verify` (on, plus the same searches and checks as `Lights`); needs `Mode` other than `off`, the far trees' texture sets need `FarTrees=on` | `OPTIPERF_SHADOW_PREPASS_ENGINE_DATA` |
| `[ShadowPrepass] FlushThread` | `main` (default) · `worker` (the renderer's outer flush on a thread of its own while the main thread waits: the render thread's groundwork, no speed-up) · `overlap` (the main thread goes on beside it until its first renderer call that the render queue doesn't take: a test of the barrier version); x64, needs `Mode` other than `off` | `OPTIPERF_SHADOW_PREPASS_FLUSH_THREAD` |
| `[ShadowPrepass] FlushThreadBarrier` | `off` (default) · `on` (with `overlap`: `BeginFrame` taken, the 3D engine's next `RenderWorld` the barrier, the main thread going on into its next update beside the flush; a test, until the console's commands are gated) | `OPTIPERF_SHADOW_PREPASS_FLUSH_THREAD_BARRIER` |
| `[ShadowPrepass] FlushThreadHangExit` | `off` (default) · `on` (tests: with no flush for 20 s after the last, the game ends; the main thread's place is logged after 10 s either way) | `OPTIPERF_SHADOW_PREPASS_FLUSH_THREAD_HANG_EXIT` |
| `[ShadowPrepass] FlushThreadPlacement` | `default` (default: the OS places the main thread and the flush's worker) · `apart` (set in the package's `optiperf.ini`: each on a physical core of its own, the process's other threads kept off both; the cores among the whole ones in the process's mask, of the most performant efficiency class, in one last-level cache, not processor 0's, at least two left for the rest); with `worker` or `overlap`, the report says where the two ran at the hand-offs either way | `OPTIPERF_SHADOW_PREPASS_FLUSH_THREAD_PLACEMENT` |
| `[ShadowPrepass] Census` | `off` · `on` (a diagnostic, the pointer census, on one flush in `CensusEvery`, default 64); needs `Mode` other than `off` and `[Allocator] Owners=on` | `OPTIPERF_SHADOW_PREPASS_CENSUS`, `OPTIPERF_SHADOW_PREPASS_CENSUS_EVERY` |
| `[ShadowPrepass] CensusChanges` | `off` · `on` (with the census: which of its targets the flush writes, and which the next frame writes before the next flush) | `OPTIPERF_SHADOW_PREPASS_CENSUS_CHANGES` |
| `[ShadowPrepass] CensusReads` | `off` · `on` (with the census: every access to its targets in the census flush, not just each page's first; slow; turns `CensusChanges` on) | `OPTIPERF_SHADOW_PREPASS_CENSUS_READS` |

## Ported from Crysis MTO: what build 711 changed

Every pattern was located again in build 711 and checked unique; offsets, sizes and slots read from the matched code. Beyond those, Warhead's engine needed these changes:

- **Modules.** CryAction and CryGame are linked into `Crysis64.exe` (CryAction's code first, then CryGame's from +0x317170): the parts that look for their code (the animation update's callers and `GetClientActorId`) search the exe, checked by its time stamp and size.
- **Allocator.** Warhead's own small-block allocator is already lock-free (per-size-class free lists, a page map), but keeps the 64-bit engine's pointers cut to 32 bits: the untouched 64-bit game crashes once its memory comes from above 4 GB. The replacement also hooks `CryGetMemSize` (the original's page map would send mimalloc's blocks to the CRT's `_msize`). CrySystem's own code has the original's size lookup inlined (its GFx memory interface and an internal free), which falls through to the CRT's `_msize` and `free`: CrySystem's imports of those two (MSVCR80) are redirected at the first allocation, answering for mimalloc's blocks and passing the rest on.
- **Particle fill budget.** Build 711's `SetVertices` appends each particle container's estimate to an array of the particle manager's, which its next update sorts into each container's cap; the per-particle vertex function fades particles by the running estimate. A repeated prepare replayed from its record also appends its estimate, after the flush (the inlined append transcribed, its array grown through the 3D engine's own module allocator, found from the append's calls). A repeated prepare whose pre-run computed the vertices keeps vanilla's call: the shares Crysis MTO recorded would change the fading. For the render thread, the manager's sort of the array and the `SetVertices` calls made on the flush's thread take one lock.
- **Environment.** `I3DEngine` gained slots (52-53, 70, 121, 180-181); the flush calls a new getter, slot 52 (a float after the volumetric fog's settings), 1.1 M times on frost: the snapshot answers it too (27 getters).
- **Animation.** Build 711's controllers take a key time instead of an animation and a time: the key decoder, the tracks' searches and samples lost that argument, and the tracks' searches no longer check their cache (the controller does, inline). The caches' layouts are unchanged.
- **Interfaces and renderer fields.** `ISystem`, `ICharacterInstance` and `IEntity` gained slots, the renderer 7 (the render queue's slot map); the renderer object grew and its fields moved (the frame's render object count +0x27238, the sun +0x27248, the sky field +0x3C050, the screen block +0x3D0E8; its fields now end below +0x40000); `SRendParams`' later fields moved by 8 bytes.
- **Shadow maps left to the game.** On some levels (frost) the shadow pre-pass leaves maps to the game's own shadow pass, when the frame's render objects might run out ("no room": about 1,160 maps in frost's 8,370 frames), which Crysis 1's levels never did. Those maps' casters run the 3D engine's and the animation's code inside the flush, so with `FlushThread=overlap` the main thread's next update waits at `BeginFrame` until the flush is past its shadow pass in those frames.

## Patch notes

**Particle batch fix** (`particle_batch.cpp`, logic in `particle_batch_index.h`). For each particle container, `EF_AddParticlesToScene` looks for an earlier `CRenderObject` with identical batching state, using two linear scans over everything queued this pass. The replacement keeps the three outcomes and their side effects:

1. object already queued → return it;
2. first equal earlier object → recycle the new one, return the earlier one;
3. otherwise append.

Candidates come from a hash index keyed on exactly the compared fields:

- buckets are insertion-ordered, so "first match wins" still holds;
- floats are normalised like IEEE `==` (±0, and denormals under DAZ);
- the x64 build compares NaN as *equal* (its `ucomiss` isn't followed by a parity check), so NaN objects become wildcards there;
- both list updates go through the game's own `PodArray::Add`.

Verification:

- the differential test against Warhead's four renderers (x86 and x64, DX9 and DX10) reports 0 divergences in 2.19 M calls each, including merges, resubmissions, NaN, ±0 and DAZ;
- in-game verify mode found 0 mismatches in 521,261 calls (frost). Warhead's particle lists are short on its levels (65 on average), so the gain is smaller than in Crysis 1.

**Allocator** (`allocator.cpp`). The 64-bit engine stores some heap pointers cut to 32 bits, which breaks once memory comes from above 4 GB: the untouched 64-bit game crashed in each of our first timedemo runs on frost (34 s in once, reading through such a pointer; in the second run another time, executing at one). The replacement keeps Crysis MTO's block contract (an `int` size header at `p-4`, `allocated = size + 4`, `CryFree` returns `size + 4`) and gives 16-byte alignment. Pointers mimalloc doesn't own go back to the original functions, and `CryGetMemSize` answers for mimalloc's blocks; CrySystem's CRT imports of `_msize` and `free` are redirected for its inlined size lookups (see above).

On x64 all engine memory comes from address space reserved below 4 GB:

- free regions are found with `VirtualQuery` and the largest ones are reserved from the top;
- mimalloc may neither reserve new arenas elsewhere (`arena_reserve=0`) nor fall back to the OS (`disallow_os_alloc`);
- when the reservation fills up, more is reserved below 4 GB.

**Call tracer** (`calltrace.cpp`, `calltrace_x64.asm`).

- **Finding the vtables.** The tracer finds a module's vtables through MSVC RTTI and redirects every slot to a small thunk:
  - calls from inside the module only bump a counter, which lives on its own data page (counters next to thunk code made the game 3× slower through self-modifying-code flushes);
  - calls from other modules are counted per thread and per call site;
  - on x86 they are also timed, by swapping the return address for a stub. On x64 that would break exception unwinding, so x64 only counts.
- **Exceptions and tail calls.** Frames of calls that an exception unwound are detected and dropped (rules in the source). A tail call out of a timed call counts as a call from inside the module.
- **Output.** Once a second, changes go to `optiperf_trace_*.tsv` next to the log. `research/profiling/trace_report.py` turns them into per-frame tables, naming slots from RTTI and the Mod SDK interface layouts.
- **The flush (x64).** While the shadow pre-pass's `EF_EndEf3D` hook runs the original outer `EF_EndEf3D`, it marks the thread (`CallTrace_SetFlush`): call sites counted then carry bit 31 of their slot, and `trace_report.py --flush` keeps only those. The Phase 2 parts read vtable entries through `CallTrace_Original`, so they work with the tracer on.
- **Test.** `calltrace_test` traces a test DLL and checks every result and count:
  - calling conventions, float/64-bit/struct returns, variadic calls;
  - callbacks, SEH exceptions across traced calls, tail calls, recursion;
  - several threads at once.
  Its `diag` switch also installs the crash diagnostics, whose `ExitProcess` hook faults inside ntdll at the end of this test program's own CRT exit; the game is not affected.

**Shadow pre-pass** (`shadow_prepass.cpp`, list handling in `shadow_prepass_lists.h`). Phase 2's first step, x64 DX10 only, off by default. The renderer's flush (`EF_EndEf3D`) renders each shadow map's casters itself, calling back into the 3D engine.
- **`Mode=on`.** At the flush entry, the patch:
  - predicts which maps `PrepareDepthMap` will render, and for a point light's map which of its six sides;
  - renders their casters as its caster loop would: same `SRendParams`, renderer flags and nested recursion level. A point light's side culls them against its own camera, which the patch builds with the game's camera code;
  - moves the render items they produce into a stash (one per side), keeping their render objects alive until the flush ends.
  When `PrepareDepthMap` reaches such a map, a placeholder caster puts each side's stash back; the rest is the game's code. The geometry-shader path and maps with too few free render objects left use the original code, as do point lights if their camera code isn't found; the report counts the render objects a caster made per side in the maps rendered early, against the 4 the free-objects check assumes.
- **`Mode=verify`.** Like `on`, but spy casters compare the inputs of every `Render` call the game's loop makes with what the pre-pass used, and each point-light side's camera with the game's. Frost: 46,950 sides with identical inputs, 6.08 M caster calls, 23,096 point-light side cameras the same. On a level with water reflections the frame id with recursion differs (vanilla renders the reflections in the flush before the shadow maps, which bumps it), as in Crysis 1; with `Reflections=on|verify` every side is identical (ambush: 46,200 of 46,200).
- **`Mode=compare`.** Renders twice and compares the outputs (affected by casters with per-call state).
- **`At=shadows`.** Runs the pre-pass at the start of the shadow pass instead, for comparison.

**Reflections before the flush** (`reflection_prepass.cpp`, driven by the shadow pre-pass's `EF_EndEf3D` hook; details in Crysis MTO's `research/docs/reflections.md`). Phase 2's second step, x64 DX10 only, off by default. The flush's preprocess stage updates the ocean's reflection through `FX_DrawToRenderTarget`, which runs the 3D engine's whole scene traversal (`I3DEngine::RenderWorld`) from inside the flush.
- **`Reflections=on`.** At the flush entry, before the shadow pre-pass, the patch runs the preprocess handler on a sorted copy of the preprocess list, after doing what `EF_EndEf3D` and its list processing would have done by then. `FX_DrawToRenderTarget` runs for real there (a dry run); its traversal's nested flush is captured instead of run (items, lights, fill lights, polygon count, and the camera, fog and sky-light setters it called, recorded through a copy of the renderer's vtable). Everything the dry run changed is put back. In the flush, `FX_DrawToRenderTarget`'s `RenderWorld` call replays the capture. Panoramas, far trees, other kinds of render targets and everything after them stay in the flush; a traversal that called an unexpected renderer method is not replayed but runs in the flush.
- **`Reflections=verify`.** Like `on`, and each replayed call's arguments, renderer state and the state its flush starts from are compared with the dry run's. Ambush: 4,398 reflections replayed, all the same; the preprocess sequences predicted in 8,091 of 8,091 flushes.
- **`Reflections=observe`.** Changes nothing; predicts and logs what the preprocess stage does.

**Far trees before the flush** (also `reflection_prepass.cpp`; details in Crysis MTO's `research/docs/far-trees.md`). The flush calls the 3D engine twice for distant-vegetation sprites: `GenerateFarTrees` in the preprocess stage (the renderer's `GenerateObjSprites` builds the sprite quads from 3D-engine data and re-renders stale impostors), and `DrawFarTrees` while drawing (which decides whether `DrawObjSprites` draws them).
- **`FarTrees=on`.** The reflections' dry run also runs `GenerateFarTrees`, at its place in the stage, and the flush skips it. Right before the flush, `DrawFarTrees` runs with `DrawObjSprites` recorded instead of drawn; the flush's `DrawFarTrees` calls replay that record. A dry run stopped earlier (at a Flash or video texture's update) leaves the generation in the flush.
- **`FarTrees=verify`.** Like `on`, but the flush generates the sprites again and compares the quads with the early ones, and checks each draw decision against the 3D engine's. On Warhead's levels 1-2 lists in 3,000-8,000 differ, always in a quad's texture coordinates in its impostor's texture: the second generation can place a regenerated impostor elsewhere (with `on` there is one generation). Draw decisions are the same everywhere.
- **`DynTextures=on|verify`.** The same for Flash and video textures on in-game screens: their updates (`CFlashTextureSource`/`CVideoTextureSource::Update`, which advance and render the movie) run in the dry run, and the flush's calls are skipped; verify checks each update's inputs at the flush's call (ambush: 466 of 466 the same). Without it, such an update stops the dry run, leaving it and everything after it (far trees) in the flush.

**Environment snapshot** (`env_snapshot.cpp`, with the vtable copy of `vtable_counter.h`; details in Crysis MTO's `research/docs/environment-snapshot.md`). While drawing, the flush reads the 3D engine's environment through `I3DEngine` getters: sun, sky and fog colours, the sun's direction, the view distance, the water level, fog settings, `GetGlobalParameter` (thousands of calls per frame).
- **`Environment=on`.** Right before the flush, each of the getters is called once (27: `GetWaterLevel` without a position; `GetGlobalParameter` for each parameter, noting which components it writes; build 711's slot 52). The 3D engine's vtable pointer is then swapped for a copy whose getter slots answer from that capture until the flush returns. Some calls still go to the 3D engine:
  - calls from other threads, and positional calls at any position other than the renderer's camera position;
  - every call made after something that could change the answers ran inside the flush. That covers the 3D engine's update, its setters and a level load (their slots set a marker), and a scene traversal the reflection part didn't replay.
- **The positional calls** at the renderer's camera position are `GetWaterLevel`, `GetOceanWaterLevel`, `GetVisAreaFromPos` and the sun shafts' two queries on that vis area (`CVisArea`'s slots patched). `DebugDraw_PushFrustrum` is answered as the no-op it is.
  - **The ocean's height** (`ocean_snapshot.cpp`) moves inside the flush with the FFT ocean: the renderer steps the grid it is sampled from when it draws the ocean. So on that branch it is computed when it's asked for, from the grid as it is then. The 3D engine's `GetWave` is transcribed with the same single-precision operations, and calls MSVCR80's `floorf` through the 3D engine's import.
  - **`GetWaterLevel` there** is the higher of that height and the water volumes' share. The capture gets that share by calling `GetWaterLevel` once more with the ocean's height answered as a NaN, which its fold drops.
  - **Checks.** Both answers are checked against the 3D engine's own at each capture, and the unit test runs the game's `GetWave` against the transcription, bit for bit.

  Each getter's code is checked by pattern; one that differs is left to the 3D engine.
- **`TraceFogVolumes`** (`fog_snapshot.cpp`) takes the object's position. The capture copies what it reads: the fog volumes near the camera (their nodes), the camera box and the 3D engine's camera position. In the flush, the 3D engine's computation, transcribed from its machine code with the same single-precision operations, answers from the copies. It calls the 3D engine's ray-box test and MSVCR80's `sqrtf`/`expf` through the 3D engine's imports. Inside a replayed reflection's flush it takes the reflection's camera, where vanilla's traversal leaves the 3D engine's camera. The unit test runs the game's own code against it, bit for bit.
- **`Environment=verify`.** Like `on`, but each call also goes to the 3D engine, whose answer is compared and returned. Frost, ambush and airfield: 26.2 M, 44.8 M and 54.8 M answers identical, none different, and no snapshot out of date.

**Particle vertices before the flush** (`particle_prepass.cpp`, record and replay in `particle_record.h`; details in Crysis MTO's `research/docs/particles.md`). While drawing, the flush's `CREParticle::mfPrepare` calls the particle container's `SetVertices` (3D engine), which updates the container, queues it for the particle thread, adds fill lights, appends its estimate to the particle manager's fill budget (build 711) and writes its vertices through the renderer's allocator (`CAllocRender`, which grants min(request, buffer size) and draws the batch when it is full).
- **`Particles=on`.** Right before the flush, each particle element of the main pass's lists gets its `SetVertices` call with the context `mfPrepare` would make, a recording allocator that grants by the same rule, and the fill-light slot recorded (a vtable copy of the renderer). In the flush, `mfPrepare` runs with the vertex creator swapped for a replayer, which checks the context, adds the recorded fill lights and makes the recorded allocator calls on the real allocator, copying the recorded vertices and the indices rebased on the batch. An element prepared again (a shadow receiver) whose pre-run copied the particle thread's vertices, or was empty, gets the same replay. One whose pre-run computed the vertices replays a second pre-run, made right after the first when the container was prepared again in the last frame too. What a repeated `SetVertices` call would also do to the container and the manager (the fill budget's append, then the per-frame counter update) is applied right after the flush on the main thread, so the replay in the flush reads and writes only its records and the renderer. Elements not pre-run, repeated prepares without a second pre-run, and calls in another context get vanilla's call.
- **`Particles=verify`.** Like `on`, plus the flush's own `SetVertices` call, recorded (its per-frame counters and the budget's count put back) and compared with the pre-run's; for a repeated prepare, also the counters and the budget append it leaves against what is applied after the flush. Ambush: 1,137,636 of 1,137,637 calls the same (the one: the particle thread computed that container's vertices between the pre-run and the flush), every repeated prepare the same.

**Skinning poses before the flush** (`skin_prepass.cpp`, record and replay in `skin_record.h`; details in Crysis MTO's `research/docs/skinning.md`). While drawing a skinned batch, the flush asks the object's skinnable (a CryAnimation character or skin attachment, or a 3D-engine plant that bends) for its pose: `ISkinnable::GetSkeletonPose`, which returns pointers to its bone arrays and does that frame's animation work on the first call.
- **`Skinning=on`.** Right before the flush, each item the flush will draw skinned (level 1's lists, the shadow pre-pass's stashes, the reflections' captures; the element's own skinned test) gets one call per (skinnable, LOD, flags), the motion-blur flag predicted from the object motion-blur pass's lists. The outputs are filled with sentinels first, so the record knows which ones the call wrote. In the flush, the three classes' slot 2 (patched in their vtables) answers the renderer's calls from those records. A call that applies morph targets also gets their copy into the render mesh, made the way `MorphTargetCopy` makes it, from a copy the pre-run kept. Calls without a record keep vanilla's call (on frost, the casters of the shadow maps left to the game; the report counts those outside the game's shadow pass).
  - **The bone copies.** The answers' pointers point at copies of what the chunks drawn with the record read: the bones by the chunks' bone ids, through a skin attachment's remap table, and that table's bytes. The motion-blur pose is copied only for the calls that read it. A character's motion-blur pose is computed once more before its copy if only one record returned it, because its first computation only sizes the array.
- **`Skinning=verify`.** Like `on`, but each answered call runs vanilla's, into scratch outputs, compared with the record. The comparison also covers what its pointers point at, against the copies, and for morph targets the targets it copied and the render mesh's stream after the copy. The answer is the record's, with the copies. Frost, ambush and airfield: 644,368, 368,502 and 1,210,326 answers the same.

**Pointer census** (`census.cpp`, with the allocator's owner tags in `allocator.cpp` and `allocator_owners.cpp`; details in Crysis MTO's `research/docs/render-thread.md`). A diagnostic for the render thread: which pointers in what the flush works from lead into memory other modules own, and which of them the flush follows.
- **`Census=on`, with `[Allocator] Owners=on`.** Each engine block keeps the code that allocated it. Right before one flush in 64, the census scans for pointers into other modules' blocks, or into writable parts of their images. It scans the render objects and elements of the items the flush will draw, the level's lights, the renderer object, the renderer's statics (its writable sections), and the renderer's blocks those point at. For that flush, the target blocks' pages get `PAGE_GUARD`, and so do 1 KB after each pointer into the 3D engine's and the animation's statics (each page's protection is put back as it was). A vectored handler attributes the first access to each page (the channel, and the code that accessed it) and lets it go on. The report lists the channels, the followed ones first.
- **`CensusChanges=on`.** The census flush's targets (up to 4 KB of a block, 1 KB of a static) are also copied at the hand-off. They are compared right after the flush, then at the next flush's hand-off. For each followed channel the report gives how many targets were written in each window, at which offsets, and how many were freed or reused by then.
- **`CensusReads=on`.** Every access the flush makes to the guarded pages during the census flush (on the flush's thread) is recorded: the access is single-stepped (the trap flag) and its page guarded again after it, at most 200,000 times a census flush. The report adds, per followed channel, the 8-byte words the flush accessed, and those of them the next hand-off had written: what a render thread would read while the main thread writes it.

**Frame phases** (`frame_phases.cpp`). A diagnostic for the render thread: where the main thread's frame goes.
- **`[Debug] FramePhases=on`.** At the second flush, the game loop's four calls into CrySystem (`ISystem::RenderBegin`, `Update`, `Render`, `RenderEnd`) are patched in CSystem's vtable. With the flush hook, they cut the main thread's frame into parts: each one is timed, and the call tracer marks each call with the part it was made in (`trace_report.py --phases`). From each interval between two flushes, the report also estimates the frame with the flush on a render thread: beside the rest of the main thread's frame, or beside the next scene traversal as well.
- **`[Debug] ProducerTiming=on`.** The renderer's producer methods (`EF_GetObject`, a render mesh's `Render`, ...) go through hooks in their vtable slots, which time the main thread's outermost calls, per frame phase and apart at the flush's entry: what a replay of them would cost (the double-buffered frame). The hooks are registered with `CallTrace_SeeThrough`, so that the parts' vtable copies measure past them.

**Light snapshot** (`light_snapshot.cpp`, the copies' bookkeeping in `light_record.h`). For the render thread: the flush reads the 3D engine's dynamic lights (`CDLight`) and their shadow frustums through pointers, and the next frame's traversal moves and rewrites them.
- **`Lights=on`.** `EF_ADDDlight` is hooked to record each light the renderer is given (not a temporary on the stack: the reflection part copies those its captures keep). Right before the flush, every recorded light is copied, and so is every frustum the lights or the drawn objects point at. The lists between them (a light's frustums, a frustum's casters, an object's frustums) are copied into an arena, pointing at the copies. The renderer is pointed at the copies: its light lists, the reflections' captured lights, the drawn objects' `m_pLight` and frustum lists, its sun, light passes and current frustum, and the shadow pre-pass's stashes. After the flush, what it wrote into the copies is written into the originals, a 4-byte word at a time, except a word the original changed since the hand-off (another thread's write, which comes later in vanilla's order too), and every pointer goes back. The main thread's lights go into two records used in turn, so that those a next traversal adds while the flush runs go to the next hand-off.
- **`Lights=verify`.** Like `on`, plus a search for pointers to the originals the redirect missed, and to the copies kept after the flush. Each original is compared with a pristine copy after the flush, and the first that changed are logged with what they are. Frost, ambush, airfield: no redirect missed, no copy kept, nothing changed while the flush ran.

**Engine data** (`engine_data.cpp`, the copies' bookkeeping in `range_copies.h`). For the render thread: the rest of the 3D engine's per-frame data the flush reads through pointers, which the next frame rewrites.
- **`EngineData=on`.** Right before the flush, these are copied, each with a pristine twin, and the renderer's pointers to them are pointed at the copies:
  - the sky's parameters (`CSkyLightManager +0x1A8`), with their texel data (copied again when its time stamp or place changes). The renderer's sky field, the shader constants' static, the reflections' recorded `SetSkyLightRenderParams` and the sky elements point at the copy;
  - the ocean (`COcean`, 0x3B0 bytes) with its fog volume's vertices and indices. Its grid's and fog's render objects and the fog's `CREWaterVolume` point at it;
  - the water volumes' parameters (`CWaterVolumeRenderNode +0x50`);
  - the render elements' custom data (`CREMesh +0x10`: the terrain sectors' texture coordinates, the big decals' parameters), 1 KB from each pointer, merged where they overlap;
  - with `FarTrees=on`, the terrain texture sets the far-tree sprite quads point at.

  After the flush, what it wrote into a copy is written into the original, and every pointer goes back. The copies stay until the flush after next (two sets used in turn): a pointer still at the last flush's copy at the next hand-off is put back and logged.
- **`EngineData=verify`.** Like `on`, plus the light part's searches and checks: pointers into the originals left after the redirect, pointers into the copies kept after the flush, what the flush wrote into the copies, and whether an original changed while it ran. Its only finds are values inside copied custom-data windows that point into other blocks (the windows take 1 KB, neighbours included), which the flush doesn't follow: it reads custom data as floats by index.

**Flush thread** (`flush_thread.cpp`). For the render thread: the parts answering the flush's calls on the flush's thread, before there is a render thread to run it beside the main thread.
- **`FlushThread=worker`.** The outer flush (`EF_EndEf3D` at level 1) runs on a thread of optiperf's own while the main thread waits for it. The worker starts at the first flush, with an 8 MB stack and the main thread's priority. The flush gets the main thread's floating-point control (MXCSR) and the C runtime's random seed (msvcr80's `rand` keeps it per thread, and the renderer calls it), and both come back with it. The call tracer's flush mark and phase are set on the worker. Every part answers the flush's calls on the flush's thread, and keeps its hand-off, pre-runs and captures on the main thread. A flush still running after 5 s is logged with where the worker is (its instruction and its stack's game call chain), and messages sent to the main thread are then let through.
- **`FlushThread=overlap`.** As `worker`, but the main thread goes on beside the flush. The renderer's gate (`render_gate.cpp`) puts every slot of the renderer's RTTI vtables through a thunk. A call the main thread makes while the flush runs waits for it to end and for optiperf's after-flush steps, then goes on. The render queue (`render_queue.cpp`) takes `EnableFog` and `DeleteRenderMesh` through their slots' thunks, without patching their code, and makes them at the join in their order. It lets nothing through when the reflections aren't rendered early, or when a part's verify mode calls vanilla's code in the flush. MXCSR and the random seed go to the worker but don't come back. A stall's log names the call where the main thread waits, with its call chain. The worker also watches the main thread: with no flush for 10 s after the last, it logs where the main thread is (its call chain, the queue's state, the after-flush step in progress). The parts' steps after the flush run at the join, wherever the main thread is: the allocator's pins (`allocator.cpp`) keep what they will write from being freed meanwhile (the frees made after them), and the particles' locks are tried rather than waited for. With `FlushThreadBarrier=on`, `BeginFrame` is taken too, and the 3D engine's `RenderWorld`, through a gate thunk added after the install, is the barrier; the next update waits at `BeginFrame` until the flush can no longer run the 3D engine's code: past the reflections it renders itself, and past its shadow pass when the shadow pre-pass left it maps. What CrySystem's console calls of the renderer's without a vtable is gated too (`console_gate.cpp`): beside the flush, a cvar set (the six cvar classes' setters), a command (`ExecuteString`) and the console's `Update` with queued commands join it first.

**Material snapshot** (`material_snapshot.cpp`, copies and transcribed getters in `material_record.h`; details in Crysis MTO's `research/docs/material-snapshot.md`). For objects with material layers (frozen, wet, cloaked), the flush's layer passes, `Renderer:SetupMultiLayers` and `Renderer:DrawShader_MultiLayers`, ask the 3D engine's material objects for their sub-materials, flags and layers (about 2,300 calls per frame on frost).
- **`Materials=on`.** Right before the flush, the materials of the objects with layers among the items it will draw are copied as the getters read them: flags, sub-materials, layers, and the layers' state and shader items. The ten getters' slots in the `CMatInfo`, `CMaterialLayer` and `CMatMan` vtables are patched. They answer the two functions' calls (found by their return addresses) from the copies, on the flush's thread inside the flush, with the getters transcribed from the machine code. `GetLayer(mask, usage)` also makes the 3D engine's note in the material (its active layer), right after the flush on the main thread: nothing in the flush reads it back, and so the flush writes no material. Every other caller goes to the 3D engine.
- **`Materials=verify`.** Like `on`, plus the 3D engine's own answer for each call, compared. 18.97 M calls on frost identical.

**Cvar snapshot** (`cvar_snapshot.cpp`; details in Crysis MTO's `research/docs/cvar-snapshot.md`). The renderer reads other modules' console variables (`e_gsm_combined`, `ca_SphericalSkinning`, …) through `ICVar` pointers it keeps in statics: about 230 calls per frame into CrySystem on frost.
- **`Cvars=on`.** The getter and setter slots of the two cvar classes read there (`CXConsoleVariableIntRef`, `CXConsoleVariableString`) are patched. A call from the renderer's code on the flush's thread inside the flush is answered from that cvar's capture:
  - the cvars the renderer has asked for so far are read right before each flush (a string is copied into a buffer of optiperf's);
  - one asked for the first time is captured at that read, its one call into CrySystem;
  - a setter called inside the flush drops the captures for the rest of it.
- **`Cvars=verify`.** Like `on`, plus CrySystem's own answer for each call, compared. Frost, ambush, airfield: 1.91 M, 1.87 M and 3.68 M calls identical.

**System snapshot** (`sys_snapshot.cpp`, with the vtable copy of `vtable_counter.h`; details in Crysis MTO's `research/docs/system-snapshot.md`). While drawing, the flush reads CrySystem's getters: `ISystem::GetI3DEngine` before each 3D-engine getter, the timer's frame time, time scale and the frame's time, the view camera (about 2,800 calls per frame on frost).
- **`System=on`.** Right before the flush, the 10 getters are called once (the view camera copied); the `CSystem` and `CTimer` objects' vtable pointers are swapped for counting copies whose getter slots answer from that capture until the flush returns. The real-time clock (`GetAsyncCurTime`) is answered by CrySystem's computation, with the timer's base and frequency taken then: the timer's frame start moves the base every frame. The setters' slots (the timer's frame start, scale, pause; `SetViewCamera`) mark it out of date when the flush's thread calls them (another thread's calls, such as the next frame's beside a render thread's flush, don't); other threads and `GetAsyncTime` go to CrySystem.
- **`System=verify`.** Like `on`, plus CrySystem's own answer for each call, compared. Frost, ambush, airfield: 23.6 M, 11.2 M and 11.4 M calls identical.

## Results

Built-in CPU timedemos (the seven levels' `autotest` demos through the demo recorder's level chain), x64 DX10, 1920×1080 fullscreen, `sys_spec 4`, runs 1-5 of each, each level's two settings back to back (8 cores of an i9-13900K in a VM, an RTX 4090). The untouched 64-bit game can't keep running through a timedemo (it crashed in the first ones), so the baseline is the 64-bit game with the allocator alone.

| Level | Allocator only (fps) | optiperf, the package's settings (fps) |
|---|---|---|
| airfield | 89.3 | 116.7 (×1.31) |
| ambush | 123.8 | 144.6 (×1.17) |
| avalanche | 138.4 | 201.4 (×1.46) |
| cargo | 92.4 | 125.8 (×1.36) |
| cave | 131.5 | 138.1 (×1.05) |
| frost | 127.9 | 197.3 (×1.54) |
| train | 113.0 | 148.6 (×1.32) |

×1.31 on average. The render thread is most of it; the frame's end on the render thread (`FlushThreadPresent`) added about 7 % on frost (not measured back to back). The traversal record (`FlushThreadRecord=beside`) stays off: in Warhead the main thread waits for the flush only 0.2-1.5 ms a frame at the barrier, less than recording its traversal costs it.

## Layout

| Path | |
|---|---|
| `optiperf/dllmain.cpp` | plugin entry: config, log, diagnostics, loader notification, dispatch to patches, reporter thread, `InitializeASI` |
| `optiperf/scan.*` | pattern search in module code; a class's vtable by its RTTI name |
| `optiperf/hooks.*` | MinHook wrappers (all-or-nothing install) |
| `optiperf/config.*` | `optiperf.ini` + environment overrides |
| `optiperf/particle_batch*`, `optiperf/allocator.*`, `optiperf/diag.*` | the patches |
| `optiperf/calltrace.*`, `optiperf/calltrace_x64.asm` | the call tracer (developer tool) |
| `optiperf/shadow_prepass*` | the shadow pre-pass (Phase 2); list handling in `shadow_prepass_lists.h` |
| `optiperf/reflection_prepass.*` | reflections and far trees before the flush (Phase 2) |
| `optiperf/env_snapshot.*`, `optiperf/vtable_counter.h` | the environment snapshot (Phase 2); the counting vtable copy it, the reflections, the particles and the system snapshot use |
| `optiperf/fog_snapshot.*` | `TraceFogVolumes` over copies of the fog volumes, transcribed from the 3D engine (for the environment snapshot) |
| `optiperf/ocean_snapshot.*` | the ocean's height at a position from the renderer's FFT grid, `COcean::GetWave` transcribed from the 3D engine (for the environment snapshot) |
| `optiperf/material_snapshot.*`, `optiperf/material_record.h` | the material snapshot (Phase 2) |
| `optiperf/key_index.h` | the per-flush record index the skinning and material parts use |
| `optiperf/particle_prepass.*`, `optiperf/particle_record.h` | particle vertices before the flush (Phase 2), with build 711's fill budget |
| `optiperf/skin_prepass.*`, `optiperf/skin_record.h` | skinning poses before the flush (Phase 2) |
| `optiperf/sys_snapshot.*` | the system snapshot (Phase 2) |
| `optiperf/cvar_snapshot.*` | the cvar snapshot (Phase 2) |
| `optiperf/census.*` | the pointer census (a diagnostic for the render thread) |
| `optiperf/frame_phases.*` | the main thread's frame timed by phase (a diagnostic for the render thread) |
| `optiperf/light_snapshot.*`, `optiperf/light_record.h` | the lights and shadow frustums copied for the flush (the render thread's groundwork) |
| `optiperf/engine_data.*`, `optiperf/range_copies.h` | the 3D engine's other per-frame data copied for the flush: sky, ocean, water volumes, custom data, terrain texture sets (the render thread's groundwork) |
| `optiperf/flush_thread.*` | the outer flush on a worker thread while the main thread waits, or beside it (the render thread's groundwork) |
| `optiperf/render_gate.*` | the renderer's gate: every vtable slot through a thunk that makes the main thread's calls wait for a flush running beside it |
| `optiperf/render_queue.*` | the main thread's renderer calls taken beside the flush and made when it ends |
| `optiperf/traversal_record.*` | the double-buffered frame: the main thread's producer calls recorded beside the flush (`FlushThreadRecord=beside`) |
| `optiperf/particle_update.*`, `optiperf/particle_update_x64.asm` | the particle emitters' update on worker threads |
| `optiperf/anim_update.*` | the characters' forward kinematics on worker threads |
| `optiperf/allocator_owners.cpp` | the mimalloc block an address is in, for the allocator's owner tags (reads mimalloc's internal layout) |
| `launcher/` | development injector |
| `tests/` | differential test, allocator test, call tracer test (with its test DLL), shadow pre-pass test |
| `dist/` | files that ship in the package |
