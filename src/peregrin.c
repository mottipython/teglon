/*
 * lazy-peregrin: screensaver for the Lazy Peregrin problem from "Anathem".
 *
 * A fraa on Peregrin must call at a number of maths scattered over the map and come
 * home again, walking as little as he can: Arbre's travelling salesman problem.
 *
 *  1. The map: an island with maths on it, the fraa asleep beside his own, his head
 *     on his sphere.
 *  2. A syntactic device tries the routes one by one, in order, keeping the best it
 *     has seen. The count of routes left makes the point.
 *  3. Saunt Grod's Machine holds every route at once and settles on the shortest.
 *     What is drawn is a crowd of routes being annealed together, each leg as dark
 *     as the share of the crowd using it. The route it ends on is the true shortest,
 *     found exactly (Held-Karp) before the round begins.
 *  4. The fraa wakes, picks up his sphere and toddles round, and the bells ring him
 *     in at each math. Then he goes back to sleep.
 *
 *   lazy-peregrin              full screen on every display; any key or mouse movement quits
 *   lazy-peregrin --window     in a window; Esc or Q quits
 *   LazyPeregrin.scr /s        Windows screensaver conventions
 */
#include <SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define MAXN 18
#define CROWD 160

/* ---- options ---- */
static int opt_window = 0, opt_min = 10, opt_max = 15;
static double opt_speed = 1.0;
static uint64_t opt_seed = 0;
static const char *opt_snapshot = NULL;
static double opt_time = 30;
static int opt_w = 1920, opt_h = 1200;

/* ---- colours ---- */
typedef struct { Uint8 r, g, b; } Rgb;
static const Rgb BG = {16, 22, 28};
static const Rgb SEA = {150, 190, 196}, SEA_DEEP = {104, 152, 168}, FOAM = {226, 240, 236}, WAVE = {72, 120, 140};
static const Rgb LAND = {236, 224, 190}, LAND_LOW = {222, 206, 164}, SAND = {246, 236, 206};
static const Rgb INK = {46, 32, 22}, FAINT = {150, 126, 92}, RED = {186, 52, 34};
static const Rgb BOLT = {228, 180, 96}, BOLT_DARK = {186, 134, 62}, SKIN = {246, 210, 172}, CHEEK = {238, 150, 130};
static const Rgb CHORD = {130, 44, 30}, SPHERE = {110, 182, 234}, WHITE = {255, 252, 240};
static const Rgb LEAF = {96, 142, 84}, LEAF_DARK = {64, 106, 66}, ROOF = {190, 66, 44}, STONE = {214, 204, 186}, ROCK = {168, 150, 124};

/* ---- geometry buffer ---- */
static SDL_Vertex *vb;
static int *ib;
static int nv, ni, vcap, icap;

static void vb_need(int v, int i)
{
    if (nv + v > vcap) { vcap = (nv + v) * 2; vb = realloc(vb, sizeof *vb * vcap); }
    if (ni + i > icap) { icap = (ni + i) * 2; ib = realloc(ib, sizeof *ib * icap); }
}
static inline SDL_Color col(Rgb c, double a)
{
    SDL_Color o = {c.r, c.g, c.b, (Uint8)(a < 0 ? 0 : a > 1 ? 255 : a * 255)};
    return o;
}
static inline void vert(double x, double y, SDL_Color c)
{
    vb[nv].position.x = (float)x; vb[nv].position.y = (float)y;
    vb[nv].color = c; vb[nv].tex_coord.x = vb[nv].tex_coord.y = 0;
    nv++;
}
static void quad4(double x0, double y0, double x1, double y1, double x2, double y2, double x3, double y3,
                  SDL_Color c0, SDL_Color c1, SDL_Color c2, SDL_Color c3)
{
    vb_need(4, 6);
    int b = nv;
    vert(x0, y0, c0); vert(x1, y1, c1); vert(x2, y2, c2); vert(x3, y3, c3);
    ib[ni++] = b; ib[ni++] = b + 1; ib[ni++] = b + 2; ib[ni++] = b; ib[ni++] = b + 2; ib[ni++] = b + 3;
}
static void quad(double x0, double y0, double x1, double y1, double x2, double y2, double x3, double y3, SDL_Color c)
{
    quad4(x0, y0, x1, y1, x2, y2, x3, y3, c, c, c, c);
}
static void rect(double x, double y, double w, double h, SDL_Color c) { quad(x, y, x + w, y, x + w, y + h, x, y + h, c); }
static void disc(double cx, double cy, double r, SDL_Color centre, SDL_Color rim)
{
    int n = r < 4 ? 10 : r < 30 ? 28 : 56;
    vb_need(n + 1, 3 * n);
    int b = nv;
    vert(cx, cy, centre);
    for (int i = 0; i < n; i++) vert(cx + r * cos(i * 2 * M_PI / n), cy + r * sin(i * 2 * M_PI / n), rim);
    for (int i = 0; i < n; i++) { ib[ni++] = b; ib[ni++] = b + 1 + i; ib[ni++] = b + 1 + (i + 1) % n; }
}
/* a stroke with round ends */
static void line(double x0, double y0, double x1, double y1, double w, SDL_Color c)
{
    double dx = x1 - x0, dy = y1 - y0, l = hypot(dx, dy);
    if (l > 1e-9) {
        double nx = -dy / l * w / 2, ny = dx / l * w / 2;
        quad(x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, c);
    }
    if (w > 2.5) { disc(x0, y0, w / 2, c, c); disc(x1, y1, w / 2, c, c); }
}
static void ring(double cx, double cy, double r, double w, SDL_Color c)
{
    int n = r < 30 ? 32 : 64;
    vb_need(2 * n, 6 * n);
    int b = nv;
    for (int i = 0; i < n; i++) {
        double a = i * 2 * M_PI / n;
        vert(cx + (r + w / 2) * cos(a), cy + (r + w / 2) * sin(a), c);
        vert(cx + (r - w / 2) * cos(a), cy + (r - w / 2) * sin(a), c);
    }
    for (int i = 0; i < n; i++) {
        int a = b + 2 * i, d = b + 2 * ((i + 1) % n);
        ib[ni++] = a; ib[ni++] = a + 1; ib[ni++] = d; ib[ni++] = a + 1; ib[ni++] = d + 1; ib[ni++] = d;
    }
}
static void flush(SDL_Renderer *r)
{
    if (ni) SDL_RenderGeometry(r, NULL, vb, nv, ib, ni);
    nv = ni = 0;
}

