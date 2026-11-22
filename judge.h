/*
 * RamRiseBalloon: balloon trigger judgement and colour thresholds
 * (shared by the app and tests). No Windows headers.
 * Combined usage counts compressed pages at uncompressed size without
 * double-counting the store's physical footprint. Colour ladders start
 * from fixed defaults and, after enough in-memory samples, follow
 * quantiles of the values actually seen this run (nothing is saved).
 */
#ifndef RAMRISE_JUDGE_H
#define RAMRISE_JUDGE_H

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef RR_MB
#define RR_MB (1024.0 * 1024.0)
#endif

/* Drop threshold is this many times the rise threshold (RiseMB). */
enum { RR_DROP_FACTOR = 3 };

/* Compression savings: original estimate minus the store's physical size.
 * used already includes that physical size, so only the savings are added. */
static double RrCombinedGain(int comp_ok, double comp, double comp_orig)
{
    if (comp_ok && comp_orig > comp)
        return comp_orig - comp;
    return 0;
}

static double RrCombinedUsed(double used, int comp_ok, double comp, double comp_orig)
{
    return used + RrCombinedGain(comp_ok, comp, comp_orig);
}

/* Denominator for the combined line: physical total plus the same savings. */
static double RrCombinedTotal(double total, int comp_ok, double comp, double comp_orig)
{
    return total + RrCombinedGain(comp_ok, comp, comp_orig);
}

typedef struct {
    int rise;      /* combined (or the value passed as delta) rose by >= RiseMB */
    int drop;      /* fell by >= RiseMB * RR_DROP_FACTOR */
    int comp_rise; /* compression store rose by >= CompRiseMB */
    int active;    /* balloon should appear (any of the above, and pct >= min) */
} RrJudge;

/* hist_count / win_samples: same meaning as the app.
 * delta     = value(now) - value(window start)
 * last_step = value(now) - value(previous sample)
 * pct       = value/total*100 (caller chooses which value; app uses combined)
 */
static void RrJudgeWindow(int hist_count, int win_samples,
                          double delta, double last_step,
                          int rise_mb, int comp_rise_mb,
                          int both_comp_ok, double delta_comp,
                          double pct, int min_percent,
                          RrJudge *out)
{
    double rise_thr = (double)rise_mb * RR_MB;
    double drop_thr = (double)rise_mb * (double)RR_DROP_FACTOR * RR_MB;
    double comp_thr = (double)comp_rise_mb * RR_MB;
    int ready = hist_count > win_samples;

    out->rise = ready && delta >= rise_thr && last_step > -rise_thr / 2.0;
    out->drop = ready && delta <= -drop_thr && last_step < drop_thr / 2.0;
    out->comp_rise = ready && both_comp_ok && delta_comp >= comp_thr;
    out->active = (out->rise || out->drop || out->comp_rise) && pct >= (double)min_percent;
}

/* ---------------------------------------------------------------- colours (0xRRGGBB)
 * JMA-like rain-intensity palette, tuned so every step stays readable on the
 * dark balloon (#202227). Deep navy would vanish, so the deepest blue is a
 * brighter indigo rather than #000080; purple is similarly lifted.
 *
 * Per-second delta bands, cold-start defaults (R = RiseMB, default 50 MB).
 * |d| < 0.5 MB is always white (same epsilon as the "0" text) and stays fixed.
 *
 *   |d| MB        +                    -
 *   < 0.5         white                white
 *   [0.5,  R/5)   green                pale cyan      (R/5 = 10)
 *   [R/5,  R/2)   yellow               light blue     (R/2 = 25)
 *   [R/2,  R)     orange               blue
 *   [R,    2R)    red                  blue
 *   [2R,   3R)    magenta              deep blue
 *   >= 3R         purple               deep blue
 *
 * Usage % cold-start defaults (compared after rounding like "%.0f"):
 *   <50 white, 50 green, 60 yellow, 70 orange, 80 red, 85 magenta, 90+ purple.
 *
 * After RR_STAT_WARM samples of a kind, thresholds move toward quantiles of
 * the in-memory ring so the same ladder covers the range actually seen.
 * Updates are eased (RR_STAT_ALPHA) so colours do not jump. A tight range
 * (almost no variation) keeps the defaults, so a flat line does not flicker.
 * Nothing is written to disk.
 */
