#!/usr/bin/env python3
"""
Generate a simple animated test pattern for the flatTube 30x4 grid.

Each of the 4 rows is a solid dim color (Red, Green, Blue, Red from top to
bottom) with a brighter marker pixel sweeping left-to-right across all rows
in lockstep, wrapping around. This makes it easy to confirm on the physical
strip that rows are wired to the right color/order and that columns advance
in the expected direction.
"""
import argparse
import os

from PIL import Image

GRID_COLS = 30
GRID_ROWS = 4

ROW_COLORS = [
    (255, 0, 0),  # row 0 (top): red
    (0, 255, 0),  # row 1: green
    (0, 0, 255),  # row 2: blue
    (255, 0, 0),  # row 3 (bottom): red
]

DIM = 0.15  # background brightness fraction
MARKER = 1.0  # marker brightness fraction


def render_frame(marker_col):
    img = Image.new("RGB", (GRID_COLS, GRID_ROWS))
    pixels = img.load()
    for row in range(GRID_ROWS):
        r, g, b = ROW_COLORS[row]
        dim = (round(r * DIM), round(g * DIM), round(b * DIM))
        bright = (round(r * MARKER), round(g * MARKER), round(b * MARKER))
        for col in range(GRID_COLS):
            pixels[col, row] = bright if col == marker_col else dim
    return img


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "-o", "--out-dir", default="videos/test_pattern", help="output directory for PNG frames"
    )
    args = parser.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)

    for frame in range(GRID_COLS):
        img = render_frame(marker_col=frame)
        img.save(os.path.join(args.out_dir, f"frame_{frame:04d}.png"))

    print(f"Wrote {GRID_COLS} frames to {args.out_dir}")


if __name__ == "__main__":
    main()
