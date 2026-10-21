/*
 * Teglon solver core. See teglon.h.
 *
 * Tiles: the seven convex equilateral polygons whose angles are all multiples of
 * 36 degrees - there are exactly seven (thin and thick rhomb, regular pentagon,
 * two hexagons, an octagon, the regular decagon). Every vertex of an edge-to-edge
 * paving by them is a point of Z[zeta], zeta = exp(i*pi/5), kept as four integers,
 * so the bookkeeping is exact. Floating point is used only for overlap tests.
 *
 * Search: the decagon is paved in a sweep from the first vertex towards the
 * opposite one. At each step the lowest exposed edge is covered by some tile.
 * Each tile has two "doors" (the sides its groove meets) and the rest are walls:
 *   - a door must face a door and a wall a wall;
 *   - the rim is all wall, except one door beside the first vertex and one beside
 *     the last;
 *   - grooves may never close into a loop (tracked with an undoable union-find);
 *   - every exposed edge near the newest tile must still admit some legal tile.
 * When the last tile goes down every door is matched and there are no loops, so
 * the grooves necessarily form one curve from the first vertex to the last.
 */
#include "teglon.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define EPS 1e-7
#define NS TG_NS
#define MAXV TG_MAXV
#define MAXT TG_MAXT

const char *tg_name[NS] = {"thin rhomb", "thick rhomb", "pentagon", "long hexagon", "squat hexagon", "octagon", "decagon", "bow tie"};
const int tg_nside[NS] = {4, 4, 5, 6, 6, 8, 10, 6};
/* exterior turn at each vertex, in units of 36 degrees */
static const int TURN[NS][MAXV] = {
    {4, 1, 4, 1},
    {3, 2, 3, 2},
    {2, 2, 2, 2, 2},
    {3, 1, 1, 3, 1, 1},
    {2, 2, 1, 2, 2, 1},
    {2, 1, 1, 1, 2, 1, 1, 1},
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    {3, 3, -1, 3, 3, -1}, /* bow tie: not convex, two 216 degree corners */
};
static int BD[NS][MAXV]; /* direction of side j in the base orientation */

int tg_ga[NS] = {0, 0, 0, 0, 0, 0, 0, 0};
int tg_gb[NS] = {1, 1, 1, 1, 1, 1, 1, 1};
int tg_fixed[NS] = {1, 1, 1, 1, 1, 1, 1, 1};
/* the seven moulds: the five girih tiles (thick rhomb, pentagon, long hexagon,
 * decagon, bow tie), the thin rhomb that fits the Decagon's corner, and the squat
 * hexagon. The octagon is left out. */
int tg_enabled[NS] = {1, 1, 1, 1, 1, 0, 1, 1};
double tg_bowtie = 0.5;
double tg_weight[NS] = {1, 1, 1, 1, 1, 1, 1, 1};

int tg_strict = 0;
TgTile *tg_tile;
int tg_nt, tg_side;
int *tg_porder, tg_plen;
double tg_corner_x[10], tg_corner_y[10];
long tg_nodes;

#define T tg_tile
#define nt tg_nt

typedef TgPt Pt;
static const int DV[10][4] = {
    {1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}, {-1, 1, -1, 1},
    {-1, 0, 0, 0}, {0, -1, 0, 0}, {0, 0, -1, 0}, {0, 0, 0, -1}, {1, -1, 1, -1},
};
static double BX[4], BY[4], AXX[5], AXY[5];

static inline Pt padd(Pt p, int k) { for (int i = 0; i < 4; i++) p.c[i] += DV[k][i]; return p; }
static inline Pt psub(Pt p, int k) { for (int i = 0; i < 4; i++) p.c[i] -= DV[k][i]; return p; }
static inline int peq(Pt a, Pt b) { return !memcmp(&a, &b, sizeof a); }
static inline double px(Pt p) { return p.c[0] * BX[0] + p.c[1] * BX[1] + p.c[2] * BX[2] + p.c[3] * BX[3]; }
static inline double py(Pt p) { return p.c[0] * BY[0] + p.c[1] * BY[1] + p.c[2] * BY[2] + p.c[3] * BY[3]; }
static inline int m10(int a) { a %= 10; return a < 0 ? a + 10 : a; }
static inline int popc(unsigned v) { int c = 0; for (; v; v &= v - 1) c++; return c; }

/* ---- vertex table ---- */
typedef struct {
    Pt p;
    uint16_t mask;    /* which of the ten 36-degree sectors here are paved */
    uint8_t used;
    int32_t tile[10]; /* tile*16+side of the tile side leaving here in each direction, or -1 */
} VE;
static VE *vt;
static size_t vcap, vcount;
static long frontier; /* number of exposed edges (paved on exactly one side) */

static inline size_t vhash(Pt p)
{
    uint64_t h = 1469598103934665603ULL;
    for (int i = 0; i < 4; i++) { h ^= (uint32_t)p.c[i]; h *= 1099511628211ULL; h ^= h >> 29; }
    return (size_t)h;
}
static VE *vfind(Pt p, int create)
{
    size_t i = vhash(p) & (vcap - 1);
    while (vt[i].used) {
        if (peq(vt[i].p, p)) return &vt[i];
        i = (i + 1) & (vcap - 1);
    }
    if (!create) return NULL;
    vt[i].used = 1; vt[i].p = p; vt[i].mask = 0; vcount++;
    for (int k = 0; k < 10; k++) vt[i].tile[k] = -1;
    return &vt[i];
}
static inline uint16_t vmask(Pt p) { VE *e = vfind(p, 0); return e ? e->mask : 0; }
static inline int trans(uint16_t m)
{
    uint16_t r = ((m << 1) | (m >> 9)) & 0x3ff;
    return popc(~m & r & 0x3ff);
}
static void vset(Pt p, uint16_t bits, int on)
{
    VE *e = vfind(p, 1);
    frontier -= trans(e->mask);
    if (on) e->mask |= bits; else e->mask &= ~bits;
    frontier += trans(e->mask);
}
static inline uint16_t sectors(int from, int n)
{
    uint16_t m = 0;
    for (int i = 0; i < n; i++) m |= 1u << m10(from + i);
    return m;
}
static inline int tile_at(Pt p, int d) { VE *e = vfind(p, 0); return e ? e->tile[d] : -1; }

