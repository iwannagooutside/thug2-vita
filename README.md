# THUG2 Vita

*Tony Hawk's Underground 2* (2004), running natively on the PS Vita.

[![Watch the trailer](https://img.youtube.com/vi/6RDFKMUtnxA/maxresdefault.jpg)](https://www.youtube.com/watch?v=6RDFKMUtnxA)

Not emulated, rebuilt: the game engine is recompiled for the Vita's ARM
processor, with a graphics backend written for it on top of vitaGL. It's the
same engine as the [THUG1 Vita port](https://github.com/iwannagooutside/thug-vita-port)
(based on kisak-thug), extended to run the Xbox version of THUG2. Part of
**Neversoft Engine Vita**, a shared base for the Neversoft games on Vita
([THUG1](https://github.com/iwannagooutside/thug-vita-port), THUG2; THUG Pro
is next).

**[Download the latest version from the Releases page](../../releases/latest).**
You need your own copy of the game: this repository and the release don't
include any game data.

Story mode (World Destruction Tour) and Classic mode have been played through
to the end on a real Vita, and Free Skate and the create modes
(Create-a-Skater, Create-a-Park, Create-a-Goal, Create-a-Trick,
Create-a-Graphic) have been tested. Most views run at or near 60 fps. A few
defects remain: see [Known issues](#known-issues).

## How it was made

This port was built with agentic AI and a custom harness. The harness drives a
real PS Vita from the computer: it builds the game, sends it to the console,
plays it, takes screenshots and reads the logs, then fixes what it finds and
starts again. Alongside that, a host build of the engine replays the game's
scripts against the original Xbox code to check behaviour, and every change
was compared against the Xbox version.

The game isn't perfect yet, and there are surely things you'll want changed.
Please [open an issue](../../issues): bugs, controls that feel off, small
details, ideas. The more detail the better: which level, what you were doing,
a photo if you can, and the log file (see *Troubleshooting*).

---

## What you need

- A PS Vita or PS TV running custom firmware (HENkaku / Enso / h-encore /
  HENlo). If yours isn't set up yet, [vita.hacks.guide](https://vita.hacks.guide)
  walks you through it. No plugin is needed. A PS4 controller on a handheld
  Vita should work through a plugin (ds4vita, MiniVitaTV), not tested; use the
  *dualshock* layout in Vita Options.
- [VitaShell](https://github.com/TheOfficialFloW/VitaShell) to install the game
  and copy files.
- `libshacccg.suprx` in `ur0:data/`. The game compiles its shaders on the
  console and can't start without it. If you've never set it up, install
  [ShaRKF00D](https://github.com/Rinnegatamante/ShaRKF00D), run it once, and
  it puts the file in the right place.
- The **Xbox** version of *Tony Hawk's Underground 2*, from a disc you own.
  The game reads the Xbox data files directly, so the PS2, GameCube and PC
  versions won't work. Only one disc has been tested: the retail Xbox disc
  whose `data` folder has the English, French and German text packs
  (`pre/*.prx`, `*.prf`, `*.prd`), PAL loading screens and the music in
  `streams/pcm/music_pcm.wad` (its executable is region-free). Other Xbox
  releases are untested; the game checks a few key files at startup and tells
  you if one is missing.
- Free space on your memory card, measured with the helper script below:
  **2.6 GB** with the game's videos, **1.6 GB** without them (3.3 GB if you
  copy the whole `data` folder as is).

## Installing

1. **Get the game files from your disc.** Xbox discs can't be read by a normal
   PC DVD drive. The usual ways are:
   - a modded original Xbox: copy the game to its hard drive from the dashboard,
     then copy the files to your computer over FTP; or
   - a PC drive with Kreon firmware, which gives you a disc image (ISO). Extract
     it with [extract-xiso](https://github.com/XboxDev/extract-xiso):

     ```
     extract-xiso -x "Tony Hawk's Underground 2.iso"
     ```

   Either way you get a folder with `default.xbe` and a `data` folder inside.
   The `data` folder is the part you need.

2. **Prepare the files (recommended).** The helper script
   [`tools/prepare_thug2_data.py`](tools/prepare_thug2_data.py) (Python 3, no
   extra package) checks that it really is the THUG2 Xbox data and copies only
   what the port reads: 2.6 GB, 6,098 files. It leaves out the PAL-only
   loading screens and the German and French text packs. Add `--no-movies` to
   leave out the game's videos too (1.6 GB, 6,076 files):

   ```
   python3 tools/prepare_thug2_data.py /path/to/data /path/to/output
   python3 tools/prepare_thug2_data.py /path/to/data /path/to/output --no-movies
   ```

   You can also skip this step and copy the whole `data` folder (3.3 GB).

3. **Copy it to the Vita.** Using VitaShell, copy the folder so you end up with
   this file on the memory card:

   ```
   ux0:data/thug2/Data/scripts/qdir.txt
   ```

   USB is much faster than FTP for this. Upper or lower case doesn't matter on
   the memory card. Make sure you don't end up with one folder inside another
   (`Data/data/`). If the files aren't where the game expects them, it tells you
   on screen at startup.

4. **Install the game.** Download `thug2_vita.vpk` from the
   [Releases page](../../releases/latest), copy it anywhere on the Vita, open
   it in VitaShell and install it.

5. **Play.** Tap the *THUG2* bubble, then *Start*. After the boot logo
   (press START, ✕ or ○ to skip it), the game plays its own intro videos and
   loads as usual. START or ✕ skips a video.

The first time you load a level it takes a little longer while shaders are
compiled and cached. That's normal.

## Controls

| PS Vita | Xbox pad | What it does |
|---|---|---|
| Left stick / D-pad | | Steer, walk, balance |
| Right stick | | Camera |
| ✕ | A | Ollie, jump |
| □ | X | Flip tricks |
| ○ | B | Grab tricks |
| △ | Y | Grind, lip tricks |
| **L** | Left trigger | Spin left in the air. On the ground: nollie. Revert when landing on a ramp |
| **R** | Right trigger | Spin right in the air. On the ground: switch stance. Revert when landing on a ramp |
| **L + R** in the air | | Spine transfer, acid drop, vert recovery |
| **SELECT** | Black | Get off the board, or back on (caveman) |
| **SELECT + R** | White | Focus (when the special meter is full) |
| Touch the bottom middle of the front screen | White | Focus, as above |
| START | START | Pause menu |

It's the Xbox layout: the Xbox pad has only two triggers, and each one does
both jobs. In the editors (Create-a-Park, Create-a-Goal, rail editor),
SELECT zooms in and SELECT + R zooms out, like White and Black on Xbox.

### Vita Options

The port adds its own settings menu: **Options > Control Setup > Vita Options**
(from the main menu or the pause menu). Changes apply right away and are saved
in `ux0:data/thug2-vita/controls.txt`, which you can also edit by hand:

| Menu entry (values) | In `controls.txt` | What it does |
|---|---|---|
| Controller (ps vita / dualshock) | `dualshock_layout=0/1` | **DualShock** layout, for a PS TV, an external DS3/DS4 pad, or trigger grips on the rear touchpad: L1, L2, R1 and R2 are separate buttons like on PS2, the rear touchpad halves act as L2/R2, L1 + R1 gets off the board (SELECT still works). Default: ps vita |
| Camera Up/Down (normal / inverted) | `invert_right_y` | Invert the right stick up/down |
| Camera Left/Right (normal / inverted) | `invert_right_x` | Invert the right stick left/right |
| Skater Up/Down (normal / inverted) | `invert_left_y` | Invert the left stick up/down (`invert_left_x` also exists in the file) |
| Frame Rate (60 fps / 30 fps) | `framerate=60/30` | Lock the game to 30 fps, steadier in the heaviest views. Default 60 |

Delete the file to get the defaults back.

## Files and saves

| Where | What |
|---|---|
| `ux0:app/THUG20001/` | The game itself (installed by VitaShell) |
| `ux0:data/thug2/Data/` | Your THUG2 Xbox game files (read only) |
| `ux0:data/thug2-vita/save/` | Your saves (career, skaters, parks, goals, tricks, graphics) |
| `ux0:data/thug2-vita/controls.txt` | Vita Options |
| `ux0:data/thug2-vita/thug.log` | Log of the last session (`thug.prev.log`, `thug.prev2.log`: the two before) |
| `ux0:data/thug2-vita/shd/` | Shader cache (safe to delete, it's rebuilt) |

Saves are separate from the game: deleting or reinstalling the game doesn't
touch them. If you played a test build of this port (same app, `THUG20001`),
installing this version over it keeps your saves and settings.

## RetroFlow cover

If you use [RetroFlow Launcher](https://github.com/jimbob4000/RetroFlow-Launcher),
there's a cover in [`covers/THUG20001.png`](covers/THUG20001.png). Copy it to:

```
ux0:data/RetroFlow/COVERS/Sony - PlayStation Vita/THUG20001.png
```

In RetroFlow, open the game's settings with △, set its category to *PS Vita*,
then rescan your games.

## Good to know

- **HD HUD.** The score, special bar, balance meter, stance icons and trick
  text use the HD redraws made by the [THUG Pro](https://thugpro.com) team,
  included with their permission. Same layout as the original, just sharper.
  (A `ux0:data/thug2/hd/` folder left by a test build is ignored; you can
  delete it.)
- **Videos.** The game's videos (Activision and Neversoft logos, intro, pro
  bails, credits, and the demo video that starts after a while on the main
  menu) play from `ux0:data/thug2/Data/movies/`. Press START or ✕ to skip one.
  To save about 1 GB, delete the `movies` folder (or use `--no-movies`): the
  videos are then skipped and the game carries on normally.
- **Boot logo.** To turn off the animated logo at startup, create an empty file
  `ux0:data/thug2-vita/nologo.txt`.
- **No online, LAN or split-screen play.** The game's online service shut down
  years ago, and LAN isn't supported on the Vita.

## Known issues

Tracked in the [issues](https://github.com/iwannagooutside/thug2-vita/issues). At release time:

- [#1](https://github.com/iwannagooutside/thug2-vita/issues/1) Some very busy views (New Orleans, Barcelona) still run in the
  50s since v1.0.1. *Frame Rate: 30 fps* in Vita Options gives a steadier picture there.
- [#2](https://github.com/iwannagooutside/thug2-vita/issues/2) Buttslaps can trigger on their own when you ollie late off a ramp. That's
  how the Xbox version behaves too (late ollie), not a port bug.
- The game's videos are new in this version and haven't been checked on every
  video yet. If one stutters, freezes or has no sound, skip it with START or
  ✕, and please open an issue. Deleting the `movies` folder skips them all.

If you see something else, an issue with a photo helps a lot.

## RetroFlow cover

If you use [RetroFlow Launcher](https://github.com/jimbob4000/RetroFlow-Launcher),
there's a cover in [`covers/THUG20001.png`](covers/THUG20001.png). Copy it to:

```
ux0:data/RetroFlow/COVERS/Sony - PlayStation Vita/THUG20001.png
```

In RetroFlow, open the game's settings with △, set its category to *PS Vita*,
then rescan your games.

## FAQ

**Is this an emulator?** No. The game's engine is rebuilt from source code for
the Vita and talks to its GPU directly. That's why it runs at full speed on a
Vita, which couldn't emulate an Xbox.

**Why the Xbox version?** The engine this port is built on comes from the PC
and Xbox branch of the code, and the Xbox data files are the ones it reads.
The PS2, GameCube and PC versions store their data differently.

**Does it work on PS TV?** Yes, set *Controller* to *dualshock* in Vita
Options to use L2/R2 on your pad.

**Can I play THUG Pro?** Not with this release. THUG Pro on the Vita is being
worked on separately.

**The game starts, then shows "GAME DATA NOT FOUND".** The files aren't where
the game looks for them. See *Installing*, step 3.

## Troubleshooting

**The game closes right away, or never gets past a black screen.**
Check that `ur0:data/libshacccg.suprx` exists (see *What you need*).

**The boot logo stopped showing.** If two launches in a row stop during or
right after the logo (a crash, or closing the game with the PS button at that
moment), the game writes `ux0:data/thug2-vita/logo_boot.plantage` and starts
without the logo from then on. Delete that file to get the logo back.

**Reporting a crash.** Attach `ux0:data/thug2-vita/thug.log` (and
`thug.prev.log` if you relaunched the game after the crash) to your issue. The
log is short and contains no personal data.

## Building from source

You need the [vitasdk](https://vitasdk.org) with the vitaGL and zlib packages
(`vdpm vitaGL zlib`; vitaGL pulls in vitaShaRK and math-neon), CMake, Python 3
and a Unix shell. It's built on macOS: the engine's include paths don't always
match the folder case, so a case-sensitive Linux file system needs extra work.

The videos need a small FFmpeg build with only the Bink decoders. Build it
once, before CMake (it installs to `~/vita-build/ffmpeg-bink`, where CMake
looks for it; without it the game builds fine and skips the videos):

```
curl -LO https://ffmpeg.org/releases/ffmpeg-7.1.1.tar.xz && tar xf ffmpeg-7.1.1.tar.xz
sh Code/Gel/Movies/Vita/build_ffmpeg_bink.sh "$PWD/ffmpeg-7.1.1" /tmp/ffmpeg-bink-build ~/vita-build/ffmpeg-bink
```

Then:

```
cmake -S vita -B build-thug2 \
      -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake \
      -DTHUG2_APP=ON -DTHUG2_RELEASE=ON
make -C build-thug2 -j4
```

This gives `build-thug2/eboot.bin` and `build-thug2/thug_vita.vpk`.
`THUG2_RELEASE=ON` builds the public version: no debug server, no file or
network input, no test shortcuts, no debug overlay, short log. Without it you
get the development build. `vita/release/thug2/verifier_release.sh build-thug2`
checks a public build: it fails if any debug tool or any input path other than
the real buttons and touch screens is left in the binary, or if the VPK holds
an unexpected file.

The HD HUD pack and the release LiveArea are made from game files, so they
aren't in the repository. Without them the VPK still builds (CMake warns),
with the original HUD and a plain placeholder LiveArea. How to generate the HD
pack from your own THUG2 and THUG Pro files, and what each build option does,
is in [docs/building.md](docs/building.md).

## Credits

- Neversoft and Activision made the game. This project isn't affiliated with
  or endorsed by them, and all trademarks belong to their owners.
- [kisak-thug](https://github.com/SwagSoftware/kisak-thug), the PC project the
  engine builds on.
- The [THUG1 Vita port](https://github.com/iwannagooutside/thug-vita-port),
  where this engine was first brought to the Vita.
- The [THUG Pro](https://thugpro.com) team for the HD redraws of the HUD.
- [FFmpeg](https://ffmpeg.org) (LGPL 2.1+) for the Bink video decoder.
- [vitaGL](https://github.com/Rinnegatamante/vitaGL) by Rinnegatamante, and
  the [vitasdk](https://vitasdk.org) team.
- Port by iwannagooutside. [Anthropic](https://www.anthropic.com)'s Claude
  models did most of the porting work through the harness.

PlayStation and PS Vita are trademarks of Sony Interactive Entertainment.
Tony Hawk's Underground 2 © Activision.

French version: [docs/README.fr.md](docs/README.fr.md). Changes:
[docs/CHANGELOG.md](docs/CHANGELOG.md).


## License

There is no license file. [kisak-thug](https://github.com/SwagSoftware/kisak-thug),
which this engine code comes from, doesn't have one either, and this repository
keeps that situation as it is. Third-party code keeps its own license (vitaGL:
LGPL-3.0; `Code/Gel/Music/binkaudio.c`, from FFmpeg: LGPL 2.1+). No game data
is included, and none should ever be added. More in [CREDITS.md](CREDITS.md).

## License

There is no license file. [kisak-thug](https://github.com/SwagSoftware/kisak-thug),
which this engine code comes from, doesn't have one either, and this repository
keeps that situation as it is. Third-party code keeps its own license (vitaGL:
LGPL-3.0; `Code/Gel/Music/binkaudio.c`, from FFmpeg: LGPL 2.1+). No game data
is included, and none should ever be added. More in [CREDITS.md](CREDITS.md).
