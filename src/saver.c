/*
 * teglon: screensaver that plays the Teglon the way "Anathem" describes it.
 *
 * A marble Decagon some two hundred feet across. A rhombus goes down in the nearest
 * corner, its obtuse angle fitting the vertex, and a tile at the vertex opposite.
 * Then the pattern is built outward from the first vertex with grooved clay tiles,
 * the groove carried on from tile to tile; stretches of it are left marooned for a
 * while and joined up later. When the last tile is down the groove is one unbroken
 * curve from the first vertex to the last, crossing every tile, and light runs along
 * it. The next solution is laid on top in another kind of tile, as at Elkhazg:
 * terra-cotta, then green, then brown, then white porcelain with a black line.
 *
 *   teglon                 full screen on every display; any key or mouse movement quits
 *   teglon --window        in a window; Esc or Q quits
 *   Teglon.scr /s          Windows screensaver conventions (/s run, /c settings, /p preview)
 */
#include "teglon.h"

#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---- options ---- */
static int opt_window = 0;
static int opt_min = 30, opt_max = 30; /* side of the Decagon in tile edges; see README for the scale */
static int opt_strict = 0;             /* play the seven-moulds-one-groove-each game on a side-2 Decagon */
static double opt_speed = 1.0;
static uint64_t opt_seed = 0;
static const char *opt_snapshot = NULL; /* write a still picture instead of running */
static double opt_time = 30;
static int opt_w = 1920, opt_h = 1200;

/* ---- colours ---- */
typedef struct { Uint8 r, g, b; } Rgb;
typedef struct { Rgb tile[TG_NS], grout, groove, glow; } Palette;
/* a colour for each shape (thin rhomb, thick rhomb, pentagon, long hexagon, squat
 * hexagon, octagon, decagon, bow tie), so the shapes read at a distance */
static const Palette PAL[4] = {
    {{{232, 134, 66}, {204, 92, 46}, {246, 176, 88}, {178, 70, 40}, {222, 116, 56}, {210, 104, 52}, {252, 198, 112}, {156, 56, 36}},
     {104, 56, 30}, {14, 5, 2}, {255, 232, 170}}, /* terra-cotta */
    {{{124, 180, 108}, {80, 148, 96}, {176, 206, 116}, {54, 122, 90}, {104, 164, 100}, {92, 156, 98}, {204, 222, 136}, {40, 104, 86}},
     {40, 70, 46}, {4, 12, 6}, {226, 255, 200}},  /* green */
    {{{176, 120, 74}, {146, 92, 54}, {206, 156, 98}, {120, 72, 44}, {164, 108, 64}, {154, 100, 60}, {224, 182, 124}, {100, 56, 36}},
     {66, 42, 24}, {10, 5, 2}, {255, 214, 164}},  /* brown */
    {{{240, 236, 226}, {214, 218, 224}, {248, 242, 222}, {194, 202, 214}, {228, 226, 218}, {220, 220, 216}, {254, 250, 236}, {178, 188, 204}},
     {120, 118, 116}, {6, 6, 10}, {120, 190, 255}}, /* white porcelain, black glaze */
};
static const Rgb BG = {18, 16, 14}, MARBLE = {196, 190, 178}, MARBLE_EDGE = {120, 114, 104};

/* ---- geometry buffer ---- */
static SDL_Vertex *vb;
static int *ib;
static int nv, ni, vcap, icap;