/* ---- the decagon ---- */
static Pt corner[10];
static Pt *rim; static int *rimd; static int nrim;
static double dec_nx[10], dec_ny[10], dec_c[10];
static double sweep_x, sweep_y;
static int s_used, g_used;

static int inside(double x, double y) /* -1 outside, 0 on the rim, 1 inside */
{
    int r = 1;
    for (int k = 0; k < 10; k++) {
        double d = x * dec_nx[k] + y * dec_ny[k] - dec_c[k];
        if (d < -EPS) return -1;
        if (d < EPS) r = 0;
    }
    return r;
}
/* tile side leaving p in direction d, lying on the rim */
static inline int is_start(Pt p, int d) { return (d == 0 && peq(p, corner[0])) || (d == 9 && peq(p, psub(corner[0], 9))); }
static inline int is_goal(Pt p, int d) { return (d == 5 && peq(p, corner[5])) || (d == 4 && peq(p, psub(corner[5], 4))); }

/* ---- spatial grid ---- */
#define CELL 3.3
#define CELLCAP 64
static int gw, gh; static double gx0, gy0;
static int *gcell, *gcnt;
static inline int gidx(double x, double y)
{
    int i = (int)((x - gx0) / CELL), j = (int)((y - gy0) / CELL);
    if (i < 0) i = 0;
    if (i >= gw) i = gw - 1;
    if (j < 0) j = 0;
    if (j >= gh) j = gh - 1;
    return j * gw + i;
}

static void build(TgTile *t, int s, int rot, Pt v0)
{
    int n = tg_nside[s];
    t->s = s; t->rot = rot; t->n = n; t->v[0] = v0;
    for (int j = 1; j < n; j++) t->v[j] = padd(t->v[j - 1], m10(BD[s][j - 1] + rot));
    t->cx = t->cy = 0;
    for (int j = 0; j < n; j++) {
        t->x[j] = px(t->v[j]); t->y[j] = py(t->v[j]);
        t->cx += t->x[j] / n; t->cy += t->y[j] / n;
    }
    /* extents along the five edge normals; a bow tie is two convex halves joined at its waist */
    static const int HALF_A[4] = {5, 0, 1, 2}, HALF_B[4] = {2, 3, 4, 5};
    int bow = (s == 7);
    for (int a = 0; a < 5; a++) {
        t->lo[a] = t->lo2[a] = 1e30; t->hi[a] = t->hi2[a] = -1e30;
        for (int q = 0; q < (bow ? 4 : n); q++) {
            int j = bow ? HALF_A[q] : q;
            double p = t->x[j] * AXX[a] + t->y[j] * AXY[a];
            if (p < t->lo[a]) t->lo[a] = p;
            if (p > t->hi[a]) t->hi[a] = p;
        }
        for (int q = 0; bow && q < 4; q++) {
            int j = HALF_B[q];
            double p = t->x[j] * AXX[a] + t->y[j] * AXY[a];
            if (p < t->lo2[a]) t->lo2[a] = p;
            if (p > t->hi2[a]) t->hi2[a] = p;
        }
    }
}
static inline int tdir(const TgTile *t, int j) { return m10(BD[t->s][j] + t->rot); }

static int tjunction(const TgTile *a, const TgTile *b) /* vertex of a inside an edge of b */
{
    for (int i = 0; i < a->n; i++)
        for (int j = 0; j < b->n; j++) {
            int k = (j + 1) % b->n;
            double ex = b->x[k] - b->x[j], ey = b->y[k] - b->y[j];
            double qx = a->x[i] - b->x[j], qy = a->y[i] - b->y[j];
            if (fabs(qx * ey - qy * ex) > 1e-6) continue;
            double u = qx * ex + qy * ey;
            if (u > 1e-6 && u < 1 - 1e-6) return 1;
        }
    return 0;
}
static int overlap1(const double *lo, const double *hi, const double *lo2, const double *hi2)
{
    int over = 1;
    for (int a = 0; a < 5; a++) {
        double ov = fmin(hi[a], hi2[a]) - fmax(lo[a], lo2[a]);
        if (ov < -EPS) return 0;
        if (ov < EPS) over = 0;
    }
    return over ? 2 : 1;
}
static int overlap(const TgTile *t, const TgTile *o) /* 2 overlap, 1 touching, 0 apart */
{
    int r = overlap1(t->lo, t->hi, o->lo, o->hi), q;
    if (t->s == 7 && (q = overlap1(t->lo2, t->hi2, o->lo, o->hi)) > r) r = q;
    if (o->s == 7 && (q = overlap1(t->lo, t->hi, o->lo2, o->hi2)) > r) r = q;
    if (t->s == 7 && o->s == 7 && (q = overlap1(t->lo2, t->hi2, o->lo2, o->hi2)) > r) r = q;
    return r;
}

/* does the tile fit: inside the decagon, on free ground, edge to edge */
static int geom_ok(const TgTile *t)
{
    for (int j = 0; j < t->n; j++) {
        VE *e = vfind(t->v[j], 0);
        if (e && e->mask) {
            if (e->mask & sectors(tdir(t, j), 5 - TURN[t->s][j])) return 0;
        } else if (inside(t->x[j], t->y[j]) != 1) return 0;
    }
    int ci = (int)((t->cx - gx0) / CELL), cj = (int)((t->cy - gy0) / CELL);
    for (int dj = -1; dj <= 1; dj++)
        for (int di = -1; di <= 1; di++) {
            int i = ci + di, j = cj + dj;
            if (i < 0 || j < 0 || i >= gw || j >= gh) continue;
            int c = j * gw + i;
            for (int k = 0; k < gcnt[c]; k++) {
                const TgTile *o = &T[gcell[c * CELLCAP + k]];
                int r = overlap(t, o);
                if (r == 2) return 0;
                if (r == 1 && (tjunction(t, o) || tjunction(o, t))) return 0;
            }
        }
    return 1;
}

/* is the far side of side j of t already paved, or outside the decagon? */
static inline int sealed(const TgTile *t, int j) { return (vmask(t->v[j]) >> m10(tdir(t, j) - 1)) & 1; }
static inline int exposed(Pt u, int j)
{
    uint16_t m = vmask(u);
    return !((m >> j) & 1) && ((m >> m10(j - 1)) & 1);
}

