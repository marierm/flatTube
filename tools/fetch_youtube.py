#!/usr/bin/env python3
"""
Fetch a YouTube video, trim it to a fixed duration from the start, and
convert it to flatTube's 30x4 PNG frame sequence.

Requires yt-dlp and ffmpeg on PATH. Downloads only the needed section at
a capped resolution (there's no point pulling 4K to shrink it to 30x4)
and lets ffmpeg's area-averaging scale filter do the downsampling in one
pass -- the same box-filter approach tools/convert_fire.py uses via
Pillow, just done in ffmpeg instead since we're already shelling out to
it for extraction.
"""
import argparse
import os
import subprocess
import sys
import tempfile

GRID_COLS = 30
GRID_ROWS = 4
DEFAULT_FPS = 25
DEFAULT_DURATION = 180  # seconds


def run(cmd):
    print("+ " + " ".join(cmd))
    subprocess.run(cmd, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("url", help="YouTube video URL")
    parser.add_argument("name", help="output subfolder under videos/ (becomes the dropdown entry)")
    parser.add_argument(
        "-d", "--duration", type=int, default=DEFAULT_DURATION,
        help=f"seconds to keep from the start (default {DEFAULT_DURATION})",
    )
    parser.add_argument(
        "-f", "--fps", type=int, default=DEFAULT_FPS,
        help=f"frames per second to extract (default {DEFAULT_FPS})",
    )
    parser.add_argument("-o", "--out-dir", default=None, help="output directory (default videos/<name>)")
    args = parser.parse_args()

    out_dir = args.out_dir or os.path.join("videos", args.name)

    mm, ss = divmod(args.duration, 60)
    section = f"*0:00-{mm}:{ss:02d}"

    with tempfile.TemporaryDirectory() as tmp:
        downloaded_template = os.path.join(tmp, "source.%(ext)s")
        run([
            "yt-dlp",
            # The default (auto) client negotiation was returning HTTP 403 on
            # the video URLs at the time this was written; the android
            # client's URLs worked without needing a PO token.
            "--extractor-args", "youtube:player_client=android",
            "-f", "best[height<=480]",
            "--download-sections", section,
            "-o", downloaded_template,
            args.url,
        ])

        matches = [f for f in os.listdir(tmp) if f.startswith("source.")]
        if not matches:
            sys.exit("yt-dlp did not produce an output file")
        source_path = os.path.join(tmp, matches[0])

        os.makedirs(out_dir, exist_ok=True)
        for f in os.listdir(out_dir):
            if f.endswith(".png"):
                os.remove(os.path.join(out_dir, f))

        run([
            "ffmpeg", "-y",
            "-i", source_path,
            "-vf", f"scale={GRID_COLS}:{GRID_ROWS}:flags=area",
            "-r", str(args.fps),
            os.path.join(out_dir, "frame_%04d.png"),
        ])

    count = len([f for f in os.listdir(out_dir) if f.endswith(".png")])
    print(f"Wrote {count} frames to {out_dir}")


if __name__ == "__main__":
    main()
