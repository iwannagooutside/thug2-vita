#!/usr/bin/env python3
"""Prepare the THUG2 game files for the PS Vita port.

Usage:
    python3 prepare_thug2_data.py <extracted_data_folder> <output_folder> [--no-movies] [--all]

<extracted_data_folder> is the "data" folder you get after extracting your own
Tony Hawk's Underground 2 Xbox disc image with extract-xiso. The script checks
that it really is the THUG2 Xbox data, then copies what the Vita port needs
into <output_folder>/Data. Copy that "Data" folder to ux0:data/thug2/ on the
Vita (you end up with ux0:data/thug2/Data/scripts, ux0:data/thug2/Data/pre...).

Left out by default (the port never reads them), about 0.76 GB saved:
  images/PALimages/        PAL-only loading screens
  pre/*.prd, pre/*.prf     German and French text packs
With --no-movies, the game's videos (movies/, about 1 GB) are left out too:
the game then skips them and carries on normally.
Use --all to copy everything anyway.
Measured on the tested disc: 2.6 GB (6,098 files) by default, 1.6 GB (6,076
files) with --no-movies, 3.3 GB with --all.

Nothing is sent anywhere and nothing in the source folder is modified.
Needs Python 3.6 or newer, no extra package.
"""
import os
import shutil
import sys

# Files that must be there in a THUG2 Xbox dump (checked before copying).
REQUIRED = [
    "scripts/qdir.txt",
    "pre/qb_scripts.prx",
    "streams/pcm/music_pcm.wad",
    "streams/pcm/pcm.wad",
]


def skipped(rel, no_movies):
    """True for files left out (rel uses '/')."""
    low = rel.lower()
    if no_movies and low.startswith("movies/"):
        return True
    if low.startswith("images/palimages/"):
        return True
    if low.startswith("pre/") and (low.endswith(".prd") or low.endswith(".prf")):
        return True
    return False


def find_ci(root, rel):
    """Case-insensitive lookup of rel under root (the dump may be upper case)."""
    cur = root
    for part in rel.split("/"):
        try:
            names = os.listdir(cur)
        except OSError:
            return None
        match = [n for n in names if n.lower() == part.lower()]
        if not match:
            return None
        cur = os.path.join(cur, match[0])
    return cur


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    copy_all = "--all" in sys.argv[1:]
    no_movies = "--no-movies" in sys.argv[1:]
    if len(args) != 2:
        print(__doc__)
        return 2
    src, out = (os.path.abspath(a) for a in args)

    # Accept the extract-xiso folder itself (containing "data") as well.
    if find_ci(src, "data/scripts/qdir.txt") and not find_ci(src, "scripts/qdir.txt"):
        src = find_ci(src, "data")
    missing = [r for r in REQUIRED if not find_ci(src, r)]
    if missing:
        print("This doesn't look like the THUG2 Xbox 'data' folder. Missing:")
        for r in missing:
            print("  " + r)
        print("Extract your Xbox disc image with extract-xiso and point this script at its 'data' folder.")
        return 1

    dst_root = os.path.join(out, "Data")
    if os.path.abspath(dst_root).startswith(src + os.sep) or os.path.abspath(dst_root) == src:
        print("The output folder must be outside the source folder.")
        return 1

    files, total, left_out, left_out_size = [], 0, 0, 0
    for dirpath, dirnames, filenames in os.walk(src):
        dirnames.sort()
        for name in sorted(filenames):
            path = os.path.join(dirpath, name)
            rel = os.path.relpath(path, src).replace(os.sep, "/")
            size = os.path.getsize(path)
            if not copy_all and skipped(rel, no_movies):
                left_out += 1
                left_out_size += size
                continue
            files.append((path, rel, size))
            total += size

    os.makedirs(out, exist_ok=True)
    free = shutil.disk_usage(out).free
    if free < total + (64 << 20):
        print("Not enough free space: %.2f GB needed, %.2f GB free." % (total / 2**30, free / 2**30))
        return 1

    print("Copying %d files (%.2f GB) to %s" % (len(files), total / 2**30, dst_root))
    if left_out:
        print("Leaving out %d files (%.2f GB) (--all to keep everything)."
              % (left_out, left_out_size / 2**30))
    done = 0
    for i, (path, rel, size) in enumerate(files):
        dst = os.path.join(dst_root, *rel.split("/"))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        if not (os.path.exists(dst) and os.path.getsize(dst) == size):
            shutil.copyfile(path, dst)
        done += size
        if i % 200 == 0 or i == len(files) - 1:
            print("  %3d%%  %s" % (done * 100 // max(total, 1), rel))
    print()
    print("Done. Copy the folder")
    print("    " + dst_root)
    print("to your Vita so that you get:")
    print("    ux0:data/thug2/Data/scripts/qdir.txt")
    return 0


if __name__ == "__main__":
    sys.exit(main())