/* ---- union-find over groove pieces, with undo ---- */
#define NODE_S MAXT
#define NODE_G (MAXT + 1)
static int uf_par[MAXT + 2], uf_rank[MAXT + 2];
static int uf_log[2 * MAXT + 8][2], uf_nlog;
static int uf_find(int a) { while (uf_par[a] != a) a = uf_par[a]; return a; }
static void uf_union(int a, int b)
{
    a = uf_find(a); b = uf_find(b);
    if (uf_rank[a] < uf_rank[b]) { int c = a; a = b; b = c; }
    uf_par[b] = a;
    uf_log[uf_nlog][0] = b; uf_log[uf_nlog][1] = (uf_rank[a] == uf_rank[b]);
    if (uf_log[uf_nlog][1]) uf_rank[a]++;
    uf_nlog++;
}
static void uf_undo(void)
{
    uf_nlog--;
    int b = uf_log[uf_nlog][0], a = uf_par[b];
    if (uf_log[uf_nlog][1]) uf_rank[a]--;
    uf_par[b] = b;
}

/* what the groove end on side k of t (not yet placed) would attach to: a union-find
 * node, -1 for open ground, -2 if illegal */
static int door_target(const TgTile *t, int k)
{
    if (!sealed(t, k)) return -1;
    int d = tdir(t, k);
    int nb = tile_at(t->v[(k + 1) % t->n], m10(d + 5));
    if (nb >= 0) {
        const TgTile *o = &T[nb >> 4];
        int side = nb & 15;
        return (side == o->a || side == o->b) ? (nb >> 4) : -2;
    }
    if (is_start(t->v[k], d)) return s_used ? -2 : NODE_S;
    if (is_goal(t->v[k], d)) return g_used ? -2 : NODE_G;
    return -2;
}
/* a wall on side k must not face a door */
static int wall_ok(const TgTile *t, int k)
{
    if (!sealed(t, k)) return 1;
    int nb = tile_at(t->v[(k + 1) % t->n], m10(tdir(t, k) + 5));
    if (nb < 0) return 1;
    const TgTile *o = &T[nb >> 4];
    int side = nb & 15;
    return !(side == o->a || side == o->b);
}
/* doors face doors, walls face walls, and the groove does not close a loop */
static int doors_ok(const TgTile *t)
{
    int ra = door_target(t, t->a), rb = door_target(t, t->b);
    if (ra == -2 || rb == -2) return 0;
    if (ra >= 0 && rb >= 0 && uf_find(ra) == uf_find(rb)) return 0;
    for (int k = 0; k < t->n; k++)
        if (k != t->a && k != t->b && !wall_ok(t, k)) return 0;
    return 1;
}
/* for a mould whose groove is not chosen yet: could any groove work here? */
static int doors_possible(const TgTile *t)
{
    int need = 0, opt = 0;
    for (int k = 0; k < t->n; k++) {
        int w = wall_ok(t, k), d = door_target(t, k) != -2;
        if (!w && !d) return 0;
        if (!w) need++; else if (d) opt++;
    }
    return need <= 2 && need + opt >= 2;
}

static void place(const TgTile *src)
{
    int id = nt++;
    TgTile *t = &T[id];
    *t = *src;
    t->open = t->n; t->nundo = 0; t->used_s = t->used_g = 0;
    t->chose = tg_strict && !tg_fixed[t->s];
    if (t->chose) { tg_ga[t->s] = t->a; tg_gb[t->s] = t->b; tg_fixed[t->s] = 1; }
    int g[2] = {t->a, t->b};
    for (int q = 0; q < 2 && tg_strict; q++) {
        int r = door_target(t, g[q]);
        if (r < 0) continue;
        if (r == NODE_S) s_used = t->used_s = 1;
        if (r == NODE_G) g_used = t->used_g = 1;
        uf_union(id, r); t->nundo++;
    }
    for (int k = 0; k < t->n; k++) {
        if (!sealed(t, k)) continue;
        t->open--;
        int nb = tile_at(t->v[(k + 1) % t->n], m10(tdir(t, k) + 5));
        if (nb >= 0) T[nb >> 4].open--;
    }
    for (int j = 0; j < t->n; j++) {
        vset(t->v[j], sectors(tdir(t, j), 5 - TURN[t->s][j]), 1);
        vfind(t->v[j], 0)->tile[tdir(t, j)] = id * 16 + j;
    }
    int c = gidx(t->cx, t->cy);
    if (gcnt[c] >= CELLCAP) { fprintf(stderr, "teglon: grid cell overflow\n"); exit(2); }
    gcell[c * CELLCAP + gcnt[c]++] = id;
}
static void unplace(void)
{
    int id = --nt;
    TgTile *t = &T[id];
    gcnt[gidx(t->cx, t->cy)]--;
    for (int j = 0; j < t->n; j++) {
        vfind(t->v[j], 0)->tile[tdir(t, j)] = -1;
        vset(t->v[j], sectors(tdir(t, j), 5 - TURN[t->s][j]), 0);
    }
    for (int k = 0; k < t->n; k++) {
        if (!sealed(t, k)) continue;
        int nb = tile_at(t->v[(k + 1) % t->n], m10(tdir(t, k) + 5));
        if (nb >= 0) T[nb >> 4].open++;
    }
    while (t->nundo--) uf_undo();
    uf_par[id] = id; uf_rank[id] = 0;
    if (t->used_s) s_used = 0;
    if (t->used_g) g_used = 0;
    if (t->chose) tg_fixed[t->s] = 0;
}

