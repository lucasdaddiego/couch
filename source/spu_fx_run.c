/* The effects (spu_fx_run.h): the PPU side of ps3gfx's effects program,
 * modelled on PS3 Health's spu_run.c (the journaled group, the terminate,
 * counts_moved) and Cell Bench's run.c and mandel.c (the mailbox words,
 * the two buffers, the check row). The reports are read with volatile
 * reads: the SPUs write them by DMA.
 *
 * The frame (design 3.14): Couch ends every frame with ui_flip, which
 * returns when the RSX has finished it. So fx_tick, at the top of the next
 * frame, may swap a picture's two textures and send the next picture: the
 * SPUs write only the texture that no frame draws until that swap. The
 * rain is inside the frame: gfx_block, the control block, the mailbox
 * word, then the wait for thread 5's report before the next gfx call. */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <sys/spu.h>
#include <sys/systime.h>
#include <lv2/systime.h>
#include <ps3gfx/ui.h>
#include <ps3gfx/journal.h>
#include <ps3gfx/fx.h>
#include <ps3gfx/spu_fx.h>
#include <ps3gfx/spu_fx_kern.h>
#include "couch.h"
#include "spu_fx_run.h"
#include "start.h"
#include "astro.h"
#include "board.h"
#include "clock.h"
#include "parse.h"

#define GROUP_PRIO 100               /* PSL1GHT's samples: 16 is the highest, 255 the lowest */
#define FIRST_US 2000000             /* the six start reports, or the group is stopped */
#define EXIT_US 1000000              /* the six exit reports, or the terminate */
#define MOVE_US 100000               /* a failed terminate: the counts read twice, this far apart */
#define PIC_LATE_US 2000000          /* a picture later than this: RSX only */
#define RAIN_SLOW_US 2000            /* a rain report later than this: a late frame in the log */
#define RAIN_LATE_US 50000           /* later than this: RSX only */
#define RAIN_POLLS 20000000L         /* the same bound for a clock that does not move inside a frame (the preview's) */
#define FIRST_RAIN 60                /* fx_first: the frames of rain */
#define CHECK_SKY 60                 /* after the first picture: one check row a second (sky 60 pictures a second, */
#define CHECK_AUR 30                 /* the aurora 30) */
#define RAIN_CHECK_EVERY 600         /* the rain's word sum: every 600th job */
#define WINDOW_US 2000000            /* the frame budget's window */
#define CALM_US 60000000             /* a step back after 60 s with no slow period */
#define SLOW_PERIOD_US 20000         /* a display period over 20 ms */
#define PICS 5                       /* threads 0 to 4: the pictures, a band each */
#define RAIN_T 5                     /* thread 5: the rain */
#define SEED 0x0c0c4c11u             /* the look of the clouds and the rain: fixed, so the particles live on */
#define PPU_RAIN 300                 /* RSX only: the PPU's drops (design 3.5) */
#define TB_HZ 79800000.0             /* the timebase (the decrementer's ticks) */
#define CLOUD_PX 6.4f                /* the clouds' drift at 10 km/h, picture px a second: the screen's width (480) in */
                                     /* 75 s, a light wind's calm crossing (round of 2026-10-14: stage 9's 0.8 px/s read as still) */
#define CLOUD_MIN 2.5f               /* calm or no wind: still a drift, the width in about 3 min */
#define CLOUD_MAX 14.0f              /* a gale: the width in 34 s at the most */
#define CLOUD_UP 0.4f                /* toward or away from the viewer: the clouds pass the zenith and the horizon slower */
#define EVOLVE 3.0f                  /* the clouds' change: the kernel's time (its warp) at 3 times the clock */
#define EVOLVE_WRAP 64000.0f         /* the kernel's warp offsets (0.021 and 0.013 cells a second, 64 cells) repeat after 64,000 s */
#define FLASH_S 0.12f                /* lightning lights the clouds of the next pictures, fading over 120 ms */
#define DRIFT_WRAP (FX_NOISE_N * FX_CLOUD_CELL)   /* the noise's period: off_x and off_y stay small */
#define LOG_LATE 10                  /* late rain frames named in couch.txt */

/* RSX only: the four cloud sprites, x and y as parts of the screen, the size */
static const float sprite_at[4][3] = {{0.08f, 0.10f, 1.6f}, {0.42f, 0.22f, 1.1f}, {0.70f, 0.06f, 2.0f}, {0.90f, 0.30f, 0.8f}};

int fx_state = FXS_NONE;
char fx_why[120];
fx_scene fx_sc;
int fx_step;

static fx_ctl ctls[FX_SPUS];         /* 128-byte aligned by the type: one DMA each */
static fx_rec recs[FX_SPUS];
static u32 serial[FX_SPUS];          /* the last job word sent to each thread */
static tex_pair cloud, glass, aur;
static int have_tex;

/* the group, as Health's: GROUP_IDLE = it exists and no thread runs (joined) */
enum { GROUP_NONE, GROUP_RUNNING, GROUP_IDLE };
static int initialized, imported, grp, stuck;
static sysSpuImage image;
static sys_spu_group_t group;
static sys_spu_thread_t threads[FX_SPUS];
static int first_open, aur_open, aur_tried;   /* fx_first, fx_aurora: the entry is open */
static int jheld;                   /* RSX only because the journal held an entry: a later fx_start tries again once it is clear */
static s64 t0;

static const char *const names[] = {"fx_init",      "fx_import", "fx_group",       "fx_start",   "fx_first", "fx_aurora",
                                    "fx_terminate", "fx_join",   "fx_exit_status", "fx_destroy", "fx_close"};

/* the pictures */
static struct {
    int busy, mode;                  /* a picture is out on threads 0 to 4, of mode FXM_* */
    u32 job, check;                  /* its number, its check row */
    s64 sent;
    u32 frame, sent_frame;           /* fx_tick's count; the frame of the last send */
    int done[PICS];
    int have_sky, have_aur;          /* a picture of each is shown */
    u32 first_frame, aur_frame;      /* the frame whose fx_tick swapped in the first checked picture of each */
    float off_x, off_y, flash, evo;   /* the drift; the lightning's light; the kernel's time (EVOLVE) */
    float spr[4];                    /* RSX only: the sprites' drift, screen pixels */
    s64 last_us;                     /* the last fx_tick */
    u32 sky_n, aur_n, checks;        /* pictures shown, check rows read back */
    double sky_ms, sky_max, aur_ms, aur_max;   /* the slowest SPU of each picture: the sum, the max */
} P;

