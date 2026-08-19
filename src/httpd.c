#include "httpd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "control.h"

#define REQ_BUF_SIZE 8192

static volatile sig_atomic_t stop_requested = 0;
static int listen_fd = -1;

typedef struct {
  const char *path;
  const char *file;
  const char *content_type;
} StaticRoute;

static const StaticRoute static_routes[] = {
    {"/", "index.html", "text/html; charset=utf-8"},
    {"/index.html", "index.html", "text/html; charset=utf-8"},
    {"/app.js", "app.js", "application/javascript; charset=utf-8"},
    {"/style.css", "style.css", "text/css; charset=utf-8"},
};

static void send_all(int fd, const char *data, size_t len)
{
  size_t sent = 0;
  while (sent < len) {
    ssize_t n = send(fd, data + sent, len - sent, 0);
    if (n <= 0)
      break;
    sent += (size_t)n;
  }
}

static void send_response(int fd, int status, const char *status_text, const char *content_type,
                           const char *body, size_t body_len)
{
  char header[256];
  int hlen = snprintf(header, sizeof(header),
                       "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
                       status, status_text, content_type, body_len);
  send_all(fd, header, (size_t)hlen);
  send_all(fd, body, body_len);
}

/* Reads headers (and, for POST, the body) into buf. Sets *body_start to
 * the offset where the body begins and *total_len to how much was read
 * in total. Returns -1 if the connection closed before headers were
 * complete. */
static int read_request(int fd, char *buf, int bufsize, int *body_start, int *total_len)
{
  int total = 0;
  int hdr_end = -1;

  while (total < bufsize - 1) {
    ssize_t n = recv(fd, buf + total, bufsize - 1 - total, 0);
    if (n <= 0)
      break;
    total += (int)n;
    buf[total] = '\0';
    char *pos = strstr(buf, "\r\n\r\n");
    if (pos) {
      hdr_end = (int)(pos - buf) + 4;
      break;
    }
  }
  if (hdr_end < 0)
    return -1;

  int content_length = 0;
  char *cl = strstr(buf, "Content-Length:");
  if (cl)
    content_length = atoi(cl + strlen("Content-Length:"));

  int need_total = hdr_end + content_length;
  if (need_total > bufsize - 1)
    need_total = bufsize - 1;

  while (total < need_total) {
    ssize_t n = recv(fd, buf + total, bufsize - 1 - total, 0);
    if (n <= 0)
      break;
    total += (int)n;
    buf[total] = '\0';
  }

  *body_start = hdr_end;
  *total_len = total;
  return 0;
}

