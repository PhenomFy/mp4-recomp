# Metroid Prime 4 Recomp — native Linux build (AppImage)

A native PC build of **your own copy** of *Metroid Prime 4: Beyond* for Linux, packaged as a single AppImage. It is built
on the SwitchRecomp runtime by MZ Labs: the game's Switch code is recompiled ahead of time into a native Linux library on your machine, and the
Switch GPU's work is translated to OpenGL 4.6.

This release is the result of a long stretch of play-testing and fixing: the game boots, plays, scans, sounds right and
runs at a steady 60 fps on the test machine, with most of the rendering bugs and stutters found along the way fixed.
Everything that changed is listed below.

> **No Nintendo files are included.** No game code, no RomFS, no keys, no generated C. You need your own legally
> obtained, dumped copy of the game. The AppImage holds the runtime, the recompiler and a bundled GCC and Python; it
> compiles the game from your files during the first-run setup. The only game-related file inside is a manifest of
> RomFS file names, sizes and SHA-1 hashes, used to verify your dump.

---

## Contents

- [Requirements](#requirements)
- [Install and play](#install-and-play)
- [Saves](#saves)
- [Controls and the in-game menu](#controls-and-the-in-game-menu)
- [Performance on the test machine](#performance-on-the-test-machine)
- [What changed](#what-changed)
  - [Rendering and visual fixes](#rendering-and-visual-fixes)
  - [Stutter and performance](#stutter-and-performance)
  - [Shaders](#shaders)
  - [Gameplay fixes](#gameplay-fixes)
  - [Audio](#audio)
  - [Video playback](#video-playback)
  - [Boot, setup and launcher](#boot-setup-and-launcher)
  - [Diagnostics and testing tools](#diagnostics-and-testing-tools)
- [Known issues](#known-issues)
- [Troubleshooting](#troubleshooting)
- [Advanced switches](#advanced-switches)
- [Building from source](#building-from-source)
- [Credits](#credits)
- [Licences](#licences)

---

## Requirements

- **Linux, any x86-64 CPU** (Steam Deck included), glibc 2.38 or newer. X11, or Wayland through XWayland.
- **GPU:** an OpenGL 4.3+ driver (Mesa or NVIDIA). Developed and tested on an **AMD Radeon RX 9070 XT** with Mesa 26.2
  (radeonsi).
- **SDL2** (controllers and audio) and **zlib**, which most distributions already ship.
- **For the game's videos:** OpenH264 2.x (`openh264` / `libopenh264`), or FFmpeg's `libavcodec` (faster, preferred when
  present). VA-API hardware decoding is used when available. Without any of these, videos stay black.
- **Your game dump:** *Metroid Prime 4: Beyond* for Switch, program `010019A01E2F2000`, with the update whose main has build
  ID `8275FBE1EC5191C279F3A943AD8524F4BAB087A3` (the cartridge alone is build `E860CBED…` and is refused). Either:
  - the **files** from your Switch: the game as `.xci` (cartridge) or `.nsp` (eShop) and the update as `.nsp` (DBI or
    nxdumptool), plus your Switch's **`prod.keys`** (Lockpick_RCM) in `~/.switch/`, or where Ryujinx or yuzu keep it.
    Setup decrypts them with those keys and applies the update to the game; or
  - an **extracted** ExeFS (`main`, `main.npdm`, `rtld`, `sdk`, `subsdk*`) and RomFS (`Worlds/`, `UI/`, `Preload/`,
    `Kernel.pak`, …) with the update applied.

  Setup checks the build ID and verifies all 445 RomFS files (26.9 GB) against the manifest.
- About 30 GB of free disk space for the installed game data, plus space for shader caches (from `.xci` / `.nsp` files the
  same: the unpacked files become the installed ones).

## Install and play

1. Download `Metroid_Prime_4_Recomp-x86_64.AppImage` from the release and make it executable:

   ```bash
   chmod +x Metroid_Prime_4_Recomp-x86_64.AppImage
   ```

2. Run it:

   ```bash
   ./Metroid_Prime_4_Recomp-x86_64.AppImage
   ```

3. **First run:** the setup screen asks for your game: its `.xci` / `.nsp` file and the update's `.nsp`, or an extracted
   game folder (it can hold `exefs` and `romfs` folders, or the files directly) and an update folder if you extracted the
   update separately. Setup verifies the files, copies them
   into the install folder and compiles the game with the bundled GCC (a few minutes on a fast CPU).
4. Press **Play**. The first launch builds shaders in the background; later launches load them from the cache.

Everything is installed to `~/.local/share/MetroidPrime4Recomp` (set `MP4_RECOMP_HOME` to use another folder). An
install from an earlier build in `~/.local/share/MetroidBeyondRecomp` is moved there on the first start, saves and
shader caches included.
Settings are in `Metroid Prime 4 Beyond.ini` there, rewritten with every option explained on the first start.

**Updates:** the Play screen checks this project's latest release when it opens (its UPDATES section, further down, also has
a CHECK button and the setting to turn it off). When there is a newer build it offers UPDATE: the new AppImage is downloaded next to the one
you started, checked against its SHA-256 and put in its place, and the launcher starts again; the previous AppImage is
kept as `<name>.previous` (UNDO puts it back). If the update changed the recompiler, the Play screen asks to rebuild
the game code (a few minutes). Downloads use the system's libcurl (the `curl` package). With `skip_launcher=1` the
check happens when you open the Play screen (`--launcher`). Builds before 2026-11-04 have no updater: download that
release once by hand.

## Saves

Saves are plain files in:

```
~/.local/share/MetroidPrime4Recomp/Metroid Prime 4 Beyond_data/save/user/
```

**Using your Switch save:** export it with a save manager on a homebrew-enabled Switch (JKSV or Checkpoint), close the
game, back up the existing `save.bin`, and copy the exported files into that folder. A save from a newer game update
than the one this build targets may not load; restore your backup if it doesn't. Nintendo's cloud saves can't be
downloaded to a PC.

## Controls and the in-game menu

- Controllers work through SDL2 (tested with an Xbox Series X controller; **ZL is the left trigger**).
- Keyboard: WASD to move, **1** = ZL, **3** = ZR.
- **F1** or the Home / Guide button opens the in-game menu (settings, cheats, FOV, upscaling).
- **F3** saves a screenshot to `captures/`, **F4** toggles the 60 fps limit, **F5** writes a frame trace for bug
  reports, **F11** toggles fullscreen.

## Performance on the test machine

AMD Radeon RX 9070 XT, Mesa 26.2 radeonsi, warm shader caches, measured with MangoHud over real play sessions (frame
times after the save had loaded):

| Session | Play time | Average | Frames over 100 ms |
|---|---|---|---|
| Before this round of fixes | 5 min | 59.5 fps | 6 |
| Mid-way | 57 min | 59.8 fps | 30 (24 of them in one newly visited area) |
| This release | 5.6 min | 59.9 fps, 1% low 43 fps | 2 (both right after the save loaded) |

Loading a save still costs one freeze of about 0.5 s (the area's lighting volumes, 1.2 GB, are uploaded).

---

## What changed

Every fix below was found by playing, reproduced (most of them by replaying the player's recorded controller input
from the same save), traced to a root cause and checked again in the game.

### Rendering and visual fixes

- **Black arm cannon and black enemies after a cutscene.** The game keeps its lighting in six huge 3D "brick" volumes
  (1408×256×1008, 173–346 MB each) and streams new lighting into them as you move. The code that detects those updates
  only worked when a volume sat in one contiguous block of host memory, which these volumes never do, so no update was
  ever picked up. A new area (for example after the Fury Green cutscene) then had no lighting data, and the cannon and
  enemies, which take their ambient light from these volumes, went black. Volumes are now checked and uploaded page by
  page. Cost: about 0.06 ms per frame.
- **Lighting volumes update in slabs.** Each volume is handled as 63 slabs of 16 slices that are checked in rotation, and
  only the slabs that changed are uploaded. Before, every change re-uploaded the whole volume (54–138 ms, in bursts of
  six), and the world showed dark boxes until the next re-upload.
- **Magenta floor and suit at every spawn.** The game streams a texture's largest mip level in after the texture is
  already on screen, and marks that level as missing in the texture descriptor until it arrives. We ignored that field
  and sampled mip 0, whose memory still held leftover data: magenta speckle on the floor and Samus's heel fins for about
  half a second at each spawn. The sampler's minimum level now follows the descriptor's lowest resident mip, and when
  that value drops (the level has just arrived) the texture is re-checked immediately. The rainbow-coloured screen
  reported after opening a door was very likely the same bug.
- **Speckled textures on tree trunks** (a bug introduced and removed in this round): a mapped upload buffer for decoded
  ASTC textures could hand a texture stale data. It also turned out to have no measurable benefit, so it was removed.
- **Black cannon while blending reflections.** Moving between lighting zones makes the game blend two reflection cube
  maps on the GPU, drawing all six faces at once per mip level. The renderer had no path for a cube map built from render
  targets drawn that way and handed reflection shaders a flat image. Cube maps and arrays can now be assembled from such
  targets, including every mip level.
- **Dark box around the morph ball.** The fog effect reads a per-light texture the renderer can't look up yet; it got a
  black texture, which reads as fully shadowed. It now gets white ("no shadow").
- **Cannon clipping at the near plane.** The game enables depth clamping on every draw; it was ignored, so geometry
  right at the near plane (the first-person cannon, shadow-tile resets) was cut away. Depth clamping is now honoured.
- **The sun shining through trees and walls.** The lens flare fades by asking the GPU how many pixels of a test shape at
  the sun's position are visible. The emulator always answered "1000". Occlusion queries are now measured on the GPU
  and returned a frame or two later, without stalling.
- **Reflection and lighting cube maps rendered onto one face.** The game renders them with geometry shaders, which the
  translator didn't have, so all six faces landed on one. Added a geometry stage (per-vertex inputs, emits,
  passthrough), a third pipeline stage, and the `VMAD` and `VSETP` instructions.
- **Black rock faces, mountains and sky.** Rock materials pick UV sets and tangent frames with indexed attribute reads
  (`IPA.IDX`), which returned 0. Also fixed on the way: `texelFetch` from render targets ignored the resolution scale,
  texture lookups now take the render target drawn last when several share an address (dynamic resolution), flow
  instructions honour their condition-code test, `ISET.CC` sets the flags, and `FMNMX` / saturate follow Maxwell's NaN
  rules.
- **Hard-edged black boxes when looking up.** NaN pixels spread through the blur and downsample passes. Pixel shader
  outputs now have NaN replaced by 0, as the hardware effectively does.
- **Neon green/orange gradients and a flat-coloured cannon.** The fog/light froxel volume is filled each frame by a vertex
  shader writing through image stores (`SUST`), which the translator lacked, and the light list is built by a vertex
  shader writing a global buffer that later draws read back from stale memory. Image stores are now supported in vertex
  and pixel shaders, and shader-written buffers stay on the GPU between draws.

### Stutter and performance

- **Gameplay at 60 fps instead of 30–40.** Every draw re-uploaded the tail of the vertex buffer it drew from (50–60 MB a
  frame). Draws into one buffer now share one cached region: 4–5 MB a frame.
- **Hitches when turning (100–300 ms).** ASTC textures streamed in as the camera turned were decoded synchronously on
  the GPU thread (this GPU has no ASTC). They now decode on background threads: a texture shows from its small mip
  levels at once and gets the large ones a frame or two later. Streamed ASTC texture arrays decode in the background too.
- **Lighting-volume uploads about 3.5× faster.** The 173–363 MB volumes upload through a persistently mapped buffer and
  are read from guest memory in parallel.
- **Fewer forced waits on the GL thread.** Removed round trips that stopped the renderer until the GL thread caught up:
  an error check after every texture view, two uniform-location lookups, and `glGenerateMipmap`, which was wrongly
  synchronous.
- **Auto-exposure readback no longer stalls.** It is read asynchronously, and the choice is remembered across sessions,
  instead of running synchronously for 30 frames after every load. Small readbacks the game needs immediately (the scan
  visor's counters) still return the same frame.
- **Driver submits cut from 125 to 15 per frame.** Bloom and exposure mip chains were recopied in full each time one more
  level was drawn (about 128 copies a frame). Only changed levels are copied now.
- **No `glFlush` per occlusion-query report.**
- **A slow frame every 5 seconds.** The background shader scan reread all of the game's GPU memory (about 300 MB) every
  5 s, pushing the game's data out of the CPU cache, and on Linux it ran at full priority. It now rereads only buffers
  that changed or hold shaders (about 70 MB, 24 ms instead of 110 ms on the test machine), with a full pass about once a
  minute or when a draw needs a shader it missed, and background threads run at a lower priority on Linux.
- **Default resolution 1×.** The launcher's auto-settings no longer pick 2× on high-end cards.
- **VSync on by default** on Linux: frames reaching the monitor wrong fell from 59 to 1–2 of 3600.

### Shaders

- **Shaders built before the game draws with them.** The game unpacks its shaders into GPU memory when an area loads; a
  scanner thread finds them, the GL thread translates a few per frame, and compile threads build them so they're ready
  before the first draw.
- **The right variant is pre-built.** Some shaders need a different version depending on what they're drawn with
  (pixel shaders reading integer textures such as the light grid; vertex shaders with integer inputs such as bone
  indices). The pre-build now predicts the variant from how the shader uses each input (right for 46 of 48 textures and
  all 132 vertex inputs in the test area). First visit to an area: 109 → 40 compiles during play (4.3 s → 2.0 s).
  Second visit: **0**.
- **No more freezes from indirect-branch shaders.** Shaders with an indirect branch (`BRX`) take their jump table from
  constant memory, which the pre-build doesn't have, so the first draw compiled them on the spot (50–160 ms each; seven
  in a row froze the game for 850 ms in a new area). The draw now uses the pre-built version, which is just as correct,
  while the exact one compiles in the background and replaces it. On a cold-cache test route: 1.48 s → 0.36 s of
  compiles.
- **A draw joins a compile already in progress** instead of starting a second one, and the translated-shader cache loads
  again instead of being rebuilt every launch.
- **The scanner wakes up early** when a shader is drawn before it was found, so shaders an area loads over the next
  seconds are found before they're drawn.
- **Several compile threads with their own shared OpenGL contexts** (Mesa and NVIDIA compile GLSL on the calling
  thread), and larger driver shader caches.
- **Translator additions:** `AL2P` and indexed attribute loads, `LDG`/`STG` global memory through any constant buffer,
  `VMNMX`, `VMAD`, `VSETP`, image stores in vertex and pixel shaders, up to 11 global buffers per shader (was 8), carry
  flags declared where they're read.

### Gameplay fixes

- **The scan visor works** (it was needed to open the first door). Several bugs stood in the way:
  - the depth-stencil buffer the scan reads was given to the shader as plain depth, so no pixel ever matched a target;
    stencil reads now get a stencil view;
  - the game copies its depth-stencil buffer with the 2D copy engine; the copy fell back to stale CPU memory and
    required an exact size match, which dynamic resolution broke;
  - a decoder bug in the instruction that works out which target a pixel belongs to;
  - the scan counter's write-back copied 7.5 KB of GPU data over live game memory every frame, rolling it back; only the
    words the GPU actually changed are written back now.
- **ZL lock-on** confirmed working (left trigger past about a quarter).

### Audio

- **Missing sound.** The game sends almost every sound through **splitters**, which the mixer didn't support: most
  sounds went to the final mix at zero gain and skipped the reverb and filter chains. Splitters are implemented, with
  their state kept between updates.
- **Missing cutscene voices.** The game mixes in 5.1, with dialogue in the centre channel, which the stereo output
  dropped. Surround downmix is now on by default, as on a console with stereo output.
- **Sound at real speed.** The runtime mixed 5 ms of audio per renderer update; the game updates 60 times a second, so
  it got 0.3 s of sound per second. Each update now mixes what real time needs.
- **Broken sound after a menu transition** (the game updating 20,000 times a second for half a second): fixed.
- Audio renderer REV15 voice layout supported.

### Video playback

- **Intro video decoded on Linux** (it showed as garbled noise): the system's OpenH264 is loaded; FFmpeg and VA-API are
  used when present. One decoder per NVDEC channel; decoder addresses kept for the buffer's lifetime.
- **Smooth video:** picture conversion on up to 8 threads with AVX2 (9 ms → 0.4 ms per 1080p picture).
- **Correct pictures in order:** decoded pictures are filed under their real surface (`CurrPicIdx`), which fixed the
  judder, the title logo flashing into the intro and the static.

### Boot, setup and launcher

- Prime 4 setup: title and build-ID checks, RomFS layout and manifest verification.
- `fsp-srv` 205 (`OpenDataStorageWithProgramIndex`): the game mounts its RomFS.
- HID: player 1 reports a single controller style, fixing the six-axis abort at boot.
- The Play screen checks for Prime 4's data and shows Prime 4's titles.
- Setup takes the game's `.xci` / `.nsp` and the update's `.nsp` directly, decrypted with your own `prod.keys`, and applies
  the update's RomFS patch (BKTR) itself: no emulator needed to extract them.
- The AppImage runs on any x86-64 CPU: its program is built with the bundled toolchain instead of the build
  machine's, which had made it need AVX-512 and glibc 2.43 ("CPU ISA level is lower than required" on a Steam Deck).
- Cheats in Atmosphere / dmnt format, toggled per cheat in the in-game menu.
- AMD FidelityFX FSR 1 upscaling with a sharpness slider.

### Diagnostics and testing tools

- **Input recording and replay:** every session records controller input to `input_record.csv`; `SWR_TEST_SCRIPT`
  replays it frame by frame, with `SWR_TEST_CAPTURE_FRAMES` / `SWR_TEST_CAPTURE_PREFIX` for frame captures and
  `SWR_TEST_EXIT_FRAME` to stop. This is how most bugs above were reproduced without the player.
- **Frame traces** (F5, or `SWR_TRACE=<frame>`): every draw with its shaders, textures (including the raw descriptor
  words), buffers and a render-target snapshot.
- **`SWR_SHADER_OVERRIDE_DIR=dir`** compiles `dir/<hash>_vs.glsl` (or `_fs`, `_gs`) instead of the translation, to test a
  change to one shader; **`SWR_TRACE_RAW=address`** writes a traced texture as floats and its guest memory next to the
  trace; **`SWR_TRACE_DMA_RANGE=address:size`** logs the copy-engine and 2D-engine writes into a range.
- **`SWR_DBG_FS=hash@anchor@expr`** shows a pixel shader's intermediate value on screen.
- **First-draw shader log** says why each shader wasn't pre-built ("not found in GPU memory yet", "found 2.1 s ago, 66
  ahead of it", "translated without constant values", …).
- **`SWR_STALL_STACK=1`** logs the GL thread's stack whenever one call runs past 15 ms. It's how the remaining driver
  hitches were traced.
- Slow-frame, present, video and audio timing logs; the last five logs are kept.

---

## Known issues

- **Driver shader-variant compiles (AMD/radeonsi).** The first time a shader program is drawn, radeonsi builds the
  machine-code variant that draw needs, on the thread that runs the draw, 25–30 ms for big shaders, and doesn't keep it
  in its disk cache. This is now the main source of remaining hitches: a few per area, mostly as it loads in. It often
  surfaces in the log as a slow `glTexSubImage2D`, but the upload isn't the cause. Not fixable from the game side so
  far.
- **First visit to a new area** can still compile a handful of shaders the game loads moments before drawing them.
- **Loading a save** freezes for about 0.5 s while the lighting volumes upload.
- **The FOV slider** has no effect yet (the camera address for this build hasn't been found).
- **Not modelled yet:** a filter effect on two audio submixes and an aux effect, so a few sounds may be clearer than
  intended.
- **Windows** is inherited from the upstream project and untested.
- The rainbow screen after opening a door is believed fixed (see above) but wasn't reproduced directly.

## Troubleshooting

- The log is `~/.local/share/MetroidPrime4Recomp/Metroid Prime 4 Beyond.log` (the last five are kept). Attach it to bug
  reports.
- Screenshots with F3; a frame trace with F5 at the moment something looks wrong helps most.
- Black videos: install OpenH264 or FFmpeg.
- Rendering glitches: delete `~/.local/share/MetroidPrime4Recomp/shader_cache` once to rule out a stale cache (the
  next launch rebuilds it).

## Advanced switches

Environment variables for testing and for turning individual fixes off if one ever misbehaves:

| Variable | Effect |
|---|---|
| `SWR_NO_TIC_MINMIP=1` | ignore the texture descriptor's lowest resident mip |
| `SWR_NO_RMIN_RECHECK=1` | don't re-check a texture when more of its mips become resident |
| `SWR_NO_SLABS=1` | upload lighting volumes whole instead of by slab |
| `SWR_NO_BRX_STANDIN=1` | compile indirect-branch shaders at the draw instead of using the pre-built stand-in |
| `SWR_NO_SCAN_WAKE=1` | scanner keeps its fixed schedule |
| `SWR_NO_MEM_PREBUILD=1` | turn the shader pre-build off |
| `SWR_NO_PREBUILD_INTTEX=1` | pre-build plain variants only |
| `SWR_NO_NAN_GUARD=1` | keep NaN pixel outputs |
| `SWR_ASTC_SYNC=1` | decode ASTC on the GPU thread |
| `SWR_NO_GPU_SSBO=1` | read shader-written buffers from guest memory |
| `SWR_SYNC_SHADERS=1` | draws wait for their shaders (the launcher default, `wait_for_shaders=1`) |
| `SWR_STALL_STACK=1` | log the GL thread's stack on calls over 15 ms |

## Building from source

The source for the runtime, recompiler and AppImage packaging is in this repository (no Nintendo files: no game code,
no RomFS, no keys, no generated C). Developer notes, the bring-up log and more build options are in
[DEVELOPMENT.md](DEVELOPMENT.md).

```bash
bash linux/install_deps.sh
bash linux/setup_and_build_runtime.sh
bash linux/make_appimage.sh
```

`make_appimage.sh` produces `linux/dist/Metroid_Prime_4_Recomp-x86_64.AppImage` and `update-linux.txt` (its version, the
UTC build time, its SHA-256 and the ID of the recompiler it brings); `bash linux/publish_release.sh [TAG]` uploads both
to the latest release (or TAG), which is what the launcher's updater reads. To run from the source tree without
the AppImage instead: `bash linux/setup_game.sh GAME_FOLDER [UPDATE_FOLDER]`, then `bash linux/run.sh`.

## Credits

- **yozander2009-stack** and contributors: the recompiled runtime and launcher this port is built from
- **MZ Labs**: SwitchRecomp (Apache-2.0); "SwitchRecomp by MZ Labs" and NOTICE.txt must be kept
- **ScarletNX**: the native Vulkan renderer (GPL-3.0)
- **AMD**: FidelityFX Super Resolution 1 (MIT)
- **Dear ImGui** (MIT), libvpx and OpenH264 (BSD), SDL2 and zlib (zlib licence)
- **Claude** (Anthropic): AI pair programmer for this fork's changes
- Program icon: [Metroid icon created by Smashicons - Flaticon](https://www.flaticon.com/free-icons/metroid)

## Licences

The released program is distributed under **GPL-3.0** (`LICENSE`), because it contains the ScarletNX renderer; its
source is this repository. Each source file keeps its own licence (`LICENSE.txt` and `NOTICE.txt` for SwitchRecomp's
Apache-2.0; Apache-2.0 and MIT code is GPL-3.0-compatible). The licence texts are also inside the AppImage under
`usr/share/metroid-prime-4-recomp/licenses`.

*Metroid Prime 4: Beyond* is © Nintendo. This project is not affiliated with or endorsed by
Nintendo.
