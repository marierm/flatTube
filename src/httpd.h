#ifndef HTTPD_H
#define HTTPD_H

/*
 * Minimal single-threaded, one-request-per-connection HTTP/1.1 server.
 * Serves a small whitelist of static files from web_dir plus a JSON
 * control API backed by control.c (GET/POST /api/state).
 *
 * Blocking: call from its own thread. Runs until httpd_request_stop()
 * is called from another thread, which closes the listening socket to
 * unblock accept(). Returns 0 on clean shutdown, -1 if the socket
 * could not be set up.
 */
int httpd_run(int port, const char *web_dir);

void httpd_request_stop(void);

#endif