/* the rain */
static struct {
    u32 jobs, frames, late, slowest_us;   /* SPU jobs; frames of rain since the start; reports over 2 ms; the latest */
    double ms, max;                  /* thread 5's busy time */
    s64 last_us;
} R;

/* the frame budget */
static struct {
    s64 since, calm_since;
    int slow, late;
} B;

static u64 ea(const void *p) { return (u64)(uintptr_t)p; }
static s64 now(void) { return (s64)sysGetSystemTime(); }
static double tick_ms(u32 t) { return t / TB_HZ * 1000.0; }

/* ---- the journal ------------------------------------------------------------------ */

/* A journaled call: jgo before (0: it froze this console before, do not
 * make it), jend after (1 when it succeeded); the first failure is logged. */
static int jgo(const char *name)
{
    frame_ev |= EV_JOURNAL;
    return journal_start(name);
}

static int jend(const char *name, s32 rc)
{
    frame_ev |= EV_JOURNAL;
    journal_done(name, rc, rc == 0);
    if (rc) couch_log("effects: %s rc 0x%08x", name, (unsigned)rc);
    return rc == 0;
}

/* fx_first and fx_aurora end: ok 1 when the console did not freeze in them
 * (also at the exit), 0 with a reason's code when the effects stop. */
static void entries_close(int ok, s32 rc)
{
    if (first_open || aur_open) frame_ev |= EV_JOURNAL;
    if (first_open) journal_done("fx_first", ok ? 0 : rc, ok);
    if (aur_open) journal_done("fx_aurora", ok ? 0 : rc, ok);
    first_open = aur_open = 0;
}

/* The first entry that froze this console before, or NULL. */
static const char *blocked(void)
{
    for (unsigned i = 0; i < sizeof names / sizeof names[0]; i++)
        if (journal_state(names[i]) == J_FROZEN || journal_state(names[i]) == J_SKIPPED) return names[i];
    return NULL;
}

/* ---- the calls behind the job screen --------------------------------------------- */

/* Each call runs in start_job's thread behind the counter (start.c: the
 * kit's run_job in the loading screen's look, the visible freeze signal),
 * one job screen per call; the main thread writes the journal lines
 * between them. */
static s32 (*job_fn)(void);
static volatile s32 job_rc;
static void job_tramp(void) { job_rc = job_fn(); }
static s32 job_call(s32 (*fn)(void))
{
    job_fn = fn;
    job_rc = -1;
    start_job(SS_FX, job_tramp);             /* the loading screen's counter (start.c: the kit's run_job in Couch's look) */
    return job_rc;
}

static s32 c_init(void) { return sysSpuInitialize(FX_SPUS, 0); }
static s32 c_import(void) { return sysSpuImageImport(&image, spu_fx_spu, SPU_IMAGE_PROTECT); }
static s32 c_start(void) { return sysSpuThreadGroupStart(group); }

/* The group: six threads of spu_fx, thread i with its report and its
 * control block. A failed thread destroys the group inside the same call. */
static s32 c_group(void)
{
    static const char name[] = "couchfx";
    sysSpuThreadGroupAttribute ga;
    memset(&ga, 0, sizeof ga);
    ga.nsize = sizeof name;
    ga.name = name;
    ga.type = SPU_THREAD_GROUP_TYPE_NORMAL;
    s32 rc = sysSpuThreadGroupCreate(&group, FX_SPUS, GROUP_PRIO, &ga);
    for (int i = 0; rc == 0 && i < FX_SPUS; i++) {
        sysSpuThreadAttribute ta;
        memset(&ta, 0, sizeof ta);
        ta.name = name;
        ta.nsize = sizeof name;
        ta.option = SPU_THREAD_ATTR_NONE;
        sysSpuThreadArgument a = {ea(&recs[i]), ea(&ctls[i]), fx_arg2((u32)i), 0};
        rc = sysSpuThreadInitialize(&threads[i], group, (u32)i, &image, &ta, &a);
        if (rc) sysSpuThreadGroupDestroy(group);
    }
    return rc;
}

/* The six start reports (serial 0, magic FX_MAGIC), up to FIRST_US. The
 * sleep opens the loop, so the syscall audit sees its li r11 before the sc
 * (Health's counts_moved). Returns the count. */
static int started_n;
static int start_reports(void)
{
    int n = 0;
    for (int i = 0; i < FX_SPUS; i++) n += ((const volatile fx_rec *)&recs[i])->magic == FX_MAGIC;
    return n;
}

static s32 c_wait(void)
{
    s64 a = now();
    int naps = 0, n = start_reports();
    if (n == FX_SPUS) {                      /* no sleep when they are in: the preview's twin puts them in the start call */
        started_n = n;
        return 0;
    }
    do {
        sysUsleep(1000);
        n = start_reports();
    } while (n < FX_SPUS && now() - a < FIRST_US && ++naps < 2000);
    started_n = n;
    return 0;
}

/* ---- the group's end ------------------------------------------------------------- */

/* After a failed terminate: did a thread still report? The counts twice,
 * 100 ms apart (Health's counts_moved, the same loop shape). */
static int counts_moved(void)
{
    u64 c[FX_SPUS];
    for (int i = 0; i < FX_SPUS; i++) c[i] = ((const volatile fx_rec *)&recs[i])->count;
    s64 a = now();
    int naps = 0;
    do sysUsleep(10000);
    while (now() - a < MOVE_US && ++naps < 100);
    for (int i = 0; i < FX_SPUS; i++)
        if (((const volatile fx_rec *)&recs[i])->count != c[i]) return 1;
    return 0;
}

/* After a join: the six exit statuses (0x700 | index after the exit flag; a
 * thread the terminate stopped has none: its read fails). */
static void exit_statuses(void)
{
    char w[100];
    int k = 0;
    if (!jgo("fx_exit_status")) return;
    s32 first = 0;
    for (int i = 0; i < FX_SPUS; i++) {
        s32 st = -1, e = sysSpuThreadGetExitStatus(threads[i], &st);
        if (e) st = -1;
        if (e && !first) first = e;
        if (st < 0) k += snprintf(w + k, sizeof w - (size_t)k, "%snone", i ? " " : "");
        else k += snprintf(w + k, sizeof w - (size_t)k, "%s0x%x", i ? " " : "", (unsigned)st);
    }
    journal_done("fx_exit_status", first, first == 0);
    couch_log("effects: exit statuses %s%s", w, first ? " (a terminated thread has none)" : "");
}

