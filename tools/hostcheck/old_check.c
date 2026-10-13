/* Couch's host check of the old Couch (tools/hostcheck/run.zsh): the parser
 * of Couch 0.1.0 (source/parse.c at bcf4243, which run.zsh takes from git)
 * against the Worker test's reply (argv[1]). The reply's new keys of
 * 2026-10-13 must leave 0.1.0's Board as it was: a body within its 4 KB, the
 * end line, every old field. Prints PASS or FAIL; the exit status is the
 * number of failed checks. */
#include <stdio.h>
#include <string.h>
#include "parse.h"

static int fails;
#define CHECK(c)                                                              \
    do {                                                                      \
        if (!(c)) {                                                           \
            fails++;                                                          \
            printf("  FAIL line %d: %s\n", __LINE__, #c);                     \
        }                                                                     \
    } while (0)

int main(int argc, char **argv)
{
    static char w[65536];
    static board_data b;
    FILE *f = argc > 1 ? fopen(argv[1], "rb") : NULL;
    if (!f) {
        printf("FAIL cannot read the reply\n");
        return 1;
    }
    int n = (int)fread(w, 1, sizeof w - 1, f);
    fclose(f);
    CHECK(n <= BOARD_MAX);                           /* 0.1.0 refuses a longer body whole */
    CHECK(board_parse(w, n, 0, &b) == BP_OK && board_has_end(w, n));
    CHECK(b.version == 1 && b.now == 1791913620L && b.has_offset && b.offset == -10800);
    CHECK(!strcmp(b.city, "Jun\xc3\xadn") && b.n_usd == 6 && !strcmp(b.usd[5].label, "Tarjeta") && !strcmp(b.usd[5].sell, "1.995,50"));
    CHECK(b.has_wx && !strcmp(b.temp, "19\xc2\xb0") && !strcmp(b.wind, "10 km/h") && b.is_day && !strcmp(b.wx_src, "Open-Meteo"));
    CHECK(b.n_day == 3 && !strcmp(b.day[2].label, "Thu") && !strcmp(b.day[2].rain, "48%"));
    CHECK(!strcmp(b.usd_feed.state, "ok") && b.usd_feed.age == 42 && !strcmp(b.wx_feed.state, "ok") && b.wx_feed.age == 310);
    printf("%s Couch 0.1.0's parser (bcf4243) reads the reply of %d bytes: every old field, the new keys skipped\n", fails ? "FAIL" : "PASS", n);
    return fails;
}
