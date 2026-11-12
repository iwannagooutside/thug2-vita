# Building THUG2 Vita

This page covers what the short section in the [README](../README.md) leaves
out: the build options, the files that come from outside the repository, and
the development loop.

## Toolchain

- [vitasdk](https://vitasdk.org), installed with `vdpm`, with `$VITASDK` set
  and `$VITASDK/bin` on the `PATH`.
- The vitaGL and zlib packages: `vdpm vitaGL zlib` (vitaGL pulls in
  vitaShaRK and math-neon).
- CMake 3.16 or later, `make`, Python 3 (it assembles the Vita Options menu
  script at build time; without Python the game builds without that menu).
- macOS. The engine code includes paths like `core/...` while the folders are
  called `Core/`, so it relies on a case-insensitive file system. On Linux,
  build from a case-insensitive mount or expect to fix include paths.

## Bink video decoder (optional)

The game's videos are decoded by a minimal FFmpeg build (Bink demuxer, Bink
video and audio decoders only), installed in its own prefix:

```
curl -LO https://ffmpeg.org/releases/ffmpeg-7.1.1.tar.xz && tar xf ffmpeg-7.1.1.tar.xz
sh Code/Gel/Movies/Vita/build_ffmpeg_bink.sh "$PWD/ffmpeg-7.1.1" /tmp/ffmpeg-bink-build ~/vita-build/ffmpeg-bink
```

CMake looks in `~/vita-build/ffmpeg-bink` (`-DTHUG_FFMPEG_BINK_DIR=...` to
change it). Without it the game builds and skips its videos. FFmpeg is LGPL
2.1+ and is linked statically.

## Build

```
cmake -S vita -B build-thug2 \
      -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake \
      -DTHUG2_APP=ON -DTHUG2_RELEASE=ON
make -C build-thug2 -j4
vita/release/thug2/verifier_release.sh build-thug2
```

Outputs: `build-thug2/eboot.bin` and `build-thug2/thug_vita.vpk` (TITLEID
`THUG20001`). The release page renames the VPK to `thug2_vita.vpk`. A full build takes several minutes. There's no global optimization
level: the engine crashes when built entirely at `-O1` or `-O2`, so only the
hot files that were checked one by one get `-O2` (the reasons are in the
comments of `vita/CMakeLists.txt`).

## Options

| Option | Default | What it does |
|---|---|---|
| `THUG2_APP` | `OFF` | Builds the THUG2 app (`THUG20001`, game data in `ux0:data/thug2/Data`, writes to `ux0:data/thug2-vita`). **Required**: CMake stops without it |
| `THUG2_RELEASE` | `OFF` | Public build (with `THUG2_APP`): no debug server, no file or network input, no test shortcuts, no debug overlay, short log, app name *THUG2*, version 01.00 |
| `THUG2_HD_DIR` | `~/vita-build/thug2-hd` | Folder holding the HD HUD pack (`*.hd`), packed into the VPK under `app0:hd/` |
| `THUG2_LA_DA` | `~/vita-build/livearea-thug2/vpk` | Folder holding the release LiveArea. Absent: CMake warns and the placeholder LiveArea in `vita/livearea/thug2/` is used |
| `THUG_FFMPEG_BINK_DIR` | `~/vita-build/ffmpeg-bink` | FFmpeg Bink prefix; absent, the videos are skipped |
| `THUG_VITAGL_DIR` | (none) | Link a vitaGL you built yourself instead of the vitasdk one |
| `VITA_IP` | `192.168.1.100` | Console address for `make send` / `make kill` (development only) |

Other options in `vita/CMakeLists.txt` (`THUG_RELEASE` alone, `NV_NET`,
`THUG_O2_LOT8`...) belong to experiments that this
repository doesn't support.

## HD HUD pack