/* The join: the group's threads are gone (the exit flag) or stopped (the terminate). */
static void group_join(int ok, s32 why)
{
    u32 cause = 0, status = 0;
    int go = jgo("fx_join");
    entries_close(ok, why);                  /* after the join's start line: no gap between the two entries */
    if (!go) return;
    s32 rc = sysSpuThreadGroupJoin(group, &cause, &status);
    if (!jend("fx_join", rc)) return;
    grp = GROUP_IDLE;
    couch_log("effects: joined, cause 0x%x, status 0x%x", (unsigned)cause, (unsigned)status);
    exit_statuses();                         /* before the destroy: the threads go with the group */
}

/* The terminate, then the join. The terminate's done line has ok=1 for
 * every rc: an error can mean the threads had ended. A failed terminate
 * whose counts still move: no join, no destroy (stuck); the group goes
 * with the process. */
static void group_kill(int ok, s32 why)
{
    int go = jgo("fx_terminate");
    entries_close(ok, why);
    if (!go) return;
    s32 rc = sysSpuThreadGroupTerminate(group, 0x0dead);
    journal_done("fx_terminate", rc, 1);
    couch_log("effects: terminated, rc 0x%08x", (unsigned)rc);
    if (rc != 0 && counts_moved()) {
        stuck = 1;
        couch_log("effects: stuck: the terminate failed and a thread still reports; no join, no destroy");
        return;
    }
    group_join(ok, why);
}

/* After the group ended: the destroy, then the image close once no thread can run. */
static void teardown(void)
{
    if (grp == GROUP_IDLE && jgo("fx_destroy") && jend("fx_destroy", sysSpuThreadGroupDestroy(group))) grp = GROUP_NONE;
    if (grp != GROUP_RUNNING && imported) {
        if (jgo("fx_close")) jend("fx_close", sysSpuImageClose(&image));
        imported = 0;
    }
}

/* RSX only for the rest of the session: the reason, the group stopped (no
 * job starts after this), the entries closed as failed (code: 1 a late
 * report, 2 a check, 3 a refused job, 4 the frame budget, 5 a failed call). */
static void rsx_only(s32 code, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void rsx_only(s32 code, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(fx_why, sizeof fx_why, fmt, ap);
    va_end(ap);
    couch_log("effects: RSX only: %s", fx_why);
    if (grp == GROUP_RUNNING) {
        group_kill(0, code);
        teardown();
    } else entries_close(0, code);
    fx_state = FXS_RSX;
    P.busy = 0;
}

/* ---- the textures ------------------------------------------------------------------ */

static int pitch_ok(const tex_pair *p, u32 pitch) { return p->tex[0].pitch == pitch && p->tex[1].pitch == pitch; }

/* The clouds, the glass and the aurora, two of each, once per process
 * (gfx_vram never gives memory back). NULL, or the reason. */
static const char *textures(void)
{
    static char w[100];
    if (have_tex) return NULL;
    if (tex_pair_new(&cloud, FX_SKY_W, FX_SKY_H, GFX_TEX_A8R8G8B8, 1) || tex_pair_new(&glass, FX_GLASS_W, FX_GLASS_H, GFX_TEX_A8R8G8B8, 1) ||
        tex_pair_new(&aur, FX_AURORA_W, FX_AURORA_H, GFX_TEX_A8R8G8B8, 1))
        return "RSX memory is short for the pictures' textures";
    if (!pitch_ok(&cloud, FX_SKY_PITCH) || !pitch_ok(&glass, FX_GLASS_PITCH) || !pitch_ok(&aur, FX_AURORA_PITCH)) {
        snprintf(w, sizeof w, "a texture's pitch is not the SPUs' (%u, %u, %u)", (unsigned)cloud.tex[0].pitch, (unsigned)glass.tex[0].pitch,
                 (unsigned)aur.tex[0].pitch);
        return w;
    }
    have_tex = 1;
    return NULL;
}

/* A small texture the PPU makes once: w x h A8R8G8B8, white in every texel
 * (the RGB rule of design 3.3: bilinear filtering mixes RGB without
 * premultiplied alpha), the alpha from fn and the 4 x 4 dither under 0x30
 * (the alpha test drops texels under 0x10). Written in 32-bit stores. */
static int make_tex(gfx_tex *t, u16 w, u16 h, float (*fn)(float, float))
{
    static const u32 bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    u8 *p = gfx_tex_new(t, w, h, GFX_TEX_A8R8G8B8, 1);
    if (!p) return 0;
    frame_ev |= EV_TEX;
    for (u32 y = 0; y < h; y++) {
        volatile u32 *row = (volatile u32 *)(p + y * t->pitch);
        for (u32 x = 0; x < w; x++) {
            float f = fn(((float)x + 0.5f) / w, ((float)y + 0.5f) / h);
            u32 a = (u32)(f < 0 ? 0 : f > 1 ? 255 : f * 255.0f + 0.5f);
            if (a < 0x30) a = (a + bayer[y & 3][x & 3]) >> 4 << 4;
            row[x] = a << 24 | 0xffffffu;
        }
    }
    return 1;
}

/* A soft cloud: five round lumps on a flat base, fading at the edges. */
static float blob(float u, float v)
{
    static const float c[5][3] = {{0.30f, 0.62f, 0.22f}, {0.50f, 0.48f, 0.28f}, {0.70f, 0.60f, 0.21f}, {0.40f, 0.70f, 0.20f}, {0.60f, 0.72f, 0.20f}};
    float s = 0;
    for (int i = 0; i < 5; i++) {
        float dx = (u - c[i][0]) / c[i][2], dy = (v - c[i][1]) / (c[i][2] * 2.0f), d = dx * dx + dy * dy;
        if (d < 1) s += (1 - d) * (1 - d);
    }
    float base = v > 0.80f ? 1.0f - (v - 0.80f) * 5.0f : 1.0f;   /* a flatter underside */
    return s * 0.9f * base;
}

/* A round flake: full at the centre, 0 at the edge. */
static float dot(float u, float v)
{
    float dx = u - 0.5f, dy = v - 0.5f, r = sqrtf(dx * dx + dy * dy) * 2.0f;
    return r >= 1 ? 0 : (1 - r) * (1 - r) * 1.6f;
}

static gfx_tex blob_tex, dot_tex;
static int blob_ok = -1, dot_ok = -1;

static const gfx_tex *blob_t(void)
{
    if (blob_ok < 0) blob_ok = make_tex(&blob_tex, 256, 128, blob);
    return blob_ok ? &blob_tex : NULL;
}

static const gfx_tex *dot_t(void)
{
    if (dot_ok < 0) dot_ok = make_tex(&dot_tex, 32, 32, dot);
    return dot_ok ? &dot_tex : NULL;
}

/* ---- the start ---------------------------------------------------------------------- */

void fx_start(void)
{
    const char *b;
    s32 rc;
    if (fx_state == FXS_SPU || (fx_state == FXS_RSX && (!jheld || blocked()))) return;
    jheld = 0;
    t0 = now();
    P.last_us = R.last_us = B.since = B.calm_since = t0;
    if (!fs_ok) {
        rsx_only(5, "the folder is not writable, so no journal and no SPU call");
        return;
    }
    if (quit) {
        rsx_only(5, "a quit came before the start");
        return;
    }
    if ((b = blocked())) {
        rsx_only(5, "%s froze this console before (SQUARE twice on this page clears the journal)", b);
        jheld = 1;
        return;
    }
    if ((b = textures())) {
        rsx_only(5, "%s", b);
        return;
    }
    s64 a = now();
    if (!initialized) {                      /* once per process */
        if (!jgo("fx_init") || !jend("fx_init", job_call(c_init))) {
            rsx_only(5, "sysSpuInitialize failed (couch.txt has the rc)");
            return;
        }
        initialized = 1;
    }
    if (!jgo("fx_import") || !jend("fx_import", job_call(c_import))) {
        rsx_only(5, "sysSpuImageImport failed (couch.txt has the rc)");
        return;
    }
    imported = 1;
    memset(recs, 0, sizeof recs);            /* before any thread runs */
    memset(ctls, 0, sizeof ctls);
    memset(serial, 0, sizeof serial);
    __sync_synchronize();
    if (!jgo("fx_group") || !jend("fx_group", job_call(c_group))) {
        teardown();
        rsx_only(5, "the SPU group was not made (couch.txt has the rc)");
        return;
    }
    if (!jgo("fx_start")) {                  /* not reached: blocked() read every name */
        grp = GROUP_IDLE;
        teardown();
        rsx_only(5, "fx_start froze this console before");
        return;
    }
    rc = job_call(c_start);
    if (rc == 0) first_open = jgo("fx_first");   /* before the start's done line: no gap between the two entries */
    if (!jend("fx_start", rc)) {
        grp = GROUP_IDLE;                    /* made, never started: the destroy is next */
        teardown();
        rsx_only(5, "sysSpuThreadGroupStart failed: rc 0x%08x", (unsigned)rc);
        return;
    }
    grp = GROUP_RUNNING;
    job_call(c_wait);
    double sec = (double)(now() - a) / 1e6;
    if (started_n < FX_SPUS) {
        int m = 0;
        while (m < FX_SPUS && ((const volatile fx_rec *)&recs[m])->magic == FX_MAGIC) m++;
        rsx_only(1, "SPU %d did not report its start in %d s", m, FIRST_US / 1000000);
        return;
    }
    __sync_synchronize();
    for (int i = 0; i < FX_SPUS; i++) {      /* Group B's rule: the known-answer test before the first job */
        u32 k = ((const volatile fx_rec *)&recs[i])->kat;
        if (k != FX_KAT) {
            rsx_only(2, "SPU %d's known-answer test gave 0x%08x, not 0x%08x", i, (unsigned)k, (unsigned)FX_KAT);
            return;
        }
    }
    fx_state = FXS_SPU;
    fx_why[0] = 0;
    couch_log("effects: the SPUs are up in %.2f s (6 start reports, the known-answer test ok); fx_first open", sec);
}

/* ---- the scene ---------------------------------------------------------------------- */

static unsigned mix_rgb(unsigned a, unsigned b, float t)
{
    unsigned r = 0;
    for (int s = 16; s >= 0; s -= 8) {
        float x = (float)((a >> s) & 255), y = (float)((b >> s) & 255);
        r |= (unsigned)(x + (y - x) * t + 0.5f) << s;
    }
    return r;
}

static float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }

