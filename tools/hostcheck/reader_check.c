/* Couch's host check of the reader's pure code in source/parse.c (0.3.1,
 * stage 11; tools/hostcheck/run.zsh): news_parse and article_parse against
 * the Worker test's replies (worker/test/reader.test.js: the cut and the
 * long /article texts of its articleText test, which run.zsh extracts; its
 * /news item and its ok and summary /article forms, which run.zsh writes
 * as the test makes them, since the test's own hold the feed's computed
 * id, title and link) and the live replies of 2026-10-14
 * (tools/preview/fixtures, decoded), each cut at every byte, with CR LF, as
 * version 2, with long fields, too many lines, a 32 KB list and a 16 KB
 * article; the fold; wrap_lines with a width of 10 px a code point;
 * grid_move, the scroll's helpers, url_sibling. With "fuzz" last, also
 * 20,000 mutated replies of the live list and of a live article (a
 * sanitizer build: no crash, every field ends inside its buffer). Since
 * 0.5.0 also futbol_parse against the template Worker's /futbol test reply
 * (worker/test/board.test.js's, made-up teams, which run.zsh extracts),
 * cut at every byte, with CR LF, as version 2, past its arrays, and 20,000
 * mutated replies. Prints PASS or FAIL per group; the exit status is the
 * number of failed checks.
 *   reader_check <news> <article ok> <article summary> <article cut> <article long> <live news> <live article> <futbol> [fuzz] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "parse.h"

static int fails;
#define CHECK(c)                                                              \
    do {                                                                      \
        if (!(c)) {                                                           \
            fails++;                                                          \
            printf("  FAIL line %d: %s\n", __LINE__, #c);                     \
        }                                                                     \
    } while (0)
#define STR(a, b) CHECK(!strcmp((a), (b)))

static void group(const char *name, int f0) { printf("%s %s\n", fails == f0 ? "PASS" : "FAIL", name); }

static news_data nd;
static article_data ad;
static futbol_data fd;

static char *read_file(const char *p, int *n)
{
    FILE *f = fopen(p, "rb");
    char *s = malloc(70000);
    if (!f || !s) {
        printf("FAIL cannot read %s\n", p);
        exit(1);
    }
    *n = (int)fread(s, 1, 69999, f);
    s[*n] = 0;
    fclose(f);
    return s;
}

#define IN(f) (memchr((f), 0, sizeof(f)) != NULL)

static int id_ok(const char *id)
{
    if (!id[0]) return 1;
    if (strlen(id) != 8) return 0;
    for (int i = 0; i < 8; i++)
        if (!((id[i] >= '0' && id[i] <= '9') || (id[i] >= 'a' && id[i] <= 'f'))) return 0;
    return 1;
}

static int news_bounded(void)
{
    int ok = nd.n >= 0 && nd.n <= NEWS_ITEMS && IN(nd.feed.state) && IN(nd.feed.reason);
    for (int i = 0; i < nd.n; i++) {
        const news_item *it = &nd.item[i];
        ok = ok && IN(it->id) && id_ok(it->id) && IN(it->source) && IN(it->category) && IN(it->title) && it->title[0] &&
             it->age >= -1;
    }
    return ok;
}

static int art_bounded(void)
{
    int ok = IN(ad.source) && IN(ad.category) && IN(ad.title) && IN(ad.date) && IN(ad.link) && IN(ad.state) &&
             IN(ad.reason) && ad.n_para >= 0 && ad.n_para <= ART_PARAS && ad.age >= -1 && !strchr(ad.link, ' ');
    for (int i = 0; ok && i < ad.n_para; i++) {
        int o = ad.para_off[i];
        ok = o >= 0 && o < ART_TEXT && memchr(ad.text + o, 0, (size_t)(ART_TEXT - o)) != NULL && ad.text[o] && (i == 0 || o > ad.para_off[i - 1]);
    }
    return ok;
}

/* ---- /news ---------------------------------------------------------------------------------- */