The HD HUD comes from the redraws made by the THUG Pro team, used with their
permission. Nothing of it is stored here: `vita/tools/thugpro_hd.py` reads your
THUG2 Xbox `data` folder and a THUG Pro install, checks that every HD image
matches the original exactly, and writes the `.hd` files the game reads.
It needs Python 3 with numpy and Pillow.

```
pip install numpy pillow
python3 vita/tools/thugpro_hd.py --jeu thug2 /path/to/thug2/data /path/to/THUGPro ~/vita-build/thug2-hd
```

Then re-run CMake: it packs the files into the VPK. You can also copy the
output folder to `ux0:data/thug2/hd/` on the console instead; the game looks
there first, then in the VPK. Any image that's missing or doesn't match keeps
its original.

## LiveArea

- `vita/livearea/thug2/` holds a plain placeholder LiveArea (no game data, no
  logo). `vita/livearea/generer_generique.py` regenerates it.
- The LiveArea of the releases is made by `vita/livearea/generer_da.py` from
  artwork made for the port and from pieces of the game (the stencil font of
  the THUG2 graffiti editor, renders of a level). Those inputs aren't in the
  repository, so neither is its output. If you have them, the script's header
  explains the layout it expects; point `THUG2_LA_DA` at its `vpk/` output.

The Vita only accepts 8-bit indexed PNGs for the bubble and LiveArea images;
the scripts write them in that format. After changing them, reinstall the VPK
(pushing `eboot.bin` alone doesn't refresh the bubble).

## Boot logo

`vita/logo/logo_boot.mp4` is versioned and packed as `app0:boot/logo.mp4`
(H.264 Main 3.1, BT.709 limited range, 960x544, AAC 48 kHz, played with
`sceAvPlayer`). `vita/logo/encode_logo.sh` shows how it was encoded, from a
PNG master that isn't in the repository.

## Member function table

`vita/gen/thug2_member_table.cpp` is generated: it lists the script member
functions THUG2 expects and how they map to the engine's components, as
CRC keys only. It's checked in so the repository builds on its own.

## Development loop

With [vitacompanion](https://github.com/devnoname120/vitacompanion) and
VitaShell's FTP server running on the console:

```
cmake -S vita -B vita/build -DTHUG2_APP=ON -DVITA_IP=<console address>
make -C vita/build send     # kill the app, upload eboot.bin, relaunch
make -C vita/build kill
```

The first install still has to be the VPK. Without `THUG2_RELEASE`, the game
also runs a TCP debug server on port 9990 (`vita/src/vita_dbgsrv.c`); the tools
that talk to it aren't part of this repository.

## Code layout

```
Code/
  Core/   foundations: math, containers, debug   (+ Core/Vita/: compiler compat, ADPCM)
  Gel/    engine: QB scripts, assets, audio, main loop
  Gfx/    Nx* rendering layer, one backend per platform
            XBox/ NGPS/ NGC/ DX9/   original references, not built
            Vita/                  this port's vitaGL backend
  Sk/     game code: skater, physics, game modes, THUG2 script functions
  Sys/    per-platform system layers           (+ Sys/Vita/: files, input, saves)
vita/
  CMakeLists.txt, engine.cmake   build
  src/        entry point, log, boot logo, profiler, debug server
  qb/         Vita Options menu scripts
  tools/      build-time tools (QB assembler, HD pack)
  livearea/   LiveArea generators and the placeholder images
  logo/       boot logo
  gen/        pre-generated sources
```

The THUG2 profile is selected at compile time (`NV_GAME_THUG2`, see
`Code/Core/nv_features.h`); most of the THUG2-specific behaviour sits behind
those feature flags, next to the original THUG1 code. The engine also carries
code for other profiles that compiles to nothing in the THUG2 build. Code
comments are mostly in French; references to `notes-re` point to
reverse-engineering notes that aren't published.

The port keeps one rule: don't modernize the original code. The compiler is
made lenient (`-std=gnu++98 -fpermissive`), and each deviation from the
original is kept small and commented.
