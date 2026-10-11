/*
 * teglon: screensaver that paves the Decagon and threads the groove through it,
 * over and over, each solution laid on top of the last in a new colour of tile.
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
static int opt_window = 0, opt_min = 3, opt_max = 7;
static int opt_strict = -1; /* -1: a strict round now and then, 0: never, 1: always */
static const char *opt_snapshot = NULL; /* write a still picture instead of running */
static double opt_time = 30;
static int opt_w = 1920, opt_h = 1200;
static double opt_speed = 1.0;
static uint64_t opt_seed = 0;

/* ---- colours ---- */
typedef struct { Uint8 r, g, b; } Rgb;
typedef struct { Rgb tile, grout, groove, glow; } Palette;
static const Palette PAL[4] = {
    {{201, 123, 74}, {132, 78, 46}, {40, 20, 10}, {255, 220, 150}},    /* terra-cotta */
    {{104, 142, 96}, {64, 92, 60}, {14, 28, 16}, {214, 255, 190}},     /* green */
    {{134, 94, 64}, {86, 58, 38}, {22, 13, 7}, {255, 205, 155}},       /* brown */
    {{222, 218, 208}, {168, 164, 156}, {22, 22, 26}, {120, 190, 255}}, /* white porcelain, black glaze */
};
static const double SHADE[TG_NS] = {1.00, 0.93, 1.08, 0.88, 0.97, 0.84, 1.05};
static const Rgb BG = {18, 16, 14}, MARBLE = {196, 190, 178}, MARBLE_EDGE = {120, 114, 104};

/* ---- geometry buffer ---- */
static SDL_Vertex *vb;
static int *ib;
static int nv, ni, vcap, icap;