/* ---- lettering: pen strokes on a 4 x 6 grid, drawn with a round nib ---- */
static const char *GLYPH[] = {
    "A06 02 20 42 46|04 44", "B00 06 36 45 44 33 03|33 42 41 30 00", "C41 30 10 01 05 16 36 45",
    "D00 06 26 44 42 20 00", "E40 00 06 46|03 33", "F40 00 06|03 33", "G41 30 10 01 05 16 36 45 43 23",
    "H00 06|40 46|03 43", "I20 26|10 30|16 36", "J40 45 36 16 05", "K00 06|40 03 46", "L00 06 46",
    "M06 00 23 40 46", "N06 00 46 40", "O10 30 41 45 36 16 05 01 10", "P06 00 30 41 42 33 03",
    "Q10 30 41 45 36 16 05 01 10|24 46", "R06 00 30 41 42 33 03|23 46", "S41 30 10 01 02 13 33 44 45 36 16 05",
    "T00 40|20 26", "U00 05 16 36 45 40", "V00 26 40", "W00 16 23 36 40", "X00 46|40 06", "Y00 23 40|23 26",
    "Z00 40 06 46", "010 30 41 45 36 16 05 01 10", "111 20 26|16 36", "201 10 30 41 42 06 46",
    "301 10 30 41 42 33 23|33 44 45 36 16 05", "436 30 04 44", "540 00 03 33 44 45 36 16 05",
    "630 10 01 05 16 36 45 44 33 03", "700 40 16", "813 02 01 10 30 41 42 33 13 04 05 16 36 45 44 33",
    "943 13 02 01 10 30 41 45 36 16", ",25 26 17", ".26 26", ":22 22|25 25", "'20 21", "?01 10 30 41 42 24|26 26",
    "-13 33", "!20 24|26 26",
};
static const char *glyph(char ch)
{
    for (unsigned i = 0; i < sizeof GLYPH / sizeof *GLYPH; i++)
        if (GLYPH[i][0] == ch) return GLYPH[i] + 1;
    return NULL;
}
static double text_width(const char *str, double s) { return strlen(str) * 6.2 * s - 2.2 * s; }
/* text centred on cx, top at y; s is one grid step, so letters are 6s tall */
static void text(const char *str, double cx, double y, double s, double weight, SDL_Color c)
{
    double x = cx - text_width(str, s) / 2;
    for (; *str; str++, x += 6.2 * s) {
        const char *g = glyph(*str);
        if (!g) continue;
        int have = 0; double lx = 0, ly = 0;
        for (; *g; g++) {
            if (*g == '|') { have = 0; continue; }
            if (*g == ' ') continue;
            double gx = x + (g[0] - '0') * s, gy = y + (g[1] - '0') * s;
            g++;
            if (have) line(lx, ly, gx, gy, s * weight, c);
            disc(gx, gy, s * weight * (have && lx == gx && ly == gy ? 0.62 : 0.5), c, c); /* the nib is round */
            lx = gx; ly = gy; have = 1;
        }
    }
}
static void commas(char *out, uint64_t v)
{
    char tmp[32];
    int n = snprintf(tmp, sizeof tmp, "%llu", (unsigned long long)v), o = 0;
    for (int i = 0; i < n; i++) { out[o++] = tmp[i]; if ((n - 1 - i) % 3 == 0 && i < n - 1) out[o++] = ','; }
    out[o] = 0;
}

/* ---- the problem ---- */
static int N;
static double px[MAXN], py[MAXN], D[MAXN][MAXN];
static int best[MAXN];          /* the shortest route, home first */
static double best_len;
static uint64_t routes_total;   /* (N-1)!/2 */
static uint64_t seed_state;

/* the island: a wobbly outline about the middle of the map */
#define NCOAST 180
static double coast_amp[5], coast_ph[5];
#define NTREE 46
#define NHILL 9
#define NWAVE 70
static double tree_x[NTREE], tree_y[NTREE], hill_x[NHILL], hill_y[NHILL], wave_x[NWAVE], wave_y[NWAVE];

static uint64_t next_rand(void)
{
    seed_state ^= seed_state << 13; seed_state ^= seed_state >> 7; seed_state ^= seed_state << 17;
    return seed_state;
}
static double frand(void) { return (next_rand() >> 11) * (1.0 / 9007199254740992.0); }

static double coast_r(double th)
{
    double r = 1;
    for (int k = 0; k < 5; k++) r += coast_amp[k] * cos((k + 2) * th + coast_ph[k]);
    return r;
}
/* how far out from the middle of the island a map point is: under 1 is dry land */
static double shore(double x, double y)
{
    double ex = (x - 0.5) / 0.46, ey = (y - 0.5) / 0.43;
    return hypot(ex, ey) / coast_r(atan2(ey, ex));
}

static double tour_len(const int *t)
{
    double l = 0;
    for (int i = 0; i < N; i++) l += D[t[i]][t[(i + 1) % N]];
    return l;
}

/* exact shortest closed route through all maths (Held-Karp) */
static void solve_exact(void)
{
    int n = N - 1; /* maths other than home, numbered 1..N-1 */
    size_t states = (size_t)1 << n;
    float *dp = malloc(sizeof(float) * states * n);
    signed char *from = malloc(states * n);
    for (size_t m = 1; m < states; m++)
        for (int j = 0; j < n; j++) {
            if (!(m >> j & 1)) continue;
            size_t pm = m ^ ((size_t)1 << j);
            float b = 1e30f; int bf = -1;
            if (!pm) b = (float)D[0][j + 1];
            else
                for (int k = 0; k < n; k++)
                    if (pm >> k & 1) {
                        float v = dp[pm * n + k] + (float)D[k + 1][j + 1];
                        if (v < b) { b = v; bf = k; }
                    }
            dp[m * n + j] = b; from[m * n + j] = (signed char)bf;
        }
    size_t m = states - 1;
    float b = 1e30f; int j = 0;
    for (int k = 0; k < n; k++) {
        float v = dp[m * n + k] + (float)D[k + 1][0];
        if (v < b) { b = v; j = k; }
    }
    best[0] = 0;
    for (int i = N - 1; i >= 1; i--) {
        best[i] = j + 1;
        int pj = from[m * n + j];
        m ^= (size_t)1 << j; j = pj;
    }
    free(dp); free(from);
    best_len = tour_len(best);
}

/* ---- the show ---- */
enum { ST_MAP, ST_DEVICE, ST_GROD, ST_SETTLE, ST_WALK, ST_REST };
static int state;
static double st_time, anim_t;
/* the syntactic device */
static int perm[MAXN], dev_best[MAXN], dev_done;
static double dev_best_len, dev_acc;
static uint64_t dev_tried;
/* Saunt Grod's Machine */
static int crowd[CROWD][MAXN];
static double freq[MAXN][MAXN], temp;

static int next_perm(int *a, int n)
{
    int i = n - 2;
    while (i >= 0 && a[i] > a[i + 1]) i--;
    if (i < 0) return 0;
    int j = n - 1;
    while (a[j] < a[i]) j--;
    int t = a[i]; a[i] = a[j]; a[j] = t;
    for (int l = i + 1, r = n - 1; l < r; l++, r--) { t = a[l]; a[l] = a[r]; a[r] = t; }
    return 1;
}

static int clear_of_maths(double x, double y, double gap)
{
    for (int k = 0; k < N; k++) if (hypot((x - px[k]) * 1.6, y - py[k]) < gap) return 0;
    return 1;
}