/* The scene from the last good reply and the clock (no geo yet: night,
 * 30 % cloud, dry, no wind). The cloud colours (design 3.3): by day the
 * horizon's colour lit toward white and the top's darkened, by night a
 * dim blue-grey, darker in the rain, orange edges near sunrise and
 * sunset. */
static void scene(long utc)
{
    fx_scene s;
    const board_data *b = &board_cur;
    memset(&s, 0, sizeof s);
    int geo = board_have && b->has_geo, fx = board_have && b->has_fx;
    if (geo) sun_pos(utc, b->lat, b->lon, &s.elev, &s.azim);
    else s.elev = board_have && b->has_wx && b->is_day ? 30 : -20;   /* a reply with no geo (before 0.3.0): wx_now's day or night */
    int cover = fx ? b->fx_cover : -1, code = fx ? b->fx_code : -1;
    s.wet = wmo_wet(code);
    sky_colors(s.elev, cover, s.wet, &s.top, &s.horizon);
    s.sky[0] = s.top;                        /* the glass source's stops, as ps3gfx's test: top, 55 % of the way, horizon, a quarter back */
    s.sky[1] = mix_rgb(s.top, s.horizon, 0.55f);
    s.sky[2] = s.horizon;
    s.sky[3] = mix_rgb(s.horizon, s.top, 0.25f);
    s.cover = cover < 0 ? 0.3f : (float)cover / 100.0f;
    float kmh = fx && b->fx_speed > 0 ? (float)b->fx_speed : 0, from = fx && b->fx_dir >= 0 ? (float)b->fx_dir : 0;
    s.rain_wind = fx_rain_wind(kmh, from);
    /* The clouds go where the wind blows to, as seen facing the equator
     * (north in the south): right is east there, and away from the viewer
     * is down the screen, toward the horizon (stage 9 moved a north wind's
     * clouds down in the south). Calm or no wind: from the west. The speed
     * grows with the root of the wind, so a gale stays readable. */
    float face = geo && b->lat >= 0 ? 180.0f : 0.0f, to = (kmh >= 1 && fx && b->fx_dir >= 0 ? from : 270.0f) + 180.0f - face;
    float v = clampf(CLOUD_PX * sqrtf(kmh / 10.0f), CLOUD_MIN, CLOUD_MAX), r = to * 0.017453292f;
    s.wind_x = sinf(r) * v;
    s.wind_y = cosf(r) * v * CLOUD_UP;
    int kind = WP_DROPS;
    s.precip = fx ? wx_precip(code, b->fx_mm, &kind) : 0;
    s.kind = kind == WP_FLAKES ? FX_FLAKES : FX_DROPS;
    float d = clampf((float)(s.elev + 6) / 12.0f, 0, 1);   /* 0 at -6 deg, 1 at +6 */
    /* by day from the sky's own colours; by night a dim blue-grey just over
     * the night sky (stage 9: stage 8's 2a3550 and 121a2c made an overcast
     * night twice as bright as a clear one) */
    s.lit = mix_rgb(0x141a28, mix_rgb(s.horizon, 0xffffff, 0.35f), d);
    s.shade = mix_rgb(0x080c16, mix_rgb(s.top, 0x000000, 0.2f), d);
    if (s.wet) {
        s.lit = mix_rgb(s.lit, mix_rgb(0x181d26, 0x5d6878, d), 0.5f * d + 0.2f);   /* the rain's grey, dim at night */
        s.shade = mix_rgb(s.shade, mix_rgb(0x0a0d14, 0x2c3442, d), 0.5f * d + 0.2f);
    }
    float tw = 1.0f - (float)fabs(s.elev) / 8.0f;
    s.edge = tw > 0 ? 0xf2a65a : s.lit;
    s.edge_amt = tw > 0 ? 0.6f * tw : 0;
    /* the sun's place: the screen spans 180 deg of azimuth, facing the
     * equator (north in the south); elevation 0 at 80 % of the height, 60
     * deg at the top */
    float da = (float)s.azim - (geo && b->lat < 0 ? 0.0f : 180.0f);
    while (da > 180) da -= 360;
    while (da < -180) da += 360;
    s.sun_x = (960.0f + da / 90.0f * 960.0f) / 4.0f;
    s.sun_y = (864.0f - (float)s.elev / 60.0f * 864.0f) / 4.0f;
    s.sun_gain = clampf((float)(s.elev + 2) / 8.0f, 0, 1) * (1.0f - 0.7f * s.cover);
    s.light = clampf((float)(s.elev + 6) / 18.0f, 0, 1);   /* 0 from -6 deg down, 1 from 12 deg up */
    fx_sc = s;
}

