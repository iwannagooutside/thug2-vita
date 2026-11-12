# Changelog

## v1.0.1

Player fixes and performance. App version 01.01; installs over v1.0.0 and keeps
saves, settings and game files.

**Fixes**
- Create-A-Skater: hands, calves and bare torso use the skater's skin tone (#7).
- Goal givers and pros are voiced again in goal intros and descriptions (#8).
- Thrown objects (tomatoes...): every throw works without restarting the goal (#9).
- Story mode: the current chapter is saved; loading continues where you were,
  pro teammate and goals included (#10).
- Level select: story points are saved; v1.0.0 saves are rebuilt level by
  level from the goals already won (#11).
- Memory card loading screen: the overlay covers the whole screen (#12).
- Classic mode: the run ends at 0:00, classic goals use their own rules again,
  and the New Orleans voodoo dolls no longer crash the game.

**Performance**
- Faster rendering of busy and far views with the same picture: off-screen
  level objects culled, character skin and shadow drawn directly on the GPU,
  animated-texture meshes batched, redundant graphics state removed, some work
  moved to the Vita's other CPU cores. Free Skate on a Vita: New Orleans street
  ~44 -> ~55 fps, rooftops ~52 -> ~59 fps, Barcelona far view ~44 -> ~52 fps.

## v1.0.0 (first release)

First public version of *Tony Hawk's Underground 2* on the PS Vita, built
from the Xbox version of the game. App ID `THUG20001`, app version 01.00.

**Game**
- Story mode (World Destruction Tour), Classic mode and Free Skate, with
  cutscenes, music and voices. Story and Classic played through on a Vita.
- Create-a-Skater, Create-a-Park, Create-a-Goal, Create-a-Trick and
  Create-a-Graphic, with saving and loading.
- 960x544, 60 fps in most views (30 fps cap available in Vita Options).

**Vita**
- Xbox button layout on the Vita (L and R are the two triggers, SELECT gets off
  the board, SELECT + R or the bottom middle of the touch screen for Focus).
- DualShock layout for PS TV, external pads and trigger grips.
- Vita Options menu (Options > Control Setup > Vita Options): controller
  layout, camera and skater stick inversion, 30/60 fps. Saved in
  `ux0:data/thug2-vita/controls.txt`.
- HD HUD (score, special bar, balance meter, stance icons, trick text) from the
  THUG Pro team, included in the game.
- LiveArea, boot screen and animated "Neversoft Engine Vita" boot logo
  (skippable; `nologo.txt` turns it off).
- The game's Bink videos (intro, pro bails, credits, menu demo), skippable
  with START or ✕; deleting `movies/` skips them.
- On-screen message when key game files are missing or misplaced.
- Saves and settings are written to a temporary file and then swapped in: a
  full memory card or a crash while saving no longer loses the previous save.
- Helper script `tools/prepare_thug2_data.py` to check and copy the Xbox game
  files (2.6 GB, or 1.6 GB without the videos, instead of 3.3 GB).

**Public build**
- No debug tools in the binary: no debug server, no file or network input, no
  test shortcuts, no debug overlay. Checked by
  `vita/release/thug2/verifier_release.sh`.
- Short log (errors and milestones), capped at 1 MB per session; the previous
  two sessions are kept (`thug.prev.log`, `thug.prev2.log`).

**Known issues**: far views in New Orleans around 40 fps; buttslaps after a late
ollie (as on Xbox); videos not yet checked one by one. See the README.