#define RR_RGB(r, g, b) ((unsigned)(((r) << 16) | ((g) << 8) | (b)))
#define RR_COL_WHITE      RR_RGB(0xEC, 0xEE, 0xF1)
#define RR_COL_GREEN      RR_RGB(0x3C, 0xE6, 0x3C)
#define RR_COL_YELLOW     RR_RGB(0xF0, 0xE6, 0x28)
#define RR_COL_ORANGE     RR_RGB(0xFF, 0x9A, 0x20)
#define RR_COL_RED        RR_RGB(0xFF, 0x3A, 0x3A)
#define RR_COL_MAGENTA    RR_RGB(0xFF, 0x4A, 0xE6)
#define RR_COL_PURPLE     RR_RGB(0xC4, 0x5C, 0xFF)
#define RR_COL_CYAN       RR_RGB(0xB0, 0xF2, 0xFF)
#define RR_COL_BLUE_LIGHT RR_RGB(0x6A, 0xB4, 0xFF)
#define RR_COL_BLUE       RR_RGB(0x4A, 0x7C, 0xFF)
#define RR_COL_BLUE_DEEP  RR_RGB(0x6A, 0x66, 0xFF)

#define RR_DELTA_EPS_MB  0.5
enum {
    RR_STAT_CAP   = 1024,
    RR_STAT_WARM  = 60,
    RR_PLUS_N     = 5,
    RR_MINUS_N    = 3,
    RR_PCT_N      = 6
};
#define RR_STAT_ALPHA 0.15

typedef struct {
    float v[RR_STAT_CAP];
    int n, head;
} RrRing;

typedef struct {
    RrRing plus, minus, pct;
    double plus_thr[RR_PLUS_N];    /* bytes: Y, O, R, M, P */
    double minus_thr[RR_MINUS_N];  /* bytes: light, blue, deep */
    double pct_thr[RR_PCT_N];      /* %: G, Y, O, R, M, P */
    int inited;
} RrColorStats;

static void RrDeltaDefaultThr(int rise_mb, double plus[RR_PLUS_N], double minus[RR_MINUS_N])
{
    double r = (double)(rise_mb < 1 ? 1 : rise_mb) * RR_MB;
    plus[0] = 0.2 * r;  plus[1] = 0.5 * r;  plus[2] = 1.0 * r;
    plus[3] = 2.0 * r;  plus[4] = 3.0 * r;
    minus[0] = 0.2 * r; minus[1] = 0.5 * r; minus[2] = 2.0 * r;
}

static void RrPercentDefaultThr(double pct[RR_PCT_N])
{
    pct[0] = 50; pct[1] = 60; pct[2] = 70;
    pct[3] = 80; pct[4] = 85; pct[5] = 90;
}

static void RrStatsInit(RrColorStats *s, int rise_mb)
{
    memset(s, 0, sizeof *s);
    RrDeltaDefaultThr(rise_mb, s->plus_thr, s->minus_thr);
    RrPercentDefaultThr(s->pct_thr);
    s->inited = 1;
}

static void RrRingPush(RrRing *r, float x)
{
    r->v[r->head] = x;
    r->head = (r->head + 1) % RR_STAT_CAP;
    if (r->n < RR_STAT_CAP) r->n++;
}

static int RrCmpFloat(const void *a, const void *b)
{
    float x = *(const float *)a, y = *(const float *)b;
    return (x > y) - (x < y);
}

/* q in 0..1. tmp must hold at least r->n floats. */
static double RrRingQuantile(const RrRing *r, double q, float *tmp)
{
    int n = r->n, lo;
    double pos, f;
    int i;
    if (n <= 0) return 0;
    for (i = 0; i < n; i++) tmp[i] = r->v[i];
    qsort(tmp, (size_t)n, sizeof(float), RrCmpFloat);
    if (n == 1 || q <= 0) return tmp[0];
    if (q >= 1) return tmp[n - 1];
    pos = q * (double)(n - 1);
    lo = (int)pos;
    f = pos - (double)lo;
    if (lo >= n - 1) return tmp[n - 1];
    return (double)tmp[lo] * (1.0 - f) + (double)tmp[lo + 1] * f;
}

static void RrFinishThr(double *t, int n, double min_gap, double floor_v)
{
    int i;
    if (t[0] < floor_v) t[0] = floor_v;
    for (i = 1; i < n; i++)
        if (t[i] < t[i - 1] + min_gap) t[i] = t[i - 1] + min_gap;
}

static void RrLerpThr(double *a, const double *t, int n, double alpha)
{
    int i;
    for (i = 0; i < n; i++) a[i] += alpha * (t[i] - a[i]);
}

