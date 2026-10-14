/* The start (start.h): the loading screen, the steps' times, the job
 * screen, the start animation. */
#include <stdio.h>
#include <string.h>
#include <sys/thread.h>
#include <lv2/systime.h>
#include <sys/systime.h>
#include <ps3gfx/ui.h>
#include "couch.h"
#include "boot.h"
#include "start.h"

const char *const start_name[SS_N] = {"ui_init", "text_utf8", "digits_init", "icons_init", "fs_selftest",
                                      "fx_start", "net_start", "first_frame", "first_fetch"};
/* The loading screen's words for the steps it lists (SS_UI to SS_NET). */
static const char *const shown_name[SS_FRAME] = {"The picture",    "Accented letters", "The display digits",  "The icons",
                                                 "The app folder", "The effects (6 SPUs)", "The network"};

s64 t_main;
static s64 t_begin[SS_N], t_end[SS_N];      /* from sysGetSystemTime; 0: not yet */
static int running = -1;                    /* the step on the screen */
static int loading = 1;                     /* the loading screen lists every step until the loop's first frame */
static int logged_late;
static s64 t_first;                         /* the loading screen's first frame on the TV (its ui_flip returned) */

static s64 now(void) { return (s64)sysGetSystemTime(); }
static double sec_of(s64 us) { return (double)us / 1e6; }

void start_main(void) { t_main = now(); }

/* A small sofa, drawn with the kit's shapes (the pkg icon's, in outline):
 * the back, the seat, two arms. */
static void sofa(float cx, float cy)
{
    const u32 c = 0x3a6fb8ff, d = 0x24497eff;
    round_rect(cx - 70, cy - 40, 140, 52, 16, c);          /* the back */
    round_rect(cx - 86, cy + 2, 172, 34, 12, d);           /* the seat */
    round_rect(cx - 104, cy - 14, 34, 60, 14, c);          /* the arms */
    round_rect(cx + 70, cy - 14, 34, 60, 14, c);
    rect(cx - 80, cy + 44, 10, 12, d);                      /* the feet */
    rect(cx + 70, cy + 44, 10, 12, d);
}

/* One loading frame: the name, "Starting", the steps (done, running with
 * the spinner, still to come), and the counter of a job (sec >= 0). Only
 * the kit's ASCII text: the other atlases may not be loaded yet. */
static void draw(double sec)
{
    begin_frame();
    float cx = (float)SW / 2, y = (float)SH * 0.20f;
    sofa(cx, y);
    y += 92;
    text_c(cx, y, F_TITLE, WHITE, "Couch");
    text_c(cx, y + (float)line_h(F_TITLE) + 2, F_SMALL, DIM, "%s", APP_VERSION);
    y = (float)SH * 0.46f;
    text_c(cx, y, F_MED, GREY, "Starting ...");
    y += (float)line_h(F_MED) + 28;
    float x = cx - 210, rh = 42, t = sec_of(now() - t_main);
    for (int i = 0; i < SS_FRAME; i++) {
        if (!loading && i != running) continue;
        float my = y + (float)line_h(F_BODY) / 2;
        u32 c;
        if (i == running) {
            spinner(x + 12, my, 13, sec >= 0 ? sec : t);
            c = WHITE;
        } else if (t_end[i]) {
            disc(x + 12, my, 6, GREEN);
            c = GREY;
        } else {
            ring(x + 12, my, 6, 2, DIM);
            c = DIM;
        }
        float xe = text(x + 40, y, F_BODY, c, "%s", shown_name[i]);
        if (i == running && sec >= 0) text(xe + 16, y, F_BODY | TNUM, GREY, "%.1f s", sec);
        y += rh;
    }
    if (sec >= 0 && fs_ok)
        text_wrap(MG, (float)SH - 150, F_SMALL, GREY, (float)SW - 2 * MG, 2,
                  "The counter runs while the console works. If it stops, the console froze: hold the power button, then start "
                  "Couch again, and it skips this step.");
    ui_flip();
}

void start_begin(int step)
{
    if (step < 0 || step >= SS_N) return;
    if (!t_end[step]) t_begin[step] = now();               /* the first run's time only (a step may run again) */
    if (step >= SS_FRAME) return;
    running = step;
    if (step > SS_UI && loading) draw(-1);                 /* the picture is up from ui_init on */
}

