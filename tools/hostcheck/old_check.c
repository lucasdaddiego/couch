/* Couch's host check of an old Couch (tools/hostcheck/run.zsh): the parser
 * of Couch 0.1.0 (source/parse.c at 129a6b7) or 0.2.0 (at aa3945b), which
 * run.zsh takes from git, against a reply (argv[1]: the Worker test's
 * reply, or the same with the keys of 0.3.0). The new keys must leave the
 * old Board as it was: a body within its 4 KB, the end line, every old
 * field. argv[2] names the parser in the PASS line. Prints PASS or FAIL;
 * the exit status is the number of failed checks. */
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
    const char *base = strrchr(argv[1], '/');
    CHECK(n <= BOARD_MAX);                           /* an old Couch refuses a longer body whole */
    CHECK(board_parse(w, n, 0, &b) == BP_OK && board_has_end(w, n));
    CHECK(b.version == 1 && b.now == 1791913620L && b.has_offset && b.offset == -10800);
    CHECK(!strcmp(b.city, "Jun\xc3\xadn") && b.n_usd == 6 && !strcmp(b.usd[5].label, "Tarjeta") && !strcmp(b.usd[5].sell, "1.995,50"));
    CHECK(!strcmp(b.usd[1].label, "Blue") && !strcmp(b.usd[1].sell, "1.540,00") && !strcmp(b.usd[1].when, "Fri 18:00"));
    CHECK(b.has_wx && !strcmp(b.temp, "19\xc2\xb0") && !strcmp(b.wind, "10 km/h") && b.is_day && !strcmp(b.wx_src, "Open-Meteo"));
    CHECK(b.n_day == 3 && !strcmp(b.day[2].label, "Thu") && !strcmp(b.day[2].rain, "48%"));
    CHECK(!strcmp(b.usd_feed.state, "ok") && b.usd_feed.age == 42 && !strcmp(b.wx_feed.state, "ok") && b.wx_feed.age == 310);
#ifdef BOARD_MONEY   /* 0.2.0's keys: money_chg is not a money line, days_fx not a day line */
    CHECK(b.n_money == 3 && !strcmp(b.money[0].label, "Riesgo pa\xc3\xads") && !strcmp(b.money[0].value, "589") && !strcmp(b.money[0].state, "ok"));
    CHECK(b.has_rain && b.rain_wet && b.has_sun && !strcmp(b.sunset, "19:16") && !strcmp(b.moon, "Waxing crescent moon"));
    CHECK(b.n_news == 8 && !strcmp(b.news_feed.state, "ok"));
#endif
    printf("%s Couch %s's parser reads %s, %d bytes: every old field, the new keys skipped\n", fails ? "FAIL" : "PASS",
           argc > 2 ? argv[2] : "?", base ? base + 1 : argv[1], n);
    return fails;
}
