# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

flatTube plays a 30x4 low-res video on a single serpentine-wired SK6812
RGBW LED strip (120 LEDs) via a Raspberry Pi Zero W's PWM/DMA output,
controlled live from a web UI. It's a stripped-down spinoff of
`../fpTube` (200 tubes over DMX/serial/OSC) — no DMX, no serial, no OSC,
no per-tube addressing. See README.md for user-facing docs (API
reference, CLI flags, build/deploy steps); this file is about the
internal architecture and dev workflow.

## Commands

- Build (Pi only): `make`
- Run: `sudo ./flattube` (root needed for `/dev/mem` GPIO/DMA access)
- Generate/regenerate video assets: `python3 tools/gen_test_pattern.py`,
  `python3 tools/convert_fire.py` (needs `pip install -r
  tools/requirements.txt`)
- Deploy to hardware: `rsync -az --exclude='.git' --exclude='presets.txt'
  . fptube:/home/pi/flatTube/`, then `ssh fptube "cd /home/pi/flatTube &&
  make && sudo systemctl restart flattube"`
- There is no test suite. Verification is done by compiling the portable
  modules directly and/or linking against a throwaway stub
  implementation of ws2811.c's four functions (see "Testing without the
  Pi" below).

## Two build targets in one tree

`src/main.c` and everything in `src/vendor/` (the rpi_ws281x DMA/PWM
driver) only compile on Linux/ARM against real BCM283x registers — they
will not build on macOS or any non-Pi box.

Everything else — `mapping.c`, `pngseq.c`, `control.c`, `httpd.c`,
`presets.c` — is plain POSIX C with no hardware dependency, and is where
almost all application logic actually lives. Prefer developing and
testing changes to these files directly on whatever machine you're on;
only the final integration needs the Pi.

### Testing without the Pi

`src/vendor/ws2811.h` declares exactly four functions
(`ws2811_init`/`_render`/`_wait`/`_fini` + `ws2811_get_return_t_str`).
Writing a ~30-line stub .c file that implements these (malloc/free the
LED buffer, no-op render) lets you compile and run the *entire* real
`main.c` — full render loop, HTTP thread, control state, presets — on
any machine with libpng installed, and drive it with `curl` against
`localhost:<port>` to exercise every state transition end to end before
ever touching the Pi. This has been the primary verification method
throughout this project's development.

## Architecture

**Two threads, one shared state.** `main()` starts an HTTP thread
(`httpd_run()`) and then runs the render loop itself. `control.c` owns a
mutex-guarded `ControlState` (video selection, hue/sat/val, fps,
paused/started, solid RGBW) — the HTTP thread mutates it via
`control_apply(ControlPatch)`, the render loop reads a snapshot via
`control_get()` once per frame. This is the only shared state in the
program; presets and the video registry are read-mostly/HTTP-thread-only
and need no locking (see comments in `presets.c`/`control.c`).

**Render loop** (in `main.c`): each iteration reads `ControlState`, and
either (a) fills all 120 LEDs with a single gamma-corrected RGBW value
(`video == "solid"`), or (b) takes the current frame of the selected
preloaded video, applies HSV hue/sat/val adjustment (`rgb_hsv.c`) +
gamma correction, computes a white channel as `min(r,g,b)`, and maps
each (col, row) pixel to an LED index via `mapping.c`. `started=0`
clears the strip and idles without touching the pipeline; `paused=1`
freezes frame advance but keeps re-rendering so color edits still show.

**LED mapping** (`src/mapping.c`): pure function, no state. Serpentine:
row 0 starts right-to-left, alternates per row, no filler LEDs between
rows. Note the mapping does *not* flip vertically even though the strip
is described as wired bottom-to-top — this was empirically corrected
against the physical hardware (see the comment in `mapping.c` and the
"Fix vertical row mapping after hardware bring-up" commit); don't
"simplify" it back without retesting on the box.

**Video registry** (`control.c`): every subdirectory of `videos/` is
scanned and *fully preloaded into memory* at startup
(`control_load_videos`) — there is no disk I/O during playback, so
switching videos live is instant. `"solid"` is a reserved name that maps
to no on-disk video.

**HTTP layer** (`httpd.c`): hand-rolled single-threaded HTTP/1.1 server,
one request per connection, no external dependencies. Deliberately never
parses JSON (it only generates JSON for responses) — incoming data is
always `application/x-www-form-urlencoded`, parsed with a small
`url_decode`/`parse_form` pair. Static files are served from an exact
path whitelist (`static_routes[]`), not a general file server. This
minimalism is intentional, matching the Pi Zero W's limited resources
and the project's decision to avoid OSC/liblo entirely.

**Presets** (`presets.c`): named snapshots of
video/hue/sat/val/fps/solid-RGBW (not paused/started — presets are
look-only). Persisted to a plain `key=value` text file (default
`presets.txt`), not JSON, to avoid ever needing a JSON *parser* in the
codebase. At most one preset can be `is_default`;
`presets_get_default()` + `presets_to_patch()` are applied at startup in
`main.c` before the render loop begins. `presets_to_patch()` is the
single place that converts a `Preset` to a `ControlPatch` — both the
HTTP apply handler and the startup path use it.

## Environment notes

- Target hardware (`ssh fptube`) is an old Raspberry Pi Zero W on
  Raspbian Stretch (armv6l, gcc 6.3.0) with only `libpng12-dev`
  available — no `libpng16-dev`. Don't `#include <setjmp.h>` before
  `<png.h>` anywhere; libpng 1.2 pulls it in itself and errors if the
  app does it first.
- This repo's local git config is set to `martin@martinmarier.com`
  (overriding whatever global default is active) — commits here should
  use that identity, not a work email.
- Runs as a systemd service (`systemd/flattube.service`) on the Pi;
  after deploying new code, restart with `sudo systemctl restart
  flattube` rather than killing/relaunching manually.