/* ---- the pictures ------------------------------------------------------------------- */

/* Thread i's last job is done: its report has the job's serial (the start
 * report: serial 0). */
static int came(int i)
{
    const volatile fx_rec *r = &recs[i];
    return r->magic == FX_MAGIC && r->serial == serial[i] && (serial[i] == 0 || r->done == 1);
}

static u32 row_sum(const void *base, u32 pitch, u32 y, u32 words)
{
    const volatile u32 *p = (const volatile u32 *)((const u8 *)base + y * pitch);
    u32 s = 0;
    for (u32 i = 0; i < words; i++) s += p[i];
    return s;
}

/* Frames per picture: the sky 60, 30, 20, then 15 a second by the frame
 * budget's step; the aurora (not measured on the console yet) 30 at most. */
static u32 period(int mode)
{
    u32 p = fx_step == 0 ? 1 : fx_step == 1 ? 2 : fx_step == 2 ? 3 : 4;
    return mode == FXM_AURORA && p < 2 ? 2 : p;
}

static void pics_send(int mode)
{
    int sky = mode == FXM_SKY;
    float t = (float)((double)(now() - t0) / 1e6);
    if (!sky && !aur_tried) {                /* fx_aurora: before the first aurora job, once per journal */
        aur_tried = 1;
        if (journal_needed("fx_aurora")) aur_open = jgo("fx_aurora");
    }
    P.mode = mode;
    P.job++;
    P.check = (P.job * 37u) % (sky ? FX_SKY_H : FX_AURORA_H);
    for (int i = 0; i < PICS; i++) {
        fx_ctl *c = &ctls[i];
        memset(c, 0, sizeof *c);
        c->serial = ++serial[i];
        c->check_row = P.check;
        c->time = sky ? P.evo : t;
        c->seed = SEED;
        if (sky) {
            c->mode = FX_SKY;
            fx_band((u32)i, PICS, FX_SKY_H, &c->y0, &c->y1);
            c->out = ea(tex_pair_written(&cloud));
            c->out2 = ea(tex_pair_written(&glass));
            c->p.sky.off_x = P.off_x;
            c->p.sky.off_y = P.off_y;
            c->p.sky.cover = fx_sc.cover;
            c->p.sky.alpha = 0.92f;
            c->p.sky.sun_x = fx_sc.sun_x;
            c->p.sky.sun_y = fx_sc.sun_y;
            c->p.sky.sun_gain = fx_sc.sun_gain;
            c->p.sky.flash = P.flash;
            c->p.sky.edge_amt = fx_sc.edge_amt;
            c->p.sky.lit = fx_sc.lit;
            c->p.sky.shade = fx_sc.shade;
            c->p.sky.edge = fx_sc.edge;
            for (int k = 0; k < 4; k++) c->p.sky.sky[k] = fx_sc.sky[k];
        } else {
            c->mode = FX_AURORA;
            fx_band((u32)i, PICS, FX_AURORA_H, &c->y0, &c->y1);
            c->out = ea(tex_pair_written(&aur));
            c->p.aurora.gain = 1.0f;         /* the cap: the mean brightness at most 20 % */
            c->p.aurora.top = 0x040814;
            c->p.aurora.bottom = 0x0a1222;
        }
    }
    __sync_synchronize();
    memset(P.done, 0, sizeof P.done);
    P.sent = now();
    P.sent_frame = P.frame;
    P.busy = 1;
    for (int i = 0; i < PICS; i++) {
        s32 rc = sysSpuThreadWriteMb(threads[i], serial[i]);
        if (rc) {
            rsx_only(5, "the mailbox word of SPU %d failed: rc 0x%08x", i, (unsigned)rc);
            return;
        }
    }
}

/* The picture's reports: all five came, so its texture(s) swap in (this
 * frame draws them), after the check row when one is due; or it is late. */