/* can some legal tile still cover the exposed edge u -> u+d_j (open ground on its left)? */
static int has_candidate(Pt u, int j)
{
    TgTile t;
    for (int s = 0; s < NS; s++) {
        if (!tg_enabled[s]) continue;
        for (int i = 0; i < tg_nside[s]; i++) {
            int rot = m10(j - BD[s][i]);
            Pt v0 = u;
            for (int m = i - 1; m >= 0; m--) v0 = psub(v0, m10(BD[s][m] + rot));
            build(&t, s, rot, v0);
            if (!geom_ok(&t)) continue;
            if (!tg_strict) return 1;
            if (tg_fixed[s]) {
                t.a = tg_ga[s]; t.b = tg_gb[s];
                if (doors_ok(&t)) return 1;
            } else if (doors_possible(&t)) return 1;
        }
    }
    return 0;
}
static int forward_ok(const TgTile *t)
{
    for (int k = 0; k < t->n; k++) {
        Pt w = t->v[k];
        uint16_t m = vmask(w);
        for (int i = 0; i < 10; i++) {
            if ((m >> i) & 1) continue;
            if ((m >> m10(i - 1)) & 1)
                if (!has_candidate(w, i)) return 0;
            if ((m >> m10(i + 1)) & 1)
                if (!has_candidate(padd(w, m10(i + 1)), m10(i + 6))) return 0;
        }
    }
    return 1;
}

static int *ppos, *padj, pstart_side; /* groove routing state, see below */
static char *pgoal;

/* ---- search ---- */
static uint64_t rng = 88172645463325252ULL;
static inline uint64_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }
static inline double rnd01(void) { return ((rnd() >> 11) + 1) * (1.0 / 9007199254740993.0); }
void tg_reseed(uint64_t seed)
{
    rng = 88172645463325252ULL ^ (seed * 0x9E3779B97F4A7C15ULL);
    if (!rng) rng = 1;
    for (int i = 0; i < 8; i++) rnd();
}

typedef struct { int8_t s, rot, a, b; Pt v0; Pt da, db; int dda, ddb; double cx, cy, key; } Cand;
typedef struct { Cand *c; int nc, i; } Level;
static Level *lev;
static int base; /* tiles below this index are not touched by the search */

/* the exposed edge nearest the first vertex along the sweep */
static int select_edge(Pt *ou, int *oj)
{
    double best = 1e30; int found = 0;
    for (int id = 0; id < nt; id++) {
        const TgTile *t = &T[id];
        if (!t->open) continue;
        for (int k = 0; k < t->n; k++) {
            int k1 = (k + 1) % t->n;
            double key = (t->x[k] + t->x[k1]) * sweep_x + (t->y[k] + t->y[k1]) * sweep_y;
            if (key >= best || sealed(t, k)) continue;
            best = key; *ou = t->v[k1]; *oj = m10(tdir(t, k) + 5); found = 1;
        }
    }
    for (int r = 0; r < nrim; r++) {
        Pt q = padd(rim[r], rimd[r]);
        double key = (px(rim[r]) + px(q)) * sweep_x + (py(rim[r]) + py(q)) * sweep_y;
        if (key >= best - 1e-9 || !exposed(rim[r], rimd[r])) continue;
        best = key; *ou = rim[r]; *oj = rimd[r]; found = 1;
    }
    return found;
}

static void gen(Level *L)
{
    Pt u = {{0, 0, 0, 0}}; int j = 0;
    L->nc = L->i = 0;
    L->c = NULL;
    if (!select_edge(&u, &j)) { L->c = malloc(sizeof(Cand)); return; }
    int cap = 64;
    L->c = malloc(sizeof(Cand) * cap);
    TgTile t;
    for (int s = 0; s < NS; s++) {
        if (!tg_enabled[s]) continue;
        int n = tg_nside[s];
        for (int i = 0; i < n; i++) {
            int rot = m10(j - BD[s][i]);
            Pt v0 = u;
            for (int m = i - 1; m >= 0; m--) v0 = psub(v0, m10(BD[s][m] + rot));
            build(&t, s, rot, v0);
            if (!geom_ok(&t)) continue;
            for (int a = 0; a < n; a++)
                for (int b = a + 1; b < n; b++) {
                    if (!tg_strict && (a || b != 1)) continue; /* grooves are routed afterwards */
                    if (tg_strict && tg_fixed[s] && !(a == tg_ga[s] && b == tg_gb[s]) && !(a == tg_gb[s] && b == tg_ga[s])) continue;
                    t.a = a; t.b = b;
                    if (tg_strict && !doors_ok(&t)) continue;
                    Cand c = {(int8_t)s, (int8_t)rot, (int8_t)a, (int8_t)b, v0, t.v[a], t.v[b], tdir(&t, a), tdir(&t, b), t.cx, t.cy, 0};
                    int dup = 0; /* symmetric tiles give the same placement more than once */
                    for (int q = 0; q < L->nc && !dup; q++) {
                        Cand *o = &L->c[q];
                        if (o->s != s || fabs(o->cx - c.cx) > EPS || fabs(o->cy - c.cy) > EPS) continue;
                        if (!tg_strict ||
                            (o->dda == c.dda && peq(o->da, c.da) && o->ddb == c.ddb && peq(o->db, c.db)) ||
                            (o->dda == c.ddb && peq(o->da, c.db) && o->ddb == c.dda && peq(o->db, c.da)))
                            dup = 1;
                    }
                    if (dup) continue;
                    c.key = -log(rnd01()) / (tg_weight[s] > 1e-9 ? tg_weight[s] : 1e-9);
                    if (L->nc == cap) { cap *= 2; L->c = realloc(L->c, sizeof(Cand) * cap); }
                    L->c[L->nc++] = c;
                }
        }
    }
    for (int a = 1; a < L->nc; a++) {
        Cand k = L->c[a]; int b = a - 1;
        while (b >= 0 && L->c[b].key > k.key) { L->c[b + 1] = L->c[b]; b--; }
        L->c[b + 1] = k;
    }
}

static void rehash(void)
{
    VE *old = vt; size_t oc = vcap;
    vt = calloc(vcap, sizeof *vt); vcount = 0;
    for (size_t i = 0; i < oc; i++)
        if (old[i].used && old[i].mask) { VE *e = vfind(old[i].p, 1); *e = old[i]; }
    free(old);
}