static void new_round(void)
{
    N = opt_min + (int)(next_rand() % (uint64_t)(opt_max - opt_min + 1));
    for (int k = 0; k < 5; k++) { coast_amp[k] = (0.02 + 0.05 * frand()) / (1 + 0.25 * k); coast_ph[k] = frand() * 2 * M_PI; }
    for (int i = 0; i < N; i++) {
        for (int tries = 0; tries < 4000; tries++) {
            px[i] = 0.06 + 0.88 * frand(); py[i] = 0.08 + 0.84 * frand();
            int ok = shore(px[i], py[i]) < 0.8;
            for (int k = 0; k < i && ok; k++) if (hypot((px[i] - px[k]) * 1.6, py[i] - py[k]) < 0.15) ok = 0;
            /* room beside the first math for a fraa to lie down */
            if (i == 0 && shore(px[i] + 0.09, py[i] + 0.03) > 0.8) ok = 0;
            if (i > 0 && fabs(py[i] - py[0] - 0.02) < 0.07 && px[i] > px[0] && px[i] - px[0] < 0.14) ok = 0;
            if (ok) break;
        }
    }
    for (int i = 0; i < N; i++)
        for (int k = 0; k < N; k++) D[i][k] = hypot((px[i] - px[k]) * 1.6, py[i] - py[k]);
    /* scenery */
    for (int i = 0; i < NHILL; i++)
        for (int tries = 0; tries < 300; tries++) {
            hill_x[i] = frand(); hill_y[i] = frand();
            if (shore(hill_x[i], hill_y[i]) < 0.78 && clear_of_maths(hill_x[i], hill_y[i], 0.13)) break;
        }
    for (int i = 0; i < NTREE; i++)
        for (int tries = 0; tries < 300; tries++) {
            /* trees gather in woods */
            if (i % 6 == 0 || tries > 40) { tree_x[i] = frand(); tree_y[i] = frand(); }
            else { tree_x[i] = tree_x[i - i % 6] + (frand() - 0.5) * 0.07; tree_y[i] = tree_y[i - i % 6] + (frand() - 0.5) * 0.09; }
            if (shore(tree_x[i], tree_y[i]) < 0.86 && clear_of_maths(tree_x[i], tree_y[i], 0.085)) break;
        }
    for (int i = 0; i < NWAVE; i++)
        for (int tries = 0; tries < 300; tries++) {
            wave_x[i] = frand(); wave_y[i] = frand();
            if (shore(wave_x[i], wave_y[i]) > 1.12) break;
        }
    solve_exact();
    routes_total = 1;
    for (int i = 2; i < N; i++) routes_total *= (uint64_t)i;
    routes_total /= 2;
    for (int i = 0; i < N; i++) perm[i] = dev_best[i] = i;
    dev_best_len = tour_len(perm); dev_tried = 1; dev_done = 0; dev_acc = 0;
    for (int c = 0; c < CROWD; c++) {
        for (int i = 0; i < N; i++) crowd[c][i] = i;
        for (int i = N - 1; i > 1; i--) { int k = 1 + (int)(next_rand() % (uint64_t)i); int t = crowd[c][i]; crowd[c][i] = crowd[c][k]; crowd[c][k] = t; }
    }
    state = ST_MAP; st_time = 0;
}

static void crowd_freq(void)
{
    memset(freq, 0, sizeof freq);
    for (int c = 0; c < CROWD; c++)
        for (int i = 0; i < N; i++) {
            int a = crowd[c][i], b = crowd[c][(i + 1) % N];
            freq[a][b] += 1.0 / CROWD; freq[b][a] += 1.0 / CROWD;
        }
}

#define T_MAP 6.0
#define T_DEVICE 17.0
#define T_GROD 13.0
#define T_SETTLE 4.0
#define T_REST 9.0
#define PAUSE 0.7
static double walk_time(void) { return 11 + N * 0.5 + N * PAUSE; }
/* where the fraa is t seconds into his walk: distance covered along the route, which
 * stop he is making (-1 if walking) and how far into it */
static double walk_pos(double t, int *stopped, double *into)
{
    double travel = 11 + N * 0.5, gone = 0;
    *stopped = -1; *into = 0;
    for (int i = 0; i < N; i++) {
        double d = D[best[i]][best[(i + 1) % N]], leg = travel * d / best_len;
        if (t < leg) return gone + d * t / leg;
        t -= leg; gone += d;
        if (t < PAUSE) { *stopped = (i + 1) % N; *into = t / PAUSE; return gone; }
        t -= PAUSE;
    }
    return best_len;
}

static void update(double dt)
{
    anim_t += dt;
    dt *= opt_speed;
    st_time += dt;
    switch (state) {
    case ST_MAP:
        if (st_time > T_MAP) { state = ST_DEVICE; st_time = 0; }
        break;
    case ST_DEVICE: {
        /* slow enough to follow at first, then as fast as it will go */
        double rate = 2.5 * exp(st_time * 1.15);
        if (rate > 2.5e6) rate = 2.5e6;
        dev_acc += rate * dt;
        while (dev_acc >= 1 && !dev_done) {
            dev_acc -= 1;
            /* each route once: skip the same route walked backwards */
            do { if (!next_perm(perm + 1, N - 1)) { dev_done = 1; break; } } while (perm[1] > perm[N - 1]);
            if (dev_done) break;
            dev_tried++;
            double l = tour_len(perm);
            if (l < dev_best_len - 1e-12) { dev_best_len = l; memcpy(dev_best, perm, sizeof(int) * N); }
        }
        if (st_time > T_DEVICE || (dev_done && st_time > 6)) { state = ST_GROD; st_time = 0; crowd_freq(); }
        break;
    }
    case ST_GROD: {
        /* every route in the crowd is jostled at once, less and less */
        double u = st_time / T_GROD;
        temp = 0.5 * pow(0.004, u);
        int moves = (int)(N * N * 6 * dt * 60) + 1;
        for (int c = 0; c < CROWD; c++) {
            int *t = crowd[c];
            for (int q = 0; q < moves; q++) {
                int i = (int)(next_rand() % (uint64_t)N), j = (int)(next_rand() % (uint64_t)N);
                if (i > j) { int s = i; i = j; j = s; }
                if (j - i < 1 || (i == 0 && j == N - 1)) continue;
                int a = t[i], b = t[i + 1], c2 = t[j], d = t[(j + 1) % N];
                double delta = D[a][c2] + D[b][d] - D[a][b] - D[c2][d];
                if (delta < 0 || frand() < exp(-delta / temp))
                    for (int l = i + 1, r = j; l < r; l++, r--) { int s = t[l]; t[l] = t[r]; t[r] = s; }
            }
        }
        crowd_freq();
        if (st_time > T_GROD) { state = ST_SETTLE; st_time = 0; }
        break;
    }
    case ST_SETTLE:
        if (st_time > T_SETTLE) { state = ST_WALK; st_time = 0; }
        break;
    case ST_WALK:
        if (st_time > walk_time() + 1.0) { state = ST_REST; st_time = 0; }
        break;
    case ST_REST:
        if (st_time > T_REST) new_round();
        break;
    }
}

/* ---- drawing ---- */
static double mx0, my0, mw, mh, unit; /* the map on screen; unit = a convenient small length */

