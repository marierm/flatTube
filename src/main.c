/*
 * flatTube - play a low-res video (30x4) on a single serpentine-wired
 * SK6812 RGBW LED strip via a Raspberry Pi's PWM/DMA output, controlled
 * live from a web UI.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <getopt.h>
#include <pthread.h>

#include "mapping.h"
#include "pngseq.h"
#include "control.h"
#include "presets.h"
#include "httpd.h"
#include "rgb_hsv.h"
#include "ws2811.h"

#define TARGET_FREQ WS2811_TARGET_FREQ
#define DMA 10
#define DEFAULT_GPIO_PIN 18
#define DEFAULT_BRIGHTNESS 255
#define DEFAULT_PORT 8080
#define DEFAULT_VIDEOS_DIR "videos"
#define DEFAULT_WEB_DIR "web"
#define DEFAULT_PRESETS_FILE "presets.txt"
#define DEFAULT_FPS 25
#define STRIP_TYPE SK6812_STRIP_GRBW /* SK6812 RGBW strip reused from fpTube */
#define IDLE_POLL_US 100000 /* how often to check for started=1 while off */

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
          "Usage: %s [options]\n"
          "  -i, --videos-dir <dir>    directory of video subfolders (default %s)\n"
          "  -w, --web-dir <dir>       directory of frontend assets (default %s)\n"
          "  -r, --presets-file <path> file presets are saved to (default %s)\n"
          "  -p, --port <n>            HTTP control server port (default %d)\n"
          "  -g, --gpio <pin>          GPIO pin driving the LED strip (default %d)\n"
          "  -b, --brightness <0-255>  overall hardware brightness cap (default %d)\n"
          "  -h, --help                this help\n",
          prog, DEFAULT_VIDEOS_DIR, DEFAULT_WEB_DIR, DEFAULT_PRESETS_FILE, DEFAULT_PORT, DEFAULT_GPIO_PIN,
          DEFAULT_BRIGHTNESS);
}

typedef struct {
  int port;
  const char *web_dir;
} HttpThreadArgs;

static void *http_thread_fn(void *arg)
{
  HttpThreadArgs *a = (HttpThreadArgs *)arg;
  httpd_run(a->port, a->web_dir);
  return NULL;
}

/* Renders one video-mode frame (HSV-adjusted, gamma-corrected) into the
 * ws2811 LED buffer through the serpentine mapping. */
