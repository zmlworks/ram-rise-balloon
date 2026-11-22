/* Unit tests for RamRiseBalloon trigger judgement (Linux/macOS, no Windows). */
#include "../judge.h"
#include <math.h>
#include <stdio.h>

static int fails, n;

#define EQ(a, b) do { \
    long long _a = (long long)(a), _b = (long long)(b); \
    n++; \
    if (_a != _b) { \
        fails++; \
        printf("FAIL %s:%d %s = %lld, want %lld\n", __FILE__, __LINE__, #a, _a, _b); \
    } \
} while (0)

#define APPROX(a, b, eps) do { \
    double _a = (double)(a), _b = (double)(b); \
    n++; \
    if (fabs(_a - _b) > (eps)) { \
        fails++; \
        printf("FAIL %s:%d %s = %.6f, want %.6f\n", __FILE__, __LINE__, #a, _a, _b); \
    } \
} while (0)

static const double GB = 1024.0 * 1024.0 * 1024.0;

static void judge(double delta_mb, double last_mb, int rise_mb,
                  int both_comp, double dcomp_mb, int comp_rise_mb,
                  double pct, int min_pct, RrJudge *o)
{
    RrJudgeWindow(6, 5, delta_mb * RR_MB, last_mb * RR_MB,
                  rise_mb, comp_rise_mb, both_comp, dcomp_mb * RR_MB,
                  pct, min_pct, o);
}