void start_end(int step)
{
    if (step < 0 || step >= SS_N || t_end[step]) return;
    t_end[step] = now();
    if (!t_begin[step]) t_begin[step] = step == SS_UI ? t_main : t_end[step];
    if (running == step) running = -1;
    boot_mark(start_name[step]);
    if (step == SS_FRAME) loading = 0;
    if (step == SS_UI) {                                   /* the first frame: right after ui_init */
        draw(-1);
        t_first = now();
    }
}

/* ---- the job (the kit's run_job, in this look) ---------------------------------------- */

static volatile int job_over;

static void job_thread(void *arg)
{
    ((void (*)(void))arg)();
    __sync_synchronize();
    job_over = 1;
    sysThreadExit(0);
}

void start_job(int step, void (*fn)(void))
{
    sys_ppu_thread_t t;
    u64 ret;
    int was = running;
    running = step;
    job_over = 0;
    s64 a = now();
    if (sysThreadCreate(&t, job_thread, (void *)fn, 1500, 0x20000, THREAD_JOINABLE, "job") != 0) {
        draw(0);                                           /* no thread: one frame, then the call here */
        fn();
    } else {
        while (!job_over) draw(sec_of(now() - a));
        sysThreadJoin(t, &ret);
    }
    running = was;
    pressed = 0;                                           /* a button still down is not a new press */
}

/* ---- couch.txt ----------------------------------------------------------------------- */

static double took(int i) { return t_end[i] && t_begin[i] ? sec_of(t_end[i] - t_begin[i]) : -1; }

void start_log_early(void)
{
    char w[300];
    int k = 0;
    for (int i = SS_UI; i <= SS_FS && k < (int)sizeof w; i++)
        k += snprintf(w + k, sizeof w - (size_t)k, "%s%s %.2f s", i ? ", " : "", start_name[i], took(i));
    couch_log("start: %s; the loading screen's first frame on the TV at %.2f s from main", w, t_first ? sec_of(t_first - t_main) : -1.0);
}

void start_log_late(void)
{
    if (logged_late) return;
    logged_late = 1;
    char a[48] = "not run", b[48] = "not run", f[48] = "none", g[48] = "none";
    if (t_end[SS_FX]) snprintf(a, sizeof a, "%.2f s", took(SS_FX));
    if (t_end[SS_NET]) snprintf(b, sizeof b, "%.2f s", took(SS_NET));
    if (t_end[SS_FRAME]) snprintf(f, sizeof f, "%.2f s", sec_of(t_end[SS_FRAME] - t_main));
    if (t_end[SS_FETCH]) snprintf(g, sizeof g, "%.2f s", sec_of(t_end[SS_FETCH] - t_main));
    couch_log("start: fx_start %s, net_start %s; from main: the first Board frame at %s, the first fetch's reply at %s", a, b, f, g);
}

/* ---- the start animation ------------------------------------------------------------ */

static s64 anim_t0;
static int anim_on;
#define ANIM_END 1.2

void start_anim_begin(void)
{
    anim_t0 = now();
    anim_on = 1;
}

void start_anim_input(unsigned pressed_now)
{
    if (anim_on && pressed_now) anim_on = 0;
}

int start_anim_on(void)
{
    if (anim_on && sec_of(now() - anim_t0) >= ANIM_END) anim_on = 0;
    return anim_on;
}

/* ease-out cubic of the part's time, from its start over its length */
static float part(double t, double from, double len)
{
    double x = (t - from) / len;
    if (x <= 0) return 0;
    if (x >= 1) return 1;
    x = 1 - x;
    return (float)(1 - x * x * x);
}

float start_k(int which)
{
    if (!start_anim_on()) return 1;
    double t = sec_of(now() - anim_t0);
    switch (which) {
    case SA_FADE: return part(t, 0.0, 0.5);
    case SA_HEADER: return part(t, 0.15, 0.45);
    case SA_LEFT: return part(t, 0.25, 0.5);
    case SA_RIGHT: return part(t, 0.32, 0.5);
    case SA_BOTTOM: return part(t, 0.40, 0.5);
    case SA_CLOCK: return (float)t;                        /* the clock's flips start at 0.3 s (page.c) */
    default: return 1;
    }
}