static void render_video_frame(ws2811_t *ledstring, const Frame *frame, int hue, int sat, int val)
{
  for (int row = 0; row < GRID_ROWS; row++) {
    for (int col = 0; col < GRID_COLS; col++) {
      const uint8_t *p = &frame->pixels[(size_t)(row * GRID_COLS + col) * 3];

      RgbColor rgb = {p[0], p[1], p[2]};
      HsvColor hsv = RgbToHsv(rgb);
      hsv.h = (uint8_t)((hsv.h + hue) % 256);
      int satu = hsv.s + ((sat - 128) * 2);
      hsv.s = (uint8_t)(satu < 0 ? 0 : satu > 255 ? 255 : satu);
      hsv.v = (uint8_t)(hsv.v * val / 255);
      rgb = HsvToRgb(hsv);

      uint8_t r = gamma_table[rgb.r];
      uint8_t g = gamma_table[rgb.g];
      uint8_t b = gamma_table[rgb.b];
      uint8_t w = r < g ? r : g;
      w = b < w ? b : w;

      int led = led_index_for_pixel(col, row);
      ledstring->channel[0].leds[led] = ((uint32_t)w << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    }
  }
}

static void render_solid(ws2811_t *ledstring, uint8_t r, uint8_t g, uint8_t b, uint8_t w)
{
  uint32_t pixel = ((uint32_t)gamma_table[w] << 24) | ((uint32_t)gamma_table[r] << 16) |
                    ((uint32_t)gamma_table[g] << 8) | gamma_table[b];
  for (int i = 0; i < LED_COUNT; i++)
    ledstring->channel[0].leds[i] = pixel;
}

static void clear_strip(ws2811_t *ledstring)
{
  memset(ledstring->channel[0].leds, 0, sizeof(ws2811_led_t) * LED_COUNT);
  ws2811_render(ledstring);
}

int main(int argc, char **argv)
{
  int gpio_pin = DEFAULT_GPIO_PIN;
  int brightness = DEFAULT_BRIGHTNESS;
  int port = DEFAULT_PORT;
  const char *videos_dir = DEFAULT_VIDEOS_DIR;
  const char *web_dir = DEFAULT_WEB_DIR;
  const char *presets_file = DEFAULT_PRESETS_FILE;

  static struct option longopts[] = {
      {"videos-dir", required_argument, 0, 'i'},
      {"web-dir", required_argument, 0, 'w'},
      {"presets-file", required_argument, 0, 'r'},
      {"port", required_argument, 0, 'p'},
      {"gpio", required_argument, 0, 'g'},
      {"brightness", required_argument, 0, 'b'},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}};

  int c;
  while ((c = getopt_long(argc, argv, "i:w:r:p:g:b:h", longopts, NULL)) != -1) {
    switch (c) {
    case 'i':
      videos_dir = optarg;
      break;
    case 'w':
      web_dir = optarg;
      break;
    case 'r':
      presets_file = optarg;
      break;
    case 'p':
      port = atoi(optarg);
      break;
    case 'g':
      gpio_pin = atoi(optarg);
      break;
    case 'b':
      brightness = atoi(optarg);
      break;
    case 'h':
      print_usage(argv[0]);
      return 0;
    default:
      print_usage(argv[0]);
      return 1;
    }
  }

  control_load_videos(videos_dir);
  control_init();
  presets_init(presets_file);

  const Preset *default_preset = presets_get_default();
  if (default_preset) {
    ControlPatch patch = presets_to_patch(default_preset);
    control_apply(&patch);
    printf("Applied default preset '%s' at startup\n", default_preset->name);
  }

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
    return 1;
  }

  HttpThreadArgs http_args = {.port = port, .web_dir = web_dir};
  pthread_t http_thread;
  pthread_create(&http_thread, NULL, http_thread_fn, &http_args);

  char last_video[NAME_LEN] = "";
  int frame_index = 0;
  int was_off = 0;

  while (running) {
    ControlState state;
    control_get(&state);

    if (!state.started) {
      if (!was_off) {
        clear_strip(&ledstring);
        was_off = 1;
      }
      usleep(IDLE_POLL_US);
      continue;
    }
    was_off = 0;

    if (strcmp(state.video, last_video) != 0) {
      frame_index = 0;
      snprintf(last_video, sizeof(last_video), "%s", state.video);
    }

    if (strcmp(state.video, SOLID_VIDEO_NAME) == 0) {
      render_solid(&ledstring, state.solid_r, state.solid_g, state.solid_b, state.solid_w);
    } else {
      const FrameSequence *seq = control_find_video(state.video);
      if (seq && seq->count > 0) {
        if (frame_index >= seq->count)
          frame_index = 0;
        render_video_frame(&ledstring, &seq->frames[frame_index], state.hue, state.sat, state.val);
        if (!state.paused)
          frame_index = (frame_index + 1) % seq->count;
      }
    }

    if ((ret = ws2811_render(&ledstring)) != WS2811_SUCCESS) {
      fprintf(stderr, "ws2811_render failed: %s\n", ws2811_get_return_t_str(ret));
      break;
    }

    int fps = state.fps > 0 ? state.fps : DEFAULT_FPS;
    usleep(1000000 / fps);
  }

  httpd_request_stop();
  pthread_join(http_thread, NULL);

  clear_strip(&ledstring);
  ws2811_fini(&ledstring);

  printf("\n");
  return 0;
}
