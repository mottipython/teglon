/*
 * Teglon solver core.
 *
 * The Teglon is the tiling problem in Neal Stephenson's "Anathem": pave a regular
 * decagon with tiles from seven moulds, each tile carrying a groove from one side
 * to another, so that the grooves join into a single unbroken curve running from
 * one vertex of the decagon to the opposite vertex and crossing every tile.
 */
#ifndef TEGLON_H
#define TEGLON_H

#include <stdint.h>

#define TG_NS 7      /* number of moulds */
#define TG_MAXV 10   /* most sides on a tile */
#define TG_MAXT 8192 /* most tiles in a paving */

typedef struct { int c[4]; } TgPt; /* a point of Z[zeta], zeta = exp(i*pi/5) */

typedef struct {
    int s;                 /* mould 0..6 */
    int rot;               /* orientation, multiples of 36 degrees */
    int n;                 /* number of sides */
    int a, b;              /* the two sides the groove joins */
    int open;              /* sides not yet against another tile or the rim */
    int nundo;             /* bookkeeping for backtracking */
    int used_s, used_g, chose;
    TgPt v[TG_MAXV];
    double x[TG_MAXV], y[TG_MAXV], lo[5], hi[5], cx, cy;
} TgTile;

enum { TG_NONE, TG_PLACE, TG_REMOVE, TG_TILED, TG_GROW, TG_REWIRE, TG_SOLVED, TG_EXHAUSTED };

extern const char *tg_name[TG_NS];
extern const int tg_nside[TG_NS];
extern int tg_ga[TG_NS], tg_gb[TG_NS]; /* groove of each mould: side ga <-> side gb */
extern int tg_fixed[TG_NS];            /* 0 = let the search choose this mould's groove */
extern int tg_enabled[TG_NS];
extern double tg_weight[TG_NS];        /* preference when ordering candidate tiles */

/* 0 (default): any tile may carry a groove between any two of its sides. The decagon
 * is paved first (tg_step until TG_TILED) and the groove is then routed through the
 * paving (tg_path_begin, tg_path_step until TG_SOLVED).
 * 1: one fixed groove per mould (tg_ga/tg_gb); paving and groove are searched
 * together and tg_step returns TG_SOLVED directly. */
extern int tg_strict;

extern TgTile *tg_tile;                /* the current paving */
extern int tg_nt;
extern int tg_side;                    /* side length of the decagon, in tile edges */
extern double tg_corner_x[10], tg_corner_y[10];
extern long tg_nodes;

void tg_init(int side, uint64_t seed);
void tg_free(void);
void tg_reset(void);                   /* clear the paving, keep the decagon */
void tg_reseed(uint64_t seed);
int tg_step(void);                     /* one search step; returns a TG_ event */

/* paving (tg_strict == 0). rich 0..1: how many of the larger tiles (hexagons,
 * octagons, decagons) to aim for; pent 0..1: share of octagons split into
 * pentagons. Returns 1 on success. */
int tg_pave(double rich, double pent);

/* groove routing (tg_strict == 0) */
extern int *tg_porder, tg_plen;        /* tiles on the groove so far, in order */
void tg_path_begin(void);
int tg_path_step(void);                /* TG_GROW, TG_REWIRE, TG_NONE or TG_SOLVED */
void tg_path_sides(int i, int *in, int *out); /* sides the groove uses on tg_porder[i]; out is -1 at the loose end */

/* After TG_SOLVED: tile indices in the order the curve visits them, with the side
 * the curve enters and leaves each by. Returns the number of tiles. */
int tg_path(int *order, int *in, int *out);

/* Independent check of a finished paving. Returns 0 if it is a valid solution,
 * otherwise a non-zero code; msg receives a description. */
int tg_verify(char *msg, int msglen);

#endif