static void pics_poll(void)
{
    int got = 0, sky = P.mode == FXM_SKY;
    for (int i = 0; i < PICS; i++) {
        if (!P.done[i] && came(i)) P.done[i] = 1;
        got += P.done[i];
    }
    if (got < PICS) {
        if (now() - P.sent > PIC_LATE_US) {
            int m = 0;
            while (P.done[m]) m++;
            rsx_only(1, "SPU %d did not finish picture %u in %d s", m, (unsigned)P.job, PIC_LATE_US / 1000000);
        }
        return;
    }
    __sync_synchronize();
    u32 slowest = 0;
    int ck = -1;
    for (int i = 0; i < PICS; i++) {
        const volatile fx_rec *r = &recs[i];
        if (r->status != FX_ST_OK) {
            rsx_only(3, "SPU %d refused picture %u (status %u)", i, (unsigned)P.job, (unsigned)r->status);
            return;
        }
        if (r->ticks > slowest) slowest = r->ticks;
        if (r->check_row == P.check) ck = i;
    }
    if (P.frame - P.sent_frame > period(P.mode)) B.late++;   /* the reports came after the next picture was due */
    int first = sky ? first_open && !P.first_frame : aur_open && !P.aur_frame;
    if (first || (sky ? P.sky_n % CHECK_SKY : P.aur_n % CHECK_AUR) == 0) {   /* the check row, read back from RSX memory */
        const volatile fx_rec *r = &recs[ck < 0 ? 0 : ck];
        u32 s1 = ck < 0 ? 0 : row_sum(sky ? tex_pair_written(&cloud) : tex_pair_written(&aur), sky ? FX_SKY_PITCH : FX_AURORA_PITCH, P.check,
                                      sky ? FX_SKY_W : FX_AURORA_W);
        u32 s2 = ck < 0 || !sky ? 0 : row_sum(tex_pair_written(&glass), FX_GLASS_PITCH, P.check / 2, FX_GLASS_W);
        P.checks++;
        frame_ev |= EV_CHECK;
        if (ck < 0 || s1 != r->sum || (sky && s2 != r->sum2)) {
            rsx_only(2, "picture %u's check row %u: %s", (unsigned)P.job, (unsigned)P.check,
                     ck < 0 ? "no SPU reported it" : "the bytes in RSX memory differ from the SPU's sum");
            return;
        }
        if (first) couch_log("effects: the first %s picture checked (row %u, sum 0x%08x), %.2f ms on the slowest SPU", sky ? "sky" : "aurora",
                             (unsigned)P.check, (unsigned)r->sum, tick_ms(slowest));
    }
    double ms = tick_ms(slowest);
    if (sky) {
        tex_pair_swap(&cloud);
        tex_pair_swap(&glass);
        P.have_sky = 1;
        P.sky_n++;
        P.sky_ms += ms;
        if (ms > P.sky_max) P.sky_max = ms;
        if (first) P.first_frame = P.frame;
    } else {
        tex_pair_swap(&aur);
        P.have_aur = 1;
        P.aur_n++;
        P.aur_ms += ms;
        if (ms > P.aur_max) P.aur_max = ms;
        if (first) P.aur_frame = P.frame;
    }
    P.busy = 0;
}

/* fx_first closes once its picture was drawn (a frame after its swap) and
 * 60 frames of rain ran; fx_aurora once its picture was drawn. The journal
 * line now, its couch.txt line in the next frame: each write waits for the
 * drive (fs_write_file's fsync), and two in one frame may miss its vsync. */
static const char *log_later;

static void entries_check(void)
{
    if (first_open && P.first_frame && P.frame > P.first_frame && R.frames >= FIRST_RAIN) {
        journal_done("fx_first", 0, 1);
        frame_ev |= EV_JOURNAL;
        first_open = 0;
        log_later = "effects: fx_first closed: the first sky picture drawn and checked, 60 frames of rain";
    }
    if (aur_open && P.aur_frame && P.frame > P.aur_frame) {
        journal_done("fx_aurora", 0, 1);
        frame_ev |= EV_JOURNAL;
        aur_open = 0;
        log_later = "effects: fx_aurora closed: the first aurora picture drawn and checked";
    }
}

/* The frame budget (design 3.13): each 2 s, more than 3 periods over 20 ms
 * or a late picture is a step down; 60 s with none, a step back up. Since
 * the round of 2026-10-14 the sky starts at 60 pictures a second. */
static const char *const step_words[7] = {"60 pictures a second", "30 pictures a second", "20 pictures a second", "15 pictures a second",
                                          "the rain at half", "no rays, no twinkle, no hero motion", "RSX only"};

static void budget(void)
{
    ui_stat s;
    ui_stats(&s, 0);
    s64 t = now();
    if (s.period_us > SLOW_PERIOD_US) {
        B.slow++;
        B.calm_since = t;
    }
    if (t - B.since < WINDOW_US) return;
    if (B.slow > 3 || B.late) {
        fx_step++;
        couch_log("effects: step %d (%s): %d slow periods and %d late pictures in 2 s", fx_step, step_words[fx_step], B.slow, B.late);
        if (fx_step >= 6) rsx_only(4, "the frame budget's last step (the frames were slow)");
        B.calm_since = t;
    } else if (fx_step > 0 && t - B.calm_since >= CALM_US) {
        fx_step--;
        couch_log("effects: step %d (%s): 60 s with no slow period", fx_step, step_words[fx_step]);
        B.calm_since = t;
    }
    B.since = t;
    B.slow = B.late = 0;
}

void fx_tick(int mode)
{
    static long minute = -1;
    static int fetches = -1;
    long utc = clock_utc();
    s64 t = now();
    float dt = clampf((float)((double)(t - P.last_us) / 1e6), 0, 0.1f);
    P.last_us = t;
    P.frame++;
    if (log_later) {                         /* entries_check's line, a frame after its journal line */
        couch_log("%s", log_later);
        log_later = NULL;
    }
    if (utc / 60 != minute || board_fetches != fetches) {   /* the scene each minute and at each reply */
        minute = utc / 60;
        fetches = board_fetches;
        scene(utc);
    }
    P.off_x = fmodf(P.off_x + fx_sc.wind_x * dt, DRIFT_WRAP);   /* Group B's rule: the drift by dt, so a wind change makes no jump */
    P.off_y = fmodf(P.off_y + fx_sc.wind_y * dt, DRIFT_WRAP);
    P.evo = fmodf(P.evo + EVOLVE * dt, EVOLVE_WRAP);   /* the clouds' change, by dt too */
    P.flash = P.flash > dt / FLASH_S ? P.flash - dt / FLASH_S : 0;
    for (int i = 0; i < 4; i++) P.spr[i] = fmodf(P.spr[i] + fx_sc.wind_x * 4 * (0.6f + 0.2f * (float)i) * dt, 1920 + 512 * sprite_at[i][2]);
    if (fx_state != FXS_SPU) return;
    entries_check();
    budget();
    if (fx_state != FXS_SPU) return;
    if (P.busy) pics_poll();
    if (fx_state != FXS_SPU || P.busy) return;
    entries_check();
    if (P.frame - P.sent_frame >= period(mode) || !P.job) pics_send(mode);
}

/* ---- the draws ---------------------------------------------------------------------- */

void fx_draw_gradient(void)
{
    float ox, oy;
    u32 c[4];
    if (fx_state == FXS_NONE) return;
    screen_origin(&ox, &oy);
    for (int i = 0; i < 4; i++) c[i] = fx_sc.sky[i] << 8 | 0xff;
    rect_v(ox, oy, 1920, 594, c[0], c[1]);   /* top to 55 %, 55 % to 80 %, 80 % to the bottom (whole pixels) */
    rect_v(ox, oy + 594, 1920, 270, c[1], c[2]);
    rect_v(ox, oy + 864, 1920, 216, c[2], c[3]);
}

