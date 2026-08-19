/*
 * flatTube - play a low-res video (30x4) on a single serpentine-wired
 * SK6812 RGBW LED strip via a Raspberry Pi's PWM/DMA output.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <getopt.h>

#include "mapping.h"
#include "pngseq.h"
#include "ws2811.h"

#define TARGET_FREQ WS2811_TARGET_FREQ
#define DMA 10
#define DEFAULT_GPIO_PIN 18
#define DEFAULT_FPS 25
#define DEFAULT_BRIGHTNESS 255
#define STRIP_TYPE SK6812_STRIP_GRBW /* SK6812 RGBW strip reused from fpTube */

/* GAMMA correction table with GAMMA = 2.7, copied from fpTubeC. */
static const uint8_t gamma_table[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 7, 7, 7, 7, 8, 8, 8, 9, 9, 9, 10, 10, 10, 11, 11, 12, 12, 12, 13, 13, 14, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 19, 19, 20, 20, 21, 21, 22, 23, 23, 24, 24, 25, 26, 26, 27, 28, 28, 29, 30, 30, 31, 32, 33, 33, 34, 35, 36, 36, 37, 38, 39, 40, 41, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 51, 52, 53, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 68, 69, 70, 71, 72, 74, 75, 76, 77, 79, 80, 81, 83, 84, 85, 87, 88, 89, 91, 92, 94, 95, 97, 98, 100, 101, 103, 104, 106, 107, 109, 110, 112, 114, 115, 117, 119, 120, 122, 124, 125, 127, 129, 131, 132, 134, 136, 138, 140, 141, 143, 145, 147, 149, 151, 153, 155, 157, 159, 161, 163, 165, 167, 169, 171, 173, 175, 178, 180, 182, 184, 186, 188, 191, 193, 195, 198, 200, 202, 205, 207, 209, 212, 214, 216, 219, 221, 224, 226, 229, 231, 234, 237, 239, 242, 244, 247, 250, 252, 255};

static volatile sig_atomic_t running = 1;

static void stop(int signum)
{
  (void)signum;
  running = 0;
}

static void print_usage(const char *prog)
{
  fprintf(stderr,
          "Usage: %s [options] <frames-dir>\n"
          "  <frames-dir>              directory of %dx%d PNG frames to play in a loop\n"
          "  -g, --gpio <pin>          GPIO pin driving the LED strip (default %d)\n"
          "  -b, --brightness <0-255>  overall brightness (default %d)\n"
          "  -f, --fps <n>             playback frame rate (default %d)\n"
          "  -h, --help                this help\n",
          prog, GRID_COLS, GRID_ROWS, DEFAULT_GPIO_PIN, DEFAULT_BRIGHTNESS, DEFAULT_FPS);
}

int main(int argc, char **argv)
{
  int gpio_pin = DEFAULT_GPIO_PIN;
  int brightness = DEFAULT_BRIGHTNESS;
  int fps = DEFAULT_FPS;

  static struct option longopts[] = {
      {"gpio", required_argument, 0, 'g'},
      {"brightness", required_argument, 0, 'b'},
      {"fps", required_argument, 0, 'f'},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}};

  int c;
  while ((c = getopt_long(argc, argv, "g:b:f:h", longopts, NULL)) != -1) {
    switch (c) {
    case 'g':
      gpio_pin = atoi(optarg);
      break;
    case 'b':
      brightness = atoi(optarg);
      break;
    case 'f':
      fps = atoi(optarg);
      break;
    case 'h':
      print_usage(argv[0]);
      return 0;
    default:
      print_usage(argv[0]);
      return 1;
    }
  }

  if (optind >= argc) {
    print_usage(argv[0]);
    return 1;
  }
  const char *frames_dir = argv[optind];

  FrameSequence seq = load_frame_sequence(frames_dir, GRID_COLS, GRID_ROWS);
  printf("Loaded %d frame(s) from %s\n", seq.count, frames_dir);

  ws2811_t ledstring = {
      .freq = TARGET_FREQ,
      .dmanum = DMA,
      .channel = {
          [0] = {
              .gpionum = gpio_pin,
              .count = LED_COUNT,
              .invert = 0,
              .brightness = brightness,
              .strip_type = STRIP_TYPE,
          },
          [1] = {.gpionum = 0, .count = 0, .invert = 0, .brightness = 0},
      },
  };

  struct sigaction sa = {.sa_handler = stop};
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);

  ws2811_return_t ret = ws2811_init(&ledstring);
  if (ret != WS2811_SUCCESS) {
    fprintf(stderr, "ws2811_init failed: %s\n", ws2811_get_return_t_str(ret));
    free_frame_sequence(&seq);
    return 1;
  }

  int wait_us = 1000000 / (fps > 0 ? fps : DEFAULT_FPS);
  int frame_index = 0;

  while (running) {
    Frame *frame = &seq.frames[frame_index];

    for (int row = 0; row < GRID_ROWS; row++) {
      for (int col = 0; col < GRID_COLS; col++) {
        const uint8_t *p = &frame->pixels[(size_t)(row * GRID_COLS + col) * 3];
        uint8_t r = gamma_table[p[0]];
        uint8_t g = gamma_table[p[1]];
        uint8_t b = gamma_table[p[2]];
        uint8_t w = r < g ? r : g;
        w = b < w ? b : w;

        int led = led_index_for_pixel(col, row);
        ledstring.channel[0].leds[led] =
            ((uint32_t)w << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
      }
    }

    if ((ret = ws2811_render(&ledstring)) != WS2811_SUCCESS) {
      fprintf(stderr, "ws2811_render failed: %s\n", ws2811_get_return_t_str(ret));
      break;
    }

    frame_index = (frame_index + 1) % seq.count;
    usleep(wait_us);
  }

  memset(ledstring.channel[0].leds, 0, sizeof(ws2811_led_t) * LED_COUNT);
  ws2811_render(&ledstring);
  ws2811_fini(&ledstring);
  free_frame_sequence(&seq);

  printf("\n");
  return 0;
}
