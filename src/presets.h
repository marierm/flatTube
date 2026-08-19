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

/* Upserts a preset from state's video/hue/sat/val/fps/solid fields under
 * name, and persists the full list to disk. */
void presets_save(const char *name, const ControlState *state);

/* Removes the named preset if present, persists to disk. */
void presets_delete(const char *name);

#endif