static void vb_need(int v, int i)
{
    if (nv + v > vcap) { vcap = (nv + v) * 2; vb = realloc(vb, sizeof *vb * vcap); }
    if (ni + i > icap) { icap = (ni + i) * 2; ib = realloc(ib, sizeof *ib * icap); }
}
static inline SDL_Color col(Rgb c, double k, double a)
{
    double r = c.r * k, g = c.g * k, b = c.b * k;
    SDL_Color o = {(Uint8)(r > 255 ? 255 : r), (Uint8)(g > 255 ? 255 : g), (Uint8)(b > 255 ? 255 : b), (Uint8)(a < 0 ? 0 : a > 1 ? 255 : a * 255)};
    return o;
}
static inline void vert(double x, double y, SDL_Color c)
{
    vb[nv].position.x = (float)x; vb[nv].position.y = (float)y;
    vb[nv].color = c; vb[nv].tex_coord.x = vb[nv].tex_coord.y = 0;
    nv++;
}
/* polygon as a fan around (cx,cy) */
static void fan(const double *x, const double *y, int n, double cx, double cy, SDL_Color centre, SDL_Color rim)
{
    vb_need(n + 1, 3 * n);
    int b = nv;
    vert(cx, cy, centre);
    for (int i = 0; i < n; i++) vert(x[i], y[i], rim);
    for (int i = 0; i < n; i++) { ib[ni++] = b; ib[ni++] = b + 1 + i; ib[ni++] = b + 1 + (i + 1) % n; }
}
static void disc(double cx, double cy, double r, SDL_Color centre, SDL_Color rim)
{
    int n = r < 3 ? 8 : 24;
    double x[24], y[24];
    for (int i = 0; i < n; i++) { x[i] = cx + r * cos(i * 2 * M_PI / n); y[i] = cy + r * sin(i * 2 * M_PI / n); }
    fan(x, y, n, cx, cy, centre, rim);
}
/* thick line through points 0..n-1 */
static void ribbon(const double *x, const double *y, int n, double w, SDL_Color c)
{
    if (n < 2) return;
    vb_need(2 * n, 6 * (n - 1));
    int b = nv;
    for (int i = 0; i < n; i++) {
        int p = i > 0 ? i - 1 : i, q = i < n - 1 ? i + 1 : i;
        double dx = x[q] - x[p], dy = y[q] - y[p], l = hypot(dx, dy);
        if (l < 1e-9) { dx = 1; dy = 0; l = 1; }
        double nx = -dy / l * w / 2, ny = dx / l * w / 2;
        vert(x[i] + nx, y[i] + ny, c);
        vert(x[i] - nx, y[i] - ny, c);
    }
    for (int i = 0; i < n - 1; i++) {
        int a = b + 2 * i;
        ib[ni++] = a; ib[ni++] = a + 1; ib[ni++] = a + 2;
        ib[ni++] = a + 1; ib[ni++] = a + 3; ib[ni++] = a + 2;
    }
}
static void flush(SDL_Renderer *r)
{
    if (ni) SDL_RenderGeometry(r, NULL, vb, nv, ib, ni);
    nv = ni = 0;
}

/* ---- the game being shown ---- */
enum { ST_LAY, ST_SEARCH, ST_PULSE, ST_HOLD, ST_LIFT };
static int state, layer, rotk, round_gen;
static double st_time;           /* seconds in the current state */
static double now_t;             /* seconds since the show began */
static double acc;
static int *p_order, *p_in, *p_out, p_len; /* the groove: tiles in order, sides in and out */
static int *lay_seq, lay_n;      /* tiles in the order they are set down */
static double *lay_at;           /* by tile: seconds into the round when it goes down */
static double t_laid[64];        /* strict game: when each tile last went down */
static double lay_end;
static uint64_t seed_state;

/* the one groove layout with which the strict game can be finished */
static const int STRICT_A[TG_NS] = {0, 1, 0, 0, 2, 0, 0, 0}, STRICT_B[TG_NS] = {1, 2, 1, 2, 5, 3, 5, 1};

static uint64_t next_rand(void)
{
    seed_state ^= seed_state << 13; seed_state ^= seed_state >> 7; seed_state ^= seed_state << 17;
    return seed_state;
}
static double frand(void) { return (next_rand() >> 11) * (1.0 / 9007199254740992.0); }