int tg_step(void)
{
    Level *L = &lev[nt];
    if (!L->c) {
        if (vcount > vcap / 2) rehash();
        gen(L);
    }
    if (L->i >= L->nc) {
        free(L->c); L->c = NULL;
        if (nt == base) return TG_EXHAUSTED;
        unplace();
        return TG_REMOVE;
    }
    Cand *c = &L->c[L->i++];
    tg_nodes++;
    TgTile t;
    build(&t, c->s, c->rot, c->v0);
    t.a = c->a; t.b = c->b;
    place(&t);
    TgTile *p = &T[nt - 1];
    if (!tg_strict) {
        if (frontier == 0) return TG_TILED;
        if (forward_ok(p)) return TG_PLACE;
        unplace();
        return TG_NONE;
    }
    int joined = uf_find(NODE_S) == uf_find(NODE_G);
    if (frontier == 0) {
        if (joined) return TG_SOLVED;
    } else if (!joined &&
               (s_used || exposed(corner[0], 0) || exposed(psub(corner[0], 9), 9)) &&
               (g_used || exposed(corner[5], 5) || exposed(psub(corner[5], 4), 4)) &&
               forward_ok(p)) {
        return TG_PLACE;
    }
    unplace();
    return TG_NONE;
}

void tg_reset(void)
{
    while (nt) unplace();
    for (int i = 0; i <= MAXT; i++)
        if (lev[i].c) { free(lev[i].c); lev[i].c = NULL; }
}

void tg_init(int side, uint64_t seed)
{
    tg_free();
    tg_side = side;
    for (int i = 0; i < 4; i++) { BX[i] = cos(i * M_PI / 5); BY[i] = sin(i * M_PI / 5); }
    for (int a = 0; a < 5; a++) { AXX[a] = -sin(a * M_PI / 5); AXY[a] = cos(a * M_PI / 5); }
    for (int s = 0; s < NS; s++) {
        BD[s][0] = 0;
        for (int j = 1; j < tg_nside[s]; j++) BD[s][j] = m10(BD[s][j - 1] + TURN[s][j]);
    }
    T = calloc(MAXT + 1, sizeof *T);
    lev = calloc(MAXT + 1, sizeof *lev);
    nt = 0; frontier = 0; s_used = g_used = 0; uf_nlog = 0; tg_nodes = 0;
    for (int i = 0; i < MAXT + 2; i++) { uf_par[i] = i; uf_rank[i] = 0; }
    vcap = 1 << 16;
    while (vcap < (size_t)side * side * 128) vcap <<= 1;
    vt = calloc(vcap, sizeof *vt); vcount = 0;
    nrim = 10 * side;
    rim = malloc(sizeof(Pt) * nrim); rimd = malloc(sizeof(int) * nrim);
    Pt p = {{0, 0, 0, 0}};
    int r = 0;
    for (int k = 0; k < 10; k++) {
        corner[k] = p;
        tg_corner_x[k] = px(p); tg_corner_y[k] = py(p);
        dec_nx[k] = -sin(k * M_PI / 5); dec_ny[k] = cos(k * M_PI / 5);
        dec_c[k] = px(p) * dec_nx[k] + py(p) * dec_ny[k];
        for (int i = 0; i < side; i++) {
            /* ground outside the decagon counts as paved: 6 sectors at a corner, 5 along a side */
            if (i == 0) vset(p, sectors(k + 4, 6), 1); else vset(p, sectors(k + 5, 5), 1);
            rim[r] = p; rimd[r] = k; r++;
            p = padd(p, k);
        }
    }
    double dx = tg_corner_x[5] - tg_corner_x[0], dy = tg_corner_y[5] - tg_corner_y[0], dl = hypot(dx, dy);
    sweep_x = dx / dl; sweep_y = dy / dl;
    double R = side / (2 * sin(M_PI / 10));
    gx0 = side / 2.0 - R - 1; gy0 = -1;
    gw = (int)((2 * R + 2) / CELL) + 2; gh = gw;
    gcell = malloc(sizeof(int) * gw * gh * CELLCAP);
    gcnt = calloc(gw * gh, sizeof(int));
    tg_reseed(seed);
}

void tg_free(void)
{
    if (!T) return;
    for (int i = 0; i <= MAXT; i++) free(lev[i].c);
    free(lev); free(T); free(vt); free(rim); free(rimd); free(gcell); free(gcnt);
    free(ppos); free(padj); free(pgoal); free(tg_porder);
    ppos = padj = NULL; pgoal = NULL; tg_porder = NULL;
    lev = NULL; T = NULL; vt = NULL; rim = NULL; rimd = NULL; gcell = NULL; gcnt = NULL;
    nt = 0;
}

/* ---- paving (tg_strict == 0) ----
 *
 * The paving is the dual of a pentagrid (de Bruijn): five families of parallel
 * lines, one family square to each edge direction, side-many lines in each. Every
 * point where lines cross becomes a tile whose sides are the edge directions of
 * the lines through it: two lines give a rhomb, three a hexagon, four an octagon,
 * five a decagon. Any choice of line positions gives a paving of the whole decagon.
 * To get plenty of the larger tiles, most line positions are drawn from a
 * quasicrystal in Z[tau], which makes many lines pass through common points. Pentagons come from splitting some octagons into two pentagons and two
 * thin rhombs.
 */
static void take_out(int id)
{
    TgTile t = T[id];
    int c = gidx(t.cx, t.cy);
    for (int k = 0; k < gcnt[c]; k++)
        if (gcell[c * CELLCAP + k] == id) { gcell[c * CELLCAP + k] = gcell[c * CELLCAP + --gcnt[c]]; break; }
    for (int j = 0; j < t.n; j++) {
        vfind(t.v[j], 0)->tile[tdir(&t, j)] = -1;
        vset(t.v[j], sectors(tdir(&t, j), 5 - TURN[t.s][j]), 0);
    }
    for (int k = 0; k < t.n; k++) {
        if (!sealed(&t, k)) continue;
        int nb = tile_at(t.v[(k + 1) % t.n], m10(tdir(&t, k) + 5));
        if (nb >= 0) T[nb >> 4].open++;
    }
    int last = --nt;
    if (id == last) return;
    T[id] = T[last];
    TgTile *m = &T[id];
    for (int j = 0; j < m->n; j++) vfind(m->v[j], 0)->tile[tdir(m, j)] = id * 16 + j;
    c = gidx(m->cx, m->cy);
    for (int k = 0; k < gcnt[c]; k++)
        if (gcell[c * CELLCAP + k] == last) { gcell[c * CELLCAP + k] = id; break; }
}

