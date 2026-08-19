#!/usr/bin/env python3
"""
Downsample the fpTube "006-Fire" PNG sequence (200x120) to flatTube's
30x4 grid.

Uses a box filter, which is the right choice for this large a reduction
(~6.7x horizontally, 30x vertically) since it averages every source pixel
into the destination instead of point-sampling and aliasing.
"""
import argparse
import glob
import os

from PIL import Image

GRID_COLS = 30
GRID_ROWS = 4

DEFAULT_SRC = "../fpTube/videos/006-Fire"
DEFAULT_OUT = "videos/fire"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-i", "--src-dir", default=DEFAULT_SRC, help="source PNG sequence directory")
    parser.add_argument("-o", "--out-dir", default=DEFAULT_OUT, help="output directory for PNG frames")
    args = parser.parse_args()

    sources = sorted(glob.glob(os.path.join(args.src_dir, "*.png")))
    if not sources:
        raise SystemExit(f"No PNG frames found in {args.src_dir}")

    os.makedirs(args.out_dir, exist_ok=True)

    for i, path in enumerate(sources):
        img = Image.open(path).convert("RGB")
        small = img.resize((GRID_COLS, GRID_ROWS), Image.Resampling.BOX)
        small.save(os.path.join(args.out_dir, f"frame_{i:04d}.png"))

    print(f"Converted {len(sources)} frames from {args.src_dir} to {args.out_dir}")


if __name__ == "__main__":
    main()
