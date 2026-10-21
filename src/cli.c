/* teglon-solve: command line front end to the Teglon solver. */
#include "teglon.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void usage(void)
{
    fprintf(stderr,
        "usage: teglon-solve [options]\n"
        "  -n SIDE      side of the decagon in tile edges (default 4)\n"
        "  -s SEED      random seed (default: clock)\n"
        "  -b STEPS     strict puzzle: steps before restarting with a new seed (default 200000, 0 = never)\n"
        "  -r RICH      0..1, how many hexagons, octagons and decagons to aim for (default 0.7)\n"
        "  -p PENT      0..1, share of octagons split into pentagons (default 0.5)\n"
        "  -t BOW       0..1, share of squat hexagons laid as bow tie + two thin rhombs (default 0.5)\n"
        "  -a N         give up after N attempts (default 0 = keep going)\n"
        "  -g GROOVES   strict puzzle: one fixed groove per mould, given as side pairs,\n"
        "               e.g. 01,12,01,02,25,03,05 (xx lets the search choose that groove)\n"
        "  -x MOULDS    shapes to leave out, by number 0-6, e.g. -x 6\n"
        "  -m MOULDS    use exactly these shapes, e.g. -m 1234567 (7 is the girih bow tie)\n"
        "  -w WEIGHTS   preference per mould, e.g. 1,1,2,1,1,1,0.2\n"
        "  --svg FILE   write the solution as an SVG picture\n"
        "  -q           no summary on stderr\n");
    exit(1);
}

static void print_tile(FILE *f, const TgTile *t, int in, int out)
{
    fprintf(f, "P %d %d %d %d", t->s, t->n, in, out);
    for (int j = 0; j < t->n; j++) fprintf(f, " %.6f %.6f", t->x[j], t->y[j]);
    fputc('\n', f);
}

static const char *FILL[TG_NS] = {"#d9a066", "#c97b4a", "#e8c07d", "#b5673c", "#cf9257", "#a85a32", "#e0b070", "#bf8050"};

static void write_svg(const char *path, const int *order, const int *in, const int *out, int n)
{
    if (!path) return;
    for (const char *c = path; *c; c++) {
        if (*c == '/' || *c == '\\' || *c == ':') {
            fprintf(stderr, "error: svg filename must not contain directory separators\n");
            return;
        }
    }
    FILE *f = fopen(path, "w");
    if (!f) { perror(path); return; }
    double minx = 1e9, maxx = -1e9, miny = 1e9, maxy = -1e9;
    for (int k = 0; k < 10; k++) {
        if (tg_corner_x[k] < minx) minx = tg_corner_x[k];
        if (tg_corner_x[k] > maxx) maxx = tg_corner_x[k];
        if (tg_corner_y[k] < miny) miny = tg_corner_y[k];
        if (tg_corner_y[k] > maxy) maxy = tg_corner_y[k];
    }
    double sc = 1000 / (maxx - minx), m = 20;
#define X(x) (((x) - minx) * sc + m)
#define Y(y) ((maxy - (y)) * sc + m)
    fprintf(f, "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 %.0f %.0f\">\n", (maxx - minx) * sc + 2 * m, (maxy - miny) * sc + 2 * m);
    fprintf(f, "<rect width=\"100%%\" height=\"100%%\" fill=\"#1b1611\"/>\n");
    for (int i = 0; i < tg_nt; i++) {
        const TgTile *t = &tg_tile[i];
        fprintf(f, "<polygon fill=\"%s\" stroke=\"#3a2a1c\" stroke-width=\"%.2f\" points=\"", FILL[t->s], sc * 0.03);
        for (int j = 0; j < t->n; j++) fprintf(f, "%.2f,%.2f ", X(t->x[j]), Y(t->y[j]));
        fprintf(f, "\"/>\n");
    }
    fprintf(f, "<path fill=\"none\" stroke=\"#1b1611\" stroke-width=\"%.2f\" stroke-linecap=\"round\" d=\"", sc * 0.09);
    for (int i = 0; i < n; i++) {
        const TgTile *t = &tg_tile[order[i]];
        int a = in[i], a1 = (a + 1) % t->n, b = out[i], b1 = (b + 1) % t->n;
        double ax = (t->x[a] + t->x[a1]) / 2, ay = (t->y[a] + t->y[a1]) / 2;
        double bx = (t->x[b] + t->x[b1]) / 2, by = (t->y[b] + t->y[b1]) / 2;
        /* leave each side at right angles so the curve is smooth across tiles */
        double h = 0.45 * hypot(bx - ax, by - ay) + 0.1;
        double nax = -(t->y[a1] - t->y[a]), nay = t->x[a1] - t->x[a];
        double nbx = -(t->y[b1] - t->y[b]), nby = t->x[b1] - t->x[b];
        if (i == 0) fprintf(f, "M%.2f,%.2f ", X(ax), Y(ay));
        fprintf(f, "C%.2f,%.2f %.2f,%.2f %.2f,%.2f ", X(ax + nax * h), Y(ay + nay * h), X(bx + nbx * h), Y(by + nby * h), X(bx), Y(by));
    }
    fprintf(f, "\"/>\n</svg>\n");
    fclose(f);
}

