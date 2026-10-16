/* Couch's SPU twin for the host preview: the effects' group (design 8.1,
 * stage 8). The SPU calls reach pv_syscall (stub/sys/spu.h, Cell Bench's);
 * this file answers them for one group of six threads of spu_fx and runs
 * ps3gfx's own kernels and job drivers (<ps3gfx/spu_fx_kern.h>) in their
 * place, as the SPU program's main does: at the start each thread puts its
 * first report (serial 0, the known-answer test); a mailbox word runs the
 * job at once, in the calling thread, from the thread's control block, and
 * puts its report; the exit flag puts the last report (done 2) and the exit
 * status 0x700 | index. So the preview stays deterministic: a job's
 * pictures and quads are ready when the word returns, and its busy ticks
 * are fixed numbers (sky 1.2 ms, aurora 0.8 ms, rain 0.09 ms), never the
 * host's time. The outputs go into the preview's RSX arena (the textures)
 * and its vertex area in 32-bit stores, as the PPU's fallback writes.
 *   PV_SPU_FAIL=<n>     syscall n fails (0x80010002)
 *   PV_FX_LATE=<kind>   the jobs of kind (sky, aurora or rain) from the
 *                       thread's PV_FX_LATE_AT-th job of that kind on
 *                       (default 1) put no report: the PPU's hard deadline;
 *                       start: thread 2 puts no start report
 *   PV_FX_KAT=bad       thread 4's known-answer test gives a wrong word
 * Each SPU call but the mailbox words goes to $PV_ROOT/spu.log as "spu: <n>
 * ..."; the join adds the count of words each thread got. */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "ppu-lv2.h"
#include "sys/spu.h"
#include "spu_fx_kern.h"

/* the program: the twin needs no bytes, only the symbols of the archive's member */
const uint8_t spu_fx_spu[128] __attribute__((aligned(128)));
const uint32_t spu_fx_spu_size = 0;

static void note(const char *fmt, ...)
{
    char p[1024];
    snprintf(p, sizeof p, "%s/spu.log", getenv("PV_ROOT") ? getenv("PV_ROOT") : "/tmp/pv");
    FILE *f = fopen(p, "ab");
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    fputs("spu: ", f);
    vfprintf(f, fmt, ap);
    fputc('\n', f);
    va_end(ap);
    fclose(f);
}

s32 sysSpuImageImport(sysSpuImage *image, const void *elf, u32 type)
{
    (void)elf;
    (void)type;
    memset(image, 0, sizeof *image);
    note("import");
    return getenv("PV_SPU_FAIL") && !strcmp(getenv("PV_SPU_FAIL"), "import") ? (s32)0x80010002 : 0;
}

s32 sysSpuImageClose(sysSpuImage *image)
{
    (void)image;
    note("close");
    return 0;
}

#define GROUP 0x4100
#define THREAD 0x5100

static struct {
    u32 num, cause, status;
    int terminated;
    sysSpuThreadArgument arg[FX_SPUS];
    int exited[FX_SPUS];
    s32 exit_status[FX_SPUS];
    u32 words[FX_SPUS], jobs[FX_SPUS][3];
} G;

/* each thread's local store: the noise table is the same in all six, the
 * buffers are used one job at a time (the twin runs one job at a time), the
 * rain's particles are thread 5's (the only rain thread) */
static uint32_t tab[FX_NOISE_N * FX_NOISE_N];
static int tab_ok;
static fx_sky_ws sws;
static uint16_t band[3 * FX_GLASS_BAND_MAX * FX_GLASS_W] __attribute__((aligned(128)));
static fx_aur_ws aws;
static fx_rain_state rain;
static fx_vtx quads[2][FX_RAIN_CHUNK * 4] __attribute__((aligned(128)));

typedef struct {
    fx_io io;
    uint64_t out[2];
} h_io;

/* the DMA: 32-bit stores into the arena or the vertex area */
static void h_put(fx_io *io, uint32_t out, uint32_t offset, const void *ls, uint32_t bytes, uint32_t buf)
{
    (void)buf;
    volatile uint32_t *d = (volatile uint32_t *)(uintptr_t)(((h_io *)io)->out[out & 1] + offset);
    const uint32_t *s = ls;
    for (uint32_t i = 0; i < bytes / 4; i++) d[i] = s[i];
}

static void h_wait(fx_io *io, uint32_t buf)
{
    (void)io;
    (void)buf;
}

static fx_rec *rec_of(u32 i) { return (fx_rec *)(uintptr_t)G.arg[i].arg0; }

/* thread i's start, as the program's main: the first report */
static void start(u32 i)
{
    fx_rec *r = rec_of(i);
    memset(r, 0, sizeof *r);
    r->index = i;
    r->arg0 = G.arg[i].arg0;
    r->arg1 = G.arg[i].arg1;
    r->arg2 = G.arg[i].arg2;
    r->check_row = FX_NO_CHECK;
    r->kat = fx_kat() ^ (getenv("PV_FX_KAT") && i == 4 ? 1u : 0u);
    if (getenv("PV_FX_LATE") && !strcmp(getenv("PV_FX_LATE"), "start") && i == 2) return;   /* no start report */
    __atomic_store_n(&r->magic, FX_MAGIC, __ATOMIC_RELEASE);
}

static void end(u32 i, s32 status)
{
    G.exited[i] = 1;
    G.exit_status[i] = status;
}