/* binary heap of (key, tile) for choosing the next tile to set down */
typedef struct { double key; int id; } HeapItem;
static HeapItem *heap;
static int heap_n, heap_cap;
static void heap_push(double key, int id)
{
    if (heap_n == heap_cap) { heap_cap = heap_cap ? heap_cap * 2 : 1024; heap = realloc(heap, sizeof *heap * heap_cap); }
    int i = heap_n++;
    while (i > 0 && heap[(i - 1) / 2].key > key) { heap[i] = heap[(i - 1) / 2]; i = (i - 1) / 2; }
    heap[i].key = key; heap[i].id = id;
}
static HeapItem heap_pop(void)
{
    HeapItem top = heap[0], last = heap[--heap_n];
    int i = 0;
    for (;;) {
        int c = 2 * i + 1;
        if (c >= heap_n) break;
        if (c + 1 < heap_n && heap[c + 1].key < heap[c].key) c++;
        if (heap[c].key >= last.key) break;
        heap[i] = heap[c]; i = c;
    }
    heap[i] = last;
    return top;
}

/* Solve a Decagon, then work out the order to set the tiles down in: outward from
 * the first vertex, but keeping on with a stretch of groove while it stays near. */
static void prepare(int side)
{
    for (int attempt = 0;; attempt++) {
        tg_strict = 0;
        tg_init(side, next_rand());
        tg_bowtie = 0.3 + 0.6 * frand();
        if (!tg_pave(0.35 + 0.65 * frand(), 1)) continue;
        /* the first tile is a rhombus whose obtuse angle fits the corner */
        int c = tg_corner_tile(0);
        if (attempt < 300 && (c < 0 || tg_tile[c].s != 0)) continue;
        tg_path_begin();
        int r = TG_NONE;
        for (long n = 0; n < 400L * tg_nt + 20000 && r != TG_SOLVED; n++) r = tg_path_step();
        if (r == TG_SOLVED) break;
    }
    int n = tg_nt;
    free(p_order); free(lay_seq); free(lay_at);
    p_order = malloc(sizeof(int) * (n + 1) * 3);
    p_in = p_order + n + 1; p_out = p_in + n + 1;
    p_len = tg_path(p_order, p_in, p_out);
    lay_seq = malloc(sizeof(int) * (n + 1));
    lay_at = malloc(sizeof(double) * (n + 1));
    int *pos = malloc(sizeof(int) * (n + 1));
    char *down = calloc(n + 1, 1);
    for (int i = 0; i < p_len; i++) pos[p_order[i]] = i;
    lay_n = 0; heap_n = 0;
    lay_seq[lay_n++] = p_order[0]; down[p_order[0]] = 1;
    if (p_len > 1) { lay_seq[lay_n++] = p_order[p_len - 1]; down[p_order[p_len - 1]] = 2; } /* the tile at the far vertex */
    for (int id = 0; id < n; id++)
        if (!down[id]) {
            double d = hypot(tg_tile[id].cx - tg_corner_x[0], tg_tile[id].cy - tg_corner_y[0]) + 0.4 * frand();
            heap_push(pos[id] == 1 ? d - 2.5 : d, id);
        }
    while (heap_n) {
        HeapItem it = heap_pop();
        if (down[it.id]) continue;
        down[it.id] = 1;
        lay_seq[lay_n++] = it.id;
        /* its neighbours along the groove are now worth going on with */
        for (int e = -1; e <= 1; e += 2) {
            int q = pos[it.id] + e;
            if (q < 0 || q >= p_len || down[p_order[q]]) continue;
            int o = p_order[q];
            heap_push(hypot(tg_tile[o].cx - tg_corner_x[0], tg_tile[o].cy - tg_corner_y[0]) - 2.5, o);
        }
    }
    free(pos); free(down);
    /* the first two tiles are set down deliberately, then the pace picks up */
    double span = (45 + n / 110.0) / opt_speed;
    for (int k = 0; k < lay_n; k++) {
        double t = k == 0 ? 0.8 : k == 1 ? 2.4 : 4.2 + span * (k - 2) / (lay_n > 3 ? lay_n - 2 : 1);
        lay_at[lay_seq[k]] = t;
    }
    lay_end = lay_at[lay_seq[lay_n - 1]] + 1.5;
}

static void new_round(void)
{
    round_gen++;
    acc = 0; st_time = 0;
    if (opt_strict) {
        tg_strict = 1;
        for (int s = 0; s < TG_NS; s++) { tg_ga[s] = STRICT_A[s]; tg_gb[s] = STRICT_B[s]; tg_fixed[s] = 1; }
        tg_init(2, next_rand());
        for (int i = 0; i < 64; i++) t_laid[i] = now_t;
        state = ST_SEARCH;
        return;
    }
    prepare(opt_min + (int)(next_rand() % (uint64_t)(opt_max - opt_min + 1)));
    state = ST_LAY;
}