static inline void mxy(double x, double y, double *ox, double *oy) { *ox = mx0 + x * mw; *oy = my0 + y * mh; }
static inline void mp(int i, double *x, double *y) { mxy(px[i], py[i], x, y); }

/* Things drawn with an inked edge are drawn twice: once fattened in ink, once in
 * colour. edge > 0 means this is the ink pass. */
static double edge;
static inline SDL_Color ec(Rgb c) { return col(edge > 0 ? INK : c, 1); }
static void odisc(double x, double y, double r, Rgb c) { disc(x, y, r + edge, ec(c), ec(c)); }
static void oline(double x0, double y0, double x1, double y1, double w, Rgb c) { line(x0, y0, x1, y1, w + 2 * edge, ec(c)); }
static void oquad(double x0, double y0, double x1, double y1, double x2, double y2, double x3, double y3, Rgb c)
{
    if (edge > 0) {
        line(x0, y0, x1, y1, 2 * edge, ec(c)); line(x1, y1, x2, y2, 2 * edge, ec(c));
        line(x2, y2, x3, y3, 2 * edge, ec(c)); line(x3, y3, x0, y0, 2 * edge, ec(c));
    }
    quad(x0, y0, x1, y1, x2, y2, x3, y3, ec(c));
}

static void island_outline(double grow, double *x, double *y)
{
    for (int i = 0; i < NCOAST; i++) {
        double th = i * 2 * M_PI / NCOAST, r = coast_r(th) * grow;
        mxy(0.5 + 0.46 * r * cos(th), 0.5 + 0.43 * r * sin(th), &x[i], &y[i]);
    }
}
static void island_fill(double grow, double oy, SDL_Color centre, SDL_Color rim)
{
    double x[NCOAST], y[NCOAST], cx, cy;
    island_outline(grow, x, y);
    mxy(0.5, 0.5, &cx, &cy);
    vb_need(NCOAST + 1, 3 * NCOAST);
    int b = nv;
    vert(cx, cy + oy, centre);
    for (int i = 0; i < NCOAST; i++) vert(x[i], y[i] + oy, rim);
    for (int i = 0; i < NCOAST; i++) { ib[ni++] = b; ib[ni++] = b + 1 + i; ib[ni++] = b + 1 + (i + 1) % NCOAST; }
}

static void draw_map(int w, int h)
{
    double m = (w < h ? w : h) * 0.03;
    /* the sheet: sea, deeper toward the edges */
    rect(m * 0.5, m * 0.5, w - m, h - m, col(INK, 1));
    double x0 = m * 0.75, y0 = m * 0.75, x1 = w - m * 0.75, y1 = h - m * 0.75, cx = w / 2.0, cy = h / 2.0;
    SDL_Color deep = col(SEA_DEEP, 1), mid = col(SEA, 1);
    quad4(x0, y0, x1, y0, cx, cy, cx, cy, deep, deep, mid, mid); quad4(x1, y0, x1, y1, cx, cy, cx, cy, deep, deep, mid, mid);
    quad4(x1, y1, x0, y1, cx, cy, cx, cy, deep, deep, mid, mid); quad4(x0, y1, x0, y0, cx, cy, cx, cy, deep, deep, mid, mid);
    /* little waves */
    for (int i = 0; i < NWAVE; i++) {
        double x, y, u = unit, ph = sin(anim_t * 0.8 + i) * u * 0.25;
        mxy(wave_x[i], wave_y[i], &x, &y);
        x += ph;
        SDL_Color c = col(WAVE, 0.5);
        line(x - u * 1.0, y, x - u * 0.5, y - u * 0.3, u * 0.14, c); line(x - u * 0.5, y - u * 0.3, x, y, u * 0.14, c);
        line(x, y, x + u * 0.5, y - u * 0.3, u * 0.14, c); line(x + u * 0.5, y - u * 0.3, x + u * 1.0, y, u * 0.14, c);
    }
    /* the island: shallows, beach, an inked coast, and the land itself */
    island_fill(1.085, 0, col(FOAM, 0.55), col(FOAM, 0.0));
    island_fill(1.035, 0, col(FOAM, 0.75), col(FOAM, 0.75));
    island_fill(1.0, unit * 0.5, col(INK, 0.35), col(INK, 0.35));
    island_fill(1.0, 0, col(INK, 1), col(INK, 1));
    island_fill(0.985, 0, col(SAND, 1), col(SAND, 1));
    island_fill(0.95, 0, col(LAND, 1), col(LAND_LOW, 1));
}

static void draw_scenery(void)
{
    double u = unit;
    for (int i = 0; i < NHILL; i++) { /* a couple of peaks, lit from the left */
        double x, y;
        mxy(hill_x[i], hill_y[i], &x, &y);
        for (edge = u * 0.14; edge >= 0; edge -= u * 0.14) {
            oquad(x - u * 2.2, y + u * 1.1, x + u * 1.6, y + u * 1.1, x - u * 0.2, y - u * 1.6, x - u * 0.2, y - u * 1.6, STONE);
            oquad(x + u * 0.2, y + u * 1.1, x + u * 3.0, y + u * 1.1, x + u * 1.5, y - u * 0.7, x + u * 1.5, y - u * 0.7, STONE);
            if (edge <= 0) break;
        }
        edge = 0;
        quad(x - u * 0.2, y - u * 1.6, x + u * 1.6, y + u * 1.1, x + u * 0.3, y + u * 1.1, x - u * 0.2, y - u * 0.2, col(ROCK, 0.85));
        quad(x + u * 1.5, y - u * 0.7, x + u * 3.0, y + u * 1.1, x + u * 2.0, y + u * 1.1, x + u * 1.5, y + u * 0.1, col(ROCK, 0.85));
        quad(x - u * 0.2, y - u * 1.6, x + u * 0.25, y - u * 0.9, x - u * 0.2, y - u * 0.75, x - u * 0.65, y - u * 0.9, col(WHITE, 0.95));
    }
    for (int i = 0; i < NTREE; i++) {
        double x, y;
        mxy(tree_x[i], tree_y[i], &x, &y);
        disc(x + u * 0.1, y + u * 0.75, u * 0.6, col(INK, 0.16), col(INK, 0));
        for (edge = u * 0.12; edge >= 0; edge -= u * 0.12) {
            oline(x, y + u * 0.7, x, y, u * 0.2, CHORD);
            odisc(x, y - u * 0.35, u * 0.72, LEAF);
            if (edge <= 0) break;
        }
        edge = 0;
        disc(x + u * 0.22, y - u * 0.2, u * 0.42, col(LEAF_DARK, 0.75), col(LEAF_DARK, 0.75));
        disc(x - u * 0.2, y - u * 0.55, u * 0.22, col(WHITE, 0.3), col(WHITE, 0));
    }
}