/* thread i woken by its mailbox: one job, or its exit */
static void job(u32 i, u32 word)
{
    static const char *const kname[3] = {"sky", "rain", "aurora"};
    fx_rec *r = rec_of(i), tmp;
    const fx_ctl *c = (const fx_ctl *)(uintptr_t)G.arg[i].arg1;
    if (G.exited[i]) return;
    G.words[i]++;
    r->word = word;
    r->mode = c->mode;
    if (c->exit) {
        r->serial = c->serial;
        r->done = 2;
        end(i, (s32)(FX_EXIT_DONE | i));
        return;
    }
    if (c->serial != word) {
        r->serial = word;
        r->status = FX_ST_SERIAL;
        r->done = 1;
        return;
    }
    if (c->mode < FX_SKY || c->mode > FX_AURORA) {
        end(i, (s32)(FX_EXIT_MODE | i));
        return;
    }
    u32 k = c->mode - FX_SKY;
    G.jobs[i][k]++;
    const char *late = getenv("PV_FX_LATE");
    int at = getenv("PV_FX_LATE_AT") ? atoi(getenv("PV_FX_LATE_AT")) : 1;
    if (late && !strcmp(late, kname[k]) && (int)G.jobs[i][k] >= at) return;   /* the job never reports */
    if (!tab_ok) {
        fx_noise_init(tab);
        tab_ok = 1;
    }
    h_io io = {{h_put, h_wait}, {c->out, c->out2}};
    uint32_t rc, ticks;
    memset(&tmp, 0, sizeof tmp);
    if (c->mode == FX_SKY) {
        rc = fx_sky_job(c, tab, &sws, band, FX_GLASS_BAND_MAX, &io.io, &tmp);
        ticks = 95760;                                   /* 1.2 ms of the 79.8 MHz timebase */
    } else if (c->mode == FX_AURORA) {
        rc = fx_aurora_job(c, tab, &aws, &io.io, &tmp);
        ticks = 63840;
    } else {
        rc = fx_rain_job(c, &rain, quads, &io.io, &tmp);
        ticks = 7182;
    }
    if (rc) {
        end(i, (s32)(rc | i));
        return;
    }
    r->sum = tmp.sum;
    r->sum2 = tmp.sum2;
    r->check_row = tmp.check_row;
    r->quads = tmp.quads;
    r->ticks = ticks;
    r->count++;
    r->status = FX_ST_OK;
    r->done = 1;
    __atomic_store_n(&r->serial, c->serial, __ATOMIC_RELEASE);
}

u64 pv_syscall(int n, u64 a, u64 b, u64 c, u64 d, u64 e, u64 f, u64 g)
{
    (void)d;
    (void)e;
    (void)g;
    if (getenv("PV_SPU_FAIL") && atoi(getenv("PV_SPU_FAIL")) == n) {
        note("%d: fails (PV_SPU_FAIL)", n);
        return 0x80010002;
    }
    switch (n) {
    case 169:                                        /* initialize */
        note("169 initialize %u", (unsigned)a);
        return 0;
    case 170:                                        /* group create */
        memset(&G, 0, sizeof G);
        G.num = (u32)b;
        *(u32 *)(uintptr_t)a = GROUP;
        note("170 group create, %u threads", (unsigned)b);
        return 0;
    case 172:                                        /* a thread in the group */
        if (c >= FX_SPUS) return 0x80010002;
        G.arg[c] = *(const sysSpuThreadArgument *)(uintptr_t)f;
        *(u32 *)(uintptr_t)a = THREAD + (u32)c;
        note("172 thread %u: arg2 0x%llx", (unsigned)c, (unsigned long long)G.arg[c].arg2);
        return 0;
    case 173:                                        /* start: each thread's first report */
        note("173 start");
        for (u32 i = 0; i < G.num; i++) start(i);
        return 0;
    case 190:                                        /* a mailbox word */
        if (a < THREAD || a >= THREAD + FX_SPUS) return 0x80010002;
        job((u32)(a - THREAD), (u32)b);
        return 0;
    case 177:                                        /* terminate: every thread stops, none with a status */
        note("177 terminate");
        for (u32 i = 0; i < G.num; i++)
            if (!G.exited[i]) {
                G.exited[i] = 1;
                G.exit_status[i] = -1;
            }
        G.terminated = 1;
        G.status = (u32)b;
        return 0;
    case 178: {                                      /* join */
        char w[80];
        int k = 0;
        for (u32 i = 0; i < G.num; i++) k += snprintf(w + k, sizeof w - (size_t)k, " %u", (unsigned)G.words[i]);
        note("178 join; the mailbox words of each thread:%s", w);
        *(u32 *)(uintptr_t)b = G.terminated ? SPU_THREAD_GROUP_JOIN_TERMINATED : SPU_THREAD_GROUP_JOIN_ALL_THREADS_EXIT;
        *(u32 *)(uintptr_t)c = G.status;
        return 0;
    }
    case 165:                                        /* a thread's exit status: none after a terminate */
        if (a < THREAD || a >= THREAD + FX_SPUS) return 0x80010002;
        if (G.exit_status[a - THREAD] < 0 || !G.exited[a - THREAD]) return 0x80010013;
        *(s32 *)(uintptr_t)b = G.exit_status[a - THREAD];
        return 0;
    case 171:                                        /* group destroy */
        note("171 destroy");
        return 0;
    }
    return 0x80010003;
}
