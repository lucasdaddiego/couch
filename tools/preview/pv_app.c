/* Couch's part of the host preview: the Board's HTTP peer, the console's
 * address and its clock settings. The socket calls here are strong and
 * replace the library's offline weak ones (stubs.c). A connect to port 80 or
 * 8787 reaches the peer, which answers one GET with a body and closes; any
 * other port refuses. DNS stays the library's, which answers nothing: a fake
 * host entry cannot work on a 64-bit host, since the network library's
 * entry holds 32-bit pointers (Health's resolve reads them). So run.zsh
 * writes board_url.txt with a dotted address, the console round's path, and
 * the default URL shows the DNS failure.
 *   PV_BOARD=<file>       the body (default: the Worker test's reply of
 *                         2026-10-10, a reply of 0.1.0's time, below;
 *                         run.zsh always passes the current one)
 *   PV_BOARD2=<file>      the body of the second and later connects (a
 *                         refresh with new values: the Board's counting
 *                         and glow); absent: PV_BOARD's
 *   PV_BOARD_FAIL=<list>  per connect, comma-separated, the last entry
 *                         repeats: ok, refused, silent (no answer: the
 *                         10 s SO_RCVTIMEO runs out at once), cut (the body
 *                         without its last 30 bytes), 301, v2 (line 1
 *                         "couch-board 2"), hang (recv waits until a
 *                         netShutdown, at most 30 s of host time), slow
 *                         (the answer comes 4 s of the preview's clock
 *                         after the connect: a slow internet)
 *   PV_NEWS=<file>        the body of GET /news (0.3.1's reader); "404" or
 *                         absent: HTTP 404 (a Worker before the reader)
 *   PV_ARTICLES=<dir>     GET /article?id=<id> answers <dir>/<id>.txt, and
 *                         the Worker's "state=none|unknown id" reply for
 *                         an id with no file; absent: HTTP 404
 *   PV_FUTBOL=<dir>       GET /futbol?d=<0 or 1> answers <dir>/d<n>.txt
 *                         (0.5.0's Futbol page); absent, or no file: 404
 *   PV_NEWS_FAIL, PV_ARTICLE_FAIL, PV_FUTBOL_FAIL=<list>  per /news, /article or /futbol request (in
 *                         their own counts, the last entry repeats), the
 *                         answer of PV_BOARD_FAIL's kinds but refused (a
 *                         connect cannot see its path), and 404; it takes
 *                         the place of the connect's PV_BOARD_FAIL kind
 *   PV_TZ, PV_SUMMER, PV_TIMEFMT  the three settings (default -180, 0, 1)
 *   PV_TZ_FAIL=1          the three calls fail
 *   PV_NOIP=1             netCtlGetInfo fails: the console has no address
 *   PV_NET_FAIL=1         netInitialize fails (rc 0x80010002)
 *   PV_NET_SLOW=<ms>      netInitialize takes that long (real time)
 *   PV_NETCTL_FAIL=1      netCtlInit fails (rc 0x80130101)
 *   PV_QUIT=net           netInitialize sets quit, as a Quit Game during the
 *                         network start would (the preview's system sends
 *                         no events)
 * The peer appends each request, as it came, to $PV_ROOT/peer.log, and a
 * "pv: " line for netCtlInit, for each exit call (netShutdown, netCtlTerm,
 * netDeinitialize, sysModuleUnload) and for the end of a hang. The Board thread makes every
 * socket call but netShutdown, which the exit makes from the main thread:
 * it only sets the slot's flag, and they keep no lock. The library's
 * sysModuleLoad is not weak, so the module load cannot fail here. */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "net/net.h"
#include "net/netctl.h"
#include "netinet/in.h"
#include "sysutil/sysutil.h"
#include "lv2/systime.h"

#define DEG "\xc2\xb0"

static const char default_body[] =
    "couch-board 1\n"
    "now=1792164000\n"
    "offset=7200\n"
    "city=Berlin\n"
    "rate=USD|-|0,8887|16 Oct\n"
    "rate=GBP|-|1,1850|16 Oct\n"
    "rate=CHF|-|1,0785|16 Oct\n"
    "rate_age=42\n"
    "rate_state=ok\n"
    "wx_now=11" DEG "|10" DEG "|92%|Overcast|9 km/h|night\n"
    "day=Today|Rain showers|16" DEG "|11" DEG "|96%\n"
    "day=Sat|Overcast|16" DEG "|9" DEG "|2%\n"
    "day=Sun|Drizzle|16" DEG "|10" DEG "|22%\n"
    "wx_age=310\n"
    "wx_state=ok\n"
    "end\n";

enum { K_OK, K_REFUSED, K_SILENT, K_CUT, K_301, K_V2, K_HANG, K_SLOW, K_404, K_KINDS };