static void RrColorRecompute(RrColorStats *s, int rise_mb)
{
    float tmp[RR_STAT_CAP];
    double def_p[RR_PLUS_N], def_m[RR_MINUS_N], def_c[RR_PCT_N];
    double tgt_p[RR_PLUS_N], tgt_m[RR_MINUS_N], tgt_c[RR_PCT_N];
    static const double q_plus[RR_PLUS_N]   = { 0.20, 0.40, 0.60, 0.80, 0.95 };
    static const double q_minus[RR_MINUS_N] = { 0.25, 0.55, 0.90 };
    static const double q_pct[RR_PCT_N]     = { 0.10, 0.25, 0.45, 0.65, 0.80, 0.95 };
    int i;

    RrDeltaDefaultThr(rise_mb, def_p, def_m);
    RrPercentDefaultThr(def_c);

    if (s->plus.n >= RR_STAT_WARM) {
        for (i = 0; i < RR_PLUS_N; i++) tgt_p[i] = RrRingQuantile(&s->plus, q_plus[i], tmp);
        if (tgt_p[RR_PLUS_N - 1] - tgt_p[0] < 4.0 * RR_MB)
            memcpy(tgt_p, def_p, sizeof tgt_p);
        else
            RrFinishThr(tgt_p, RR_PLUS_N, 0.5 * RR_MB, RR_DELTA_EPS_MB * RR_MB);
    } else {
        memcpy(tgt_p, def_p, sizeof tgt_p);
    }

    if (s->minus.n >= RR_STAT_WARM) {
        for (i = 0; i < RR_MINUS_N; i++) tgt_m[i] = RrRingQuantile(&s->minus, q_minus[i], tmp);
        if (tgt_m[RR_MINUS_N - 1] - tgt_m[0] < 4.0 * RR_MB)
            memcpy(tgt_m, def_m, sizeof tgt_m);
        else
            RrFinishThr(tgt_m, RR_MINUS_N, 0.5 * RR_MB, RR_DELTA_EPS_MB * RR_MB);
    } else {
        memcpy(tgt_m, def_m, sizeof tgt_m);
    }

    if (s->pct.n >= RR_STAT_WARM) {
        for (i = 0; i < RR_PCT_N; i++) tgt_c[i] = RrRingQuantile(&s->pct, q_pct[i], tmp);
        if (tgt_c[RR_PCT_N - 1] - tgt_c[0] < 8.0)
            memcpy(tgt_c, def_c, sizeof tgt_c);
        else
            RrFinishThr(tgt_c, RR_PCT_N, 1.0, 0.0);
    } else {
        memcpy(tgt_c, def_c, sizeof tgt_c);
    }

    if (!s->inited) {
        memcpy(s->plus_thr, tgt_p, sizeof tgt_p);
        memcpy(s->minus_thr, tgt_m, sizeof tgt_m);
        memcpy(s->pct_thr, tgt_c, sizeof tgt_c);
        s->inited = 1;
    } else {
        RrLerpThr(s->plus_thr, tgt_p, RR_PLUS_N, RR_STAT_ALPHA);
        RrLerpThr(s->minus_thr, tgt_m, RR_MINUS_N, RR_STAT_ALPHA);
        RrLerpThr(s->pct_thr, tgt_c, RR_PCT_N, RR_STAT_ALPHA);
    }
}

/* plus_thr / minus_thr are in bytes. NULL = cold-start defaults from rise_mb. */
static unsigned RrDeltaColor(double bytes, int rise_mb,
                             const double *plus_thr, const double *minus_thr)
{
    double a = fabs(bytes);
    double p[RR_PLUS_N], m[RR_MINUS_N];
    const double *pp = plus_thr, *mm = minus_thr;
    if (a < RR_DELTA_EPS_MB * RR_MB) return RR_COL_WHITE;
    if (!pp || !mm) {
        RrDeltaDefaultThr(rise_mb, p, m);
        if (!pp) pp = p;
        if (!mm) mm = m;
    }
    if (bytes > 0) {
        if (a >= pp[4]) return RR_COL_PURPLE;
        if (a >= pp[3]) return RR_COL_MAGENTA;
        if (a >= pp[2]) return RR_COL_RED;
        if (a >= pp[1]) return RR_COL_ORANGE;
        if (a >= pp[0]) return RR_COL_YELLOW;
        return RR_COL_GREEN;
    }
    if (a >= mm[2]) return RR_COL_BLUE_DEEP;
    if (a >= mm[1]) return RR_COL_BLUE;
    if (a >= mm[0]) return RR_COL_BLUE_LIGHT;
    return RR_COL_CYAN;
}

/* pct_thr NULL = cold-start defaults. Colour follows the displayed (rounded) %. */
static unsigned RrPercentColor(double pct, const double *pct_thr)
{
    double d[RR_PCT_N], shown;
    const double *t = pct_thr;
    if (!t) { RrPercentDefaultThr(d); t = d; }
    shown = floor(pct + 0.5);
    if (shown >= t[5]) return RR_COL_PURPLE;
    if (shown >= t[4]) return RR_COL_MAGENTA;
    if (shown >= t[3]) return RR_COL_RED;
    if (shown >= t[2]) return RR_COL_ORANGE;
    if (shown >= t[1]) return RR_COL_YELLOW;
    if (shown >= t[0]) return RR_COL_GREEN;
    return RR_COL_WHITE;
}

#endif
