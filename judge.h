/*
 * RamRiseBalloon: balloon trigger judgement (shared by the app and tests).
 * No Windows headers. Combined usage counts compressed pages at their
 * uncompressed size without double-counting the store's physical footprint.
 */
#ifndef RAMRISE_JUDGE_H
#define RAMRISE_JUDGE_H

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

#endif