static void update(double dt)
{
    st_time += dt;
    now_t += dt;
    switch (state) {
    case ST_LAY:
        if (st_time > lay_end) { state = ST_PULSE; st_time = 0; }
        break;
    case ST_SEARCH:
        acc += 7 * opt_speed * (st_time > 60 ? 1 + (st_time - 60) * 0.3 : 1) * dt;
        while (acc >= 1 && state == ST_SEARCH) {
            acc -= 1;
            /* run on to the next tile laid or lifted */
            for (int guard = 0; guard < 100000; guard++) {
                int r = tg_step();
                if (r == TG_PLACE) { t_laid[tg_nt - 1] = now_t; break; }
                if (r == TG_REMOVE) break;
                if (r == TG_SOLVED) {
                    t_laid[tg_nt - 1] = now_t;
                    free(p_order);
                    p_order = malloc(sizeof(int) * (tg_nt + 1) * 3);
                    p_in = p_order + tg_nt + 1; p_out = p_in + tg_nt + 1;
                    p_len = tg_path(p_order, p_in, p_out);
                    state = ST_PULSE; st_time = 0;
                    break;
                }
                if (r == TG_EXHAUSTED) { state = ST_LIFT; st_time = 0; break; }
            }
        }
        break;
    case ST_PULSE:
        if (st_time > (9 + p_len / 1500.0) / opt_speed) { state = ST_HOLD; st_time = 0; }
        break;
    case ST_HOLD:
        break; /* the main loop starts the next layer */
    case ST_LIFT:
        if (st_time > 2.5) new_round();
        break;
    }
}

/* ---- drawing ---- */
typedef struct {
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *scene; /* the frame, drawn at twice the size for smooth edges */
    SDL_Texture *floor; /* everything that has settled: marble, earlier layers, tiles already down */
    int w, h;           /* drawing size in pixels */
    int have_floor, baked, gen;
} View;

static double vx0, vy0, vscale, vcos, vsin, dcx, dcy, gw;

static inline void map(double x, double y, double *ox, double *oy)
{
    double a = x - dcx, b = y - dcy;
    *ox = vx0 + (a * vcos - b * vsin) * vscale;
    *oy = vy0 - (a * vsin + b * vcos) * vscale;
}
static void set_view(const View *v)
{
    double R = tg_side / (2 * sin(M_PI / 10));
    dcx = tg_side / 2.0; dcy = tg_side / (2 * tan(M_PI / 10));
    vscale = 0.47 * (v->w < v->h ? v->w : v->h) / R;
    vx0 = v->w / 2.0; vy0 = v->h / 2.0;
    vcos = cos(rotk * M_PI / 5 + M_PI / 10); vsin = sin(rotk * M_PI / 5 + M_PI / 10);
    gw = 0.13 * vscale; /* width of the groove */
    if (gw < 2.2) gw = 2.2;
}
static unsigned hash_u(unsigned a) { a ^= a >> 16; a *= 0x7feb352dU; a ^= a >> 15; a *= 0x846ca68bU; a ^= a >> 16; return a; }

static void draw_marble(void)
{
    double x[10], y[10], cx, cy;
    map(dcx, dcy, &cx, &cy);
    for (int k = 0; k < 10; k++) {
        map(tg_corner_x[k], tg_corner_y[k], &x[k], &y[k]);
        x[k] = cx + (x[k] - cx) * 1.035; y[k] = cy + (y[k] - cy) * 1.035;
    }
    fan(x, y, 10, cx, cy, col(MARBLE_EDGE, 1, 1), col(MARBLE_EDGE, 0.8, 1));
    for (int k = 0; k < 10; k++) map(tg_corner_x[k], tg_corner_y[k], &x[k], &y[k]);
    fan(x, y, 10, cx, cy, col(MARBLE, 1.04, 1), col(MARBLE, 0.9, 1));
}

