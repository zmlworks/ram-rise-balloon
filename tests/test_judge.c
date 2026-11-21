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

    if (fails) {
        printf("%d failed / %d checks\n", fails, n);
        return 1;
    }
    printf("ok  %d checks\n", n);
    return 0;
}