/* lay mould s with its vertex k at p and side k leaving in direction dir */
static int lay(int s, int k, Pt p, int dir)
{
    TgTile t;
    int rot = m10(dir - BD[s][k]);
    for (int m = k - 1; m >= 0; m--) p = psub(p, m10(BD[s][m] + rot));
    build(&t, s, rot, p);
    t.a = t.b = -1;
    if (nt >= MAXT || !geom_ok(&t)) return 0;
    place(&t);
    return 1;
}

typedef struct { double x, y; int fam; } Cross;
static int cmp_dbl(const void *a, const void *b) { double d = *(const double *)a - *(const double *)b; return d < 0 ? -1 : d > 0; }

static int pave_once(double rich, double pent)
{
    int n = tg_side;
    const double tau = (1 + sqrt(5)) / 2;
    /* pool of line positions: numbers a + b*tau whose conjugate a + b*tau' is small
     * (a one-dimensional quasicrystal), nearest zero first */
    int R = 3 * n + 12, np = 0;
    double *pool = malloc(sizeof(double) * (2 * R + 1) * 4);
    for (int b = -R; b <= R; b++)
        for (int a = (int)floor(-b * (1 - tau) - 1); a <= (int)ceil(-b * (1 - tau) + 1); a++)
            if (fabs(a + b * (1 - tau)) <= 1.0) pool[np++] = a + b * tau;
    for (int i = 1; i < np; i++) {
        double v = pool[i]; int j = i - 1;
        while (j >= 0 && fabs(pool[j]) > fabs(v)) { pool[j + 1] = pool[j]; j--; }
        pool[j + 1] = v;
    }
    int use = n + (int)((1 - rich) * 1.2 * n + 0.5);
    if (use < np) np = use;
    double span = fabs(pool[np - 1]) + 0.5;
    double *off = malloc(sizeof(double) * 5 * n);
    char *taken = malloc(np);
    for (int k = 0; k < 5; k++) {
        memset(taken, 0, np);
        for (int i = 0; i < n; i++) {
            double v;
            if (rnd01() >= 0.25 * (1 - rich)) {
                int q;
                do q = (int)(rnd() % np); while (taken[q]);
                taken[q] = 1; v = pool[q];
            } else v = (2 * rnd01() - 1) * span;
            off[k * n + i] = v;
        }
        qsort(off + k * n, n, sizeof(double), cmp_dbl);
        for (int i = 1; i < n; i++) /* parallel lines must stay apart */
            if (off[k * n + i] - off[k * n + i - 1] < 1e-4) { free(pool); free(off); free(taken); return 0; }
    }
    free(pool); free(taken);
    /* crossing points */
    int cap = 10 * n * n + 8, nc = 0;
    Cross *cr = malloc(sizeof(Cross) * cap);
    for (int k1 = 0; k1 < 5; k1++)
        for (int k2 = k1 + 1; k2 < 5; k2++) {
            double a1 = k1 * M_PI / 5, a2 = k2 * M_PI / 5, det = sin(a2 - a1);
            for (int i = 0; i < n; i++)
                for (int j = 0; j < n; j++) {
                    double c1 = off[k1 * n + i], c2 = off[k2 * n + j];
                    double x = (c1 * sin(a2) - c2 * sin(a1)) / det, y = (c2 * cos(a1) - c1 * cos(a2)) / det;
                    int q;
                    for (q = 0; q < nc; q++)
                        if (fabs(cr[q].x - x) < 1e-7 && fabs(cr[q].y - y) < 1e-7) break;
                    if (q == nc) { cr[nc].x = x; cr[nc].y = y; cr[nc].fam = 0; nc++; }
                    cr[q].fam |= (1 << k1) | (1 << k2);
                }
        }
    int ok = 1;
    for (int q = 0; q < nc && ok; q++) {
        Pt p = corner[0];
        int dirs[5], m = 0;
        for (int k = 0; k < 5; k++) {
            double pr = cr[q].x * cos(k * M_PI / 5) + cr[q].y * sin(k * M_PI / 5);
            for (int i = 0; i < n; i++)
                if (off[k * n + i] < pr - 1e-7) p = padd(p, k);
            if (cr[q].fam >> k & 1) dirs[m++] = k;
        }
        /* the zonogon with edge directions dirs[], lowest vertex at p */
        int tt[MAXV];
        for (int j = 0; j < 2 * m; j++) {
            int d = dirs[j % m] + (j >= m ? 5 : 0), dp = dirs[(j + m - 1) % m] + ((j + 2 * m - 1) % (2 * m) >= m ? 5 : 0);
            tt[j] = m10(d - dp);
        }
        int done = 0;
        for (int s = 0; s < NS && !done; s++) {
            if (tg_nside[s] != 2 * m) continue;
            for (int k = 0; k < 2 * m && !done; k++) {
                int same = 1;
                for (int j = 0; j < 2 * m; j++) if (TURN[s][(k + j) % (2 * m)] != tt[j]) same = 0;
                if (same) { done = 1; ok = lay(s, k, p, dirs[0]); }
            }
        }
        if (!done) ok = 0;
    }
    free(cr); free(off);
    if (!ok || frontier != 0) return 0;
    /* split octagons into two pentagons and two thin rhombs: all of them if the
     * octagon is not one of the moulds */
    if (!tg_enabled[5]) pent = 2;
    for (int id = nt - 1; id >= 0; id--) {
        if (T[id].s != 5 || rnd01() >= pent || nt + 3 >= MAXT) continue;
        TgTile o = T[id];
        take_out(id);
        if (!(lay(2, 0, o.v[0], tdir(&o, 0)) && lay(2, 0, o.v[4], tdir(&o, 4)) &&
              lay(0, 1, o.v[2], tdir(&o, 2)) && lay(0, 1, o.v[6], tdir(&o, 6)))) return 0;
    }
    /* a squat hexagon is also a bow tie with a thin rhomb in each notch */
    for (int id = nt - 1; id >= 0 && tg_enabled[7]; id--) {
        if (T[id].s != 4 || rnd01() >= (tg_enabled[4] ? tg_bowtie : 2) || nt + 2 >= MAXT) continue;
        TgTile o = T[id];
        take_out(id);
        if (!(lay(7, 0, o.v[0], tdir(&o, 0)) && lay(0, 1, o.v[2], tdir(&o, 2)) && lay(0, 1, o.v[5], tdir(&o, 5)))) return 0;
    }
    return frontier == 0;
}