/* groove of one tile: from the middle of side a to the middle of side b, leaving
 * each side at right angles so that it runs on smoothly into the next tile */
#define GSEG 8
static int groove_pts(const TgTile *t, int a, int b, double *px, double *py)
{
    int a1 = (a + 1) % t->n, b1 = (b + 1) % t->n;
    double ax = (t->x[a] + t->x[a1]) / 2, ay = (t->y[a] + t->y[a1]) / 2;
    double nax = -(t->y[a1] - t->y[a]), nay = t->x[a1] - t->x[a];
    double bx = (t->x[b] + t->x[b1]) / 2, by = (t->y[b] + t->y[b1]) / 2;
    double nbx = -(t->y[b1] - t->y[b]), nby = t->x[b1] - t->x[b];
    double h = 0.45 * hypot(bx - ax, by - ay) + 0.08;
    double c1x = ax + nax * h, c1y = ay + nay * h, c2x = bx + nbx * h, c2y = by + nby * h;
    for (int i = 0; i <= GSEG; i++) {
        double u = (double)i / GSEG, v = 1 - u;
        double x = v * v * v * ax + 3 * v * v * u * c1x + 3 * v * u * u * c2x + u * u * u * bx;
        double y = v * v * v * ay + 3 * v * v * u * c1y + 3 * v * u * u * c2y + u * u * u * by;
        map(x, y, &px[i], &py[i]);
    }
    return GSEG + 1;
}

/* a tile with its groove; appear runs 0..1 as it drops into place */
static void draw_tile(const TgTile *t, int id, const Palette *p, double appear, double alpha)
{
    double x[TG_MAXV], y[TG_MAXV], cx, cy, rin = 1e9;
    for (int j = 0; j < t->n; j++) {
        int k = (j + 1) % t->n;
        double ex = t->x[k] - t->x[j], ey = t->y[k] - t->y[j];
        double d = fabs((t->cx - t->x[j]) * ey - (t->cy - t->y[j]) * ex);
        if (d < rin) rin = d;
    }
    double grow = 1 + 0.35 * (1 - appear) * (1 - appear);
    double jit = 0.96 + 0.08 * (hash_u((unsigned)id * 2654435761u + (unsigned)layer * 97u) & 1023) / 1023.0;
    double k = jit;
    map(t->cx, t->cy, &cx, &cy);
    for (int j = 0; j < t->n; j++)
        map(t->cx + (t->x[j] - t->cx) * grow, t->cy + (t->y[j] - t->cy) * grow, &x[j], &y[j]);
    fan(x, y, t->n, cx, cy, col(p->grout, 1, alpha), col(p->grout, 1, alpha));
    /* the joint between tiles: a fixed width, never thinner than shows on screen */
    double joint = 0.03 * vscale;
    if (joint < 0.8) joint = 0.8;
    double in = 1 - joint / (rin * vscale);
    if (in < 0.62) in = 0.62;
    for (int j = 0; j < t->n; j++) { x[j] = cx + (x[j] - cx) * in; y[j] = cy + (y[j] - cy) * in; }
    fan(x, y, t->n, cx, cy, col(p->tile[t->s], k * 1.08, alpha), col(p->tile[t->s], k * 0.94, alpha)); /* a little sheen */
    if (t->a >= 0 && t->b >= 0 && t->a != t->b) {
        double qx[GSEG + 1], qy[GSEG + 1];
        int np = groove_pts(t, t->a, t->b, qx, qy);
        SDL_Color g = col(p->groove, 1, alpha);
        ribbon(qx, qy, np, gw, g);
        if (gw > 3) { disc(qx[0], qy[0], gw * 0.5, g, g); disc(qx[np - 1], qy[np - 1], gw * 0.5, g, g); }
    }
}