static void news_worker(const char *w, int wn)
{
    int f0 = fails;
    CHECK(news_parse(w, wn, 0, &nd) == BP_OK);
    CHECK(nd.version == 1 && nd.n == 1);
    STR(nd.item[0].id, "1a2b3c4d");
    STR(nd.item[0].source, "Z\xc3\xbcrich Post");
    STR(nd.item[0].category, "world");
    CHECK(nd.item[0].age == 600);
    STR(nd.item[0].title, "Leaders meet in Z\xc3\xbcrich to agree on a new plan for river shipping");
    STR(nd.feed.state, "ok");
    CHECK(nd.feed.age == 0 && !nd.feed.reason[0]);
    CHECK(news_parse(w, wn, 1, &nd) == BP_OK);
    STR(nd.item[0].source, "Zurich Post");
    STR(nd.item[0].title, "Leaders meet in Zurich to agree on a new plan for river shipping");
    group("the Worker's /news item: every field, and folded", f0);
}

static void news_live(const char *w, int wn)
{
    int f0 = fails;
    CHECK(news_parse(w, wn, 0, &nd) == BP_OK);
    CHECK(nd.n == 30 && news_bounded());
    int asc = 1, cats = 1, srcs = 1;
    for (int i = 0; i < nd.n; i++) {
        const news_item *it = &nd.item[i];
        CHECK(strlen(it->id) == 8);
        if (i && it->age < nd.item[i - 1].age) asc = 0;
        if (strcmp(it->category, "economy") && strcmp(it->category, "politics") && strcmp(it->category, "world")) cats = 0;
        if (strcmp(it->source, "Courier") && strcmp(it->source, "Herald") && strcmp(it->source, "Wire")) srcs = 0;
    }
    CHECK(asc && cats && srcs);
    STR(nd.feed.state, "ok");
    /* every cut: the end line's LF is the reply's last byte */
    int whole = 0, cut = 0, bad = 0;
    for (int k = 0; k <= wn; k++) {
        int rc = news_parse(w, k, k & 1, &nd);
        if (!news_bounded()) bad++;
        if (rc == BP_OK) whole++;
        else if (rc == BP_CUT) cut++;
    }
    CHECK(bad == 0 && whole == 2 && cut == wn - 13);           /* whole: the reply, and the reply without its last LF */
    /* CR LF */
    char *c = malloc((size_t)wn * 2 + 1);
    int m = 0;
    for (int i = 0; i < wn; i++) {
        if (w[i] == '\n') c[m++] = '\r';
        c[m++] = w[i];
    }
    CHECK(news_parse(c, m, 0, &nd) == BP_OK && nd.n == 30 && strlen(nd.item[29].title) > 10 && nd.item[29].title[strlen(nd.item[29].title) - 1] != '\r');
    free(c);
    group("the live /news of 2026-10-14: 30 items, the newest first, cut at every byte, CR LF", f0);
}

