/* Couch's network: the start, as PS3 Health's net_start (mod_net.c at
 * 5becd5d), Health's HTTP client for the Board thread (resolve,
 * tcp_connect, the GET loop), with HTTP/1.0 and a stop at the Board's end
 * line, and the exit's part (http_wake, net_end). */
#ifndef HTTP_H
#define HTTP_H

#include <ppu-types.h>

/* The network start: not run yet, up, failed (netInitialize's rc), or not
 * run because the journal holds it as frozen or skipped. */
enum { NET_NONE, NET_UP, NET_FAILED, NET_BLOCKED };
extern int net_state;
extern s32 net_rc;                   /* netInitialize's rc */
extern s32 net_mod_rc, net_ctl_rc;   /* the netctl module load's and netCtlInit's last rcs, 1 before a call */
extern char net_ip[32];              /* the console's address, "no address", or "not read" */

/* The main thread only: journal_start("net_start"), then netInitialize, the
 * netctl module, netCtlInit and the first address read (behind run_job's
 * counter when the journal works, fs_ok), then journal_done. A frozen or
 * skipped entry runs nothing (NET_BLOCKED), and so does a pending quit (the
 * state stays as it was). A failed netInitialize runs again at the next
 * call; the module load and netCtlInit run until each succeeded once. */
void net_start(int fs_ok);
void net_addr(void);                 /* a later read of the address ("not read" when netCtlInit did not succeed), never during a fetch */
int net_has_ip(void);
/* The exit's end of the network, after the Board thread ended and before
 * ui_end: netCtlTerm, netDeinitialize, the netctl module's unload (PSL1GHT's
 * sample order), each only where its start succeeded. The flags clear first:
 * a second call does nothing. The main thread only. */
void net_end(void);

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
/* The exit (board_stop), after it set *on to 0: netShutdown on the GET's
 * socket while one is open, so that a recv or send in the Board thread
 * returns at once. It does not end a DNS lookup, and a connect may run on
 * to its own timeout: http_get reads *on after each. */
void http_wake(void);

#endif
