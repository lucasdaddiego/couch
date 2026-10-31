/* Couch's network (http.h). The start and the client are PS3 Health's
 * (source/mod_net.c at 5becd5d): net_init's four calls as one journaled
 * call, and resolve, tcp_connect and the download loop, which ran on this
 * console in Health's job thread. Couch's changes: HTTP/1.0 (no chunked
 * answer), SO_RCVTIMEO and SO_SNDTIMEO of 10 s, net_errno kept for the
 * words, a body of at most the caller's max (the Board's 4 KB, 0.3.1's
 * /news 32 KB and /article 16 KB), the stop at the reply's end line, and
 * the exit's part: http_wake shuts the open socket down, net_end ends what the start
 * began. */
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <net/net.h>
#include <net/netctl.h>
#include <sysmodule/sysmodule.h>
#include <net/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/systime.h>
#include <lv2/systime.h>
#include <ps3gfx/ui.h>
#include <ps3gfx/journal.h>
#include "couch.h"
#include "http.h"
#include "start.h"
#include "parse.h"

#define HEAD_MAX 4096                /* the head of an answer, at most */

int net_state = NET_NONE;
s32 net_rc;
s32 net_mod_rc = 1, net_ctl_rc = 1;
char net_ip[32] = "not read";
/* 1 where netInitialize, the netctl module load and netCtlInit succeeded:
 * net_end undoes only these, and a later net_start does not repeat the
 * last two. */
static int net_lib, net_mod, net_ctl;

/* The console's address (Health's net_addr): at the start, then after each
 * fetch in board_take, where the Board thread is idle (DHCP may have
 * answered since). */
void net_addr(void)
{
    union net_ctl_info info;
    if (!net_ctl) {                          /* netCtlInit did not succeed: no netctl call */
        snprintf(net_ip, sizeof net_ip, "not read");
        return;
    }
    memset(&info, 0, sizeof info);
    if (netCtlGetInfo(NET_CTL_INFO_IP_ADDRESS, &info) == 0 && info.ip_address[0] && strcmp(info.ip_address, "0.0.0.0"))
        snprintf(net_ip, sizeof net_ip, "%.15s", info.ip_address);
    else snprintf(net_ip, sizeof net_ip, "no address");
}

int net_has_ip(void) { return strcmp(net_ip, "no address") && strcmp(net_ip, "not read"); }

/* Health's net_init calls, in the job thread of start_job: no journal call
 * here. PSL1GHT's netInitialize loads the net module itself and returns 0
 * at once when it already succeeded. The netctl module and netCtlInit run
 * until each succeeded once (a SQUARE after a failed netInitialize does not
 * load or start them twice), and netCtlInit only with its module loaded. */
static void start_calls(void)
{
    net_rc = netInitialize();
    if (net_rc == 0) net_lib = 1;
    if (!net_mod && (net_mod_rc = sysModuleLoad(SYSMODULE_NETCTL)) == 0) net_mod = 1;   /* libnetctl does not load its module itself */
    if (net_mod && !net_ctl && (net_ctl_rc = netCtlInit()) == 0) net_ctl = 1;
    net_addr();
}

void net_start(int fs_ok)
{
    if (net_state == NET_UP || quit) return; /* a pending quit (Quit Game): no journal line, no call */
    if (!journal_start("net_start")) {       /* 0 for a frozen or skipped entry: no network call at all */
        net_state = NET_BLOCKED;
        snprintf(net_ip, sizeof net_ip, "not read");
        return;
    }
    /* The counter shows the console works, and the journal backs the job
     * screen's skip promise. Without a journal the start still runs, with
     * no job screen: no screen promises a skip that cannot happen. */
    if (fs_ok) start_job(SS_NET, start_calls);   /* the job screen's counter after 2 s (start.c: the kit's run_job in Couch's look) */
    else start_calls();
    journal_done("net_start", net_rc, net_rc == 0);
    net_state = net_rc == 0 ? NET_UP : NET_FAILED;
}

/* PSL1GHT's sample order: httptest's http_end ends the libraries, then
 * unloads the modules; ps3load's finalize joins its thread, then calls
 * netDeinitialize, then ends the RSX. netDeinitialize is netFinalizeNetwork,
 * the free of its 128 KB pool and the net module's unload. No socket is open
 * here: the Board thread ended (board_stop) and it closes each socket. */
void net_end(void)
{
    int ctl = net_ctl, lib = net_lib, mod = net_mod;
    char d[24] = "not run", u[24] = "not run";
    if (!ctl && !lib && !mod) return;        /* never started, or ended already */
    net_ctl = net_lib = net_mod = 0;
    net_state = NET_NONE;
    snprintf(net_ip, sizeof net_ip, "not read");
    couch_log("net_end: begins");            /* the mark of a hang in these calls */
    if (ctl) netCtlTerm();
    if (lib) snprintf(d, sizeof d, "rc 0x%08x", (unsigned)netDeinitialize());
    if (mod) snprintf(u, sizeof u, "rc 0x%08x", (unsigned)sysModuleUnload(SYSMODULE_NETCTL));
    couch_log("net_end: netCtlTerm %s, netDeinitialize %s, the netctl module's unload %s", ctl ? "run" : "not run", d, u);
}