static void news_rules(void)
{
    int f0 = fails;
    static char b[NEWS_MAX + 4096];
    CHECK(news_parse("couch-news 2\nitem=a|b|c|1|t|s\nend\n", 33, 0, &nd) == BP_VERSION && nd.version == 2 && nd.n == 0);
    CHECK(news_parse("couch-board 1\nend\n", 18, 0, &nd) == BP_NOT_BOARD);
    CHECK(news_parse("couch-news\nend\n", 15, 0, &nd) == BP_NOT_BOARD);
    CHECK(news_parse("", 0, 0, &nd) == BP_NOT_BOARD);
    const char *r = "couch-news 1\n"
                    "item=0123abcd|BBC|-|-|No category, no age|\n"
                    "item=XYZ|BBC|world|5|A bad id|s\n"
                    "item=01234567|BBC|world|5||no title: not an item\n"
                    "item=89abcdef|BBC|world|7|An older Worker's summary|the summary|and more\n"
                    "noequals\n"
                    "unknown=1\n"
                    "item=fedcba98|  Spaced  |  world |  9  |  T  |  S  \n"
                    "news_state=stale|news.example.org: HTTP 503\n"
                    "news_age=120\n"
                    "end\n"
                    "item=00000000|After|end|1|not read|x\n";
    CHECK(news_parse(r, (int)strlen(r), 0, &nd) == BP_OK && nd.n == 4);
    STR(nd.item[0].category, "");
    CHECK(nd.item[0].age == -1);
    STR(nd.item[1].id, "");
    STR(nd.item[1].title, "A bad id");
    STR(nd.item[2].title, "An older Worker's summary");   /* its 6th field (the summary) and more: dropped */
    STR(nd.item[3].source, "Spaced");
    STR(nd.item[3].category, "world");
    CHECK(nd.item[3].age == 9);
    STR(nd.item[3].title, "T");
    STR(nd.feed.state, "stale");
    STR(nd.feed.reason, "news.example.org: HTTP 503");
    CHECK(nd.feed.age == 120);
    /* 31 items: 30 kept */
    int k = snprintf(b, sizeof b, "couch-news 1\n");
    for (int i = 0; i < 31; i++) k += snprintf(b + k, sizeof b - (size_t)k, "item=%08x|S|world|%d|Title %d|Summary\n", i, i, i);
    k += snprintf(b + k, sizeof b - (size_t)k, "end\n");
    CHECK(news_parse(b, k, 0, &nd) == BP_OK && nd.n == 30);
    STR(nd.item[29].title, "Title 29");
    /* a long title: 150 three-byte code points (450 bytes), cut on a code point */
    k = snprintf(b, sizeof b, "couch-news 1\nitem=00000001|S|world|1|");
    for (int i = 0; i < 150; i++) k += snprintf(b + k, sizeof b - (size_t)k, "\xe2\x82\xac");
    k += snprintf(b + k, sizeof b - (size_t)k, "\nend\n");
    CHECK(news_parse(b, k, 0, &nd) == BP_OK && nd.n == 1);
    CHECK(strlen(nd.item[0].title) == 333);
    /* an older Worker's 32 KB list: 30 items of 110 and 280 three-byte code points (title and summary) */
    k = snprintf(b, sizeof b, "couch-news 1\nnow=1792022941\n");
    for (int i = 0; i < 30; i++) {
        k += snprintf(b + k, sizeof b - (size_t)k, "item=%08x|Z\xc3\xbcrich Post|politics|%d|", i, 100 + i);
        for (int j = 0; j < 110; j++) k += snprintf(b + k, sizeof b - (size_t)k, "\xe2\x82\xac");
        k += snprintf(b + k, sizeof b - (size_t)k, "|");
        for (int j = 0; j < 280; j++) k += snprintf(b + k, sizeof b - (size_t)k, "\xe2\x82\xac");
        k += snprintf(b + k, sizeof b - (size_t)k, "\n");
    }
    k += snprintf(b + k, sizeof b - (size_t)k, "news_age=0\nnews_state=ok\nend\n");
    CHECK(k <= NEWS_MAX + 4096);
    CHECK(news_parse(b, k, 0, &nd) == BP_OK && nd.n == 30 && strlen(nd.item[29].title) == 330);
    group("/news's rules: version, keys, ids, '-', fields, 30 items, long fields, a list at the Worker's caps", f0);
}

/* ---- /article ----------------------------------------------------------------------------- */

static void article_worker(const char *ok, int okn, const char *sm, int smn, const char *cut, int cutn, const char *lg, int lgn)
{
    int f0 = fails;
    CHECK(article_parse(ok, okn, 0, &ad) == BP_OK && art_bounded());
    STR(ad.source, "Z\xc3\xbcrich Post");
    STR(ad.category, "world");
    STR(ad.title, "Leaders meet in Z\xc3\xbcrich to agree on a new plan for river shipping");
    STR(ad.date, "Fri 16 Oct 17:10");
    CHECK(ad.age == 600);
    STR(ad.link, "https://news.example.org/world/leaders-meet-in-z%C3%BCrich");
    STR(ad.state, "ok");
    CHECK(ad.n_para == 2 && !ad.text_cut);
    STR(ad.text + ad.para_off[0], "First paragraph: caf\xc3\xa9, na\xc3\xafve.");
    STR(ad.text + ad.para_off[1], "Second paragraph with spaces.");
    CHECK(article_parse(ok, okn, 1, &ad) == BP_OK);
    STR(ad.text + ad.para_off[0], "First paragraph: cafe, naive.");
    STR(ad.source, "Zurich Post");
    CHECK(article_parse(sm, smn, 0, &ad) == BP_OK && art_bounded());
    STR(ad.state, "summary");
    STR(ad.reason, "news.example.org: HTTP 403");
    STR(ad.title, "Leaders meet in Z\xc3\xbcrich to agree on a new plan for river shipping");
    CHECK(ad.n_para == 1 && !strncmp(ad.text, "A short summary", 15));
    CHECK(article_parse(cut, cutn, 0, &ad) == BP_OK && art_bounded());
    STR(ad.state, "cut");
    STR(ad.source, "L");
    STR(ad.category, "");
    STR(ad.title, "T whole");
    STR(ad.date, "Wed 14 Oct 17:46");
    CHECK(ad.age == 600);
    STR(ad.link, "https://x.example/a%7Cb");
    CHECK(ad.n_para == 2);
    STR(ad.text + ad.para_off[1], "two");
    CHECK(article_parse(lg, lgn, 0, &ad) == BP_OK && art_bounded());
    STR(ad.source, "");
    CHECK(!ad.date[0] && ad.age == -1 && !ad.link[0] && ad.n_para == 0);
    STR(ad.state, "summary");
    STR(ad.reason, "x: HTTP 403");
    const char *none = "couch-article 1\nnow=1792164000\nstate=none|unknown id\nend\n";
    CHECK(article_parse(none, (int)strlen(none), 0, &ad) == BP_OK && ad.n_para == 0);
    STR(ad.state, "none");
    STR(ad.reason, "unknown id");
    group("the Worker's /article forms: ok, summary, cut, no date and no link, none; folded", f0);
}