static int hex_val(char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

static void url_decode(char *dst, size_t dstsize, const char *src, size_t srclen)
{
  size_t di = 0;
  for (size_t si = 0; si < srclen && di < dstsize - 1; si++) {
    char c = src[si];
    if (c == '+') {
      dst[di++] = ' ';
    } else if (c == '%' && si + 2 < srclen) {
      int hi = hex_val(src[si + 1]);
      int lo = hex_val(src[si + 2]);
      if (hi >= 0 && lo >= 0) {
        dst[di++] = (char)((hi << 4) | lo);
        si += 2;
      } else {
        dst[di++] = c;
      }
    } else {
      dst[di++] = c;
    }
  }
  dst[di] = '\0';
}

/* Parses an application/x-www-form-urlencoded body into a patch. The
 * solid_r/g/b/w fields are only ever meaningful together -- the caller
 * (the frontend) always sends all four whenever it sends any of them,
 * since ControlPatch.has_solid applies all four channels atomically. */
static void parse_form(const char *body, int body_len, ControlPatch *patch)
{
  memset(patch, 0, sizeof(*patch));

  const char *p = body;
  const char *end = body + body_len;

  while (p < end) {
    const char *amp = memchr(p, '&', (size_t)(end - p));
    const char *pair_end = amp ? amp : end;
    const char *eq = memchr(p, '=', (size_t)(pair_end - p));

    if (eq) {
      char key[32], value[64];
      url_decode(key, sizeof(key), p, (size_t)(eq - p));
      url_decode(value, sizeof(value), eq + 1, (size_t)(pair_end - eq - 1));

      if (strcmp(key, "video") == 0) {
        patch->has_video = 1;
        snprintf(patch->video, NAME_LEN, "%s", value);
      } else if (strcmp(key, "hue") == 0) {
        patch->has_hue = 1;
        patch->hue = atoi(value);
      } else if (strcmp(key, "sat") == 0) {
        patch->has_sat = 1;
        patch->sat = atoi(value);
      } else if (strcmp(key, "val") == 0) {
        patch->has_val = 1;
        patch->val = atoi(value);
      } else if (strcmp(key, "fps") == 0) {
        patch->has_fps = 1;
        patch->fps = atoi(value);
      } else if (strcmp(key, "paused") == 0) {
        patch->has_paused = 1;
        patch->paused = atoi(value);
      } else if (strcmp(key, "started") == 0) {
        patch->has_started = 1;
        patch->started = atoi(value);
      } else if (strcmp(key, "solid_r") == 0) {
        patch->has_solid = 1;
        patch->solid_r = (uint8_t)atoi(value);
      } else if (strcmp(key, "solid_g") == 0) {
        patch->has_solid = 1;
        patch->solid_g = (uint8_t)atoi(value);
      } else if (strcmp(key, "solid_b") == 0) {
        patch->has_solid = 1;
        patch->solid_b = (uint8_t)atoi(value);
      } else if (strcmp(key, "solid_w") == 0) {
        patch->has_solid = 1;
        patch->solid_w = (uint8_t)atoi(value);
      }
    }

    p = pair_end + 1;
  }
}

static int build_state_json(char *buf, size_t bufsize)
{
  ControlState s;
  control_get(&s);

  int n = snprintf(buf, bufsize,
                    "{\"video\":\"%s\",\"hue\":%d,\"sat\":%d,\"val\":%d,\"fps\":%d,"
                    "\"paused\":%s,\"started\":%s,"
                    "\"solid\":{\"r\":%d,\"g\":%d,\"b\":%d,\"w\":%d},"
                    "\"videos\":[",
                    s.video, s.hue, s.sat, s.val, s.fps, s.paused ? "true" : "false",
                    s.started ? "true" : "false", s.solid_r, s.solid_g, s.solid_b, s.solid_w);

  int count = control_video_count();
  for (int i = 0; i < count && n < (int)bufsize; i++)
    n += snprintf(buf + n, bufsize - (size_t)n, "%s\"%s\"", i > 0 ? "," : "", control_video_name(i));

  n += snprintf(buf + n, bufsize - (size_t)n, "]}");
  return n;
}

/* First frame of seq, as a flat [r,g,b,r,g,b,...] array -- a still-frame
 * thumbnail, not the live gamma/HSV-adjusted render. */
static int build_preview_json(char *buf, size_t bufsize, const FrameSequence *seq)
{
  int n = snprintf(buf, bufsize, "{\"width\":%d,\"height\":%d,\"pixels\":[", seq->width, seq->height);

  const uint8_t *pixels = seq->frames[0].pixels;
  int total = seq->width * seq->height * 3;
  for (int i = 0; i < total && n < (int)bufsize; i++)
    n += snprintf(buf + n, bufsize - (size_t)n, "%s%d", i > 0 ? "," : "", pixels[i]);

  n += snprintf(buf + n, bufsize - (size_t)n, "]}");
  return n;
}

static int serve_static(int fd, const char *web_dir, const char *req_path)
{
  for (size_t i = 0; i < sizeof(static_routes) / sizeof(static_routes[0]); i++) {
    if (strcmp(req_path, static_routes[i].path) != 0)
      continue;

    char full[512];
    snprintf(full, sizeof(full), "%s/%s", web_dir, static_routes[i].file);

    FILE *f = fopen(full, "rb");
    if (!f) {
      send_response(fd, 404, "Not Found", "text/plain", "not found", 9);
      return 1;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *data = malloc((size_t)size);
    size_t read_bytes = fread(data, 1, (size_t)size, f);
    fclose(f);

    send_response(fd, 200, "OK", static_routes[i].content_type, data, read_bytes);
    free(data);
    return 1;
  }
  return 0;
}

static void handle_connection(int fd, const char *web_dir)
{
  static char buf[REQ_BUF_SIZE];
  int body_start, total_len;

  if (read_request(fd, buf, sizeof(buf), &body_start, &total_len) < 0)
    return;

  char method[8] = {0}, path[256] = {0};
  sscanf(buf, "%7s %255s", method, path);

  if (strcmp(method, "GET") == 0 && strcmp(path, "/api/state") == 0) {
    char json[4096];
    int n = build_state_json(json, sizeof(json));
    send_response(fd, 200, "OK", "application/json; charset=utf-8", json, (size_t)n);
    return;
  }

  if (strcmp(method, "POST") == 0 && strcmp(path, "/api/state") == 0) {
    ControlPatch patch;
    parse_form(buf + body_start, total_len - body_start, &patch);
    control_apply(&patch);

    char json[4096];
    int n = build_state_json(json, sizeof(json));
    send_response(fd, 200, "OK", "application/json; charset=utf-8", json, (size_t)n);
    return;
  }

  static const char preview_prefix[] = "/api/preview/";
  if (strcmp(method, "GET") == 0 && strncmp(path, preview_prefix, strlen(preview_prefix)) == 0) {
    char name[NAME_LEN];
    const char *raw_name = path + strlen(preview_prefix);
    url_decode(name, sizeof(name), raw_name, strlen(raw_name));

    const FrameSequence *seq = control_find_video(name);
    if (!seq) {
      send_response(fd, 404, "Not Found", "text/plain", "not found", 9);
      return;
    }

    char json[4096];
    int n = build_preview_json(json, sizeof(json), seq);
    send_response(fd, 200, "OK", "application/json; charset=utf-8", json, (size_t)n);
    return;
  }

  if (strcmp(method, "GET") == 0 && serve_static(fd, web_dir, path))
    return;

  send_response(fd, 404, "Not Found", "text/plain", "not found", 9);
}

void httpd_request_stop(void)
{
  stop_requested = 1;
  if (listen_fd >= 0) {
    shutdown(listen_fd, SHUT_RDWR);
    close(listen_fd);
    listen_fd = -1;
  }
}

int httpd_run(int port, const char *web_dir)
{
  signal(SIGPIPE, SIG_IGN);

  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    perror("socket");
    return -1;
  }

  int opt = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons((uint16_t)port);

  if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("bind");
    close(fd);
    return -1;
  }
  if (listen(fd, 8) < 0) {
    perror("listen");
    close(fd);
    return -1;
  }

  listen_fd = fd;
  printf("HTTP control server listening on :%d\n", port);

  while (!stop_requested) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(fd, (struct sockaddr *)&client_addr, &client_len);
    if (client_fd < 0) {
      if (stop_requested)
        break;
      continue;
    }
    handle_connection(client_fd, web_dir);
    close(client_fd);
  }

  return 0;
}
