#include "presets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Preset presets[MAX_PRESETS];
static int preset_count = 0;
static char presets_path[512] = "";

static void parse_line(const char *line, char *key, size_t keysize, char *value, size_t valsize)
{
  const char *eq = strchr(line, '=');
  if (!eq) {
    key[0] = '\0';
    value[0] = '\0';
    return;
  }

  size_t klen = (size_t)(eq - line);
  if (klen >= keysize)
    klen = keysize - 1;
  memcpy(key, line, klen);
  key[klen] = '\0';

  snprintf(value, valsize, "%s", eq + 1);
  size_t vlen = strlen(value);
  while (vlen > 0 && (value[vlen - 1] == '\n' || value[vlen - 1] == '\r'))
    value[--vlen] = '\0';
}

static void apply_field(Preset *p, const char *key, const char *value)
{
  if (strcmp(key, "name") == 0)
    snprintf(p->name, NAME_LEN, "%s", value);
  else if (strcmp(key, "video") == 0)
    snprintf(p->video, NAME_LEN, "%s", value);
  else if (strcmp(key, "hue") == 0)
    p->hue = atoi(value);
  else if (strcmp(key, "sat") == 0)
    p->sat = atoi(value);
  else if (strcmp(key, "val") == 0)
    p->val = atoi(value);
  else if (strcmp(key, "fps") == 0)
    p->fps = atoi(value);
  else if (strcmp(key, "solid_r") == 0)
    p->solid_r = (uint8_t)atoi(value);
  else if (strcmp(key, "solid_g") == 0)
    p->solid_g = (uint8_t)atoi(value);
  else if (strcmp(key, "solid_b") == 0)
    p->solid_b = (uint8_t)atoi(value);
  else if (strcmp(key, "solid_w") == 0)
    p->solid_w = (uint8_t)atoi(value);
  else if (strcmp(key, "default") == 0)
    p->is_default = atoi(value);
}

static void save_to_disk(void)
{
  if (presets_path[0] == '\0')
    return;

  FILE *f = fopen(presets_path, "w");
  if (!f) {
    fprintf(stderr, "Could not write presets file: %s\n", presets_path);
    return;
  }

  for (int i = 0; i < preset_count; i++) {
    const Preset *p = &presets[i];
    fprintf(f,
            "name=%s\nvideo=%s\nhue=%d\nsat=%d\nval=%d\nfps=%d\n"
            "solid_r=%d\nsolid_g=%d\nsolid_b=%d\nsolid_w=%d\ndefault=%d\n\n",
            p->name, p->video, p->hue, p->sat, p->val, p->fps, p->solid_r, p->solid_g, p->solid_b,
            p->solid_w, p->is_default);
  }

  fclose(f);
}

void presets_init(const char *path)
{
  snprintf(presets_path, sizeof(presets_path), "%s", path);
  preset_count = 0;

  FILE *f = fopen(path, "r");
  if (!f)
    return;

  char line[256];
  Preset current;
  memset(&current, 0, sizeof(current));
  int have_fields = 0;

  while (fgets(line, sizeof(line), f)) {
    if (line[0] == '\n' || line[0] == '\0') {
      if (have_fields && preset_count < MAX_PRESETS)
        presets[preset_count++] = current;
      memset(&current, 0, sizeof(current));
      have_fields = 0;
      continue;
    }

    char key[32], value[NAME_LEN];
    parse_line(line, key, sizeof(key), value, sizeof(value));
    if (key[0] == '\0')
      continue;

    have_fields = 1;
    apply_field(&current, key, value);
  }

  if (have_fields && preset_count < MAX_PRESETS)
    presets[preset_count++] = current;

  fclose(f);
}

int presets_count(void)
{
  return preset_count;
}

const Preset *presets_get(int index)
{
  if (index < 0 || index >= preset_count)
    return NULL;
  return &presets[index];
}

const Preset *presets_find(const char *name)
{
  for (int i = 0; i < preset_count; i++)
    if (strcmp(presets[i].name, name) == 0)
      return &presets[i];
  return NULL;
}

void presets_save(const char *name, const ControlState *state)
{
  int idx = -1;
  for (int i = 0; i < preset_count; i++) {
    if (strcmp(presets[i].name, name) == 0) {
      idx = i;
      break;
    }
  }

  if (idx < 0) {
    if (preset_count >= MAX_PRESETS) {
      fprintf(stderr, "Preset limit reached (%d), ignoring save of '%s'\n", MAX_PRESETS, name);
      return;
    }
    idx = preset_count++;
    /* Zero the slot first: it may be a reused array position left behind
     * by presets_delete()'s shift, and stale is_default must not leak
     * into what is supposed to be a brand-new preset. */
    memset(&presets[idx], 0, sizeof(presets[idx]));
  }

  Preset *p = &presets[idx];
  snprintf(p->name, NAME_LEN, "%s", name);
  snprintf(p->video, NAME_LEN, "%s", state->video);
  p->hue = state->hue;
  p->sat = state->sat;
  p->val = state->val;
  p->fps = state->fps;
  p->solid_r = state->solid_r;
  p->solid_g = state->solid_g;
  p->solid_b = state->solid_b;
  p->solid_w = state->solid_w;

  save_to_disk();
}

void presets_delete(const char *name)
{
  for (int i = 0; i < preset_count; i++) {
    if (strcmp(presets[i].name, name) == 0) {
      for (int j = i; j < preset_count - 1; j++)
        presets[j] = presets[j + 1];
      preset_count--;
      save_to_disk();
      return;
    }
  }
}

const Preset *presets_get_default(void)
{
  for (int i = 0; i < preset_count; i++)
    if (presets[i].is_default)
      return &presets[i];
  return NULL;
}

void presets_set_default(const char *name)
{
  int found = (name[0] == '\0') ? 1 : 0; /* empty name: just clear everyone */
  for (int i = 0; i < preset_count; i++) {
    int match = strcmp(presets[i].name, name) == 0;
    presets[i].is_default = match;
    found = found || match;
  }
  if (found)
    save_to_disk();
}

ControlPatch presets_to_patch(const Preset *p)
{
  ControlPatch patch;
  memset(&patch, 0, sizeof(patch));

  patch.has_video = 1;
  snprintf(patch.video, NAME_LEN, "%s", p->video);
  patch.has_hue = 1;
  patch.hue = p->hue;
  patch.has_sat = 1;
  patch.sat = p->sat;
  patch.has_val = 1;
  patch.val = p->val;
  patch.has_fps = 1;
  patch.fps = p->fps;
  patch.has_solid = 1;
  patch.solid_r = p->solid_r;
  patch.solid_g = p->solid_g;
  patch.solid_b = p->solid_b;
  patch.solid_w = p->solid_w;

  return patch;
}