static void article_live(const char *w, int wn)
{
    int f0 = fails;
    CHECK(article_parse(w, wn, 0, &ad) == BP_OK && art_bounded());
    CHECK(ad.n_para >= 10 && ad.link[0] && !strncmp(ad.link, "https://", 8));
    STR(ad.state, "ok");
    int whole = 0, bad = 0;
    for (int k = 0; k <= wn; k++) {
        int rc = article_parse(w, k, k & 1, &ad);
        if (!art_bounded()) bad++;
        whole += rc == BP_OK;
    }
    CHECK(bad == 0 && whole == 2);
    group("a live /article of 2026-10-14: its fields, cut at every byte", f0);
}

static void article_rules(void)
{
    int f0 = fails;
    static char b[ARTICLE_MAX * 4];
    CHECK(article_parse("couch-article 3\nend\n", 20, 0, &ad) == BP_VERSION && ad.version == 3);
    CHECK(article_parse("couch-news 1\nend\n", 17, 0, &ad) == BP_NOT_BOARD);
    const char *r = "couch-article 1\nid=0123456Z\nsource=-\ncategory=world\ntitle=  T  \ndate=Wed 14 Oct 9:05|abc\n"
                    "link=https://x.example/\xc3\xa1 b\nstate=weird words\np=\np=  \np=one\nother=x\nend\np=after\n";
    CHECK(article_parse(r, (int)strlen(r), 1, &ad) == BP_OK);
    CHECK(!ad.source[0] && ad.age == -1 && !ad.link[0] && ad.n_para == 1);
    STR(ad.title, "T");
    STR(ad.date, "Wed 14 Oct 9:05");
    STR(ad.state, "none");
    STR(ad.reason, "weird words");
    STR(ad.text, "one");
    const char *u = "couch-article 1\nlink=https://x.example/\xc3\xa1\ntitle=\xc3\xa1\nend\n";
    CHECK(article_parse(u, (int)strlen(u), 1, &ad) == BP_OK);
    STR(ad.link, "https://x.example/\xc3\xa1");                       /* never folded */
    STR(ad.title, "a");
    /* a link of 1024 bytes: none; of 1023: kept */
    int k = snprintf(b, sizeof b, "couch-article 1\nlink=https://x/");
    while (k < 16 + 5 + 1024) b[k++] = 'a';
    k += snprintf(b + k, sizeof b - (size_t)k, "\nend\n");
    CHECK(article_parse(b, k, 0, &ad) == BP_OK && !ad.link[0]);
    k = snprintf(b, sizeof b, "couch-article 1\nlink=https://x/");
    while (k < 16 + 5 + 1023) b[k++] = 'a';
    k += snprintf(b + k, sizeof b - (size_t)k, "\nend\n");
    CHECK(article_parse(b, k, 0, &ad) == BP_OK && strlen(ad.link) == 1023);
    /* 500 short paragraphs: ART_PARAS kept, the text marked cut */
    k = snprintf(b, sizeof b, "couch-article 1\nstate=ok\n");
    for (int i = 0; i < 500; i++) k += snprintf(b + k, sizeof b - (size_t)k, "p=%d\n", i);
    k += snprintf(b + k, sizeof b - (size_t)k, "end\n");
    CHECK(article_parse(b, k, 0, &ad) == BP_OK && ad.n_para == ART_PARAS && ad.text_cut && art_bounded());
    STR(ad.text + ad.para_off[ART_PARAS - 1], "399");
    /* 9 paragraphs of 1500 three-byte code points (4500 bytes each): 2 whole, the third cut, the rest none */
    k = snprintf(b, sizeof b, "couch-article 1\nstate=ok\n");
    for (int i = 0; i < 9; i++) {
        k += snprintf(b + k, sizeof b - (size_t)k, "p=");
        for (int j = 0; j < 1500; j++) k += snprintf(b + k, sizeof b - (size_t)k, "\xe2\x82\xac");
        k += snprintf(b + k, sizeof b - (size_t)k, "\n");
    }
    k += snprintf(b + k, sizeof b - (size_t)k, "end\n");
    CHECK(article_parse(b, k, 0, &ad) == BP_OK && art_bounded() && ad.text_cut && ad.n_para == 3);
    CHECK(strlen(ad.text + ad.para_off[2]) % 3 == 0 && ad.para_off[2] + (int)strlen(ad.text + ad.para_off[2]) < ART_TEXT);
    group("/article's rules: version, '-', the state's words, the link never folded or cut, 400 paragraphs, 12 KB of text", f0);
}