/* tiles that have settled are painted onto the floor once and for all */
static void bake(View *v)
{
    const Palette *p = &PAL[layer % 4];
    int all = state == ST_PULSE || state == ST_HOLD;
    int total = tg_strict ? tg_nt : lay_n;
    if (tg_strict && !all) return;
    if (v->baked >= total) return;
    if (!tg_strict && !all && st_time - lay_at[lay_seq[v->baked]] < 0.4) return;
    SDL_SetRenderTarget(v->ren, v->floor);
    while (v->baked < total) {
        int id = tg_strict ? v->baked : lay_seq[v->baked];
        if (!all && st_time - lay_at[id] < 0.4) break;
        draw_tile(&tg_tile[id], id, p, 1, 1);
        v->baked++;
        if (nv > 60000) flush(v->ren);
    }
    flush(v->ren);
}

static void draw_moving(View *v, int from_start)
{
    SDL_Renderer *r = v->ren;
    const Palette *p = &PAL[layer % 4];
    /* tiles */
    if (state == ST_SEARCH || (state == ST_LIFT && tg_strict)) {
        double lift = state == ST_LIFT ? 1 - st_time / 2.0 : 1;
        for (int id = 0; id < tg_nt; id++) {
            double age = (now_t - t_laid[id < 64 ? id : 63]) / 0.3;
            if (age > 1) age = 1;
            draw_tile(&tg_tile[id], id, p, age, state == ST_LIFT ? (lift < 0 ? 0 : lift) : age);
        }
    } else if (!tg_strict || from_start) {
        int all = state != ST_LAY;
        for (int k = from_start ? 0 : v->baked; k < (tg_strict ? tg_nt : lay_n); k++) {
            int id = tg_strict ? k : lay_seq[k];
            double age = all ? 1 : (st_time - lay_at[id]) / 0.3;
            if (age <= 0) break;
            if (age > 1) age = 1;
            draw_tile(&tg_tile[id], id, p, age, age);
            if (nv > 60000) flush(r);
        }
    }
    flush(r);
    /* light runs along the finished groove from the first vertex to the last */
    double run = (7 + p_len / 1500.0) / opt_speed;
    if (state == ST_PULSE && st_time < run && p_len > 0) {
        double u = st_time / run;
        u = u * u * (3 - 2 * u);
        int head = (int)(u * (p_len - 1)), tail = p_len / 40 + 12;
        double qx[GSEG + 1], qy[GSEG + 1];
        for (int i = head - tail < 0 ? 0 : head - tail; i <= head; i++) {
            double f = 1 - (double)(head - i) / tail;
            int np = groove_pts(&tg_tile[p_order[i]], p_in[i], p_out[i], qx, qy);
            ribbon(qx, qy, np, gw * 0.9 + 1, col(p->glow, 1, f));
            if (i == head) {
                disc(qx[np - 1], qy[np - 1], gw * 3 + 5, col(p->glow, 1, 0.6), col(p->glow, 1, 0));
                disc(qx[np - 1], qy[np - 1], gw * 0.9 + 1, col(p->glow, 1.2, 1), col(p->glow, 1, 1));
            }
            if (nv > 60000) flush(r);
        }
        flush(r);
    }
    /* the first vertex, and the last once the groove has reached it */
    double mx, my, pr = 0.012 * (v->w < v->h ? v->w : v->h);
    for (int e = 0; e < 2; e++) {
        map(tg_corner_x[e * 5], tg_corner_y[e * 5], &mx, &my);
        double on = e == 0 ? 1 : (state == ST_PULSE && st_time > run * 0.97) || state == ST_HOLD ? 1 : 0.3;
        disc(mx, my, pr * 1.8, col(p->glow, 1, 0.35 * on), col(p->glow, 1, 0));
        disc(mx, my, pr * 0.55, col(p->glow, 1, on), col(p->glow, 0.9, on));
    }
    flush(r);
}