int main(int argc, char **argv)
{
    int n = 4, quiet = 0;
    double rich = 0.7, pent = 0.5;
    tg_enabled[5] = 1; /* the solver may use the octagon; -m or -x chooses the moulds */
    long budget = 200000, attempts = 0;
    const char *svg = NULL;
    uint64_t seed = (uint64_t)time(NULL) * 2654435761u + (uint64_t)clock();
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-q")) quiet = 1;
        else if (i + 1 >= argc) usage();
        else if (!strcmp(argv[i], "-n")) n = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-r")) rich = atof(argv[++i]);
        else if (!strcmp(argv[i], "-p")) pent = atof(argv[++i]);
        else if (!strcmp(argv[i], "-t")) tg_bowtie = atof(argv[++i]);
        else if (!strcmp(argv[i], "-s")) seed = strtoull(argv[++i], 0, 10);
        else if (!strcmp(argv[i], "-b")) {
            long b = atol(argv[++i]);
            if (b > 0 && b <= 1000000000L) budget = b;
        }
        else if (!strcmp(argv[i], "-a")) {
            long a = atol(argv[++i]);
            if (a > 0 && a <= 10000000L) attempts = a;
        }
        else if (!strcmp(argv[i], "--svg")) {
            const char *s = argv[++i];
            if (strstr(s, "..") != NULL) {
                fprintf(stderr, "error: unsafe path for --svg\n");
                return 1;
            }
            svg = s;
        }
        else if (!strcmp(argv[i], "-m")) {
            for (int s = 0; s < TG_NS; s++) tg_enabled[s] = 0;
            for (char *p = argv[++i]; *p; p++) if (*p >= '0' && *p < '0' + TG_NS) tg_enabled[*p - '0'] = 1;
        }
        else if (!strcmp(argv[i], "-x")) { for (char *p = argv[++i]; *p; p++) if (*p >= '0' && *p < '0' + TG_NS) tg_enabled[*p - '0'] = 0; }
        else if (!strcmp(argv[i], "-g")) {
            char *p = argv[++i];
            tg_strict = 1;
            for (int s = 0; s < TG_NS && p[0] && p[1]; s++) {
                if (p[0] == '?' || p[0] == 'x') tg_fixed[s] = 0;
                else { tg_ga[s] = p[0] - '0'; tg_gb[s] = p[1] - '0'; }
                p += 2;
                if (*p == ',') p++;
            }
        } else if (!strcmp(argv[i], "-w")) {
            char *p = argv[++i];
            for (int s = 0; s < TG_NS && *p; s++) { tg_weight[s] = strtod(p, &p); if (*p == ',') p++; }
        } else usage();
    }
    for (int s = 0; s < TG_NS; s++)
        if (tg_ga[s] < 0 || tg_gb[s] < 0 || tg_ga[s] >= tg_nside[s] || tg_gb[s] >= tg_nside[s] || tg_ga[s] == tg_gb[s]) {
            fprintf(stderr, "bad groove for the %s\n", tg_name[s]); return 1;
        }
    if (n < 1 || n > 50) { fprintf(stderr, "side must be 1..50\n"); return 1; }
    tg_init(n, seed);
    long att = 1, steps = 0;
    if (tg_strict) {
        /* one groove per mould: paving and groove are searched together */
        for (;;) {
            int r = tg_step();
            steps++;
            if (r == TG_SOLVED) break;
            if (r == TG_EXHAUSTED) {
                if (!quiet) fprintf(stderr, "no solution exists with these moulds (searched exhaustively, %ld steps)\n", tg_nodes);
                return 3;
            }
            if (budget && steps >= budget) {
                if (attempts && att >= attempts) { if (!quiet) fprintf(stderr, "gave up after %ld steps\n", tg_nodes); return 4; }
                att++; steps = 0;
                tg_reset();
                uint64_t att_seed = seed;
                for (int k = 0; k < (int)(att & 0x3fff); k++) att_seed += 7919ULL;
                tg_reseed(att_seed);
            }
        }
    } else {
        for (;; att++) {
            if (!tg_pave(rich, pent)) { fprintf(stderr, "paving failed\n"); return 2; }
            tg_path_begin();
            long limit = 100000L;
            for (int t = 0; t < tg_nt && t < 2000; t++) limit += 2000L;
            int r = TG_NONE;
            for (steps = 0; steps < limit && r != TG_SOLVED; steps++) r = tg_path_step();
            if (r == TG_SOLVED) break;
            /* a paving whose groove will not close is abandoned for a fresh one */
            if (attempts && att >= attempts) { if (!quiet) fprintf(stderr, "gave up after %ld steps\n", tg_nodes); return 4; }
            uint64_t att_seed = seed;
            for (int k = 0; k < (int)(att & 0x3fff); k++) att_seed += 7919ULL;
            tg_reseed(att_seed);
        }
    }
    char msg[200];
    int bad = tg_verify(msg, sizeof msg);
    if (bad) { fprintf(stderr, "internal error: solution failed verification: %s\n", msg); return 5; }
    if (tg_nt <= 0 || tg_nt > 100000) { fprintf(stderr, "invalid tile count\n"); return 1; }
    int *order = calloc((size_t)tg_nt, sizeof(int));
    int *in = calloc((size_t)tg_nt, sizeof(int));
    int *out = calloc((size_t)tg_nt, sizeof(int));
    if (!order || !in || !out) {
        fprintf(stderr, "out of memory\n");
        free(order); free(in); free(out);
        return 1;
    }
    int len = tg_path(order, in, out);
    {
        printf("D %d", n);
        for (int k = 0; k < 10; k++) printf(" %.6f %.6f", tg_corner_x[k], tg_corner_y[k]);
        putchar('\n');
        for (int i = 0; i < len; i++) print_tile(stdout, &tg_tile[order[i]], in[i], out[i]);
        puts("S");
    }
    if (svg) write_svg(svg, order, in, out, len);
    free(order);
    free(in);
    free(out);
    if (!quiet) {
        int cnt[TG_NS] = {0};
        for (int i = 0; i < tg_nt; i++) cnt[tg_tile[i].s]++;
        fprintf(stderr, "solved and verified: side %d, %d tiles, %ld steps, %ld attempt%s\n  tiles:", n, tg_nt, tg_nodes, att, att == 1 ? "" : "s");
        for (int s = 0; s < TG_NS; s++) fprintf(stderr, " %s %d%s", tg_name[s], cnt[s], s < TG_NS - 1 ? "," : "\n");
        if (tg_strict) {
            fprintf(stderr, "  grooves:");
            for (int s = 0; s < TG_NS; s++) fprintf(stderr, tg_fixed[s] ? "%c%d%d" : "%cxx", s ? ',' : ' ', tg_ga[s], tg_gb[s]);
            fputc('\n', stderr);
        }
    }
    return 0;
}