/* a compass rose out at sea */
static void draw_compass(void)
{
    double cx = mx0 + mw - unit * 6.5, cy = my0 + unit * 7, r = unit * 3.6;
    ring(cx, cy, r * 0.7, unit * 0.16, col(INK, 0.75));
    ring(cx, cy, r * 0.55, unit * 0.08, col(INK, 0.55));
    for (int k = 0; k < 8; k++) {
        double a = k * M_PI / 4, ca = cos(a), sa = sin(a), len = k % 2 ? r * 0.6 : r, w2 = r * 0.15;
        SDL_Color c0 = col(k == 6 ? RED : INK, 0.9), c1 = col(WHITE, 0.9);
        quad(cx + ca * len, cy + sa * len, cx - sa * w2, cy + ca * w2, cx, cy, cx, cy, c0);
        quad(cx + ca * len, cy + sa * len, cx + sa * w2, cy - ca * w2, cx, cy, cx, cy, k % 2 ? c0 : c1);
    }
    text("N", cx, cy - r - unit * 2.6, unit * 0.3, 0.7, col(INK, 0.9));
}

static void draw_route(const int *t, double w, SDL_Color c)
{
    for (int i = 0; i < N; i++) {
        double x0, y0, x1, y1;
        mp(t[i], &x0, &y0); mp(t[(i + 1) % N], &x1, &y1);
        line(x0, y0, x1, y1, w, c);
    }
}

/* a math: a round wall with a gate, a clock tower with a red roof, and a pennant
 * once the fraa has called */
static void draw_math(int i, double pop, int visited, double ringing)
{
    double x, y, r = unit * (i == 0 ? 2.5 : 2.0) * pop, u = unit * pop;
    mp(i, &x, &y);
    if (ringing > 0) { /* the bells ring him in */
        ring(x, y, r * (1.2 + 2.0 * ringing), u * 0.26, col(RED, 0.85 * (1 - ringing)));
        ring(x, y, r * (1.0 + 1.2 * ringing), u * 0.2, col(RED, 0.6 * (1 - ringing)));
    }
    disc(x + u * 0.2, y + r * 0.35, r * 1.1, col(INK, 0.25), col(INK, 0));
    if (i == 0) ring(x, y, r * 1.32, u * 0.2, col(RED, 1));
    disc(x, y, r + u * 0.18, col(INK, 1), col(INK, 1));
    disc(x, y, r, col(STONE, 1), col(STONE, 1));
    disc(x, y, r * 0.74, col(INK, 1), col(INK, 1));
    disc(x, y, r * 0.74 - u * 0.16, col(LEAF, 0.55), col(SAND, 1)); /* the close */
    rect(x - r * 0.16, y + r * 0.62, r * 0.32, r * 0.5, col(INK, 1));   /* the gate */
    double tw = r * 0.46, th = r * 1.45, ty = y + r * 0.3;
    for (edge = u * 0.16; edge >= 0; edge -= u * 0.16) {
        oquad(x - tw / 2, ty - th, x + tw / 2, ty - th, x + tw / 2, ty, x - tw / 2, ty, WHITE);
        oquad(x - tw * 0.85, ty - th, x + tw * 0.85, ty - th, x, ty - th - r * 0.7, x, ty - th - r * 0.7, ROOF);
        if (edge <= 0) break;
    }
    edge = 0;
    rect(x, ty - th, tw / 2, th, col(INK, 0.12));
    disc(x, ty - th * 0.7, tw * 0.33, col(INK, 1), col(INK, 1)); /* the clock */
    disc(x, ty - th * 0.7, tw * 0.22, col(WHITE, 1), col(WHITE, 1));
    line(x, ty - th * 0.7, x, ty - th * 0.7 - tw * 0.16, u * 0.08, col(INK, 1));
    line(x, ty - th * 0.7, x + tw * 0.11, ty - th * 0.7, u * 0.08, col(INK, 1));
    if (visited) {
        double fx = x, fy = ty - th - r * 0.68;
        line(fx, fy, fx, fy - r * 0.8, u * 0.14, col(INK, 1));
        double wv = sin(anim_t * 5 + i) * r * 0.06;
        quad(fx, fy - r * 0.8, fx + r * 0.7, fy - r * 0.62 + wv, fx, fy - r * 0.42, fx, fy - r * 0.42, col(INK, 1));
        quad(fx + u * 0.1, fy - r * 0.74, fx + r * 0.56, fy - r * 0.62 + wv, fx + u * 0.1, fy - r * 0.49, fx + u * 0.1, fy - r * 0.49, col(RED, 1));
    }
}

/* his sphere: a pale blue ball with a glint on it */
static void draw_sphere(double x, double y, double r)
{
    odisc(x, y, r, SPHERE);
    if (edge > 0) return;
    disc(x - r * 0.15, y - r * 0.15, r * 0.8, col(WHITE, 0.55), col(SPHERE, 0));
    disc(x - r * 0.35, y - r * 0.38, r * 0.2, col(WHITE, 1), col(WHITE, 0.7));
}
static void draw_face(double hx, double hy, double s, int dir, int shut)
{
    odisc(hx - dir * s * 0.09, hy - s * 0.04, s * 0.47, BOLT_DARK); /* hood */
    if (edge > 0) return;
    disc(hx, hy, s * 0.36, col(SKIN, 1), col(SKIN, 1));
    disc(hx + dir * s * 0.21, hy + s * 0.1, s * 0.09, col(CHEEK, 0.9), col(CHEEK, 0.5));
    if (shut || fmod(anim_t, 3.4) < 0.13) {
        line(hx - dir * s * 0.02, hy - s * 0.03, hx + dir * s * 0.09, hy - s * 0.005, s * 0.05, col(INK, 1));
        line(hx + dir * s * 0.18, hy - s * 0.03, hx + dir * s * 0.29, hy - s * 0.005, s * 0.05, col(INK, 1));
    } else {
        disc(hx + dir * s * 0.04, hy - s * 0.04, s * 0.065, col(INK, 1), col(INK, 1));
        disc(hx + dir * s * 0.23, hy - s * 0.04, s * 0.065, col(INK, 1), col(INK, 1));
        disc(hx + dir * s * 0.055, hy - s * 0.06, s * 0.02, col(WHITE, 1), col(WHITE, 1));
        disc(hx + dir * s * 0.245, hy - s * 0.06, s * 0.02, col(WHITE, 1), col(WHITE, 1));
    }
    line(hx + dir * s * 0.07, hy + s * 0.13, hx + dir * s * 0.14, hy + s * 0.17, s * 0.045, col(INK, 1)); /* a small smile */
    line(hx + dir * s * 0.14, hy + s * 0.17, hx + dir * s * 0.21, hy + s * 0.13, s * 0.045, col(INK, 1));
}

/* The fraa, with the three things an avout owns: his bolt (the robe), his chord (the
 * rope round his middle) and his sphere. Asleep he lies with his head on the sphere;
 * awake he carries it in his arms and toddles. dir is +1 facing right, -1 left. */