/* ---- the helpers ---------------------------------------------------------------------------- */

/* 10 px a code point (a byte that starts no sequence counts as one) */
static float w10(const char *s, int len, void *ctx)
{
    (void)ctx;
    int n = 0;
    for (int i = 0; i < len; i++) n += ((unsigned char)s[i] & 0xc0) != 0x80;
    return 10.0f * (float)n;
}

static int wrap(const char *s, float maxw, int max, char out[][64])
{
    int off[32], len[32];
    int n = wrap_lines(s, (int)strlen(s), maxw, w10, NULL, off, len, max);
    for (int i = 0; i < n; i++) snprintf(out[i], 64, "%.*s", len[i], s + off[i]);
    return n;
}

static void helpers(void)
{
    int f0 = fails;
    char o[32][64];
    CHECK(wrap("aaa bbb ccc", 70, 8, o) == 2);
    STR(o[0], "aaa bbb");
    STR(o[1], "ccc");
    CHECK(wrap("aaa bbb ccc", 69, 8, o) == 3);
    CHECK(wrap("  aaa   bbb  ", 90, 8, o) == 1);
    STR(o[0], "aaa   bbb");
    CHECK(wrap("  aaa   bbb  ", 80, 8, o) == 2);
    CHECK(wrap("", 100, 8, o) == 0 && wrap("    ", 100, 8, o) == 0);
    CHECK(wrap("abcdefghij", 35, 8, o) == 4);                         /* a long word: cut at the code points that fit */
    STR(o[0], "abc");
    STR(o[3], "j");
    CHECK(wrap("\xc3\xa1\xc3\xa1\xc3\xa1\xc3\xa1", 25, 8, o) == 2);   /* never inside a sequence */
    STR(o[0], "\xc3\xa1\xc3\xa1");
    CHECK(wrap("ab cd", 0, 8, o) == 4);                               /* nothing fits: one code point a line, no stall */
    CHECK(wrap("a b c d e f g h i j", 10, 5, o) == 5);                /* the cap */
    CHECK(wrap("hello\xc3", 100, 8, o) == 1);
    /* the grid: 0 the featured card, then rows of two */
    CHECK(grid_move(0, 30, NAV_DOWN) == 1 && grid_move(1, 30, NAV_UP) == 0 && grid_move(2, 30, NAV_UP) == 0);
    CHECK(grid_move(1, 30, NAV_RIGHT) == 2 && grid_move(2, 30, NAV_RIGHT) == 2 && grid_move(2, 30, NAV_LEFT) == 1 && grid_move(0, 30, NAV_LEFT) == 0);
    CHECK(grid_move(2, 30, NAV_DOWN) == 4 && grid_move(28, 30, NAV_DOWN) == 29 && grid_move(29, 30, NAV_DOWN) == 29);
    CHECK(grid_move(27, 30, NAV_DOWN) == 29 && grid_move(26, 30, NAV_DOWN) == 28);   /* the last row has a left card only */
    CHECK(grid_move(27, 29, NAV_DOWN) == 27);                                          /* 29 items: the last row is whole */
    CHECK(grid_move(1, 2, NAV_RIGHT) == 1 && grid_move(0, 1, NAV_DOWN) == 0 && grid_move(5, 0, NAV_DOWN) == 0);
    CHECK(grid_row(0) == 0 && grid_row(1) == 1 && grid_row(2) == 1 && grid_row(3) == 2 && grid_row(29) == 15);
    /* the scroll */
    CHECK(scroll_clamp(-5, 100, 300) == 0 && scroll_clamp(250, 100, 300) == 200 && scroll_clamp(50, 400, 300) == 0);
    CHECK(scroll_keep(0, 10, 60, 100, 300, 20) == 0);                 /* in view */
    CHECK(scroll_keep(0, 150, 200, 100, 300, 20) == 120);             /* below: its bottom and the margin at the view's end */
    CHECK(scroll_keep(120, 30, 80, 100, 300, 20) == 10);              /* above */
    CHECK(scroll_keep(120, 0, 80, 100, 300, 20) == 0);                /* the top: no margin above */
    CHECK(scroll_keep(0, 250, 300, 100, 300, 20) == 200);             /* the end: no margin past it */
    CHECK(scroll_keep(0, 100, 400, 100, 500, 20) == 80);              /* taller than the view: its top shows */
    double s = 0;
    for (int i = 0; i < 60; i++) s = scroll_follow(s, 100, 1 / 60.0, 0.075);
    CHECK(s == 100);
    s = scroll_follow(0, 100, 0.075, 0.075);
    CHECK(fabs(s - 100 * (1 - exp(-1))) < 1e-9 && scroll_follow(5, 100, 0, 0.075) == 5);
    /* the paths beside the Board's */
    char p[64];
    CHECK(url_sibling("/board", "news", p, sizeof p) == 0 && !strcmp(p, "/news"));
    CHECK(url_sibling("/x/board?y=1/2", "article?id=", p, sizeof p) == 0 && !strcmp(p, "/x/article?id="));
    CHECK(url_sibling("board", "news", p, sizeof p) == 0 && !strcmp(p, "/news"));
    CHECK(url_sibling("/", "news", p, sizeof p) == 0 && !strcmp(p, "/news"));
    CHECK(url_sibling("/board", "news", p, 5) == -1);
    group("wrap_lines, grid_move, the scroll's helpers, url_sibling", f0);
}