extern volatile int quit;                    /* ps3gfx's: Quit Game from the PS button menu */

/* The kind of entry n (0 first) of a list: PV_BOARD_FAIL's, per connect. */
static int kind_of(const char *s, int n)
{
    static const char *const names[K_KINDS] = {"ok", "refused", "silent", "cut", "301", "v2", "hang", "slow", "404"};
    char tok[16] = "ok";
    for (int i = 0; s && *s; i++) {
        int len = (int)strcspn(s, ",");
        snprintf(tok, sizeof tok, "%.*s", len, s);
        if (i == n || !s[len]) break;
        s += len + 1;
    }
    for (int k = 0; k < K_KINDS; k++)
        if (!strcmp(tok, names[k])) return k;
    return K_OK;
}

static int kind_at(int n) { return kind_of(getenv("PV_BOARD_FAIL"), n); }

/* One "pv: " line in peer.log. */
static void note(const char *fmt, ...)
{
    char p[1024];
    snprintf(p, sizeof p, "%s/peer.log", getenv("PV_ROOT") ? getenv("PV_ROOT") : "/tmp/pv");
    FILE *f = fopen(p, "ab");
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    fputs("pv: ", f);
    vfprintf(f, fmt, ap);
    fputc('\n', f);
    va_end(ap);
    fclose(f);
}

#define NSOCK 8
#define FD0 3
static struct {
    int used, kind, n, in_len, out_len, out_pos;   /* n: the connect's number, 0 first */
    volatile int shut;                       /* netShutdown came (the main thread) */
    u64 t0;                                  /* the preview's clock at the connect */
    char in[2048];
    char *out;
} sk[NSOCK];
static int connects, news_reqs, art_reqs, fut_reqs;
static s32 err_val = NET_ENETDOWN;

s32 *netErrnoLoc(void) { return &err_val; }

static int slot_of(s32 s)
{
    int i = s - FD0;
    return i >= 0 && i < NSOCK && sk[i].used ? i : -1;
}

s32 netSocket(s32 d, s32 t, s32 p)
{
    (void)d;
    (void)t;
    (void)p;
    for (int i = 0; i < NSOCK; i++)
        if (!sk[i].used) {
            memset(&sk[i], 0, sizeof sk[i]);
            sk[i].used = 1;
            return FD0 + i;
        }
    err_val = 24;                                        /* NET_EMFILE */
    return -1;
}

s32 netSetSockOpt(s32 s, s32 lv, s32 o, const void *v, socklen_t l)
{
    (void)lv;
    (void)o;
    (void)v;
    (void)l;
    return slot_of(s) < 0 ? -1 : 0;
}

s32 netConnect(s32 s, const struct sockaddr *a, socklen_t l)
{
    (void)l;
    int i = slot_of(s);
    if (i < 0) return -1;
    const struct sockaddr_in *in = (const struct sockaddr_in *)(const void *)a;
    int kind = kind_at(connects++);
    if ((in->sin_port != 80 && in->sin_port != 8787) || kind == K_REFUSED) {   /* htons is the identity in the stub */
        err_val = NET_ECONNREFUSED;
        return -1;
    }
    sk[i].kind = kind;
    sk[i].n = connects - 1;
    sk[i].t0 = sysGetSystemTime();
    return 0;
}

/* The body of connect n: PV_BOARD2's file after the first when it is set,
 * else PV_BOARD's, else the default; malloc'd, *len bytes. */
static char *body_text(int n, int *len)
{
    const char *p = n > 0 && getenv("PV_BOARD2") ? getenv("PV_BOARD2") : getenv("PV_BOARD");
    FILE *f = p ? fopen(p, "rb") : NULL;
    char *b = malloc(65536);
    if (!b) return NULL;
    if (f) {
        *len = (int)fread(b, 1, 65535, f);
        fclose(f);
    } else {
        *len = (int)strlen(default_body);
        memcpy(b, default_body, (size_t)*len);
    }
    return b;
}

/* The path of the request's line ("GET <path> HTTP/1.0"). */
static void req_path(int i, char *path, int n)
{
    path[0] = 0;
    if (!strncmp(sk[i].in, "GET ", 4)) snprintf(path, (size_t)n, "%.*s", (int)strcspn(sk[i].in + 4, " \r\n"), sk[i].in + 4);
}

/* A file's bytes, malloc'd (64 KB at most), or NULL. */
static char *file_text(const char *p, int *len)
{
    FILE *f = p ? fopen(p, "rb") : NULL;
    if (!f) return NULL;
    char *b = malloc(65536);
    if (b) *len = (int)fread(b, 1, 65535, f);
    fclose(f);
    return b;
}

/* The reader's paths (0.3.1) and the Futbol page's (0.5.0): the body of
 * /news, /article or /futbol, and the request's kind from PV_NEWS_FAIL,
 * PV_ARTICLE_FAIL or PV_FUTBOL_FAIL when set; NULL and K_404 for no body. 0
 * for another path (the Board's). */
