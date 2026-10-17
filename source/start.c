/* The start (start.h): the steps' times, the first frame, the job
 * screen, the start animation. */
#include <stdio.h>
#include <sys/thread.h>
#include <lv2/systime.h>
#include <sys/systime.h>
#include <ps3gfx/ui.h>
#include "couch.h"
#include "boot.h"
#include "start.h"

const char *const start_name[SS_N] = {"ui_init", "text_utf8", "digits_init", "icons_init", "fs_selftest",
                                      "fx_start", "net_start", "first_frame", "first_fetch"};
/* A slow start job's name on the screen (SS_UI to SS_NET). */
static const char *const shown_name[SS_FRAME] = {"The picture",    "Accented letters", "The display digits",  "The icons",
                                                 "The app folder", "The effects (6 SPUs)", "The network"};

s64 t_main;
static s64 t_begin[SS_N], t_end[SS_N];      /* from sysGetSystemTime; 0: not yet */
static int running = -1;                    /* the step on the screen */
static int logged_late;
static s64 t_first;                         /* the first frame on the TV (its ui_flip returned) */

static s64 now(void) { return (s64)sysGetSystemTime(); }
static double sec_of(s64 us) { return (double)us / 1e6; }

void start_main(void) { t_main = now(); }

#define JOB_SHOW_S 2.0                  /* a start job's counter shows after this time: the freeze signal */

/* One start frame: the background only (since 0.4.1: the TV showed the old
 * loading screen too briefly to read it). A job that runs JOB_SHOW_S or
 * more (sec) shows its name and its counter. Only the kit's ASCII text: the
 * other atlases may not be loaded yet. */
static void draw(double sec)
{
    begin_frame();
    if (sec >= JOB_SHOW_S && running >= 0 && running < SS_FRAME) {
        float cx = (float)SW / 2, y = (float)SH * 0.42f;
        spinner(cx, y - 40, 18, sec);
        text_c(cx, y, F_MED, WHITE, "%s", shown_name[running]);
        text_c(cx, y + (float)line_h(F_MED) + 10, F_BODY | TNUM, GREY, "%.1f s", sec);
        if (fs_ok)
            text_wrap(MG, (float)SH - 150, F_SMALL, GREY, (float)SW - 2 * MG, 2,
                      "The counter runs while the console works. If it stops, the console froze: hold the power button, then start "
                      "Couch again, and it skips this step.");
    }
    ui_flip();
}

void start_begin(int step)
{
    if (step < 0 || step >= SS_N) return;
    if (!t_end[step]) t_begin[step] = now();               /* the first run's time only (a step may run again) */
    if (step < SS_FRAME) running = step;
}

void start_end(int step)
{
    if (step < 0 || step >= SS_N || t_end[step]) return;
    t_end[step] = now();
    if (!t_begin[step]) t_begin[step] = step == SS_UI ? t_main : t_end[step];
    if (running == step) running = -1;
    boot_mark(start_name[step]);
    if (step == SS_UI) {                                   /* the first frame: right after ui_init, the background */
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
    couch_log("start: %s; the first frame on the TV at %.2f s from main", w, t_first ? sec_of(t_first - t_main) : -1.0);
    couch_log("ui_init: journal_init %.3f s, pad %.3f s, gfx_init %.3f s (gcmInitBody %.3f s, videoConfigure %.3f s), fonts_init %.3f s, safe_apply %.3f s",
              ui_init_us[UI_T_JOURNAL] / 1e6, ui_init_us[UI_T_PAD] / 1e6, ui_init_us[UI_T_GFX] / 1e6, ui_init_us[UI_T_GCM] / 1e6,
              ui_init_us[UI_T_VIDEO] / 1e6, ui_init_us[UI_T_FONTS] / 1e6, ui_init_us[UI_T_SAFE] / 1e6);
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
    default: return 1;
    }
}