/* Mutated replies: random bytes changed, '|', '=' and LF put in, cuts. */
/* ---- the futbol reply (0.5.0) ---------------------------------------------------- */

/* Every text of fd ends inside its buffer, every count and index in its range. */
static int fut_bounded(void)
{
#define IN(f) (memchr((f), 0, sizeof(f)) != NULL)
    int ok = IN(fd.day) && IN(fd.feed.state) && IN(fd.feed.reason) && fd.n_league >= 0 && fd.n_league <= FUT_LEAGUES && fd.n_game >= 0 &&
             fd.n_game <= FUT_GAMES && fd.more_leagues >= 0 && fd.more_games >= 0;
    int sum = 0;
    for (int i = 0; i < fd.n_league; i++) {
        const fut_league *g = &fd.league[i];
        ok = ok && IN(g->name) && IN(g->country) && g->first >= 0 && g->n >= 0 && g->first + g->n <= fd.n_game && g->first == sum;
        sum += g->n;
    }
    ok = ok && sum == fd.n_game;
    for (int i = 0; i < fd.n_game; i++) {
        const fut_game *g = &fd.game[i];
        ok = ok && IN(g->when) && IN(g->home) && IN(g->away) && IN(g->tv) &&
             g->kind >= FK_PRE && g->kind <= FK_OFF && g->sh >= -1 && g->sh <= 99 && g->sa >= -1 && g->sa <= 99;
    }
    return ok;
#undef IN
}

static int fparse(const char *s, int fold) { return futbol_parse(s, (int)strlen(s), fold, &fd); }