static void render(View *v)
{
    SDL_Renderer *r = v->ren;
    set_view(v);
    if (v->gen != round_gen) { v->gen = round_gen; v->baked = 0; }
    if (!v->scene) { /* no off-screen drawing: paint the lot every frame */
        SDL_SetRenderDrawColor(r, BG.r, BG.g, BG.b, 255);
        SDL_RenderClear(r);
        draw_marble(); flush(r);
        draw_moving(v, 1);
        SDL_RenderPresent(r);
        return;
    }
    if (!v->have_floor) {
        SDL_SetRenderTarget(r, v->floor);
        SDL_SetRenderDrawColor(r, BG.r, BG.g, BG.b, 255);
        SDL_RenderClear(r);
        draw_marble(); flush(r);
        v->have_floor = 1; v->baked = 0;
    }
    bake(v);
    SDL_SetRenderTarget(r, v->scene);
    SDL_RenderCopy(r, v->floor, NULL, NULL);
    draw_moving(v, 0);
    SDL_SetRenderTarget(r, NULL);
    SDL_RenderCopy(r, v->scene, NULL, NULL);
    SDL_RenderPresent(r);
}

static int make_view(View *v, int display)
{
    Uint32 flags = SDL_WINDOW_ALLOW_HIGHDPI;
    int x = SDL_WINDOWPOS_CENTERED_DISPLAY(display);
    if (opt_window) flags |= SDL_WINDOW_RESIZABLE;
    else flags |= SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_BORDERLESS;
    memset(v, 0, sizeof *v);
    v->win = SDL_CreateWindow("Teglon", x, x, 900, 900, flags);
    if (!v->win) return 0;
    v->ren = SDL_CreateRenderer(v->win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE);
    if (!v->ren) v->ren = SDL_CreateRenderer(v->win, -1, SDL_RENDERER_SOFTWARE);
    if (!v->ren) return 0;
    SDL_SetRenderDrawBlendMode(v->ren, SDL_BLENDMODE_BLEND);
    return 1;
}

/* (re)create the off-screen pictures */
static void size_view(View *v)
{
    int ow, oh;
    SDL_RendererInfo info;
    SDL_GetRendererOutputSize(v->ren, &ow, &oh);
    if (ow < 1 || oh < 1) return;
    int ss = 2;
    if (SDL_GetRendererInfo(v->ren, &info) == 0) {
        if (!(info.flags & SDL_RENDERER_TARGETTEXTURE)) ss = 0;
        else if (info.flags & SDL_RENDERER_SOFTWARE) ss = 1;
        else if (info.max_texture_width && (ow * 2 > info.max_texture_width || oh * 2 > info.max_texture_height)) ss = 1;
    }
    if (v->w == ow * (ss ? ss : 1) && v->h == oh * (ss ? ss : 1) && (v->scene || !ss)) return;
    if (v->scene) SDL_DestroyTexture(v->scene);
    if (v->floor) SDL_DestroyTexture(v->floor);
    v->scene = v->floor = NULL; v->have_floor = 0; v->baked = 0;
    v->w = ow; v->h = oh;
    if (!ss) return;
    v->w = ow * ss; v->h = oh * ss;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    v->scene = SDL_CreateTexture(v->ren, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, v->w, v->h);
    v->floor = SDL_CreateTexture(v->ren, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, v->w, v->h);
    if (!v->scene || !v->floor) {
        if (v->scene) SDL_DestroyTexture(v->scene);
        if (v->floor) SDL_DestroyTexture(v->floor);
        v->scene = v->floor = NULL; v->w = ow; v->h = oh;
    }
}

static void start_show(void)
{
    seed_state = opt_seed ? opt_seed * 0x9E3779B97F4A7C15ULL : ((uint64_t)time(NULL) << 20) ^ SDL_GetPerformanceCounter();
    if (!seed_state) seed_state = 1;
    for (int i = 0; i < 6; i++) next_rand();
    layer = 0; /* terra-cotta first, on the bare marble */
    rotk = (int)(next_rand() % 10);
    now_t = 0;
    new_round();
}

/* after a solution has been admired: it stays as the floor, and the next goes on top */
static int next_layer_due(void) { return state == ST_HOLD && st_time > 10.0 / opt_speed; }
static void next_layer(void)
{
    layer++;
    rotk = (int)(next_rand() % 10);
    new_round();
}

