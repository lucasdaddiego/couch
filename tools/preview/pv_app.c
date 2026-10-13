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
 *   PV_BOARD_FAIL=<list>  per connect, comma-separated, the last entry
 *                         repeats: ok, refused, silent (no answer: the
 *                         10 s SO_RCVTIMEO runs out at once), cut (the body
 *                         without its last 30 bytes), 301, v2 (line 1
 *                         "couch-board 2"), hang (recv waits until a
 *                         netShutdown, at most 30 s of host time), slow
 *                         (the answer comes 4 s of the preview's clock
 *                         after the connect: a slow internet)
 *   PV_TZ, PV_SUMMER, PV_TIMEFMT  the three settings (default -180, 0, 1)
 *   PV_TZ_FAIL=1          the three calls fail
 *   PV_NOIP=1             netCtlGetInfo fails: the console has no address
 *   PV_NET_FAIL=1         netInitialize fails (rc 0x80010002)
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
#include "net/net.h"
#include "net/netctl.h"
#include "netinet/in.h"
#include "sysutil/sysutil.h"
#include "lv2/systime.h"

#define DEG "\xc2\xb0"

static const char default_body[] =
    "couch-board 1\n"
    "now=1791614218\n"
    "offset=-10800\n"
    "city=Buenos Aires\n"
    "usd=Oficial|1.485,00|1.535,00|Fri 15:00\n"
    "usd=Blue|1.520,00|1.540,00|Fri 18:00\n"
    "usd=MEP|1.535,90|1.542,90|Fri 18:00\n"
    "usd=CCL|1.605,50|1.606,60|Fri 18:00\n"
    "usd=Cripto|1.601,81|1.606,88|Fri 18:00\n"
    "usd=Tarjeta|1.930,50|1.995,50|Fri 15:00\n"
    "usd_age=42\n"
    "usd_state=ok\n"
    "wx_now=11" DEG "|10" DEG "|92%|Overcast|9 km/h|night\n"
    "day=Today|Rain showers|16" DEG "|11" DEG "|96%\n"
    "day=Sun|Overcast|16" DEG "|9" DEG "|2%\n"
    "day=Mon|Drizzle|16" DEG "|10" DEG "|22%\n"
    "wx_age=310\n"
    "wx_state=ok\n"
    "end\n";

enum { K_OK, K_REFUSED, K_SILENT, K_CUT, K_301, K_V2, K_HANG, K_SLOW, K_KINDS };

extern volatile int quit;                    /* ps3gfx's: Quit Game from the PS button menu */

/* The kind of connect n (0 first) from PV_BOARD_FAIL. */
static int kind_at(int n)
{
    static const char *const names[K_KINDS] = {"ok", "refused", "silent", "cut", "301", "v2", "hang", "slow"};
    const char *s = getenv("PV_BOARD_FAIL");
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
    int used, kind, in_len, out_len, out_pos;
    volatile int shut;                       /* netShutdown came (the main thread) */
    u64 t0;                                  /* the preview's clock at the connect */
    char in[2048];
    char *out;
} sk[NSOCK];
static int connects;
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
    sk[i].t0 = sysGetSystemTime();
    return 0;
}

/* The body: PV_BOARD's file, or the default; malloc'd, *len bytes. */
static char *body_text(int *len)
{
    const char *p = getenv("PV_BOARD");
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

/* The answer to a whole request, by its kind. */
static void answer(int i)
{
    int blen = 0;
    char *body = body_text(&blen);
    char *out = malloc(65536 + 512);
    if (!body || !out) {
        free(body);
        free(out);
        return;
    }
    int n;
    if (sk[i].kind == K_301)
        n = snprintf(out, 512, "HTTP/1.1 301 Moved Permanently\r\nLocation: https://couch.daddiego.com.ar/board\r\nContent-Length: 0\r\n"
                               "Connection: close\r\n\r\n");
    else {
        if (sk[i].kind == K_V2 && blen >= 13 && !memcmp(body, "couch-board 1", 13)) body[12] = '2';
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