static void futbol_worker(const char *w, int wn)
{
    int f0 = fails;
    CHECK(futbol_parse(w, wn, 0, &fd) == BP_OK && fd.version == 1);
    STR(fd.day, "Today, Fri 16 Oct");
    CHECK(fd.n_league == 2 && fd.n_game == 5 && !fd.more_leagues && !fd.more_games && futbol_live(&fd) == 0);
    STR(fd.league[0].name, "Spree League");
    STR(fd.league[0].country, "Germany");
    CHECK(fd.league[0].first == 0 && fd.league[0].n == 3 && fd.league[1].first == 3 && fd.league[1].n == 2);
    const fut_game *g = &fd.game[0];
    CHECK(g->kind == FK_END && g->sh == 2 && g->sa == 1);
    STR(g->when, "Final");
    STR(g->home, "FC Kiezblau");
    STR(g->away, "Union Mitte");
    STR(g->tv, "Spree TV");
    g = &fd.game[1];
    CHECK(g->kind == FK_PRE && g->sh == -1 && g->sa == -1 && hm_parse(g->when) == 18 * 60 + 30);
    STR(g->tv, "Spree TV, Kanal 9");
    CHECK(fd.game[2].tv[0] == 0);                                     /* "-": no channel */
    CHECK(fd.game[3].kind == FK_END && fd.game[3].sh == 1);                    /* the second league's first game */
    STR(fd.game[3].when, "Pens 4-3");
    CHECK(fd.game[4].kind == FK_OFF);
    STR(fd.game[4].home, "Gr\xc3\xbcnau 04");
    STR(fd.feed.state, "ok");
    CHECK(fd.feed.age == 120 && fut_bounded());
    group("the futbol reply of the Worker test, every field", f0);

    f0 = fails;
    CHECK(futbol_parse(w, wn, 1, &fd) == BP_OK);
    STR(fd.game[4].home, "Grunau 04");
    STR(fd.game[4].away, "Muggel SC");
    char *crlf = malloc((size_t)wn * 2 + 1);
    int k = 0;
    for (int i = 0; i < wn; i++) {
        if (w[i] == '\n') crlf[k++] = '\r';
        crlf[k++] = w[i];
    }
    CHECK(futbol_parse(crlf, k, 0, &fd) == BP_OK && fd.n_game == 5 && !strcmp(fd.game[0].tv, "Spree TV"));
    free(crlf);
    int bad = 0;
    for (int n = 0; n < wn; n++) {                   /* every cut: only the one before the last LF is whole */
        int rc = futbol_parse(w, n, 0, &fd);
        if ((n == wn - 1 ? rc != BP_OK : rc == BP_OK) || !fut_bounded()) bad++;
    }
    CHECK(bad == 0);
    char *v2 = strdup(w);
    v2[13] = '2';
    CHECK(futbol_parse(v2, wn, 0, &fd) == BP_VERSION && fd.version == 2);
    free(v2);
    CHECK(fparse("couch-board 1\nend\n", 0) == BP_NOT_BOARD && fparse("", 0) == BP_NOT_BOARD);
    group("the futbol reply folded, with CR LF, cut at every byte, as version 2", f0);

    f0 = fails;
    CHECK(fparse("couch-futbol 1\ngame=1|A|B|1|0|-|end\nleague=L|-\ngame=x|A|B|-|-|-|live\ngame=y|C|D|100|-1|tv|weird\ngame=z|E\nend\n", 0) == BP_OK);
    CHECK(fd.n_league == 1 && fd.n_game == 3 && fd.league[0].country[0] == 0);   /* the game before any league is not kept */
    CHECK(fd.game[0].kind == FK_LIVE && futbol_live(&fd) == 1);
    CHECK(fd.game[1].kind == FK_OFF && fd.game[1].sh == -1 && fd.game[1].sa == -1);   /* out of range, an unknown kind */
    CHECK(fd.game[2].kind == FK_OFF && !strcmp(fd.game[2].away, "-") && fd.game[2].sh == -1);   /* a short line */
    CHECK(fparse("couch-futbol 1\nleague=L|C\nmore=3|27\nfutbol_state=stale|x: HTTP 503\nfutbol_age=90\nend\n", 0) == BP_OK);
    CHECK(fd.more_leagues == 3 && fd.more_games == 27 && fd.feed.age == 90 && fd.league[0].n == 0);
    STR(fd.feed.state, "stale");
    STR(fd.feed.reason, "x: HTTP 503");
    static char big[65536];                          /* past the arrays: 45 leagues of 7 games */
    int bk = snprintf(big, sizeof big, "couch-futbol 1\n");
    for (int i = 0; i < 45; i++) {
        bk += snprintf(big + bk, sizeof big - bk, "league=L%d|C\n", i);
        for (int j = 0; j < 7; j++) bk += snprintf(big + bk, sizeof big - bk, "game=21:00|H%d|A%d|-|-|-|pre\n", j, j);
    }
    snprintf(big + bk, sizeof big - bk, "end\n");
    CHECK(fparse(big, 0) == BP_OK && fd.n_game == FUT_GAMES && fd.n_league == 35 && fd.more_leagues == 10 && fd.more_games == 315 - FUT_GAMES);
    CHECK(fut_bounded());
    bk = snprintf(big, sizeof big, "couch-futbol 1\nleague=");
    memset(big + bk, 'N', 400);
    bk += 400;
    bk += snprintf(big + bk, sizeof big - bk, "|C\ngame=");
    for (int i = 0; i < 60; i++) bk += snprintf(big + bk, sizeof big - bk, "\xc3\xa9");
    bk += snprintf(big + bk, sizeof big - bk, "|");
    for (int i = 0; i < 60; i++) bk += snprintf(big + bk, sizeof big - bk, "\xe2\x80\x93");
    snprintf(big + bk, sizeof big - bk, "|B|1|2|T|end\nend\n");
    CHECK(fparse(big, 0) == BP_OK && fut_bounded() && strlen(fd.league[0].name) == sizeof fd.league[0].name - 1);
    CHECK(strlen(fd.game[0].when) == 22 && strlen(fd.game[0].home) == 87);   /* cut on a code point boundary */
    group("futbol rules: the games before a league, the ranges, an unknown kind, more=, the states, past the arrays, long fields", f0);
}

