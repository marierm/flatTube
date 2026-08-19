#ifndef CONTROL_H
#define CONTROL_H

#include <stdint.h>
#include "pngseq.h"

#define MAX_VIDEOS 32
#define NAME_LEN 64
#define SOLID_VIDEO_NAME "solid"

typedef struct {
  int hue;    /* 0-255 */
  int sat;    /* 0-255, 128 = neutral */
  int val;    /* 0-255 */
  int fps;    /* 1-60 */
  int paused; /* 0/1: freezes frame advance, live edits still apply */
  int started; /* 0/1: 0 clears the strip and idles the render loop */
  char video[NAME_LEN]; /* a loaded video's name, or "solid" */
  uint8_t solid_r, solid_g, solid_b, solid_w;
} ControlState;

/* A partial update: only fields with the matching has_* flag set are
 * applied. Lets the HTTP layer build one patch from whichever form
 * fields were present in a request and apply it atomically. */
typedef struct {
  int has_video;
  char video[NAME_LEN];
  int has_hue;
  int hue;
  int has_sat;
  int sat;
  int has_val;
  int val;
  int has_fps;
  int fps;
  int has_paused;
  int paused;
  int has_started;
  int started;
  int has_solid;
  uint8_t solid_r, solid_g, solid_b, solid_w;
} ControlPatch;

/*
 * Scans dir for subdirectories and preloads each as a GRID_COLS x
 * GRID_ROWS frame sequence, sorted by name. Exits the process with an
 * error message if none are found or a frame set is malformed.
 */
void control_load_videos(const char *dir);

/* NULL if name is unknown or is the reserved solid-color entry. */
const FrameSequence *control_find_video(const char *name);

int control_video_count(void);
const char *control_video_name(int index);

/* Sets defaults: first loaded video, hue 0, sat 128, val 255, fps 25,
 * not paused, started. */
void control_init(void);

/* Thread-safe snapshot copy. */
void control_get(ControlState *out);

/* Applies a patch under lock. Clamps numeric fields to their valid
 * range and ignores an unknown video name (leaving the prior value). */
void control_apply(const ControlPatch *patch);

#endif