static void draw_fraa(double x, double y, double s, int dir, double step, int asleep)
{
    double o = s * 0.055;
    if (asleep) {
        double br = 1 + 0.05 * sin(anim_t * 1.7);
        disc(x + s * 0.2, y + s * 0.02, s * 1.2, col(INK, 0.2), col(INK, 0)); /* shadow */
        for (edge = o; edge >= 0; edge -= o) {
            draw_sphere(x - s * 0.78, y - s * 0.4, s * 0.42); /* his pillow */
            oline(x - s * 0.3, y - s * 0.34, x + s * 0.95, y - s * 0.3, s * 0.64 * br, BOLT); /* the bolt, lying down */
            odisc(x + s * 1.27, y - s * 0.38, s * 0.13, INK); /* feet */
            odisc(x + s * 1.3, y - s * 0.19, s * 0.13, INK);
            draw_face(x - s * 0.55, y - s * 0.78, s, 1, 1);
            if (edge <= 0) break;
        }
        edge = 0;
        line(x - s * 0.12, y - s * 0.6, x + s * 0.16, y - s * 0.06, s * 0.05, col(BOLT_DARK, 0.9)); /* a fold */
        /* the chord round his middle, knotted, ends trailing */
        line(x + s * 0.42, y - s * 0.64 * br, x + s * 0.42, y + s * 0.0, s * 0.12, col(CHORD, 1));
        disc(x + s * 0.42, y - s * 0.62 * br, s * 0.1, col(CHORD, 1), col(CHORD, 1));
        line(x + s * 0.42, y - s * 0.62 * br, x + s * 0.62, y - s * 0.84, s * 0.07, col(CHORD, 1));
        line(x + s * 0.42, y - s * 0.62 * br, x + s * 0.72, y - s * 0.68, s * 0.07, col(CHORD, 1));
        for (int k = 0; k < 3; k++) { /* zzz */
            double u = fmod(anim_t * 0.4 + k / 3.0, 1.0), zs = s * (0.04 + 0.06 * u);
            text("Z", x - s * 0.15 + s * 0.9 * u, y - s * (1.5 + 1.5 * u), zs, 1.0, col(INK, 0.95 * sin(u * M_PI)));
        }
        return;
    }
    double bob = fabs(sin(step)) * s * 0.13, sway = sin(step) * s * 0.07; /* the toddle */
    double by = y - bob, bx = x + sway, f = sin(step) * s * 0.26;
    double sx = bx + dir * s * 0.5, sy = by - s * 0.66;
    disc(x, y + s * 0.06, s * 0.66, col(INK, 0.22), col(INK, 0)); /* shadow */
    for (edge = o; edge >= 0; edge -= o) {
        odisc(x + f, y, s * 0.15, INK); /* feet */
        odisc(x - f, y - s * 0.02, s * 0.15, INK);
        oline(bx - dir * s * 0.1, by - s * 0.86, sx + dir * s * 0.05, sy - s * 0.3, s * 0.16, BOLT_DARK); /* far arm */
        oquad(bx - s * 0.52, by - s * 0.12, bx + s * 0.52, by - s * 0.12, bx + s * 0.3, by - s * 1.0, bx - s * 0.3, by - s * 1.0, BOLT); /* the bolt */
        odisc(bx, by - s * 0.22, s * 0.52, BOLT);
        odisc(bx, by - s * 0.95, s * 0.32, BOLT);
        if (edge <= 0) break;
    }
    edge = 0;
    quad(bx - s * 0.5, by - s * 0.2, bx + s * 0.5, by - s * 0.2, bx + s * 0.32, by - s * 0.95, bx - s * 0.32, by - s * 0.95, col(BOLT, 1));
    line(bx - dir * s * 0.28, by - s * 1.0, bx + dir * s * 0.3, by - s * 0.05, s * 0.05, col(BOLT_DARK, 0.9)); /* a fold */
    /* the chord, knotted, ends hanging */
    line(bx - s * 0.46, by - s * 0.5, bx + s * 0.46, by - s * 0.5, s * 0.12, col(CHORD, 1));
    disc(bx - dir * s * 0.18, by - s * 0.5, s * 0.11, col(CHORD, 1), col(CHORD, 1));
    line(bx - dir * s * 0.18, by - s * 0.5, bx - dir * s * 0.27, by - s * 0.12, s * 0.07, col(CHORD, 1));
    line(bx - dir * s * 0.18, by - s * 0.5, bx - dir * s * 0.07, by - s * 0.16, s * 0.07, col(CHORD, 1));
    /* the sphere, carried in both arms */
    for (edge = o; edge >= 0; edge -= o) {
        draw_sphere(sx, sy, s * 0.38);
        oline(bx + dir * s * 0.12, by - s * 0.8, sx + dir * s * 0.18, sy + s * 0.2, s * 0.17, BOLT); /* near arm */
        odisc(sx + dir * s * 0.2, sy + s * 0.22, s * 0.1, SKIN);
        odisc(sx + dir * s * 0.07, sy - s * 0.32, s * 0.09, SKIN);
        draw_face(bx + dir * s * 0.05, by - s * 1.3, s, dir, 0);
        if (edge <= 0) break;
    }
    edge = 0;
}

/* the caption, on a strip of paper pinned across the bottom of the sheet */
static void draw_caption(int w, int h, const char *l1, const char *l2)
{
    double s1 = unit * 0.5, s2 = unit * 0.34;
    double widest = fmax(text_width(l1, s1), text_width(l2, s2)), room = w * 0.86 - unit * 8;
    if (widest > room) { s1 *= room / widest; s2 *= room / widest; } /* long lines are written smaller */
    double bw = fmax(text_width(l1, s1), text_width(l2, s2)) + unit * 8, bh = unit * 11.5;
    double bx = w / 2.0 - bw / 2, by = h - (w < h ? w : h) * 0.03 * 1.6 - bh;
    rect(bx + unit * 0.5, by + unit * 0.6, bw, bh, col(INK, 0.3));
    rect(bx - unit * 0.22, by - unit * 0.22, bw + unit * 0.44, bh + unit * 0.44, col(INK, 1));
    rect(bx, by, bw, bh, col(SAND, 1));
    rect(bx + unit * 0.7, by + unit * 0.7, bw - unit * 1.4, unit * 0.12, col(FAINT, 0.9));
    rect(bx + unit * 0.7, by + bh - unit * 0.82, bw - unit * 1.4, unit * 0.12, col(FAINT, 0.9));
    text(l1, w / 2.0, by + unit * 2.2, s1, 0.95, col(INK, 1));
    text(l2, w / 2.0, by + unit * 7.2, s2, 0.9, col(CHORD, 1));
}