int tg_corner_tile(int far)
{
    if (far) { int a = tile_at(corner[5], 5); return a < 0 ? -1 : a >> 4; }
    int a = tile_at(corner[0], 0), b = tile_at(psub(corner[0], 9), 9);
    return (a >= 0 && b >= 0 && (a >> 4) == (b >> 4)) ? a >> 4 : -1;
}

int tg_pave(double rich, double pent)
{
    for (int tries = 0; tries < 50; tries++) {
        tg_reset();
        if (vcount > vcap / 2) rehash();
        if (pave_once(rich, pent)) return 1;
    }
    tg_reset();
    return 0;
}

/* ---- routing the groove through a finished paving ----
 *
 * With the paving fixed, the groove is a path through neighbouring tiles that
 * starts at the first vertex, ends at the opposite one and visits every tile once.
 * The path is grown from the first vertex. When its end is boxed in, it is rewired:
 * pick a tile next to the end that is already on the path, and reverse everything
 * after it, which moves the end somewhere else without losing any tile.
 */

static int nbr(int id, int k)
{
    const TgTile *t = &T[id];
    int nb = tile_at(t->v[(k + 1) % t->n], m10(tdir(t, k) + 5));
    return nb < 0 ? -1 : nb >> 4;
}
static int side_towards(int id, int other)
{
    for (int k = 0; k < T[id].n; k++)
        if (padj[id * MAXV + k] == other) return k;
    return -1;
}
/* the side by which the groove enters `id`, coming from `from`: the far face of the
 * very edge it left by (a bow tie and the rhomb in its notch share two edges) */
static int side_from(int id, int from)
{
    const TgTile *f = &T[from];
    int k = side_towards(from, id);
    int nb = tile_at(f->v[(k + 1) % f->n], m10(tdir(f, k) + 5));
    return nb & 15;
}
static int side_on(int id, int (*test)(Pt, int))
{
    const TgTile *t = &T[id];
    for (int k = 0; k < t->n; k++)
        if (test(t->v[k], tdir(t, k))) return k;
    return -1;
}

void tg_path_begin(void)
{
    free(ppos); free(padj); free(pgoal); free(tg_porder);
    ppos = malloc(sizeof(int) * (nt + 1));
    padj = malloc(sizeof(int) * (nt + 1) * MAXV);
    pgoal = calloc(nt + 1, 1);
    tg_porder = malloc(sizeof(int) * (nt + 1));
    for (int id = 0; id < nt; id++) {
        ppos[id] = -1;
        for (int k = 0; k < T[id].n; k++) padj[id * MAXV + k] = nbr(id, k);
        pgoal[id] = side_on(id, is_goal) >= 0;
        T[id].a = T[id].b = -1;
    }
    int s0 = tile_at(corner[0], 0) >> 4, s1 = tile_at(psub(corner[0], 9), 9) >> 4;
    int st = (rnd() & 1) ? s0 : s1;
    tg_porder[0] = st; ppos[st] = 0; tg_plen = 1;
    pstart_side = side_on(st, is_start);
}

static int unvisited_around(int id)
{
    int c = 0;
    for (int k = 0; k < T[id].n; k++) { int o = padj[id * MAXV + k]; if (o >= 0 && ppos[o] < 0) c++; }
    return c;
}

static void finish_path(void)
{
    for (int i = 0; i < tg_plen; i++) {
        int id = tg_porder[i];
        T[id].a = i ? side_from(id, tg_porder[i - 1]) : pstart_side;
        T[id].b = i + 1 < tg_plen ? side_towards(id, tg_porder[i + 1]) : side_on(id, is_goal);
    }
}

int tg_path_step(void)
{
    int end = tg_porder[tg_plen - 1];
    const TgTile *e = &T[end];
    tg_nodes++;
    if (tg_plen == nt && pgoal[end]) { finish_path(); return TG_SOLVED; }
    /* grow into a neighbour not yet visited, preferring the one with fewest ways out */
    if (rnd01() > 0.02) {
        int best = -1, bestc = 99, ties = 0;
        for (int k = 0; k < e->n; k++) {
            int o = padj[end * MAXV + k];
            if (o < 0 || ppos[o] >= 0) continue;
            int c = unvisited_around(o);
            if (c < bestc) { bestc = c; best = o; ties = 1; }
            else if (c == bestc && rnd() % ++ties == 0) best = o;
        }
        if (best >= 0) { ppos[best] = tg_plen; tg_porder[tg_plen++] = best; return TG_GROW; }
    }
    /* rewire: aim the new end at the nearest unvisited tile, or at the last vertex */
    double tx = tg_corner_x[5], ty = tg_corner_y[5];
    if (tg_plen < nt) {
        double bd = 1e30;
        for (int id = 0; id < nt; id++) {
            if (ppos[id] >= 0) continue;
            double d = hypot(T[id].cx - e->cx, T[id].cy - e->cy);
            if (d < bd) { bd = d; tx = T[id].cx; ty = T[id].cy; }
        }
    }
    int pick = -1, npick = 0; double bs = 1e30;
    int greedy = rnd01() < 0.6;
    for (int k = 0; k < e->n; k++) {
        int o = padj[end * MAXV + k];
        if (o < 0 || ppos[o] < 0 || ppos[o] >= tg_plen - 2) continue;
        int ne = tg_porder[ppos[o] + 1];
        double sc = greedy ? hypot(T[ne].cx - tx, T[ne].cy - ty) : 0;
        if (greedy ? sc < bs : rnd() % ++npick == 0) { bs = sc; pick = o; }
    }
    if (pick < 0) return TG_NONE;
    for (int i = ppos[pick] + 1, j = tg_plen - 1; i < j; i++, j--) {
        int a = tg_porder[i], b = tg_porder[j];
        tg_porder[i] = b; tg_porder[j] = a; ppos[b] = i; ppos[a] = j;
    }
    return TG_REWIRE;
}