/* RSX only: four soft sprites, each a size of the blob, that drift with the
 * wind across the screen and come back on the other side. */
static void sprites(void)
{
    const float (*at)[3] = sprite_at;
    const gfx_tex *t = blob_t();
    float ox, oy;
    if (!t || fx_sc.cover <= 0.02f) return;
    screen_origin(&ox, &oy);
    u32 a = (u32)(0x50 + fx_sc.cover * 0x90);
    for (int i = 0; i < 4; i++) {
        float w = 512 * at[i][2], h = 256 * at[i][2], span = 1920 + w;
        float x = fmodf(at[i][0] * 1920 + P.spr[i], span);
        if (x < 0) x += span;
        tex_sub(t, ox + x - w, oy + at[i][1] * 1080, w, h, 0, 0, 1, 1, fx_sc.lit << 8 | a);
    }
}

void fx_draw_clouds(void)
{
    float ox, oy;
    if (fx_state == FXS_RSX) {
        sprites();
        return;
    }
    if (fx_state != FXS_SPU || !P.have_sky) return;
    screen_origin(&ox, &oy);
    tex_rect(tex_pair_shown(&cloud), ox, oy, 1920, 1080, 0xffffffff);
}

void fx_draw_sky(void)
{
    fx_draw_gradient();
    fx_draw_clouds();
}

int fx_draw_aurora(void)
{
    float ox, oy;
    if (fx_state != FXS_SPU || !P.have_aur) return 0;
    screen_origin(&ox, &oy);
    tex_rect(tex_pair_shown(&aur), ox, oy, 1920, 1080, 0xffffffff);
    return 1;
}

const gfx_tex *fx_glass_tex(void) { return fx_state == FXS_SPU && P.have_sky ? tex_pair_shown(&glass) : NULL; }

/* The rounded corners get aa_corners' fringe in the tint (round of
 * 2026-10-14: the corners were stair-stepped). */
void fx_glass(float x, float y, float w, float h, float r, u32 tint)
{
    const gfx_tex *g = fx_glass_tex();
    if (g) {
        float u0, v0, u1, v1;
        glass_uv(x, y, w, h, &u0, &v0, &u1, &v1);
        tex_round(g, x, y, w, h, r, u0, v0, u1, v1, 0xffffffff);
        round_rect(x, y, w, h, r, tint);
    } else {
        u32 a = (tint & 255) + 0x30;
        tint = (tint & ~0xffu) | (a > 255 ? 255 : a);
        round_rect(x, y, w, h, r, tint);
    }
    aa_corners(x, y, w, h, r, tint);
}

void fx_flash(float level) { P.flash = clampf(level, 0, 1); }

/* The rain's output on the PPU: the quads into the block in 32-bit stores
 * (gfx_put's rule: no merged stores into the vertex area), never memcpy. */
typedef struct {
    fx_io io;
    u8 *base;
} ppu_io;

static void p_put(fx_io *io, uint32_t out, uint32_t offset, const void *ls, uint32_t bytes, uint32_t buf)
{
    (void)out;
    (void)buf;
    volatile u32 *d = (volatile u32 *)(((ppu_io *)io)->base + offset);
    const u32 *s = ls;
    for (u32 i = 0; i < bytes / 4; i++) d[i] = s[i];
}

static void p_wait(fx_io *io, uint32_t buf)
{
    (void)io;
    (void)buf;
}

/* a block the SPU did not fill: every word 0 (no colour, no area) */
static void zero_block(gfx_vtx *q, u32 n)
{
    volatile u32 *d = (volatile u32 *)q;
    for (u32 i = 0; i < n * 5; i++) d[i] = 0;
}

/* Thread 5's job into the block: the word, then the wait (the PPU makes no
 * gfx call until the report: a ring wrap or a drain then would send the
 * draw before its vertices). 1 when the quads are there. */
static int rain_spu(fx_ctl *c, gfx_vtx *q, u32 nq)
{
    c->serial = ++serial[RAIN_T];
    __sync_synchronize();
    s32 rc = sysSpuThreadWriteMb(threads[RAIN_T], c->serial);
    if (rc) {
        rsx_only(5, "the rain's mailbox word failed: rc 0x%08x", (unsigned)rc);
        return 0;
    }
    s64 a = now(), us;
    long polls = 0;
    while (!came(RAIN_T)) {
        if (now() - a > RAIN_LATE_US || ++polls > RAIN_POLLS) {
            rsx_only(1, "SPU 5 did not finish the rain in %d ms", RAIN_LATE_US / 1000);
            return 0;
        }
    }
    us = now() - a;
    __sync_synchronize();
    const volatile fx_rec *r = &recs[RAIN_T];
    if (r->status != FX_ST_OK || r->quads != nq) {
        rsx_only(3, "SPU 5 refused the rain (status %u, %u quads of %u)", (unsigned)r->status, (unsigned)r->quads, (unsigned)nq);
        return 0;
    }
    if (us > RAIN_SLOW_US && ++R.late <= LOG_LATE) couch_log("effects: a late rain report: %.2f ms (frame %u)", (double)us / 1000, (unsigned)P.frame);
    if ((u32)us > R.slowest_us) R.slowest_us = (u32)us;
    if ((first_open && R.frames < FIRST_RAIN) || R.jobs % RAIN_CHECK_EVERY == 0) {   /* the word sum of every vertex sent */
        if (fx_vsum((const fx_vtx *)q, 4 * nq) != r->sum) {
            rsx_only(2, "the rain's vertices differ from SPU 5's sum");
            return 0;
        }
    }
    double ms = tick_ms(r->ticks);
    R.jobs++;
    R.ms += ms;
    if (ms > R.max) R.max = ms;
    return 1;
}