/* ---- the GET (the Board thread) ----------------------------------------------- */

static int resolve(const char *host, in_addr_t *out)
{
    *out = inet_addr(host);
    if (*out != (in_addr_t)-1) return 0;
    struct net_hostent *he = netGetHostByName(host);
    if (!he || !he->h_addr_list) return -1;
    uint32_t *list = (uint32_t *)(uintptr_t)he->h_addr_list;     /* 32-bit pointers in the network library */
    if (!list[0]) return -1;
    *out = *(uint32_t *)(uintptr_t)list[0];
    return 0;
}

/* The GET's socket while it is open, for http_wake: set before the connect,
 * -1 before the close. Only the Board thread opens sockets, one at a time. */
static volatile int sock = -1;

static void sock_close(int s)
{
    sock = -1;
    __sync_synchronize();
    netClose(s);
}

/* The exit's half of a two-sided handshake (Dekker): board_stop stores 0 in
 * *on, then the fence here, then reads sock; tcp_connect stores sock, then a
 * fence, then reads *on. So the thread sees the 0 and closes, or this sees
 * the socket and shuts it down, or both. A shutdown that comes after the
 * close finds a closed descriptor: an error and no harm, since the thread
 * opens no socket after it saw the 0. */
void http_wake(void)
{
    __sync_synchronize();
    int s = sock;
    if (s >= 0) netShutdown(s, SHUT_RDWR);
}

/* The socket, or -1 (socket) or -2 (connect) with *err, or -3 (*on was 0). */
static int tcp_connect(in_addr_t ip, int port, s32 *err, volatile int *on)
{
    int s = netSocket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s < 0) {
        *err = net_errno;
        return -1;
    }
    sock = s;
    __sync_synchronize();                    /* sock is visible before *on is read (http_wake) */
    if (!*on) {
        sock_close(s);
        return -3;
    }
    struct timeval tv = {10, 0};
    netSetSockOpt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    netSetSockOpt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_len = sizeof a;
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = ip;
    if (netConnect(s, (struct sockaddr *)&a, sizeof a) < 0) {
        *err = net_errno;
        sock_close(s);
        return -2;
    }
    return s;
}

/* the head and the body, the Board thread's only: room for the largest
 * reply, /news's 32 KB (0.3.1's reader; the Board's is 4 KB) */
static char rx[HEAD_MAX + NEWS_MAX + 1] __attribute__((aligned(128)));

/* The GET's start, shared by both GETs: the result cleared, the lookup, the
 * connect, the request sent. The socket, or -1 with r->step set. */
static int get_start(const http_url *u, http_result *r, volatile int *on)
{
    char req[400];
    in_addr_t ip;
    s64 t0 = sysGetSystemTime();
    memset(r, 0, sizeof *r);
    r->connect_us = -1;
    r->expect = -1;
    if (!*on) {
        r->step = H_STOPPED;
        return -1;
    }
    if (resolve(u->host, &ip)) {             /* the one call no shutdown ends: the resolver's own timeout */
        r->step = H_DNS;
        return -1;
    }
    if (!*on) {
        r->step = H_STOPPED;
        return -1;
    }
    int s = tcp_connect(ip, u->port, &r->err, on);
    if (s < 0) {
        r->step = s == -1 ? H_SOCKET : s == -2 ? H_CONNECT : H_STOPPED;
        return -1;
    }
    r->connect_us = sysGetSystemTime() - t0;   /* the DNS lookup and the TCP connect */
    if (!*on) {                              /* the exit came during the connect: a shutdown before it may not end a connect */
        sock_close(s);
        r->step = H_STOPPED;
        return -1;
    }
    char port[8] = "";
    if (u->port != 80) snprintf(port, sizeof port, ":%d", u->port);
    int n = snprintf(req, sizeof req, "GET %s HTTP/1.0\r\nHost: %s%s\r\nUser-Agent: Couch/%s\r\nConnection: close\r\n\r\n", u->path,
                     u->host, port, APP_VERSION);
    if (n <= 0 || n >= (int)sizeof req || netSend(s, req, n, 0) != n) {
        r->err = net_errno;
        sock_close(s);
        r->step = H_SEND;
        return -1;
    }
    return s;
}

/* The head's Content-Length (the head's n bytes, case not counted), -1 for none or a bad one. */
static int content_length(const char *h, int n)
{
    for (int i = 0; i + 16 < n; i++) {
        if ((h[i] == '\n') && !strncasecmp(h + i + 1, "content-length:", 15)) {
            const char *p = h + i + 16;
            while (p < h + n && *p == ' ') p++;
            long v = 0;
            int d = 0;
            while (p < h + n && *p >= '0' && *p <= '9' && d < 10) v = v * 10 + (*p++ - '0'), d++;
            return d ? (int)(v > 0x7fffffff ? 0x7fffffff : v) : -1;
        }
    }
    return -1;
}