/* run the show without a window for opt_time seconds and save what it looks like */
static int snapshot(void)
{
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, opt_w, opt_h, 32, SDL_PIXELFORMAT_ARGB8888);
    View v;
    memset(&v, 0, sizeof v);
    if (!surf || !(v.ren = SDL_CreateSoftwareRenderer(surf))) { fprintf(stderr, "teglon: %s\n", SDL_GetError()); return 1; }
    SDL_SetRenderDrawBlendMode(v.ren, SDL_BLENDMODE_BLEND);
    size_view(&v);
    start_show();
    for (double t = 0; t < opt_time; t += 1.0 / 30) {
        update(1.0 / 30);
        if (next_layer_due()) { render(&v); next_layer(); }
    }
    render(&v);
    if (SDL_SaveBMP(surf, opt_snapshot) != 0) { fprintf(stderr, "teglon: %s\n", SDL_GetError()); return 1; }
    return 0;
}

static void usage(void)
{
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Teglon",
        "Teglon screensaver\n\n"
        "  --window        run in a window (Esc or Q quits)\n"
        "  --side N        side of the Decagon in tile edges (default 30)\n"
        "  --min N --max N pick the side at random in a range\n"
        "  --speed X       animation speed, 1 = normal\n"
        "  --strict        the strict game: seven moulds, one fixed groove each (side 2)\n"
        "  --seed N        repeatable sequence\n"
        "  --snapshot FILE.bmp [--time SECONDS] [--size WxH]\n"
        "                  write a picture of the screensaver after SECONDS and exit\n", NULL);
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if ((a[0] == '/' || a[0] == '-') && (a[1] == 's' || a[1] == 'S') && !a[2]) continue; /* Windows: run */
        else if ((a[0] == '/' || a[0] == '-') && (a[1] == 'p' || a[1] == 'P') && (!a[2] || a[2] == ':')) return 0; /* Windows: preview pane */
        else if ((a[0] == '/' || a[0] == '-') && (a[1] == 'c' || a[1] == 'C') && (!a[2] || a[2] == ':')) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Teglon", "The Teglon screensaver has no settings.", NULL);
            return 0;
        }
        else if (!strcmp(a, "--window")) opt_window = 1;
        else if (!strcmp(a, "--strict")) opt_strict = 1;
        else if (!strcmp(a, "--side") && i + 1 < argc) opt_min = opt_max = SDL_atoi(argv[++i]);
        else if (!strcmp(a, "--min") && i + 1 < argc) opt_min = SDL_atoi(argv[++i]);
        else if (!strcmp(a, "--max") && i + 1 < argc) opt_max = SDL_atoi(argv[++i]);
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
    if (opt_min < 2) opt_min = 2;
    if (opt_max > 48) opt_max = 48;
    if (opt_min > 48) opt_min = 48;
    if (opt_max < opt_min) opt_max = opt_min;
    if (opt_speed < 0.05) opt_speed = 0.05;

    if (opt_snapshot) return snapshot();
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1"); /* let the display still power down */
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "teglon: %s\n", SDL_GetError()); return 1; }

    View views[8];
    int nviews = 0, ndisp = opt_window ? 1 : SDL_GetNumVideoDisplays();
    if (ndisp < 1) ndisp = 1;
    for (int d = 0; d < ndisp && nviews < 8; d++)
        if (make_view(&views[nviews], d)) nviews++;
    if (!nviews) { fprintf(stderr, "teglon: %s\n", SDL_GetError()); return 1; }
    if (!opt_window) SDL_ShowCursor(SDL_DISABLE);

    start_show();

    Uint64 freq = SDL_GetPerformanceFrequency(), last = SDL_GetPerformanceCounter(), start = last;
    double moved = 0;
    int running = 1;
    while (running) {
        SDL_Event e;
        double since = (double)(SDL_GetPerformanceCounter() - start) / freq;
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
        double dt = (double)(now - last) / freq;
        last = now;
        if (dt > 0.1) dt = 0.1;
        update(dt);
        for (int i = 0; i < nviews; i++) { size_view(&views[i]); render(&views[i]); }
        if (next_layer_due()) {
            next_layer();
            last = SDL_GetPerformanceCounter(); /* solving the next one took a moment */
        }
        SDL_Delay(views[0].scene ? 1 : 12);
    }
    SDL_Quit();
    return 0;
}
