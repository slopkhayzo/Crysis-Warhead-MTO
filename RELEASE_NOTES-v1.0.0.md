# Crysis Warhead MTO v1.0.0

## What it is

The first release of Crysis MTO's CPU-multithreading mod for Crysis Warhead, ported to its engine build (711):

- The render thread: the renderer's main pass, the HUD and the present run on a thread of their own while the game
  works on its next frame. Before handing a frame over, the mod prepares, copies or records what the renderer would
  otherwise ask the rest of the engine for while drawing (shadow casters, water reflections, far trees, particles'
  vertices, characters' poses, lights, environment, materials, console variables, the sky and the ocean).
- The game's main thread and the render thread on a physical core each, the game's other threads kept off both.
- The particle emitters' update and the characters' animation on worker threads.
- The particle fix (a quadratic search in the renderer's particle batching) and the allocator (mimalloc), which also
  keeps the 64-bit game running: the untouched 64-bit game crashes once the engine's memory comes from above 4 GB.

Everything is on in the supplied `optiperf.ini`, and each part can be turned off there on its own.

What Warhead's engine needed beyond Crysis MTO's code: CryAction and CryGame live in `Crysis64.exe`; CrySystem's own
copies of the original allocator's size lookup (its CRT imports redirected); the particles' fill budget, new in build
711 (replayed after the render thread's flush, under a lock beside it); the 3D engine's random numbers, which build
711's particle emitters draw (one at a time on the workers); the object motion-blur pass's third list (the skinned
poses it asks for); the animation controllers' new key-time signatures; and the shadow maps the game renders itself
when a frame's render objects might run out (the next frame's update waits for them).

## Release archives

- `Crysis-Warhead-MTO-v1.0.0-plugin-only.zip` contains the plugin and its configuration for both games (`Bin32`,
  `Bin64`), the project license, documentation, the notices of the libraries linked into the plugin, and payload
  hashes, for installations that already have an ASI loader.
  SHA-256:
  `E124F2471B2F3F78ABA053944767852C95616CE16672D35F6C67B56BF9FBBD49`
- `Crysis-Warhead-MTO-v1.0.0-with-Ultimate-ASI-Loader-v9.7.4.zip` additionally contains the pinned loader for both
  games (as `version.dll`) and its MIT notice.
  SHA-256:
  `635C301D85F40DC4AC0E64190BD3AEDC313EFE8DB5E58809BAD2A696E555400C`

## Validation

- Reference build: the Steam release of Crysis Warhead 1.1.1.711; the GOG release's files are the same, byte for byte
  (it has no `Bin32`, and its `Bin64\Crysis.exe`, which Galaxy starts, is a copy of `Crysis64.exe`). The SHA-256 of
  the reference build's files whose code or tables the supplied settings change:

  | File | SHA-256 |
  | --- | --- |
  | `Bin32\CrySystem.dll` | `A3FBD87D16FEE0F8BABA053A103B94F310BC0E4C8A3506B0E29570501EF1E539` |
  | `Bin32\CryRenderD3D9.dll` | `5ACFD6574A201E5744ECE66E8B89F1C82F90FFA78352796896D6F254E19A0EDC` |
  | `Bin32\CryRenderD3D10.dll` | `0B46D9B335191AB33D05AE578954FEE93C7E7D142C471BF59A526ECC9DFCCE67` |
  | `Bin64\CrySystem.dll` | `03D1563FB1747F0A446116A24E376B9A577EEB80B3A254A963B041C4715E395E` |
  | `Bin64\CryRenderD3D9.dll` | `8749768D044F2F38567272036615A883DCE881C5B753BCB0E22F0A62E9A09825` |
  | `Bin64\CryRenderD3D10.dll` | `A3AE2603258B755CB86A648802636B06DC992FBE792E2CE10B5ED7486E5B6F8B` |
  | `Bin64\Cry3DEngine.dll` | `49E3A4E0C70BEEA45CB5E8216B5BB7B0068F48DB5FDDCBD620C8B19DD58C7852` |
  | `Bin64\CryAnimation.dll` | `52D1AFB2A77FE1F17FF531B87AEF4C352BC54FF9A5171ABE73A948DA96704028` |
  | `Bin64\CryEntitySystem.dll` | `F47DBB872E5DB453F824EF555D96E8AA765D941796CC4437D489C7BAD8C16B12` |
  | `Bin64\CryPhysics.dll` | `BF0C38C355CB6BA9D4820A50A7336700534D13012D32D0AEB058FA0E76E1175D` |
  | `Bin64\CrySoundSystem.dll` | `F0432F50C7E4935805132CB29BF5EB60E36106F61CB4CE8BE44812C95E271A73` |

  These identify the tested build; they are not checked. The plugin finds the code it changes by byte pattern and
  leaves off any part whose code differs.
- `optiperf.asi` SHA-256:
  - `Bin32`: `A51D382D9C0BAA6286504483119875D63ECA7E8CCD2F3E88BF86630148D9C7DD`
  - `Bin64`: `D31784CC82FB1BCD18B5233A3448F82CE7014399E7B3B60D59F611473B48E1A9`
- Ultimate ASI Loader v9.7.4 SHA-256 (the release's `dinput8.dll`, bundled as `version.dll`):
  - x86: `EC2F4824ECA58DD40F425756A4A7CEC77B8E381F21D11D7C846EC4B339B617AB`
  - x64: `031A3E5576D91DCE1E438D36B9A3D462C7334AB4791990A8FF1E3DDC0E132DAF`
- Compilation with warnings enabled, and the allocator, call tracer and shadow pre-pass tests passed; the particle
  fix's differential test against Warhead's four renderers (x86 and x64, DX9 and DX10): 0 divergences in 2.19 M calls
  each.
- Every part that prepares the render thread's work, in its verify mode on the seven levels (six with the render
  thread beside the game, airfield with the main pass on the main thread): the same as the game's own code (the shadow
  casters' inputs, the reflections, the environment, the particles, the skinned poses, the materials, the console
  variables, the system's state, the lights and the engine data). The animation on workers in verify mode (ambush,
  cargo): every pose the same as the game's.
- The game's own benchmarks of its seven levels (64-bit DX10, 1080p Very High, 8 cores of an i9-13900K in a VM, an
  RTX 4090), the supplied settings against the same game with only the allocator, back to back:

  | Level | Allocator only | v1.0.0 | |
  | --- | ---: | ---: | ---: |
  | frost | 127.9 fps | 197.3 fps | x1.54 |
  | avalanche | 138.4 fps | 201.4 fps | x1.46 |
  | cargo | 92.4 fps | 125.8 fps | x1.36 |
  | train | 113.0 fps | 148.6 fps | x1.32 |
  | airfield | 89.3 fps | 116.7 fps | x1.31 |
  | ambush | 123.8 fps | 144.6 fps | x1.17 |
  | cave | 131.5 fps | 138.1 fps | x1.05 |

