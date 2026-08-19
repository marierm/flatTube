#include "pngseq.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glob.h>
#include <png.h> /* pulls in setjmp.h itself; must not be included before it (breaks on libpng 1.2) */

static void die(const char *fmt, const char *arg)
{
  fprintf(stderr, fmt, arg);
  fprintf(stderr, "\n");
  exit(1);
}

static uint8_t *read_png_rgb(const char *path, int width, int height)
{
  FILE *fp = fopen(path, "rb");
  if (!fp)
    die("Could not open PNG file: %s", path);

  uint8_t header[8];
  if (fread(header, 1, 8, fp) != 8 || png_sig_cmp(header, 0, 8))
    die("Not a valid PNG file: %s", path);

  png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!png)
    die("png_create_read_struct failed for: %s", path);

  png_infop info = png_create_info_struct(png);
  if (!info)
    die("png_create_info_struct failed for: %s", path);

  if (setjmp(png_jmpbuf(png)))
    die("Error decoding PNG file: %s", path);

  png_init_io(png, fp);
  png_set_sig_bytes(png, 8);
  png_read_info(png, info);

  int w = (int)png_get_image_width(png, info);
  int h = (int)png_get_image_height(png, info);
  if (w != width || h != height) {
    fprintf(stderr, "Frame %s is %dx%d, expected %dx%d\n", path, w, h, width, height);
    exit(1);
  }

  png_byte color_type = png_get_color_type(png, info);
  png_byte bit_depth = png_get_bit_depth(png, info);

  if (bit_depth == 16)
    png_set_strip_16(png);
  if (color_type == PNG_COLOR_TYPE_PALETTE)
    png_set_palette_to_rgb(png);
  if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
    png_set_gray_to_rgb(png);
  if (png_get_valid(png, info, PNG_INFO_tRNS))
    png_set_tRNS_to_alpha(png);
  if (color_type & PNG_COLOR_MASK_ALPHA)
    png_set_strip_alpha(png);

  png_read_update_info(png, info);

  uint8_t *pixels = malloc((size_t)w * h * 3);
  png_bytep *rows = malloc(sizeof(png_bytep) * h);
  for (int y = 0; y < h; y++)
    rows[y] = pixels + (size_t)y * w * 3;

  png_read_image(png, rows);

  free(rows);
  png_destroy_read_struct(&png, &info, NULL);
  fclose(fp);

  return pixels;
}

FrameSequence load_frame_sequence(const char *dir, int width, int height)
{
  char pattern[1024];
  snprintf(pattern, sizeof(pattern), "%s/*.png", dir);

  glob_t results;
  if (glob(pattern, 0, NULL, &results) != 0 || results.gl_pathc == 0)
    die("No PNG frames found in: %s", dir);

  FrameSequence seq;
  seq.count = (int)results.gl_pathc;
  seq.width = width;
  seq.height = height;
  seq.frames = malloc(sizeof(Frame) * seq.count);

  for (int i = 0; i < seq.count; i++)
    seq.frames[i].pixels = read_png_rgb(results.gl_pathv[i], width, height);

  globfree(&results);
  return seq;
}

void free_frame_sequence(FrameSequence *seq)
{
  for (int i = 0; i < seq->count; i++)
    free(seq->frames[i].pixels);
  free(seq->frames);
  seq->frames = NULL;
  seq->count = 0;
}