void http_get(const http_url *u, char *body, int max, http_result *r, volatile int *on)
{
    body[0] = 0;
    int s = get_start(u, r, on);
    if (s < 0) return;
    int got = 0, head = 0, fail = 0, chunked = 0, scanned = 0;   /* scanned: the body's bytes read for the end line */
    while (1) {
        if (!*on) {
            fail = H_STOPPED;
            break;
        }
        int room = (int)sizeof rx - 1 - got;
        if (room <= 0) {                     /* the head and a body past max */
            fail = head ? H_OVER : H_HEAD;
            break;
        }
        ssize_t k = netRecv(s, rx + got, (size_t)room, 0);
        if (k < 0) {                         /* also SO_RCVTIMEO's 10 s */
            r->err = net_errno;
            fail = H_RECV;
            break;
        }
        if (k == 0) break;                   /* the server closed: the end of the answer */
        got += (int)k;
        if (!head) {
            head = http_head(rx, got, &r->http, r->location, sizeof r->location, &chunked);
            if (head < 0) {
                head = 0;
                fail = H_NOT_HTTP;
                break;
            }
            if (!head) {
                if (got > HEAD_MAX) {
                    fail = H_HEAD;
                    break;
                }
                continue;
            }
            if (r->http != 200 || chunked) break;
        }
        if (got - head > max) {
            fail = H_OVER;
            break;
        }
        /* the end line: no wait for the close. Only the new bytes and the 5
         * before them (a line end and "end" that two reads split) are read:
         * a 32 KB reply in 1460-byte reads is not read 23 times over. */
        int from = scanned > 5 ? scanned - 5 : 0;
        if (board_has_end(rx + head + from, got - head - from)) break;
        scanned = got - head;
    }
    sock_close(s);
    if (!head) r->step = fail ? fail : H_CLOSED;   /* no head: the failed step, or a close before the answer */
    else if (r->http != 200) r->step = H_STATUS;
    else if (chunked) r->step = H_CHUNKED;
    else if (fail == H_OVER || fail == H_STOPPED) r->step = fail;
    else r->step = H_OK;                     /* a recv error after the head: the body as it came, the parser decides */
    if (head && r->step == H_OK) {
        int len = got - head > max ? max : got - head;
        memcpy(body, rx + head, (size_t)len);
        body[len] = 0;
        r->len = len;
    }
}

void http_get_bin(const http_url *u, unsigned char *buf, int cap, http_result *r, volatile int *on)
{
    int s = get_start(u, r, on);
    if (s < 0) return;
    int got = 0, head = 0, fail = 0, chunked = 0, len = 0;   /* got: the head's bytes in rx; len: the body's bytes in buf */
    while (1) {
        if (!*on) {
            fail = H_STOPPED;
            break;
        }
        ssize_t k;
        if (!head) {                         /* the head through rx, as the text GET */
            int room = HEAD_MAX - got;
            if (room <= 0) {
                fail = H_HEAD;
                break;
            }
            k = netRecv(s, rx + got, (size_t)room, 0);
            if (k < 0) {
                r->err = net_errno;
                fail = H_RECV;
                break;
            }
            if (k == 0) break;
            got += (int)k;
            head = http_head(rx, got, &r->http, r->location, sizeof r->location, &chunked);
            if (head < 0) {
                head = 0;
                fail = H_NOT_HTTP;
                break;
            }
            if (!head) continue;
            if (r->http != 200 || chunked) break;
            r->expect = content_length(rx, head);
            if (r->expect > cap) {
                fail = H_OVER;
                break;
            }
            len = got - head;                /* the body's first bytes came with the head */
            if (len > cap) {
                fail = H_OVER;
                break;
            }
            memcpy(buf, rx + head, (size_t)len);
        } else {
            int room = cap - len;
            if (room <= 0) {                 /* a body past cap: one more byte tells */
                unsigned char one;
                k = netRecv(s, &one, 1, 0);
                if (k > 0) fail = H_OVER;
                break;
            }
            k = netRecv(s, buf + len, (size_t)room, 0);
            if (k < 0) {
                r->err = net_errno;
                fail = H_RECV;
                break;
            }
            if (k == 0) break;
            len += (int)k;
        }
        if (r->expect >= 0 && len >= r->expect) break;   /* the whole body: no wait for the close */
    }
    sock_close(s);
    if (!head) r->step = fail ? fail : H_CLOSED;
    else if (r->http != 200) r->step = H_STATUS;
    else if (chunked) r->step = H_CHUNKED;
    else if (fail == H_OVER || fail == H_STOPPED) r->step = fail;
    else r->step = H_OK;                     /* a recv error after the head: the bytes as they came, r->expect tells a cut */
    r->len = head && r->step == H_OK ? len : 0;
}