void fx_draw_rain(float alpha)
{
    static fx_rain_state ppu_rain;           /* RSX only: the PPU's particles */
    static fx_vtx ppu_q[2][FX_RAIN_CHUNK * 4];
    static fx_ctl pc;
    int spu = fx_state == FXS_SPU, count = fx_sc.precip, kind = fx_sc.kind;
    if (fx_state == FXS_NONE) return;
    s64 t = now();
    float dt = clampf((float)((double)(t - R.last_us) / 1e6), 0, 0.1f);
    R.last_us = t;
    if (alpha < 1 && count > PPU_RAIN) count = PPU_RAIN;     /* over the aurora: dimmer, 300 drops */
    if (spu && fx_step >= 4 && kind == FX_DROPS) count /= 2;
    if (!spu && count > PPU_RAIN) count = PPU_RAIN;
    int probe = spu && first_open && R.frames < FIRST_RAIN;   /* fx_first: 60 frames of the DMA path, also when dry (40 empty splashes) */
    if (!count && !probe) return;
    u32 nq = fx_rain_quads((u32)count, (u32)kind);
    const gfx_tex *tx = kind == FX_FLAKES ? dot_t() : NULL;
    gfx_texture(tx);
    gfx_vtx *q = gfx_block(GFX_QUADS, 4 * nq, 128);
    if (!q) return;                          /* the area cannot hold it: no job this frame */
    float ox, oy;
    screen_origin(&ox, &oy);
    fx_ctl *c = spu ? &ctls[RAIN_T] : &pc;
    memset(c, 0, sizeof *c);
    c->mode = FX_RAIN;
    c->check_row = FX_NO_CHECK;
    c->out = ea(q);
    c->time = (float)((double)(t - t0) / 1e6);
    c->seed = SEED;
    c->p.rain.count = (u32)count;
    c->p.rain.kind = (u32)kind;
    c->p.rain.rgb = kind == FX_FLAKES ? 0xf2f5fa : 0x9cc8ff;
    c->p.rain.dt = dt;
    c->p.rain.wind = fx_sc.rain_wind;
    c->p.rain.fall = kind == FX_FLAKES ? 90.0f : 1300.0f;
    c->p.rain.ox = ox;
    c->p.rain.oy = oy;
    c->p.rain.alpha = alpha;
    if (spu) {
        if (!rain_spu(c, q, nq)) zero_block(q, 4 * nq);
    } else {
        ppu_io io = {{p_put, p_wait}, (u8 *)q};
        fx_rec rec;
        if (fx_rain_job(c, &ppu_rain, ppu_q, &io.io, &rec)) zero_block(q, 4 * nq);
    }
    R.frames++;
}

/* ---- the exit ----------------------------------------------------------------------- */

/* Thread i has no job out: its last report is in. */
static int idle(int i) { return came(i); }

void fx_stop(void)
{
    if (log_later) couch_log("%s", log_later);
    log_later = NULL;
    if (stuck) couch_log("effects: the group is stuck since the terminate failed: it goes with the process");
    else if (grp == GROUP_RUNNING) {
        couch_log("effects: the exit stops the SPUs%s", P.busy ? ", a picture is out" : "");   /* the mark of a hang below */
        for (int i = 0; i < FX_SPUS; i++) {
            ctls[i].exit = 1;                /* read before the serial: any word ends the thread */
            ctls[i].serial = serial[i] + 1;
        }
        __sync_synchronize();
        int sent[FX_SPUS] = {0}, n = 0, naps = 0;
        s64 a = now();
        do {
            sysUsleep(1000);
            n = 0;
            for (int i = 0; i < FX_SPUS; i++) {
                if (!sent[i] && idle(i)) {   /* a word only to a thread whose last report came: the mailbox never fills */
                    sent[i] = 1;
                    if (sysSpuThreadWriteMb(threads[i], ctls[i].serial)) sent[i] = 2;
                }
                n += ((const volatile fx_rec *)&recs[i])->done == 2;
            }
        } while (n < FX_SPUS && now() - a < EXIT_US && ++naps < 1000);
        double sec = (double)(now() - a) / 1e6;
        if (n == FX_SPUS) {
            couch_log("effects: 6 exit reports in %.2f s", sec);
            group_join(1, 0);
        } else {
            couch_log("effects: %d of 6 exit reports in %.2f s: the terminate", n, sec);
            group_kill(1, 0);
        }
        teardown();
    } else if (grp == GROUP_IDLE || imported)
        teardown();
    entries_close(1, 0);
    if (P.sky_n || P.aur_n || R.jobs)
        couch_log("effects: %u sky pictures, %.2f ms mean, %.2f max; %u aurora pictures, %.2f ms mean, %.2f max (the slowest SPU of each); "
                  "%u rain jobs, %.3f ms mean, %.3f max, %u reports over 2 ms (latest %.2f ms); %u check rows",
                  (unsigned)P.sky_n, P.sky_n ? P.sky_ms / P.sky_n : 0.0, P.sky_max, (unsigned)P.aur_n, P.aur_n ? P.aur_ms / P.aur_n : 0.0,
                  P.aur_max, (unsigned)R.jobs, R.jobs ? R.ms / R.jobs : 0.0, R.max, (unsigned)R.late, R.slowest_us / 1000.0,
                  (unsigned)P.checks);
}

/* ---- the words -------------------------------------------------------------------- */

int fx_reduced(void) { return fx_step >= 5; }

void fx_words(char *buf, int n)
{
    const char *jf = journal_state("fx_first") == J_OK ? "ok" : journal_state("fx_first") == J_PENDING ? "running"
                   : journal_state("fx_first") == J_BAD ? "failed" : journal_state("fx_first") == J_FROZEN ? "froze" : "not run";
    if (fx_state == FXS_SPU)
        snprintf(buf, (size_t)n, "SPUs, %s; sky %.2f ms, rain %.3f ms a job (the slowest SPU's mean); fx_first %s", step_words[fx_step],
                 P.sky_n ? P.sky_ms / P.sky_n : 0.0, R.jobs ? R.ms / R.jobs : 0.0, jf);
    else if (fx_state == FXS_RSX) snprintf(buf, (size_t)n, "RSX only: %s", fx_why);
    else snprintf(buf, (size_t)n, "not started");
}

void fx_get_stats(fx_stats *st)
{
    memset(st, 0, sizeof *st);
    st->sky_n = P.sky_n;
    st->aur_n = P.aur_n;
    st->rain_jobs = R.jobs;
    st->sky_ms = P.sky_n ? P.sky_ms / P.sky_n : 0;
    st->sky_max = P.sky_max;
    st->aur_ms = P.aur_n ? P.aur_ms / P.aur_n : 0;
    st->rain_ms = R.jobs ? R.ms / R.jobs : 0;
    st->rain_late = R.late;
    st->sky_rate = 60 / (int)period(FXM_SKY);
    st->aur_rate = 60 / (int)period(FXM_AURORA);
    st->step_words = step_words[fx_step];
    st->drops = fx_sc.precip;
    st->flakes = fx_sc.kind == FX_FLAKES;
}

void fx_log_words(char *buf, int n)
{
    if (fx_state == FXS_SPU)
        snprintf(buf, (size_t)n, "; effects: picture %u, rain job %u, %u late rain reports, step %d", (unsigned)P.job, (unsigned)serial[RAIN_T],
                 (unsigned)R.late, fx_step);
    else if (fx_state == FXS_RSX) snprintf(buf, (size_t)n, "; effects: RSX only");
    else buf[0] = 0;
}
