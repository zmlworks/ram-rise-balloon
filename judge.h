/*
 * RamRiseBalloon: balloon trigger judgement (shared by the app and tests).
 * No Windows headers. Combined usage counts compressed pages at their
 * uncompressed size without double-counting the store's physical footprint.
 * Also maps per-second deltas and usage % to a JMA-like colour scale.
 */
#ifndef RAMRISE_JUDGE_H
#define RAMRISE_JUDGE_H

#include <math.h>

#ifndef RR_MB
#define RR_MB (1024.0 * 1024.0)
#endif

/* Drop threshold is this many times the rise threshold (RiseMB). */
enum { RR_DROP_FACTOR = 3 };

/*
 * used (TotalPhys - AvailPhys) already includes the Memory Compression
 * process working set, i.e. the physical size of the compressed store.
 * Add only the savings (estimated original size minus that physical size)
 * so the total is "what In use would be without compression".
 */
static double RrCombinedUsed(double used, int comp_ok, double comp, double comp_orig)
{
    if (comp_ok && comp_orig > comp)
        return used + (comp_orig - comp);
    return used;
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
 * Per-second delta bands scale with RiseMB (R). |d| < 0.5 MB is always white
 * (same epsilon as the "0" text). Default R = 50 MB:
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
 * Usage % (rounded the same way as "%.0f"):
 *   <50 white, 50 green, 60 yellow, 70 orange, 80 red, 85 magenta, 90+ purple.
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

/* |d| < this (bytes) is drawn as "0" and coloured white. */
#define RR_DELTA_EPS_MB  0.5

static unsigned RrDeltaColor(double bytes, int rise_mb)
{
    double a = fabs(bytes);
    double r = (double)(rise_mb < 1 ? 1 : rise_mb) * RR_MB;
    if (a < RR_DELTA_EPS_MB * RR_MB) return RR_COL_WHITE;
    if (bytes > 0) {
        if (a >= 3.0 * r) return RR_COL_PURPLE;
        if (a >= 2.0 * r) return RR_COL_MAGENTA;
        if (a >= 1.0 * r) return RR_COL_RED;
        if (a >= 0.5 * r) return RR_COL_ORANGE;
        if (a >= 0.2 * r) return RR_COL_YELLOW;   /* R/5 */
        return RR_COL_GREEN;
    }
    if (a >= 2.0 * r) return RR_COL_BLUE_DEEP;
    if (a >= 0.5 * r) return RR_COL_BLUE;
    if (a >= 0.2 * r) return RR_COL_BLUE_LIGHT;
    return RR_COL_CYAN;
}

/* pct is 0..100 (or more). Rounding matches typical "%.0f" for pct >= 0. */
static unsigned RrPercentColor(double pct)
{
    int p = (int)(pct + 0.5);
    if (p >= 90) return RR_COL_PURPLE;
    if (p >= 85) return RR_COL_MAGENTA;
    if (p >= 80) return RR_COL_RED;
    if (p >= 70) return RR_COL_ORANGE;
    if (p >= 60) return RR_COL_YELLOW;
    if (p >= 50) return RR_COL_GREEN;
    return RR_COL_WHITE;
}

#endif