static int reader_body(int i, char **body, int *len)
{
    char path[300], p[1100];
    req_path(i, path, sizeof path);
    const char *q = strrchr(path, '/');
    q = q ? q + 1 : path;
    if (!strcmp(q, "news")) {
        const char *f = getenv("PV_NEWS_FAIL");
        if (f) sk[i].kind = kind_of(f, news_reqs);
        news_reqs++;
        const char *n = getenv("PV_NEWS");
        *body = n && strcmp(n, "404") ? file_text(n, len) : NULL;
        if (!*body) sk[i].kind = K_404;
        return 1;
    }
    if (!strncmp(q, "futbol?d=", 9)) {
        const char *f = getenv("PV_FUTBOL_FAIL"), *d = getenv("PV_FUTBOL");
        if (f) sk[i].kind = kind_of(f, fut_reqs);
        fut_reqs++;
        *body = NULL;
        if (d) {
            snprintf(p, sizeof p, "%s/d%c.txt", d, q[9] == '1' ? '1' : '0');
            *body = file_text(p, len);
        }
        if (!*body) sk[i].kind = K_404;
        return 1;
    }
    if (!strncmp(q, "article?id=", 11)) {
        const char *f = getenv("PV_ARTICLE_FAIL"), *d = getenv("PV_ARTICLES");
        if (f) sk[i].kind = kind_of(f, art_reqs);
        art_reqs++;
        *body = NULL;
        if (!d) {
            sk[i].kind = K_404;
            return 1;
        }
        snprintf(p, sizeof p, "%s/%.8s.txt", d, q + 11);
        *body = file_text(p, len);
        if (!*body && (*body = malloc(256)))
            *len = snprintf(*body, 256, "couch-article 1\nnow=1792022946\nstate=none|unknown id\nend\n");
        return 1;
    }
    return 0;
}