static void fuzz(const char *w, int wn, int article, const char *name)
{
    int f0 = fails, bad = 0;
    char *s = malloc((size_t)wn + 64);
    unsigned x = 777;
    for (int it = 0; it < 20000; it++) {
        memcpy(s, w, (size_t)wn);
        int n = wn, edits = 1 + it % 8;
        for (int e = 0; e < edits; e++) {
            x = x * 1103515245u + 12345u;
            int pos = (int)((x >> 8) % (unsigned)n);
            static const char put[] = "|=\n\xc3\xb0\x00\xff\r p";
            s[pos] = (x >> 4) & 1 ? put[(x >> 16) % (sizeof put - 1)] : (char)(x >> 20);
        }
        if (it % 3 == 0) n = (int)((x >> 3) % (unsigned)n);
        if (article == 2) {
            futbol_parse(s, n, it & 1, &fd);
            if (!fut_bounded()) bad++;
        } else if (article) {
            article_parse(s, n, it & 1, &ad);
            if (!art_bounded()) bad++;
        } else {
            news_parse(s, n, it & 1, &nd);
            if (!news_bounded()) bad++;
        }
    }
    free(s);
    CHECK(bad == 0);
    char g[160];
    snprintf(g, sizeof g, "20,000 mutated replies of %s: every field ends inside its buffer", name);
    group(g, f0);
}

int main(int argc, char **argv)
{
    if (argc < 9) {
        printf("usage: reader_check <news> <article ok> <summary> <cut> <long> <live news> <live article> <futbol> [fuzz]\n");
        return 1;
    }
    int n[8];
    char *f[8];
    for (int i = 0; i < 8; i++) f[i] = read_file(argv[i + 1], &n[i]);
    news_worker(f[0], n[0]);
    news_live(f[5], n[5]);
    news_rules();
    article_worker(f[1], n[1], f[2], n[2], f[3], n[3], f[4], n[4]);
    article_live(f[6], n[6]);
    article_rules();
    helpers();
    futbol_worker(f[7], n[7]);
    if (argc > 9 && !strcmp(argv[9], "fuzz")) {
        fuzz(f[5], n[5], 0, "the live /news");
        fuzz(f[6], n[6], 1, "a live /article");
        fuzz(f[7], n[7], 2, "the /futbol reply");
    }
    for (int i = 0; i < 8; i++) free(f[i]);
    printf("%d failed\n", fails);
    return fails > 125 ? 125 : fails;
}
