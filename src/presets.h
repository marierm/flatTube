#ifndef PRESETS_H
#define PRESETS_H

#include <stdint.h>
#include "control.h"

#define MAX_PRESETS 32

typedef struct {
  char name[NAME_LEN];
  char video[NAME_LEN];
  int hue, sat, val, fps;
  uint8_t solid_r, solid_g, solid_b, solid_w;
  int is_default; /* applied automatically at startup; at most one preset */
} Preset;

/*
 * Loads existing presets from path (a simple key=value text format, not
 * JSON -- kept consistent with the rest of the server, which never
 * parses JSON). A missing file just starts with zero presets. Remembers
 * path for future saves. Call once at startup, before the HTTP thread
 * starts. Only ever touched from the HTTP thread afterward, so no lock.
 */
void presets_init(const char *path);

int presets_count(void);
const Preset *presets_get(int index);
const Preset *presets_find(const char *name);
const Preset *presets_get_default(void);

/* Upserts a preset from state's video/hue/sat/val/fps/solid fields under
 * name, and persists the full list to disk. Leaves is_default alone. */
void presets_save(const char *name, const ControlState *state);

/* Removes the named preset if present, persists to disk. */
void presets_delete(const char *name);

/* Marks the named preset as the one applied at startup, clearing the
 * flag on any other preset first (at most one default). An empty name
 * clears the default entirely. Persists to disk. */
void presets_set_default(const char *name);

/* Builds a ControlPatch (video/hue/sat/val/fps/solid) from a preset,
 * ready to pass to control_apply(). */
ControlPatch presets_to_patch(const Preset *p);

#endif