- The release package was installed into a fresh copy of the game's binaries and started through Ultimate ASI
  Loader, without the development launcher, in 15 timedemo launches with the HUD on, none with an exception:
  - the 64-bit game with DX10 ran the render thread, the two threads' cores and both worker parts on frost, ambush,
    airfield, cave and avalanche, and with the game's frame rate display on (`r_DisplayInfo 1`);
  - DX9 (`r_Driver = "DX9"` in the game folder's `system.cfg`: the 64-bit game ignores `-dx9`) kept the render
    thread and the worker threads off by itself, with the particle fix and the allocator on (frost, ambush);
  - with the game limited to fewer processors (frost's benchmark, against the allocator alone on the same
    processors): 4 cores with both their threads 188 against 141 fps (x1.34), one thread on each of 4 cores 191
    against 132 (x1.45), 2 cores 175 against 132 (x1.32). The thread placement stayed off by itself on 2 cores.
- With the HUD on and the supplied settings, every level's benchmark played through (ambush three times). The
  benchmarks in the table ran with the HUD off, as the game's timedemos set it: with the HUD on, the first frame
  of a level with water renders its reflection inside the render thread's frame (a Scaleform call makes the early
  capture unsafe), and the game's next frame now waits for it there.
- On the GOG release (its own copy of the install, through Ultimate ASI Loader, `Bin64\Crysis.exe` started as Galaxy
  starts it: with the Play button's `-dx9`, and without, as its DX10 task; the HUD on): frost, ambush and cave played
  through with every part on (the `-dx9` has no effect: DX10), frost 207 fps, ambush 141, cave 183.
- Two short play sessions at the start of the first mission (64-bit DX10, 1080p Very High, the same machine;
  about 2 minutes of play each), against the same game with only the allocator, which the 64-bit game needs to
  keep running:

  | | Allocator only | v1.0.0 |
  | --- | ---: | ---: |
  | Average | 109 fps | 185 fps |
  | 1 % low | 72 fps | 109 fps |
  | 0.1 % low | 54 fps | 68 fps |
  | Time under 100 fps | 44 % | 2.1 % |

  The scripted benchmarks show less, x1.31 on average over the seven levels: play has more of the frames that the
  main thread holds back. The release package (through Ultimate ASI Loader) played without a problem.
- The two archives contain the same `optiperf.asi`, `optiperf.ini` and loader files, byte for byte, as the
  package these tests ran.
- Not tested: the 32-bit game (Steam's release runs it only through Steam, which starts the 64-bit one), other
  releases than Steam's, and mods that replace game code.
