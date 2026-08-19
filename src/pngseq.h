#ifndef PNGSEQ_H
#define PNGSEQ_H

#include <stdint.h>

typedef struct {
  uint8_t *pixels; /* width * height * 3 bytes, 8-bit RGB, row-major */
} Frame;

typedef struct {
  Frame *frames;
  int count;
  int width;
  int height;
} FrameSequence;

/*
 * Loads every *.png file in dir (sorted by filename) into memory. Each
 * file must be exactly width x height pixels; any PNG color type is
 * accepted and normalized to 8-bit RGB. Exits the process with an error
 * message on any failure.
 */
FrameSequence load_frame_sequence(const char *dir, int width, int height);
void free_frame_sequence(FrameSequence *seq);

#endif
