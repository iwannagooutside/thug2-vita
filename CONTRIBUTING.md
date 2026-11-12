# Contributing

Bug reports and fixes are welcome.

## Reporting a bug

Open an issue with the *Bug report* template. What helps most:

- the version you're running (Releases page tag, or the commit if you built it);
- the level and the mode (Story, Classic, Free Skate, an editor...);
- what you were doing, and whether it happens again;
- a photo or screenshot;
- the log: `ux0:data/thug2-vita/thug.log` (or `thug.prev.log` if you
  restarted the game since).

## Sending a change

- Keep changes small and focused, and say how you tested them on a real Vita.
- Don't modernize the original engine code: the port keeps it as close to the
  original as possible, and each deviation is small and commented.
- THUG2-specific behaviour goes behind the feature flags in
  `Code/Core/nv_features.h`.
- Build with `-DTHUG2_APP=ON` before sending (see
  [docs/building.md](docs/building.md)).

## Never add game data

No files from the game, in any form: no ISO, no `data/` files, no extracted
textures, models, sounds, videos or scripts, no executables or dumps, and no
images made from them. Pull requests containing any will be closed.