static void draw_scene(SDL_Renderer *r, int w, int h)
{
    double m = (w < h ? w : h) * 0.03;
    unit = (w < h ? w : h) * 0.0125;
    mx0 = m * 1.2; my0 = m * 1.0; mw = w - m * 2.4; mh = h - m * 2.0 - unit * 11;
    SDL_SetRenderDrawColor(r, BG.r, BG.g, BG.b, 255);
    SDL_RenderClear(r);
    double fade = 1;
    if (state == ST_MAP && st_time < 1.2) fade = st_time / 1.2;
    if (state == ST_REST && st_time > T_REST - 1.5) fade = (T_REST - st_time) / 1.5;
    draw_map(w, h);
    draw_scenery();
    draw_compass();
    char l1[128] = "", l2[160] = "", a[40], b[40];
    int walk = 0;
    double rw = unit * 0.5;

    if (state == ST_MAP) {
        snprintf(l1, sizeof l1, "THE LAZY PEREGRIN");
        snprintf(l2, sizeof l2, "A FRAA MUST CALL AT %d MATHS AND COME HOME. WHICH WAY IS SHORTEST?", N - 1);
    } else if (state == ST_DEVICE) {
        draw_route(dev_best, rw * 1.5, col(INK, 0.35));
        draw_route(dev_best, rw, col(RED, 0.95));
        if (!dev_done) draw_route(perm, rw * 0.42, col(INK, 0.85));
        commas(a, dev_tried); commas(b, routes_total);
        snprintf(l1, sizeof l1, dev_done ? "THE SYNTACTIC DEVICE HAS TRIED EVERY ROUTE" : "A SYNTACTIC DEVICE TRIES THE ROUTES ONE BY ONE");
        snprintf(l2, sizeof l2, "TRIED %s OF %s    BEST SO FAR: %d MILES", a, b, (int)(dev_best_len * 1000 + 0.5));
    } else if (state == ST_GROD || state == ST_SETTLE) {
        double s = state == ST_SETTLE ? st_time / (T_SETTLE * 0.5) : 0;
        if (s > 1) s = 1;
        for (int i = 0; i < N; i++)
            for (int k = i + 1; k < N; k++) {
                double f = freq[i][k];
                if (f <= 0) continue;
                double x0, y0, x1, y1;
                mp(i, &x0, &y0); mp(k, &x1, &y1);
                line(x0, y0, x1, y1, unit * (0.14 + 0.5 * f), col(INK, (0.12 + 0.88 * pow(f, 0.8)) * (1 - s)));
            }
        if (s > 0) { draw_route(best, rw * 1.5, col(INK, 0.35 * s)); draw_route(best, rw, col(RED, s)); }
        snprintf(l1, sizeof l1, "SAUNT GROD'S MACHINE HOLDS EVERY ROUTE AT ONCE");
        if (state == ST_SETTLE) snprintf(l2, sizeof l2, "AND SETTLES ON THE SHORTEST: %d MILES", (int)(best_len * 1000 + 0.5));
        else { commas(b, routes_total); snprintf(l2, sizeof l2, "ALL %s OF THEM", b); }
    } else {
        walk = 1;
        snprintf(l1, sizeof l1, state == ST_WALK && st_time < walk_time() ? "THE LAZY PEREGRIN SETS OUT" : "HOME AGAIN, AND NOT A STEP WASTED");
        snprintf(l2, sizeof l2, "THE SHORTEST WAY ROUND %d MATHS: %d MILES", N - 1, (int)(best_len * 1000 + 0.5));
    }

    /* the road ahead dashed in red, his footprints behind */
    double fx, fy, gone = 0, into = 0;
    int stopped = -1, reached = -1, dir = 1, walking = 0;
    mp(0, &fx, &fy);
    if (walk) {
        gone = state == ST_WALK ? walk_pos(st_time, &stopped, &into) : best_len;
        walking = state == ST_WALK && gone < best_len && stopped < 0;
        double acc2 = 0, sp = unit * 1.5;
        int foot = 0;
        for (int i = 0; i < N; i++) {
            double d = D[best[i]][best[(i + 1) % N]], x0, y0, x1, y1;
            mp(best[i], &x0, &y0); mp(best[(i + 1) % N], &x1, &y1);
            double L = hypot(x1 - x0, y1 - y0), ux = (x1 - x0) / L, uy = (y1 - y0) / L;
            double done = gone - acc2 >= d ? 1 : gone - acc2 <= 0 ? 0 : (gone - acc2) / d;
            for (double q = sp * 0.5; q < L; q += sp, foot++) {
                double qx = x0 + ux * q, qy = y0 + uy * q, side = (foot & 1) ? 1 : -1;
                if (q / L <= done) { /* a little shoe print */
                    double ax = qx - uy * side * unit * 0.32, ay = qy + ux * side * unit * 0.32;
                    line(ax - ux * unit * 0.2, ay - uy * unit * 0.2, ax + ux * unit * 0.2, ay + uy * unit * 0.2, unit * 0.36, col(INK, 0.7));
                } else if (foot % 2 == 0) line(qx, qy, qx + ux * sp * 0.55, qy + uy * sp * 0.55, unit * 0.3, col(RED, 0.8));
            }
            if (done >= 1) reached = i + 1;
            if (done > 0 && done < 1) { fx = x0 + (x1 - x0) * done; fy = y0 + (y1 - y0) * done; dir = ux >= 0 ? 1 : -1; }
            else if (done >= 1) { fx = x1; fy = y1; dir = ux >= 0 ? 1 : -1; }
            acc2 += d;
        }
    }
    for (int i = 0; i < N; i++) {
        double pop = 1;
        if (state == ST_MAP) { pop = (st_time - 1.2 - i * 0.2) / 0.35; pop = pop < 0 ? 0 : pop > 1 ? 1 + 0.25 * sin((pop > 1.6 ? 1.6 : pop) * M_PI) : pop; }
        int visited = 0;
        if (walk) for (int k = 1; k <= reached && k <= N; k++) if (best[k % N] == i) visited = 1;
        if (pop > 0) draw_math(i, pop > 1.25 ? 1 : pop, visited, stopped >= 0 && best[stopped] == i ? into : 0);
    }
    /* he dozes beside his math while the machines work, and again when he gets back */
    int awake = (state == ST_WALK && gone < best_len) || (state == ST_SETTLE && st_time > T_SETTLE * 0.5);
    if (state != ST_MAP || st_time > 1.6) {
        double s = unit * 3.9, ox = walking ? 0 : unit * (awake ? 4.6 : 6.8), oy = walking ? unit * 0.6 : unit * 2.0;
        draw_fraa(fx + ox, fy + oy, s, walking ? dir : 1, walking ? st_time * 10 : 0, !awake);
        if (state == ST_SETTLE && awake) text("!", fx + ox + s * 0.75, fy + oy - s * 2.5, unit * 0.5, 1.2, col(RED, 1));
    }
    draw_caption(w, h, l1, l2);
    flush(r);
    if (fade < 1) { rect(0, 0, w, h, col(BG, 1 - (fade < 0 ? 0 : fade))); flush(r); }
}

typedef struct { SDL_Window *win; SDL_Renderer *ren; SDL_Texture *scene; int w, h; } View;

