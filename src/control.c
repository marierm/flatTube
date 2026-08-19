#include "control.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <pthread.h>

#include "mapping.h"

typedef struct {
  char name[NAME_LEN];
  FrameSequence seq;
} VideoEntry;

static VideoEntry videos[MAX_VIDEOS];
static int video_count = 0;

static ControlState state;
static pthread_mutex_t state_lock = PTHREAD_MUTEX_INITIALIZER;

static int clamp(int v, int lo, int hi)
{
  return v < lo ? lo : (v > hi ? hi : v);
}

static int cmp_name(const void *a, const void *b)
{
  return strcmp((const char *)a, (const char *)b);
}

void control_load_videos(const char *dir)
{
  DIR *d = opendir(dir);
  if (!d) {
    fprintf(stderr, "Could not open videos directory: %s\n", dir);
    exit(1);
  }

  char names[MAX_VIDEOS][NAME_LEN];
  int count = 0;

  struct dirent *entry;
  while ((entry = readdir(d)) != NULL) {
    if (entry->d_name[0] == '.')
      continue;
    if (count >= MAX_VIDEOS) {
      fprintf(stderr, "Too many video directories in %s (max %d), skipping %s\n",
              dir, MAX_VIDEOS, entry->d_name);
      continue;
    }
    snprintf(names[count], NAME_LEN, "%s", entry->d_name);
    count++;
  }
  closedir(d);

  if (count == 0) {
    fprintf(stderr, "No video directories found in: %s\n", dir);
    exit(1);
  }

  qsort(names, count, NAME_LEN, cmp_name);

  video_count = count;
  for (int i = 0; i < count; i++) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
    snprintf(videos[i].name, NAME_LEN, "%s", names[i]);
    videos[i].seq = load_frame_sequence(path, GRID_COLS, GRID_ROWS);
    printf("Loaded video '%s': %d frame(s) from %s\n", videos[i].name, videos[i].seq.count, path);
  }
}

const FrameSequence *control_find_video(const char *name)
{
  for (int i = 0; i < video_count; i++)
    if (strcmp(videos[i].name, name) == 0)
      return &videos[i].seq;
  return NULL;
}

int control_video_count(void)
{
  return video_count;
}

const char *control_video_name(int index)
{
  return videos[index].name;
}

void control_init(void)
{
  memset(&state, 0, sizeof(state));
  snprintf(state.video, NAME_LEN, "%s", video_count > 0 ? videos[0].name : SOLID_VIDEO_NAME);
  state.hue = 0;
  state.sat = 128;
  state.val = 255;
  state.fps = 25;
  state.paused = 0;
  state.started = 1;
  state.solid_r = 0;
  state.solid_g = 0;
  state.solid_b = 0;
  state.solid_w = 255;
}

void control_get(ControlState *out)
{
  pthread_mutex_lock(&state_lock);
  *out = state;
  pthread_mutex_unlock(&state_lock);
}

void control_apply(const ControlPatch *patch)
{
  pthread_mutex_lock(&state_lock);

  if (patch->has_video) {
    if (strcmp(patch->video, SOLID_VIDEO_NAME) == 0 || control_find_video(patch->video))
      snprintf(state.video, NAME_LEN, "%s", patch->video);
  }
  if (patch->has_hue)
    state.hue = clamp(patch->hue, 0, 255);
  if (patch->has_sat)
    state.sat = clamp(patch->sat, 0, 255);
  if (patch->has_val)
    state.val = clamp(patch->val, 0, 255);
  if (patch->has_fps)
    state.fps = clamp(patch->fps, 1, 60);
  if (patch->has_paused)
    state.paused = patch->paused ? 1 : 0;
  if (patch->has_started)
    state.started = patch->started ? 1 : 0;
  if (patch->has_solid) {
    state.solid_r = patch->solid_r;
    state.solid_g = patch->solid_g;
    state.solid_b = patch->solid_b;
    state.solid_w = patch->solid_w;
  }

  pthread_mutex_unlock(&state_lock);
}
