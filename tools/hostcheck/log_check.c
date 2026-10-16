/* The logger implementation is extracted from main.c by run.zsh so this
 * deterministic slow-write test exercises the production queue and lock. */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned long long u64;
int fs_ok = 1;
unsigned frame_ev;
volatile int log_async;
#define EV_LOG 1
#define LOG "host-log"
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static int writer_waiting, release_writer, second_started, write_count;
static char first_copy[32], written[4][400];

void sysUsleep(unsigned us) { (void)us; }
int fs_write_file(const char *path, const void *data, u64 n, int append)
{
    (void)path; (void)append;
    pthread_mutex_lock(&mu);
    if (strstr(data, "FIRST\n")) {
        snprintf(first_copy, sizeof first_copy, "%.*s", (int)n, (const char *)data);
        writer_waiting = 1;
        pthread_cond_broadcast(&cv);
        while (!release_writer) pthread_cond_wait(&cv, &mu);
    }
    if (write_count >= 4) abort();
    snprintf(written[write_count++], sizeof written[0], "%.*s", (int)n, (const char *)data);
    pthread_mutex_unlock(&mu);
    return 0;
}
#include "log_impl.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); exit(1); } } while (0)
static void *drain(void *unused) { (void)unused; couch_log_drain(); return NULL; }
static void *write_second(void *unused)
{
    (void)unused;
    pthread_mutex_lock(&mu);
    second_started = 1;
    pthread_cond_broadcast(&cv);
    pthread_mutex_unlock(&mu);
    couch_log("SECOND");
    return NULL;
}

int main(void)
{
    pthread_t thread, second;
    log_async = 1;
    couch_log("FIRST");
    CHECK(pthread_create(&thread, NULL, drain, NULL) == 0);
    pthread_mutex_lock(&mu);
    while (!writer_waiting) pthread_cond_wait(&cv, &mu);
    pthread_mutex_unlock(&mu);

    /* A second caller must wait for the first fsync/close before it can
     * reuse lq_out or append its line. */
    log_async = 0;
    CHECK(pthread_create(&second, NULL, write_second, NULL) == 0);
    pthread_mutex_lock(&mu);
    while (!second_started) pthread_cond_wait(&cv, &mu);
    pthread_mutex_unlock(&mu);
    CHECK(!strcmp(first_copy, "FIRST\n"));
    pthread_mutex_lock(&mu);
    release_writer = 1;
    pthread_cond_broadcast(&cv);
    pthread_mutex_unlock(&mu);
    CHECK(pthread_join(thread, NULL) == 0);
    CHECK(pthread_join(second, NULL) == 0);
    CHECK(write_count == 2);
    CHECK(!strcmp(written[0], "FIRST\n") && !strcmp(written[1], "SECOND\n"));
    puts("PASS delayed logger write serializes both lines");
    return 0;
}