static void size_view(View *v)
{
    int ow, oh;
    SDL_RendererInfo info;
    SDL_GetRendererOutputSize(v->ren, &ow, &oh);
    if (ow < 1 || oh < 1) return;
    int ss = 2; /* drawn at twice the size and scaled down, for smooth edges */
    if (SDL_GetRendererInfo(v->ren, &info) == 0) {
        if (!(info.flags & SDL_RENDERER_TARGETTEXTURE) || (info.flags & SDL_RENDERER_SOFTWARE)) ss = 0;
        else if (info.max_texture_width && (ow * 2 > info.max_texture_width || oh * 2 > info.max_texture_height)) ss = 0;
    }
    if (v->w == ow * (ss ? ss : 1) && v->h == oh * (ss ? ss : 1) && (v->scene || !ss)) return;
    if (v->scene) SDL_DestroyTexture(v->scene);
    v->scene = NULL; v->w = ow; v->h = oh;
    if (!ss) return;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    v->scene = SDL_CreateTexture(v->ren, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, ow * ss, oh * ss);
    if (v->scene) { v->w = ow * ss; v->h = oh * ss; }
}
static void render(View *v)
{
    if (v->scene) SDL_SetRenderTarget(v->ren, v->scene);
    draw_scene(v->ren, v->w, v->h);
    if (v->scene) { SDL_SetRenderTarget(v->ren, NULL); SDL_RenderCopy(v->ren, v->scene, NULL, NULL); }
    SDL_RenderPresent(v->ren);
}
static int make_view(View *v, int display)
{
    Uint32 flags = SDL_WINDOW_ALLOW_HIGHDPI;
    int x = SDL_WINDOWPOS_CENTERED_DISPLAY(display);
    if (opt_window) flags |= SDL_WINDOW_RESIZABLE;
    else flags |= SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_BORDERLESS;
    memset(v, 0, sizeof *v);
    v->win = SDL_CreateWindow("The Lazy Peregrin", x, x, 1200, 750, flags);
    if (!v->win) return 0;
    v->ren = SDL_CreateRenderer(v->win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE);
    if (!v->ren) v->ren = SDL_CreateRenderer(v->win, -1, SDL_RENDERER_SOFTWARE);
    if (!v->ren) return 0;
    SDL_SetRenderDrawBlendMode(v->ren, SDL_BLENDMODE_BLEND);
    return 1;
}

static void start_show(void)
{
    seed_state = opt_seed ? opt_seed * 0x9E3779B97F4A7C15ULL : ((uint64_t)time(NULL) << 20) ^ SDL_GetPerformanceCounter();
    if (!seed_state) seed_state = 1;
    for (int i = 0; i < 6; i++) next_rand();
    new_round();
}

static int snapshot(void)
{
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, opt_w, opt_h, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer *r = surf ? SDL_CreateSoftwareRenderer(surf) : NULL;
    if (!r) { fprintf(stderr, "lazy-peregrin: %s\n", SDL_GetError()); return 1; }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    start_show();
    for (double t = 0; t < opt_time; t += 1.0 / 60) update(1.0 / 60);
    draw_scene(r, opt_w, opt_h);
    SDL_RenderPresent(r);
    if (SDL_SaveBMP(surf, opt_snapshot) != 0) { fprintf(stderr, "lazy-peregrin: %s\n", SDL_GetError()); return 1; }
    return 0;
}

static void usage(void)
{
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "The Lazy Peregrin",
        "The Lazy Peregrin screensaver\n\n"
        "  --window        run in a window (Esc or Q quits)\n"
        "  --maths N       number of maths, 6 to 18 (default: 10 to 15 at random)\n"
        "  --speed X       animation speed, 1 = normal\n"
        "  --seed N        repeatable sequence\n"
        "  --snapshot FILE.bmp [--time SECONDS] [--size WxH]\n"
        "                  write a picture of the screensaver after SECONDS and exit\n", NULL);
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if ((a[0] == '/' || a[0] == '-') && (a[1] == 's' || a[1] == 'S') && !a[2]) continue;
        else if ((a[0] == '/' || a[0] == '-') && (a[1] == 'p' || a[1] == 'P') && (!a[2] || a[2] == ':')) return 0;
        else if ((a[0] == '/' || a[0] == '-') && (a[1] == 'c' || a[1] == 'C') && (!a[2] || a[2] == ':')) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "The Lazy Peregrin", "The Lazy Peregrin screensaver has no settings.", NULL);
            return 0;
        }
        else if (!strcmp(a, "--window")) opt_window = 1;
        else if (!strcmp(a, "--maths") && i + 1 < argc) opt_min = opt_max = SDL_atoi(argv[++i]);
        else if (!strcmp(a, "--speed") && i + 1 < argc) opt_speed = SDL_atof(argv[++i]);
        else if (!strcmp(a, "--seed") && i + 1 < argc) opt_seed = SDL_strtoull(argv[++i], NULL, 10);
        else if (!strcmp(a, "--snapshot") && i + 1 < argc) opt_snapshot = argv[++i];
        else if (!strcmp(a, "--time") && i + 1 < argc) opt_time = SDL_atof(argv[++i]);
        else if (!strcmp(a, "--size") && i + 1 < argc) {
            const char *x = SDL_strchr(argv[++i], 'x');
            opt_w = SDL_atoi(argv[i]); opt_h = x ? SDL_atoi(x + 1) : 0;
            if (opt_w < 64 || opt_h < 64) { usage(); return 1; }
        }
        else { usage(); return 1; }
    }
    if (opt_min < 6) opt_min = 6;
    if (opt_max > MAXN) opt_max = MAXN;
    if (opt_min > MAXN) opt_min = MAXN;
    if (opt_max < opt_min) opt_max = opt_min;
    if (opt_speed < 0.05) opt_speed = 0.05;

    if (opt_snapshot) return snapshot();
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "lazy-peregrin: %s\n", SDL_GetError()); return 1; }

    View views[8];
    int nviews = 0, ndisp = opt_window ? 1 : SDL_GetNumVideoDisplays();
    if (ndisp < 1) ndisp = 1;
    for (int d = 0; d < ndisp && nviews < 8; d++)
        if (make_view(&views[nviews], d)) nviews++;
    if (!nviews) { fprintf(stderr, "lazy-peregrin: %s\n", SDL_GetError()); return 1; }
    if (!opt_window) SDL_ShowCursor(SDL_DISABLE);
    start_show();

    Uint64 freqc = SDL_GetPerformanceFrequency(), last = SDL_GetPerformanceCounter(), start = last;
    double moved = 0;
    int running = 1;
    while (running) {
        SDL_Event e;
        double since = (double)(SDL_GetPerformanceCounter() - start) / freqc;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            else if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE) running = 0;
            else if (e.type == SDL_KEYDOWN) {
                if (!opt_window || e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_q) running = 0;
            } else if (!opt_window && since > 1.0) {
                if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEWHEEL || e.type == SDL_FINGERDOWN) running = 0;
                else if (e.type == SDL_MOUSEMOTION) {
                    moved += abs(e.motion.xrel) + abs(e.motion.yrel);
                    if (moved > 24) running = 0;
                }
            }
        }
        Uint64 now = SDL_GetPerformanceCounter();
        double dt = (double)(now - last) / freqc;
        last = now;
        if (dt > 0.1) dt = 0.1;
        update(dt);
        for (int i = 0; i < nviews; i++) { size_view(&views[i]); render(&views[i]); }
        SDL_Delay(views[0].scene ? 1 : 12);
    }
    SDL_Quit();
    return 0;
}