int main(void)
{
    RrJudge j;

    /* CombinedUsed: no compression info → used as-is */
    APPROX(RrCombinedUsed(13.3 * GB, 0, 0, 0), 13.3 * GB, 1.0);
    /* orig missing or not greater than physical → do not add (would double-count) */
    APPROX(RrCombinedUsed(13.3 * GB, 1, 0.8 * GB, 0), 13.3 * GB, 1.0);
    APPROX(RrCombinedUsed(13.3 * GB, 1, 0.8 * GB, 0.8 * GB), 13.3 * GB, 1.0);
    /* used already includes the store WS (0.8 GB); add only savings (2.52-0.8) */
    APPROX(RrCombinedUsed(13.3 * GB, 1, 0.8 * GB, 2.52 * GB),
           13.3 * GB + (2.52 - 0.8) * GB, 1.0);

    /* Rise uses the delta passed in (the app passes combined, not plain used). */
    /* Default RiseMB=50: 50 MB over the window, last step not a large drop. */
    judge(50, 10, 50, 0, 0, 20, 40, 0, &j);
    EQ(j.rise, 1); EQ(j.drop, 0); EQ(j.comp_rise, 0); EQ(j.active, 1);

    judge(49, 10, 50, 0, 0, 20, 40, 0, &j);
    EQ(j.rise, 0); EQ(j.active, 0);

    /* Last sample dropped by more than RiseMB/2 → not a rise. */
    judge(50, -26, 50, 0, 0, 20, 40, 0, &j);
    EQ(j.rise, 0);
    judge(50, -25, 50, 0, 0, 20, 40, 0, &j); /* -25 is not > -25, still no */
    EQ(j.rise, 0);
    judge(50, -24.9, 50, 0, 0, 20, 40, 0, &j);
    EQ(j.rise, 1);

    /* Combined rose 80 MB while "used" would have been flat — still a rise
       if the caller passes the combined delta (this is the app's contract). */
    judge(80, 20, 50, 0, 0, 20, 40, 0, &j);
    EQ(j.rise, 1);
    /* Combined flat (used grew but savings shrank by the same amount) → no rise. */
    judge(0, 0, 50, 0, 0, 20, 40, 0, &j);
    EQ(j.rise, 0); EQ(j.drop, 0); EQ(j.active, 0);

    /* Drop: threshold is 3x RiseMB (150 MB when RiseMB=50). */
    EQ(RR_DROP_FACTOR, 3);
    judge(-150, -40, 50, 0, 0, 20, 40, 0, &j);
    EQ(j.drop, 1); EQ(j.rise, 0); EQ(j.comp_rise, 0); EQ(j.active, 1);

    judge(-149, -40, 50, 0, 0, 20, 40, 0, &j);
    EQ(j.drop, 0); EQ(j.active, 0);

    /* Last sample rose by more than drop_thr/2 (75 MB) → not a drop. */
    judge(-150, 76, 50, 0, 0, 20, 40, 0, &j);
    EQ(j.drop, 0);
    judge(-150, 74, 50, 0, 0, 20, 40, 0, &j);
    EQ(j.drop, 1);

    /* A 50 MB drop (the rise threshold) is not enough. */
    judge(-50, -20, 50, 0, 0, 20, 40, 0, &j);
    EQ(j.drop, 0); EQ(j.rise, 0);

    /* Rise and drop are mutually exclusive at the default thresholds. */
    judge(150, 40, 50, 0, 0, 20, 40, 0, &j);
    EQ(j.rise, 1); EQ(j.drop, 0);
    judge(-50, -10, 50, 0, 0, 20, 40, 0, &j);
    EQ(j.rise, 0); EQ(j.drop, 0);

    /* Not enough history. */
    {
        RrJudge o;
        RrJudgeWindow(5, 5, 200 * RR_MB, 40 * RR_MB, 50, 20, 0, 0, 40, 0, &o);
        EQ(o.rise, 0); EQ(o.drop, 0); EQ(o.active, 0);
        RrJudgeWindow(6, 5, 200 * RR_MB, 40 * RR_MB, 50, 20, 0, 0, 40, 0, &o);
        EQ(o.rise, 1);
    }

    /* MinPercent filters the balloon even when the delta qualifies. */
    judge(80, 20, 50, 0, 0, 20, 10, 50, &j);
    EQ(j.rise, 1); EQ(j.active, 0);
    judge(80, 20, 50, 0, 0, 20, 50, 50, &j);
    EQ(j.active, 1);

    /* Compression-store rise is an independent trigger (still used when
       original size is unknown and combined equals used). */
    judge(0, 0, 50, 1, 20, 20, 40, 0, &j);
    EQ(j.rise, 0); EQ(j.drop, 0); EQ(j.comp_rise, 1); EQ(j.active, 1);
    judge(0, 0, 50, 1, 19, 20, 40, 0, &j);
    EQ(j.comp_rise, 0); EQ(j.active, 0);
    judge(0, 0, 50, 0, 100, 20, 40, 0, &j); /* missing old/new comp → no */
    EQ(j.comp_rise, 0);

    /* Combined drop + compression rise: both flags, balloon active. */
    judge(-200, -50, 50, 1, 30, 20, 40, 0, &j);
    EQ(j.drop, 1); EQ(j.comp_rise, 1); EQ(j.active, 1);

    /* Colour bands: per-second deltas scale with RiseMB (default 50). */
    EQ(RrDeltaColor(0, 50, NULL, NULL), RR_COL_WHITE);
    EQ(RrDeltaColor(0.4 * RR_MB, 50, NULL, NULL), RR_COL_WHITE);
    EQ(RrDeltaColor(-0.4 * RR_MB, 50, NULL, NULL), RR_COL_WHITE);
    EQ(RrDeltaColor(0.5 * RR_MB, 50, NULL, NULL), RR_COL_GREEN);
    EQ(RrDeltaColor(9.9 * RR_MB, 50, NULL, NULL), RR_COL_GREEN);
    EQ(RrDeltaColor(10 * RR_MB, 50, NULL, NULL), RR_COL_YELLOW);    /* R/5 */
    EQ(RrDeltaColor(24.9 * RR_MB, 50, NULL, NULL), RR_COL_YELLOW);
    EQ(RrDeltaColor(25 * RR_MB, 50, NULL, NULL), RR_COL_ORANGE);    /* R/2 */
    EQ(RrDeltaColor(49.9 * RR_MB, 50, NULL, NULL), RR_COL_ORANGE);
    EQ(RrDeltaColor(50 * RR_MB, 50, NULL, NULL), RR_COL_RED);       /* R */
    EQ(RrDeltaColor(99.9 * RR_MB, 50, NULL, NULL), RR_COL_RED);
    EQ(RrDeltaColor(100 * RR_MB, 50, NULL, NULL), RR_COL_MAGENTA);  /* 2R */
    EQ(RrDeltaColor(149.9 * RR_MB, 50, NULL, NULL), RR_COL_MAGENTA);
    EQ(RrDeltaColor(150 * RR_MB, 50, NULL, NULL), RR_COL_PURPLE);   /* 3R */
    EQ(RrDeltaColor(2000 * RR_MB, 50, NULL, NULL), RR_COL_PURPLE);

    EQ(RrDeltaColor(-0.5 * RR_MB, 50, NULL, NULL), RR_COL_CYAN);
    EQ(RrDeltaColor(-9.9 * RR_MB, 50, NULL, NULL), RR_COL_CYAN);
    EQ(RrDeltaColor(-10 * RR_MB, 50, NULL, NULL), RR_COL_BLUE_LIGHT);
    EQ(RrDeltaColor(-24.9 * RR_MB, 50, NULL, NULL), RR_COL_BLUE_LIGHT);
    EQ(RrDeltaColor(-25 * RR_MB, 50, NULL, NULL), RR_COL_BLUE);
    EQ(RrDeltaColor(-99.9 * RR_MB, 50, NULL, NULL), RR_COL_BLUE);
    EQ(RrDeltaColor(-100 * RR_MB, 50, NULL, NULL), RR_COL_BLUE_DEEP);
    EQ(RrDeltaColor(-150 * RR_MB, 50, NULL, NULL), RR_COL_BLUE_DEEP);

    /* Bands follow RiseMB (here 100: R/5=20, 3R=300). Epsilon stays 0.5 MB. */
    EQ(RrDeltaColor(0.4 * RR_MB, 100, NULL, NULL), RR_COL_WHITE);
    EQ(RrDeltaColor(19.9 * RR_MB, 100, NULL, NULL), RR_COL_GREEN);
    EQ(RrDeltaColor(20 * RR_MB, 100, NULL, NULL), RR_COL_YELLOW);
    EQ(RrDeltaColor(50 * RR_MB, 100, NULL, NULL), RR_COL_ORANGE);
    EQ(RrDeltaColor(100 * RR_MB, 100, NULL, NULL), RR_COL_RED);
    EQ(RrDeltaColor(200 * RR_MB, 100, NULL, NULL), RR_COL_MAGENTA);
    EQ(RrDeltaColor(300 * RR_MB, 100, NULL, NULL), RR_COL_PURPLE);
    EQ(RrDeltaColor(-20 * RR_MB, 100, NULL, NULL), RR_COL_BLUE_LIGHT);
    EQ(RrDeltaColor(-50 * RR_MB, 100, NULL, NULL), RR_COL_BLUE);
    EQ(RrDeltaColor(-200 * RR_MB, 100, NULL, NULL), RR_COL_BLUE_DEEP);

    /* Usage % (rounded like %.0f). */
    EQ(RrPercentColor(0, NULL), RR_COL_WHITE);
    EQ(RrPercentColor(49.4, NULL), RR_COL_WHITE);
    EQ(RrPercentColor(49.5, NULL), RR_COL_GREEN);   /* displays as 50% */
    EQ(RrPercentColor(59.4, NULL), RR_COL_GREEN);
    EQ(RrPercentColor(59.5, NULL), RR_COL_YELLOW);
    EQ(RrPercentColor(69.5, NULL), RR_COL_ORANGE);
    EQ(RrPercentColor(79.5, NULL), RR_COL_RED);
    EQ(RrPercentColor(84.4, NULL), RR_COL_RED);
    EQ(RrPercentColor(84.5, NULL), RR_COL_MAGENTA);
    EQ(RrPercentColor(89.4, NULL), RR_COL_MAGENTA);
    EQ(RrPercentColor(89.5, NULL), RR_COL_PURPLE);
    EQ(RrPercentColor(90, NULL), RR_COL_PURPLE);
    EQ(RrPercentColor(110, NULL), RR_COL_PURPLE);

    /* Combined denominator includes the same savings as the numerator. */
    {
        double used = 13.0 * GB, total = 32.0 * GB, comp = 0.8 * GB, orig = 2.5 * GB;
        double gain = orig - comp;
        APPROX(RrCombinedGain(1, comp, orig), gain, 1.0);
        APPROX(RrCombinedUsed(used, 1, comp, orig), used + gain, 1.0);
        APPROX(RrCombinedTotal(total, 1, comp, orig), total + gain, 1.0);
        /* 14.7 / 33.7 GB, not 14.7 / 32.0 */
        APPROX(RrCombinedUsed(used, 1, comp, orig) / GB, 13.0 + (2.5 - 0.8), 1e-9);
        APPROX(RrCombinedTotal(total, 1, comp, orig) / GB, 32.0 + (2.5 - 0.8), 1e-9);
        APPROX(RrCombinedTotal(total, 0, comp, orig), total, 1.0);
        APPROX(RrCombinedTotal(total, 1, comp, comp), total, 1.0);
    }

    /* Colour stats: cold start stays on the fixed defaults. */
    {
        RrColorStats st;
        int i;
        RrStatsInit(&st, 50);
        EQ(st.plus.total, 0);
        APPROX(st.plus_thr[0] / RR_MB, 10.0, 1e-6);
        APPROX(st.plus_thr[4] / RR_MB, 150.0, 1e-6);
        APPROX(st.pct_thr[5], 90.0, 1e-6);
        EQ(RrDeltaColor(12 * RR_MB, 50, st.plus_thr, st.minus_thr), RR_COL_YELLOW);
        EQ(RrPercentColor(50, st.pct_thr), RR_COL_GREEN);
        EQ(RrPercentColor(90, st.pct_thr), RR_COL_PURPLE);
        for (i = 0; i < RR_STAT_WARM - 1; i++)
            RrHistAddDiff(&st.plus, (i + 1) * 5 * RR_MB);
        RrColorRecompute(&st, 50);
        EQ(st.plus.total, RR_STAT_WARM - 1);
        APPROX(st.plus_thr[4] / RR_MB, 150.0, 1e-6);
    }

    /* A wide, even plus distribution moves thresholds toward quantiles,
       but one recompute only takes a 15% step (no flicker). */
    {
        RrColorStats st;
        int i, k;
        double before, after;
        RrStatsInit(&st, 50);
        for (i = 1; i <= 100; i++)
            RrHistAddDiff(&st.plus, i * RR_MB);   /* 1..100 MB, n=100 */
        before = st.plus_thr[4];
        RrColorRecompute(&st, 50);
        after = st.plus_thr[4];
        if (!(after < before && after > 95 * RR_MB)) {
            fails++; n++;
            printf("FAIL %s:%d plus_thr[4] did not ease toward quantile (%.1f -> %.1f MB)\n",
                   __FILE__, __LINE__, before / RR_MB, after / RR_MB);
        } else {
            n++;
        }
        for (k = 0; k < 40; k++) RrColorRecompute(&st, 50);
        APPROX(st.plus_thr[0] / RR_MB, 20.0, 8.0);   /* q20 of 1..100, log bins */
        APPROX(st.plus_thr[4] / RR_MB, 95.0, 12.0);  /* q95, wider bins at the top */
        EQ(RrDeltaColor(0.4 * RR_MB, 50, st.plus_thr, st.minus_thr), RR_COL_WHITE);
        EQ(RrDeltaColor(15 * RR_MB, 50, st.plus_thr, st.minus_thr), RR_COL_GREEN);
        EQ(RrDeltaColor(110 * RR_MB, 50, st.plus_thr, st.minus_thr), RR_COL_PURPLE);
    }

    /* Narrow plus range keeps the defaults (a flat line must not flicker). */
    {
        RrColorStats st;
        int i, k;
        RrStatsInit(&st, 50);
        for (i = 0; i < 80; i++)
            RrHistAddDiff(&st.plus, (10.0 + (i % 2) * 0.2) * RR_MB);
        for (k = 0; k < 40; k++) RrColorRecompute(&st, 50);
        APPROX(st.plus_thr[0] / RR_MB, 10.0, 1e-3);
        APPROX(st.plus_thr[4] / RR_MB, 150.0, 1e-3);
    }

    /* Percentage quantiles over a 30–70% range; minus similarly. */
    {
        RrColorStats st;
        int i, k;
        RrStatsInit(&st, 50);
        for (i = 0; i <= 80; i++)
            RrHistAddPct(&st.pct, 30.0 + i * 0.5);   /* 30..70, n=81 */
        for (i = 1; i <= 80; i++)
            RrHistAddDiff(&st.minus, i * RR_MB);     /* 1..80 MB */
        for (k = 0; k < 40; k++) RrColorRecompute(&st, 50);
        APPROX(st.pct_thr[0], 34.0, 3.0);
        APPROX(st.pct_thr[5], 68.0, 3.0);
        EQ(RrPercentColor(30, st.pct_thr), RR_COL_WHITE);
        EQ(RrPercentColor(72, st.pct_thr), RR_COL_PURPLE);
        APPROX(st.minus_thr[0] / RR_MB, 20.0, 8.0);
        APPROX(st.minus_thr[2] / RR_MB, 72.0, 12.0);
        EQ(RrDeltaColor(-2 * RR_MB, 50, st.plus_thr, st.minus_thr), RR_COL_CYAN);
        EQ(RrDeltaColor(-90 * RR_MB, 50, st.plus_thr, st.minus_thr), RR_COL_BLUE_DEEP);
    }

    /* Histogram decay, format, and parse (including malformed lines). */
    {
        unsigned c[RR_HIST_DIFF_BINS], d[RR_HIST_DIFF_BINS];
        unsigned total = 0;
        char buf[2048];
        int i;
        memset(c, 0, sizeof c);
        for (i = 0; i < RR_HIST_DECAY_CAP + 10; i++)
            RrHistAdd(c, RR_HIST_DIFF_BINS, &total, 3);
        /* After crossing the cap, counts were halved once, then 9 more adds
           (the add that crossed the cap also triggered the half). */
        if (!(total < RR_HIST_DECAY_CAP && total > RR_HIST_DECAY_CAP / 4)) {
            fails++; n++;
            printf("FAIL %s:%d decay total=%u\n", __FILE__, __LINE__, total);
        } else {
            n++;
        }
        EQ(RrHistSum(c, RR_HIST_DIFF_BINS), (long long)total);
        EQ(RrHistFormat(c, RR_HIST_DIFF_BINS, buf, (int)sizeof buf), 1);
        EQ(RrHistParse(buf, d, RR_HIST_DIFF_BINS), 1);
        EQ(RrHistSum(d, RR_HIST_DIFF_BINS), (long long)total);
        EQ(d[3], c[3]);
        EQ(RrHistParse("", d, RR_HIST_DIFF_BINS), 0);
        EQ(RrHistParse("1,2,3", d, RR_HIST_DIFF_BINS), 0);
        EQ(RrHistParse("x,1,2", d, 3), 0);
        EQ(RrHistParse("1,2,3,", d, 3), 0);
        EQ(RrHistParse("1,2,3,4", d, 3), 0);
        EQ(RrHistParse("1,2,3", d, 3), 1);
        EQ(d[0], 1); EQ(d[1], 2); EQ(d[2], 3);
    }

    /* Loaded warm histogram snaps thresholds on inited=0 recompute. */
    {
        RrColorStats st;
        int i;
        char buf[2048];
        unsigned tmp[RR_HIST_PCT_BINS];
        RrStatsInit(&st, 50);
        for (i = 30; i <= 70; i++) {
            int k;
            for (k = 0; k < 3; k++) RrHistAddPct(&st.pct, (double)i);
        }
        EQ(RrHistFormat(st.pct.c, RR_HIST_PCT_BINS, buf, (int)sizeof buf), 1);
        RrStatsInit(&st, 50);
        EQ(RrHistParse(buf, tmp, RR_HIST_PCT_BINS), 1);
        memcpy(st.pct.c, tmp, sizeof tmp);
        st.pct.total = RrHistSum(st.pct.c, RR_HIST_PCT_BINS);
        st.inited = 0;
        RrColorRecompute(&st, 50);
        APPROX(st.pct_thr[0], 34.0, 4.0);
        APPROX(st.pct_thr[5], 68.0, 4.0);
    }

    if (fails) {
        printf("%d failed / %d checks\n", fails, n);
        return 1;
    }
    printf("ok  %d checks\n", n);
    return 0;
}