static void vb_reset(void) { nv = ni = 0; }
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
/* convex polygon as a fan around (cx,cy) */
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
    double x[28], y[28];
    for (int i = 0; i < 28; i++) { x[i] = cx + r * cos(i * 2 * M_PI / 28); y[i] = cy + r * sin(i * 2 * M_PI / 28); }
    fan(x, y, 28, cx, cy, centre, rim);
}
/* thick line through points i0..i1-1 */
static void ribbon(const double *x, const double *y, int i0, int i1, double w, SDL_Color c)
{
    int n = i1 - i0;
    if (n < 2) return;
    vb_need(2 * n, 6 * (n - 1));
    int b = nv;
    for (int i = i0; i < i1; i++) {
        int p = i > i0 ? i - 1 : i, q = i < i1 - 1 ? i + 1 : i;
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
    vb_reset();
}

/* ---- the puzzle being shown ----
 *
 * Every tile has its groove on it before it is set down. Two kinds of round:
 *
 *  - The strict game, on a small Decagon: seven moulds with one fixed groove each.
 *    The real search is shown, tiles being tried and lifted again until every
 *    groove meets its neighbour.
 *  - A large Decagon, where a mould may carry its groove between any two sides.
 *    Tiles go down one after another along the curve, each continuing the groove
 *    of the last. When the curve is boxed in, tiles are lifted and others with
 *    different grooves put in their place, which sends the curve off elsewhere.
 */
enum { ST_ROUTE, ST_SEARCH, ST_PULSE, ST_HOLD, ST_LIFT };
static int state, layer, rotk, round_no;
static double st_time;         /* seconds in the current state */
static double now_t;           /* seconds since the show began */
static double rate, acc;       /* visible moves per second */
static long steps;
static double t_laid[TG_MAXT + 1];             /* when each tile last went down */
static int last_in[TG_MAXT + 1], last_out[TG_MAXT + 1];
static int *p_order, *p_in, *p_out, p_len; /* the finished groove */
static uint64_t seed_state;

/* the one groove layout for which the strict game can be finished (side 2) */
static const int STRICT_A[TG_NS] = {0, 1, 0, 0, 2, 0, 0}, STRICT_B[TG_NS] = {1, 2, 1, 2, 5, 3, 5};

static uint64_t next_rand(void)
{
    seed_state ^= seed_state << 13; seed_state ^= seed_state >> 7; seed_state ^= seed_state << 17;
    return seed_state;
}
static double frand(void) { return (next_rand() >> 11) * (1.0 / 9007199254740992.0); }

static void new_round(int fresh)
{
    static int side = 4, strict = 0;
    if (fresh) {
        strict = opt_strict == 1 || (opt_strict < 0 && round_no % 3 == 0);
        side = opt_min + (int)(next_rand() % (uint64_t)(opt_max - opt_min + 1));
        round_no++;
    }
    for (int i = 0; i <= TG_MAXT; i++) { t_laid[i] = now_t; last_in[i] = last_out[i] = -2; }
    acc = 0; steps = 0; st_time = 0;
    if (strict) {
        tg_strict = 1;
        for (int s = 0; s < TG_NS; s++) { tg_ga[s] = STRICT_A[s]; tg_gb[s] = STRICT_B[s]; tg_fixed[s] = 1; }
        tg_init(2, next_rand());
        rate = 7 * opt_speed;
        state = ST_SEARCH;
        return;
    }
    tg_strict = 0;
    tg_init(side, next_rand());
    double rich = 0.35 + 0.65 * frand(), pent = frand() < 0.25 ? 0 : 0.2 + 0.7 * frand();
    if (!tg_pave(rich, pent)) { tg_init(side, next_rand()); tg_pave(0.5, 0); }
    tg_path_begin();
    double t = tg_nt / 14.0;
    if (t < 12) t = 12;
    if (t > 32) t = 32;
    rate = tg_nt / t * opt_speed;
    state = ST_ROUTE;
}

static void solved(void)
{
    free(p_order);
    p_order = malloc(sizeof(int) * (tg_nt + 1) * 3);
    p_in = p_order + tg_nt + 1; p_out = p_in + tg_nt + 1;
    p_len = tg_path(p_order, p_in, p_out);
    state = ST_PULSE; st_time = 0;
}

static void update(double dt)
{
    st_time += dt;
    now_t += dt;
    switch (state) {
    case ST_ROUTE: {
        /* a watchable pace, quickening if it drags on */
        acc += rate * (st_time > 40 ? 1 + (st_time - 40) * 0.5 : 1) * dt;
        while (acc >= 1 && state == ST_ROUTE) {
            acc -= 1;
            int r = tg_path_step();
            if (r == TG_SOLVED) { solved(); break; }
            if (++steps > 400L * tg_nt + 20000) { state = ST_LIFT; st_time = 0; } /* this paving has no way through */
        }
        if (state != ST_ROUTE) break;
        /* tiles that are new, or were swapped for one with another groove, drop in afresh */
        for (int i = 0; i < tg_plen; i++) {
            int id = tg_porder[i], a, b;
            tg_path_sides(i, &a, &b);
            if (a != last_in[id] || (last_out[id] >= 0 && b != last_out[id])) t_laid[id] = now_t;
            last_in[id] = a; last_out[id] = b;
        }
        break;
    }
    case ST_SEARCH:
        acc += rate * (st_time > 60 ? 1 + (st_time - 60) * 0.3 : 1) * dt;
        while (acc >= 1 && state == ST_SEARCH) {
            acc -= 1;
            /* run on to the next tile laid or lifted */
            for (int guard = 0; guard < 100000; guard++) {
                int r = tg_step();
                if (r == TG_PLACE) { t_laid[tg_nt - 1] = now_t; break; }
                if (r == TG_REMOVE) break;
                if (r == TG_SOLVED) { t_laid[tg_nt - 1] = now_t; solved(); break; }
                if (r == TG_EXHAUSTED) { state = ST_LIFT; st_time = 0; break; }
            }
        }
        break;
    case ST_PULSE:
        if (st_time > 6.0 / opt_speed) { state = ST_HOLD; st_time = 0; }
        break;
    case ST_HOLD:
        break; /* main loop snapshots the picture and starts the next layer */
    case ST_LIFT:
        if (st_time > 2.5) new_round(0);
        break;
    }
}

/* ---- drawing ---- */
typedef struct {
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *scene, *prev;
    int w, h;       /* scene size in pixels */
    int have_prev;
} View;

static double vx0, vy0, vscale, vcos, vsin, dcx, dcy;

static inline void map(double x, double y, double *ox, double *oy)
{
    double a = x - dcx, b = y - dcy;
    *ox = vx0 + (a * vcos - b * vsin) * vscale;
    *oy = vy0 - (a * vsin + b * vcos) * vscale;
}
static void set_view(const View *v, int k)
{
    double R = tg_side / (2 * sin(M_PI / 10));
    dcx = tg_side / 2.0; dcy = tg_side / (2 * tan(M_PI / 10));
    vscale = 0.47 * (v->w < v->h ? v->w : v->h) / R;
    vx0 = v->w / 2.0; vy0 = v->h / 2.0;
    vcos = cos(k * M_PI / 5 + M_PI / 10); vsin = sin(k * M_PI / 5 + M_PI / 10);
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

static void draw_tile(const TgTile *t, int id, const Palette *p, double appear, double alpha)
{
    double x[TG_MAXV], y[TG_MAXV], cx, cy, rin = 1e9;
    for (int j = 0; j < t->n; j++) {
        int k = (j + 1) % t->n;
        double ex = t->x[k] - t->x[j], ey = t->y[k] - t->y[j];
        double d = fabs((t->cx - t->x[j]) * ey - (t->cy - t->y[j]) * ex);
        if (d < rin) rin = d;
    }
    double grow = 1 + 0.35 * (1 - appear) * (1 - appear); /* a tile drops into place */
    double jit = 0.94 + 0.12 * (hash_u((unsigned)id * 2654435761u + (unsigned)layer * 97u) & 1023) / 1023.0;
    double k = SHADE[t->s] * jit;
    map(t->cx, t->cy, &cx, &cy);
    for (int j = 0; j < t->n; j++) {
        map(t->cx + (t->x[j] - t->cx) * grow, t->cy + (t->y[j] - t->cy) * grow, &x[j], &y[j]);
    }
    fan(x, y, t->n, cx, cy, col(p->grout, 1, alpha), col(p->grout, 1, alpha));
    double in = 1 - 0.035 / rin;
    for (int j = 0; j < t->n; j++) { x[j] = cx + (x[j] - cx) * in; y[j] = cy + (y[j] - cy) * in; }
    fan(x, y, t->n, cx, cy, col(p->tile, k * 1.07, alpha), col(p->tile, k * 0.93, alpha));
}

/* groove of one tile: from the middle of side a to the middle of side b (or to the
 * centre if b < 0), leaving each side at right angles so it runs on smoothly */
#define GSEG 10
static int groove_pts(const TgTile *t, int a, int b, double *px, double *py)
{
    int a1 = (a + 1) % t->n;
    double ax = (t->x[a] + t->x[a1]) / 2, ay = (t->y[a] + t->y[a1]) / 2;
    double nax = -(t->y[a1] - t->y[a]), nay = t->x[a1] - t->x[a];
    double bx = t->cx, by = t->cy, nbx = 0, nby = 0;
    if (b >= 0) {
        int b1 = (b + 1) % t->n;
        bx = (t->x[b] + t->x[b1]) / 2; by = (t->y[b] + t->y[b1]) / 2;
        nbx = -(t->y[b1] - t->y[b]); nby = t->x[b1] - t->x[b];
    }
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

static double *gx, *gy;
static int gcap_pts;

static void draw_scene(View *v)
{
    SDL_Renderer *r = v->ren;
    const Palette *p = &PAL[layer % 4];
    set_view(v, rotk);
    SDL_SetRenderDrawColor(r, BG.r, BG.g, BG.b, 255);
    SDL_RenderClear(r);
    if (v->have_prev) SDL_RenderCopy(r, v->prev, NULL, NULL);
    else { draw_marble(); flush(r); }

    /* tiles: only those that have been set down */
    double lift = state == ST_LIFT ? 1 - st_time / 2.0 : 1;
    if (lift < 0) lift = 0;
    int shown = state == ST_ROUTE || (state == ST_LIFT && !tg_strict) ? tg_plen : tg_nt;
    for (int i = 0; i < shown; i++) {
        int id = state == ST_ROUTE || (state == ST_LIFT && !tg_strict) ? tg_porder[i] : i;
        double age = (now_t - t_laid[id]) / 0.3;
        if (age > 1 || state == ST_PULSE || state == ST_HOLD) age = 1;
        if (age < 0.02) age = 0.02;
        draw_tile(&tg_tile[id], id, p, age, state == ST_LIFT ? lift : age);
    }
    flush(r);
    double w = 0.11 * vscale;
    if (w < 2.5) w = 2.5;
    if (state == ST_SEARCH || (state == ST_LIFT && tg_strict)) {
        /* strict game: each tile's own groove, joined or not */
        double qx[GSEG + 1], qy[GSEG + 1];
        for (int id = 0; id < tg_nt; id++) {
            const TgTile *t = &tg_tile[id];
            double al = (now_t - t_laid[id]) / 0.3;
            if (al > 1) al = 1;
            if (state == ST_LIFT) al = lift;
            int np = groove_pts(t, t->a, t->b, qx, qy);
            ribbon(qx, qy, 0, np, w, col(p->groove, 1, al));
            disc(qx[0], qy[0], w * 0.5, col(p->groove, 1, al), col(p->groove, 1, al));
            disc(qx[np - 1], qy[np - 1], w * 0.5, col(p->groove, 1, al), col(p->groove, 1, al));
        }
        flush(r);
    }

    /* groove */
    int len = 0, loose = 0;
    if (state == ST_ROUTE || (state == ST_LIFT && !tg_strict)) { len = tg_plen; loose = 1; }
    else if (state == ST_PULSE || state == ST_HOLD) len = p_len;
    if (len > 0) {
        int need = len * (GSEG + 1) + 4;
        if (need > gcap_pts) { gcap_pts = need * 2; gx = realloc(gx, sizeof(double) * gcap_pts); gy = realloc(gy, sizeof(double) * gcap_pts); }
        int np = 0;
        for (int i = 0; i < len; i++) {
            int id, a, b;
            if (loose) { id = tg_porder[i]; tg_path_sides(i, &a, &b); }
            else { id = p_order[i]; a = p_in[i]; b = p_out[i]; }
            if (np) np--; /* shares its first point with the previous tile's last */
            np += groove_pts(&tg_tile[id], a, b, gx + np, gy + np);
        }
        ribbon(gx, gy, 0, np, w, col(p->groove, 1, lift));
        disc(gx[0], gy[0], w * 0.5, col(p->groove, 1, lift), col(p->groove, 1, lift));
        flush(r);
        if (loose && state == ST_ROUTE) {
            /* the loose end of the groove, feeling its way */
            double pulse = 0.75 + 0.25 * sin(st_time * 7);
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            disc(gx[np - 1], gy[np - 1], w * 2.6 * pulse, col(p->glow, 1, 0.55), col(p->glow, 1, 0));
            disc(gx[np - 1], gy[np - 1], w * 0.8, col(p->glow, 1, 1), col(p->glow, 1, 1));
            flush(r);
        } else if (!loose) {
            /* light runs down the finished groove from the first vertex to the last */
            double u = state == ST_PULSE ? st_time / (4.5 / opt_speed) : 1;
            if (u > 1) u = 1;
            u = u * u * (3 - 2 * u);
            int head = (int)(u * (np - 1));
            double fade = 1;
            if (state == ST_HOLD) fade = 0;
            else if (st_time > 5.0 / opt_speed) fade = 1 - (st_time - 5.0 / opt_speed) / (1.0 / opt_speed);
            if (fade < 0) fade = 0;
            if (fade > 0 && head > 0) {
                ribbon(gx, gy, 0, head + 1, w * 0.55, col(p->glow, 1, 0.9 * fade));
                if (u < 1) {
                    disc(gx[head], gy[head], w * 3.2, col(p->glow, 1, 0.6), col(p->glow, 1, 0));
                    disc(gx[head], gy[head], w * 0.9, col(p->glow, 1.2, 1), col(p->glow, 1, 1));
                }
                flush(r);
            }
        }
    }
    /* the first and last vertices */
    double mx, my, pr = 0.16 * vscale;
    for (int e = 0; e < 2; e++) {
        map(tg_corner_x[e * 5], tg_corner_y[e * 5], &mx, &my);
        double on = e == 0 ? 1 : (state == ST_PULSE && st_time > 4.3 / opt_speed) || state == ST_HOLD ? 1 : 0.35;
        disc(mx, my, pr * 1.8, col(p->glow, 1, 0.35 * on), col(p->glow, 1, 0));
        disc(mx, my, pr * 0.55, col(p->glow, 1, on), col(p->glow, 0.9, on));
    }
    flush(r);
}

static void render(View *v, int snapshot)
{
    if (v->scene) {
        SDL_SetRenderTarget(v->ren, snapshot ? v->prev : v->scene);
        draw_scene(v);
        SDL_SetRenderTarget(v->ren, NULL);
        if (snapshot) { v->have_prev = 1; return; }
        SDL_RenderCopy(v->ren, v->scene, NULL, NULL);
    } else {
        if (snapshot) return;
        draw_scene(v);
    }
    SDL_RenderPresent(v->ren);
}

static int make_view(View *v, int display)
{
    Uint32 flags = SDL_WINDOW_ALLOW_HIGHDPI;
    int x = SDL_WINDOWPOS_CENTERED_DISPLAY(display), w = 900, h = 900;
    if (opt_window) flags |= SDL_WINDOW_RESIZABLE;
    else flags |= SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_BORDERLESS;
    memset(v, 0, sizeof *v);
    v->win = SDL_CreateWindow("Teglon", x, x, w, h, flags);
    if (!v->win) return 0;
    v->ren = SDL_CreateRenderer(v->win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE);
    if (!v->ren) v->ren = SDL_CreateRenderer(v->win, -1, SDL_RENDERER_SOFTWARE);
    if (!v->ren) return 0;
    SDL_SetRenderDrawBlendMode(v->ren, SDL_BLENDMODE_BLEND);
    return 1;
}

/* (re)create the off-screen pictures: drawn at twice the size and scaled down, for smooth edges */
static void size_view(View *v)
{
    int ow, oh;
    SDL_RendererInfo info;
    SDL_GetRendererOutputSize(v->ren, &ow, &oh);
    if (ow < 1 || oh < 1) return;
    int ss = 2;
    if (SDL_GetRendererInfo(v->ren, &info) == 0) {
        if (!(info.flags & SDL_RENDERER_TARGETTEXTURE)) ss = 0;
        else if (info.flags & SDL_RENDERER_SOFTWARE) ss = opt_snapshot ? 1 : 0; /* too slow to do live */
        else if (info.max_texture_width && (ow * 2 > info.max_texture_width || oh * 2 > info.max_texture_height)) ss = 1;
    }
    if (v->scene && v->w == ow * (ss ? ss : 1) && v->h == oh * (ss ? ss : 1)) return;
    if (v->scene) SDL_DestroyTexture(v->scene);
    if (v->prev) SDL_DestroyTexture(v->prev);
    v->scene = v->prev = NULL; v->have_prev = 0;
    v->w = ow; v->h = oh;
    if (!ss) return;
    v->w = ow * ss; v->h = oh * ss;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    v->scene = SDL_CreateTexture(v->ren, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, v->w, v->h);
    v->prev = SDL_CreateTexture(v->ren, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, v->w, v->h);
    if (!v->scene || !v->prev) {
        if (v->scene) SDL_DestroyTexture(v->scene);
        if (v->prev) SDL_DestroyTexture(v->prev);
        v->scene = v->prev = NULL; v->w = ow; v->h = oh;
    }
}

static void start_show(void)
{
    seed_state = opt_seed ? opt_seed * 0x9E3779B97F4A7C15ULL : ((uint64_t)time(NULL) << 20) ^ SDL_GetPerformanceCounter();
    if (!seed_state) seed_state = 1;
    for (int i = 0; i < 6; i++) next_rand();
    layer = (int)(next_rand() % 4);
    rotk = (int)(next_rand() % 10);
    now_t = 0; round_no = 0;
    new_round(1);
}

/* after a solution has been admired: keep it as the floor and start the next */
static int next_layer_due(void) { return state == ST_HOLD && st_time > 9.0 / opt_speed; }
static void next_layer(void)
{
    layer++;
    rotk = (int)(next_rand() % 10);
    new_round(1);
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
    for (double t = 0; t < opt_time; t += 1.0 / 60) {
        update(1.0 / 60);
        if (next_layer_due()) { render(&v, 1); next_layer(); }
    }
    render(&v, 0);
    if (SDL_SaveBMP(surf, opt_snapshot) != 0) { fprintf(stderr, "teglon: %s\n", SDL_GetError()); return 1; }
    return 0;
}

static void usage(void)
{
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Teglon",
        "Teglon screensaver\n\n"
        "  --window        run in a window (Esc or Q quits)\n"
        "  --min N         smallest decagon side, in tile edges (default 3)\n"
        "  --max N         largest decagon side (default 7)\n"
        "  --speed X       animation speed, 1 = normal\n"
        "  --strict        only the strict game: seven moulds, one fixed groove each (side 2)\n"
        "  --no-strict     never the strict game\n"
        "  --seed N        repeatable sequence\n"
        "  --snapshot FILE.bmp [--time SECONDS] [--size WxH]\n"
        "                  write a picture of the screensaver after SECONDS and exit\n", NULL);
}

int main(int argc, char **argv)
{
    int saver = 1;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if ((a[0] == '/' || a[0] == '-') && (a[1] == 's' || a[1] == 'S') && !a[2]) saver = 1; /* Windows: run */
        else if ((a[0] == '/' || a[0] == '-') && (a[1] == 'p' || a[1] == 'P') && (!a[2] || a[2] == ':')) return 0; /* Windows: preview pane */
        else if ((a[0] == '/' || a[0] == '-') && (a[1] == 'c' || a[1] == 'C') && (!a[2] || a[2] == ':')) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Teglon", "The Teglon screensaver has no settings.", NULL);
            return 0;
        }
        else if (!strcmp(a, "--window")) opt_window = 1;
        else if (!strcmp(a, "--strict")) opt_strict = 1;
        else if (!strcmp(a, "--no-strict")) opt_strict = 0;
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
    if (opt_min < 1) opt_min = 1;
    if (opt_max > 16) opt_max = 16;
    if (opt_max < opt_min) opt_max = opt_min;
    if (opt_speed < 0.05) opt_speed = 0.05;
    (void)saver;

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
        for (int i = 0; i < nviews; i++) { size_view(&views[i]); render(&views[i], 0); }
        if (next_layer_due()) {
            for (int i = 0; i < nviews; i++) render(&views[i], 1);
            next_layer();
        }
        SDL_Delay(views[0].scene ? 1 : 12);
    }
    SDL_Quit();
    return 0;
}