void tg_path_sides(int i, int *in, int *out)
{
    int id = tg_porder[i];
    *in = i ? side_from(id, tg_porder[i - 1]) : pstart_side;
    *out = i + 1 < tg_plen ? side_towards(id, tg_porder[i + 1]) : -1;
}

int tg_path(int *order, int *in, int *out)
{
    int cur = -1, side = -1;
    for (int id = 0; id < nt && cur < 0; id++) {
        const TgTile *t = &T[id];
        int g[2] = {t->a, t->b};
        for (int q = 0; q < 2; q++)
            if (is_start(t->v[g[q]], tdir(t, g[q]))) { cur = id; side = g[q]; }
    }
    int n = 0;
    while (cur >= 0 && n < nt) {
        const TgTile *t = &T[cur];
        int o = (side == t->a) ? t->b : t->a;
        order[n] = cur; in[n] = side; out[n] = o; n++;
        int nb = tile_at(t->v[(o + 1) % t->n], m10(tdir(t, o) + 5));
        if (nb < 0) break;
        cur = nb >> 4; side = nb & 15;
    }
    return n;
}

/* ---- independent verification, using none of the search bookkeeping ---- */
static double poly_area(const TgTile *t)
{
    double a = 0;
    for (int j = 0; j < t->n; j++) { int k = (j + 1) % t->n; a += t->x[j] * t->y[k] - t->x[k] * t->y[j]; }
    return a / 2;
}
int tg_verify(char *msg, int msglen)
{
    int ret = 0;
    char *seen = NULL;
    double area = 0, want = 2.5 * tg_side * tg_side / tan(M_PI / 10);
    for (int i = 0; i < nt; i++) {
        const TgTile *t = &T[i];
        if (tg_strict && tg_fixed[t->s] && !((t->a == tg_ga[t->s] && t->b == tg_gb[t->s]) || (t->a == tg_gb[t->s] && t->b == tg_ga[t->s]))) {
            if (msg) snprintf(msg, msglen, "tile %d has the wrong groove for its mould", i);
            return 1;
        }
        for (int j = 0; j < t->n; j++) {
            if (inside(t->x[j], t->y[j]) < 0) {
                if (msg) snprintf(msg, msglen, "tile %d sticks out of the decagon", i);
                return 2;
            }
            int k = (j + 1) % t->n;
            if (fabs(hypot(t->x[k] - t->x[j], t->y[k] - t->y[j]) - 1) > 1e-6) {
                if (msg) snprintf(msg, msglen, "tile %d is not equilateral", i);
                return 3;
            }
        }
        area += poly_area(t);
        for (int o = 0; o < i; o++)
            if (overlap(t, &T[o]) == 2) {
                if (msg) snprintf(msg, msglen, "tiles %d and %d overlap", o, i);
                return 4;
            }
    }
    if (fabs(area - want) > 1e-6) {
        if (msg) snprintf(msg, msglen, "paved area %.6f, decagon area %.6f", area, want);
        return 5;
    }
    /* walk the groove from the first vertex, matching sides by their midpoints */
    int cur = -1, side = -1, starts = 0;
    for (int i = 0; i < nt; i++) {
        const TgTile *t = &T[i];
        int g[2] = {t->a, t->b};
        for (int q = 0; q < 2; q++) {
            int k = g[q], k1 = (k + 1) % t->n;
            double mx = (t->x[k] + t->x[k1]) / 2, my = (t->y[k] + t->y[k1]) / 2;
            if (inside(mx, my) != 0) continue;
            double d0 = hypot(mx - tg_corner_x[0], my - tg_corner_y[0]), d5 = hypot(mx - tg_corner_x[5], my - tg_corner_y[5]);
            if (fabs(d0 - 0.5) < 1e-6) { cur = i; side = k; starts++; }
            else if (fabs(d5 - 0.5) > 1e-6) {
                if (msg) snprintf(msg, msglen, "groove of tile %d runs into the rim", i);
                return 6;
            }
        }
    }
    if (starts != 1) {
        if (msg) snprintf(msg, msglen, "%d groove ends at the first vertex, expected 1", starts);
        return 7;
    }
    seen = calloc((size_t)nt + 1, 1);
    if (!seen) return -1;
    int count = 0, done = 0;
    while (!done) {
        const TgTile *t = &T[cur];
        if (seen[cur]) {
            if (msg) snprintf(msg, msglen, "groove revisits tile %d", cur);
            ret = 8;
            goto cleanup;
        }
        seen[cur] = 1; count++;
        int o = (side == t->a) ? t->b : t->a, o1 = (o + 1) % t->n;
        double mx = (t->x[o] + t->x[o1]) / 2, my = (t->y[o] + t->y[o1]) / 2;
        if (inside(mx, my) == 0) {
            if (fabs(hypot(mx - tg_corner_x[5], my - tg_corner_y[5]) - 0.5) > 1e-6) {
                if (msg) snprintf(msg, msglen, "groove ends at the rim away from the last vertex");
                ret = 9;
                goto cleanup;
            }
            done = 1; break;
        }
        int next = -1, nside = -1;
        for (int i = 0; i < nt && next < 0; i++) {
            if (i == cur) continue;
            const TgTile *q = &T[i];
            for (int k = 0; k < q->n; k++) {
                int k1 = (k + 1) % q->n;
                if (fabs((q->x[k] + q->x[k1]) / 2 - mx) < 1e-6 && fabs((q->y[k] + q->y[k1]) / 2 - my) < 1e-6) { next = i; nside = k; break; }
            }
        }
        if (next < 0) {
            if (msg) snprintf(msg, msglen, "groove of tile %d leads onto bare ground", cur);
            ret = 10;
            goto cleanup;
        }
        if (nside != T[next].a && nside != T[next].b) {
            if (msg) snprintf(msg, msglen, "groove of tile %d runs into a wall of tile %d", cur, next);
            ret = 11;
            goto cleanup;
        }
        cur = next; side = nside;
    }
    if (count != nt) {
        if (msg) snprintf(msg, msglen, "groove crosses %d of %d tiles", count, nt);
        ret = 12;
        goto cleanup;
    }
    if (msg) snprintf(msg, msglen, "ok");
    ret = 0;

cleanup:
    free(seen);
    return ret;
}
