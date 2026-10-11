/* Couch's network: the start, as PS3 Health's net_start (mod_net.c at
 * 5becd5d), and Health's HTTP client for the Board thread (resolve,
 * tcp_connect, the GET loop), with HTTP/1.0 and a stop at the Board's end
 * line. */
#ifndef HTTP_H
#define HTTP_H

#include <ppu-types.h>

/* The network start: not run yet, up, failed (netInitialize's rc), or not
 * run because the journal holds it as frozen or skipped. */
enum { NET_NONE, NET_UP, NET_FAILED, NET_BLOCKED };
extern int net_state;
extern s32 net_rc;                   /* netInitialize's rc */
extern char net_ip[32];              /* the console's address, "no address", or "not read" */

/* The main thread only: journal_start("net_start"), then netInitialize, the
 * netctl module, netCtlInit and the first address read (behind run_job's
 * counter when the journal works, fs_ok), then journal_done. A frozen or
 * skipped entry runs nothing (NET_BLOCKED). */
void net_start(int fs_ok);
void net_addr(void);                 /* a later read of the address: after the start (libnetctl needs its module), never during a fetch */
int net_has_ip(void);

typedef struct {
    char host[96];
    int port;
    char path[160];
} http_url;

/* How a GET ended: H_OK is a 200 answer with its body (cut or whole: the
 * parser decides); the others name the step that failed. */
enum { H_OK, H_DNS, H_SOCKET, H_CONNECT, H_SEND, H_RECV, H_CLOSED, H_NOT_HTTP, H_HEAD, H_STATUS, H_CHUNKED, H_OVER, H_STOPPED };

typedef struct {
    int step;                        /* H_* */
    s32 err;                         /* net_errno after the failed call */
    int http;                        /* the status, 0 before a head */
    char location[128];              /* a 3xx answer's Location */
    int len;                         /* the body's bytes */
} http_result;

/* GET u with "GET <path> HTTP/1.0", Host, User-Agent Couch/<version> and
 * Connection: close; SO_RCVTIMEO and SO_SNDTIMEO 10 s. It reads until the
 * server closes, the body holds its end line, or the body passes max bytes.
 * body gets the body and a NUL (max + 1 bytes). *on is read between the
 * steps: 0 stops the GET (H_STOPPED). The Board thread's: a static buffer. */
void http_get(const http_url *u, char *body, int max, http_result *r, volatile int *on);

#endif