/* The answer to a whole request, by its kind. */
static void answer(int i)
{
    int blen = 0;
    char *body = NULL;
    if (!reader_body(i, &body, &blen)) body = body_text(sk[i].n, &blen);
    else if (!body && (body = malloc(1))) blen = 0;
    char *out = malloc(65536 + 512);
    if (!body || !out) {
        free(body);
        free(out);
        return;
    }
    int n;
    if (sk[i].kind == K_404)
        n = snprintf(out, 512, "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
    else if (sk[i].kind == K_301)
        n = snprintf(out, 512, "HTTP/1.1 301 Moved Permanently\r\nLocation: https://board.example.com/board\r\nContent-Length: 0\r\n"
                               "Connection: close\r\n\r\n");
    else {
        if (sk[i].kind == K_V2 && blen >= 13 && !memcmp(body, "couch-board 1", 13)) body[12] = '2';
        if (sk[i].kind == K_V2 && blen >= 12 && !memcmp(body, "couch-news 1", 12)) body[11] = '2';
        if (sk[i].kind == K_V2 && blen >= 14 && !memcmp(body, "couch-futbol 1", 14)) body[13] = '2';
        int send_len = sk[i].kind == K_CUT ? (blen > 30 ? blen - 30 : 0) : blen;
        n = snprintf(out, 512, "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: %d\r\nCache-Control: no-store\r\n"
                               "Connection: close\r\n\r\n", blen);
        memcpy(out + n, body, (size_t)send_len);
        n += send_len;
    }
    free(body);
    sk[i].out = out;
    sk[i].out_len = n;
    sk[i].out_pos = 0;
}

static void log_request(int i)
{
    char p[1024];
    snprintf(p, sizeof p, "%s/peer.log", getenv("PV_ROOT") ? getenv("PV_ROOT") : "/tmp/pv");
    FILE *f = fopen(p, "ab");
    if (!f) return;
    fprintf(f, "--- connect %d\n", connects);
    fwrite(sk[i].in, 1, (size_t)sk[i].in_len, f);
    fclose(f);
}

ssize_t netSend(s32 s, const void *b, size_t l, s32 f)
{
    (void)f;
    int i = slot_of(s);
    if (i < 0) {
        err_val = 9;                                     /* NET_EBADF */
        return -1;
    }
    size_t room = sizeof sk[i].in - 1 - (size_t)sk[i].in_len, k = l < room ? l : room;
    memcpy(sk[i].in + sk[i].in_len, b, k);
    sk[i].in_len += (int)k;
    sk[i].in[sk[i].in_len] = 0;
    if (!sk[i].out && strstr(sk[i].in, "\r\n\r\n")) {
        log_request(i);
        answer(i);
    }
    return (ssize_t)l;
}

ssize_t netRecv(s32 s, void *b, size_t l, s32 f)
{
    (void)f;
    int i = slot_of(s);
    if (i >= 0 && sk[i].kind == K_HANG) {   /* a peer that never answers, and a recv with no timeout */
        for (int t = 0; t < 30000 && !sk[i].shut; t++) usleep(1000);
        note(sk[i].shut ? "recv: the shutdown ended it" : "recv: no shutdown in 30 s");
        if (sk[i].shut) return 0;            /* BSD: a read side shut down reads as the end */
        err_val = NET_EAGAIN;
        return -1;
    }
    if (i >= 0 && sk[i].kind == K_SLOW)      /* the preview's clock moves with the frames: 4 s of it, whatever the host's speed */
        while (sysGetSystemTime() - sk[i].t0 < 4000000 && !sk[i].shut) usleep(1000);
    if (i < 0 || !sk[i].out || sk[i].kind == K_SILENT) {
        err_val = NET_EAGAIN;                            /* SO_RCVTIMEO ran out */
        return -1;
    }
    int n = sk[i].out_len - sk[i].out_pos;
    if (n > (int)l) n = (int)l;
    if (n > 700) n = 700;                                /* a few recv calls per answer, as on a network */
    memcpy(b, sk[i].out + sk[i].out_pos, (size_t)n);
    sk[i].out_pos += n;
    return n;                                            /* 0 once all is sent: the peer closed */
}

s32 netClose(s32 s)
{
    int i = slot_of(s);
    if (i < 0) return -1;
    free(sk[i].out);
    memset(&sk[i], 0, sizeof sk[i]);
    return 0;
}

s32 netInitialize(void)
{
    if (getenv("PV_QUIT") && !strcmp(getenv("PV_QUIT"), "net")) quit = 1;
    if (getenv("PV_NET_SLOW")) usleep((useconds_t)atoi(getenv("PV_NET_SLOW")) * 1000);   /* a slow start (ms): the job screen's counter */
    return getenv("PV_NET_FAIL") ? (s32)0x80010002 : 0;
}

s32 netShutdown(s32 s, s32 how)
{
    int i = slot_of(s);
    note("netShutdown %d how %d%s", s, how, i < 0 ? ": no such socket" : "");
    if (i < 0) {
        err_val = 9;                                     /* NET_EBADF */
        return -1;
    }
    sk[i].shut = 1;
    return 0;
}

s32 netDeinitialize(void)
{
    note("netDeinitialize");
    return 0;
}

s32 netCtlInit(void)
{
    note("netCtlInit");
    return getenv("PV_NETCTL_FAIL") ? (s32)0x80130101 : 0;
}

void netCtlTerm(void) { note("netCtlTerm"); }

s32 sysModuleUnload(u32 id)
{
    note("sysModuleUnload 0x%x", (unsigned)id);
    return 0;
}

s32 netCtlGetInfo(s32 code, union net_ctl_info *info)
{
    (void)code;
    if (getenv("PV_NOIP")) return (s32)0x80130106;
    snprintf(info->ip_address, sizeof info->ip_address, "192.168.0.129");
    return 0;
}

static int env_int(const char *name, int dflt) { return getenv(name) ? atoi(getenv(name)) : dflt; }

s32 sysUtilGetSystemParamInt(s32 id, s32 *value)
{
    if (getenv("PV_TZ_FAIL")) return (s32)0x8002b102;
    switch (id) {
    case SYSUTIL_SYSTEMPARAM_ID_TIME_FORMAT: *value = env_int("PV_TIMEFMT", 1); return 0;
    case SYSUTIL_SYSTEMPARAM_ID_TIMEZONE: *value = env_int("PV_TZ", -180); return 0;
    case SYSUTIL_SYSTEMPARAM_ID_SUMMERTIME: *value = env_int("PV_SUMMER", 0); return 0;
    default: return (s32)0x8002b102;
    }
}

/* ---- boot.txt at the cap ------------------------------------------------- */

/* boot_mark's rename (PS3 Health's pv_app.c): the library's stubs have no
 * sysLv2FsRename (stub/pv_couch.h declares it). The paths go to the sandbox
 * as stubs.c maps them ($PV_ROOT, else /tmp/pv). An existing target is
 * refused, as lv2 does: a boot.c that stops removing boot.old.txt first
 * empties boot.txt here, as on the console. */
s32 sysLv2FsRename(const char *from, const char *to)
{
    char a[1024], b[1024];
    struct stat st;
    const char *root = getenv("PV_ROOT") ? getenv("PV_ROOT") : "/tmp/pv";
    snprintf(a, sizeof a, "%s%s", root, from);
    snprintf(b, sizeof b, "%s%s", root, to);
    if (stat(b, &st) == 0) return (s32)0x80010014;                   /* EEXIST */
    return rename(a, b) ? (s32)0x80010006 : 0;
}
