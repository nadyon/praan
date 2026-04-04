/*
 * pan_main.c
 * Integer-only wrapper for PanTompkins fixed-point detector.
 *
 * Features computed (Feature Vector - integer-friendly):
 * - n_beats (int)
 * - RR_mean_ms (int)
 * - RR_median_ms (int)
 * - RR_min_ms (int)
 * - RR_max_ms (int)
 * - SDNN_ms (int)
 * - RMSSD_ms (int)
 * - CVRR_q10 (int)   -- coefficient of variation * 1024
 * - HR_bpm (int)
 * - QRS_mean_ms (int)
 * - SQI_q10 (int)    -- signal quality index * 1024 (detected/expected)
 *
 * QRS method: use MVA around R-peak + adaptive ThI1/2
 *
 * Build (host):
 *    gcc -O3 pan_main.c PanTompkins.c -o pan_det
 *
 * Usage:
 *    ./pan_det <clean_csv> <fs> [out_prefix]
 *      
 * Example:
 *    ./pan_det ../cleaned_ecg/100_clean.csv 360
 *
 * Notes:
 *  - pan_main stores per-sample MVA and ThI1 samples (from PT_get_MVFilter_output and PT_get_ThI1_output)
 *  - QRS width measured by searching backward/forward from detected peak index until MVA < ThI1/2 (or reaches bounds)
 *  - All feature math uses integer arithmetic and integer sqrt helper
 *  - If your CSV is already in ADC units, set SCALE_FACTOR to 1; if it's in volts/mV, choose scale accordingly.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "PanTompkins.h"

#define MAX_PEAKS       20000
#define MAX_SAMPLES     2000000
#define SCALE_FACTOR    1000
#define SAMPLE_CLIP     30000
#define CV_SCALE_Q10    1024
#define SQI_SCALE_Q10   1024

/* -------- Integer sqrt -------- */
static uint32_t isqrt_u64(uint64_t x) {
    if (x == 0) return 0;
    uint64_t r = 1ULL << ((63 - __builtin_clzll(x)) / 2);
    for (int i = 0; i < 32; ++i) {
        uint64_t nr = (r + x / r) >> 1;
        if (nr >= r) break;
        r = nr;
    }
    return (uint32_t)r;
}

/* -------- CSV Loader (float -> int16_t scaled) -------- */
static int16_t *load_csv_to_i16(const char *path, int *ns_out) {
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); return NULL; }

    int cap = 65536;
    int16_t *buf = malloc(sizeof(int16_t) * cap);
    if (!buf) { fclose(f); return NULL; }

    int n = 0;
    char line[256];

    while (fgets(line, sizeof(line), f)) {
        char *s = line;
        while (*s==' ' || *s=='\t') s++;
        if (*s=='\0' || *s=='\n') continue;

        double v = atof(s);
        long scaled = (long)(v * SCALE_FACTOR);

        if (scaled >  SAMPLE_CLIP) scaled =  SAMPLE_CLIP;
        if (scaled < -SAMPLE_CLIP) scaled = -SAMPLE_CLIP;

        if (n >= cap) {
            cap <<= 1;
            int16_t *tmp = realloc(buf, sizeof(int16_t) * cap);
            if (!tmp) { free(buf); fclose(f); return NULL; }
            buf = tmp;
        }
        buf[n++] = (int16_t)scaled;
    }
    fclose(f);
    *ns_out = n;
    return buf;
}

/* -------- Sorting comparator -------- */
static int cmp_int32(const void *a, const void *b) {
    int32_t A = *(int32_t*)a;
    int32_t B = *(int32_t*)b;
    return (A > B) - (A < B);
}

/* -------- Append Features to CSV -------- */
static int write_features_csv(
    const char *outpath,
    const char *record,
    int n_beats,
    int32_t rr_mean_ms,
    int32_t rr_median_ms,
    int32_t rr_min_ms,
    int32_t rr_max_ms,
    int32_t sdnn_ms,
    int32_t rmssd_ms,
    int32_t cvrr_q10,
    int32_t hr_bpm,
    int32_t qrs_mean_ms,
    int32_t sqi_q10
) {
    FILE *f = fopen(outpath, "a");
    if (!f) return -1;

    long pos = ftell(f);
    if (pos == 0) {
        fprintf(f,
            "record,n_beats,RR_mean_ms,RR_median_ms,RR_min_ms,RR_max_ms,"
            "SDNN_ms,RMSSD_ms,CVRR_q10,HR_bpm,mean_QRS_ms,SQI_q10\n");
    }

    fprintf(f,
        "%s,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
        record,
        n_beats,
        rr_mean_ms,
        rr_median_ms,
        rr_min_ms,
        rr_max_ms,
        sdnn_ms,
        rmssd_ms,
        cvrr_q10,
        hr_bpm,
        qrs_mean_ms,
        sqi_q10
    );

    fclose(f);
    return 0;
}

/* -------- Extract record name (Option A) -------- */
static void extract_record_name(const char *csv_path, char *out) {

    const char *base = strrchr(csv_path, '/');
    base = base ? base + 1 : csv_path;

    strcpy(out, base);

    char *p = strstr(out, "_clean.csv");
    if (p) *p = '\0';
}

/* ========================= MAIN ========================= */
int main(int argc, char **argv) {

    if (argc < 3) {
        fprintf(stderr, "Usage: %s <clean_csv> <fs>\n", argv[0]);
        return 1;
    }

    const char *csv = argv[1];
    int fs = atoi(argv[2]);

    char record_name[128];
    extract_record_name(csv, record_name);

    /* Load signal */
    int ns = 0;
    int16_t *sig = load_csv_to_i16(csv, &ns);
    if (!sig) { fprintf(stderr,"Failed to load %s\n",csv); return 2; }
    if (ns < 10) { fprintf(stderr,"Signal too short\n"); free(sig); return 3; }

    printf("Loaded %d samples from %s (record=%s)\n", ns, csv, record_name);

    /* allocate arrays */
    uint16_t *mva_arr = malloc(sizeof(uint16_t)*ns);
    uint16_t *thI_arr = malloc(sizeof(uint16_t)*ns);
    int *peaks = malloc(sizeof(int)*MAX_PEAKS);
    if (!mva_arr || !thI_arr || !peaks) return 5;

    int npeaks = 0;
    PT_init();

    /* Process */
    for (int i=0; i<ns; i++){
        int16_t s = sig[i];
        if (s > SAMPLE_CLIP) s = SAMPLE_CLIP;
        if (s < -SAMPLE_CLIP) s = -SAMPLE_CLIP;

        int16_t bd = PT_StateMachine(s);

        mva_arr[i] = PT_get_MVFilter_output();
        thI_arr[i] = PT_get_ThI1_output();

        if (bd > 0) {
            int pk = i - bd;
            if (pk >= 0 && npeaks < MAX_PEAKS)
                peaks[npeaks++] = pk;
        }
    }

    if (npeaks < 2) {
        fprintf(stderr,"Not enough peaks\n");
        return 0;
    }

    /* RR intervals (ms) */
    int rr_n = npeaks - 1;
    int32_t *rr_ms = malloc(sizeof(int32_t)*rr_n);
    if (!rr_ms) return 9;

    int32_t rr_min = INT32_MAX, rr_max = 0;
    int64_t rr_sum = 0;

    for (int i=0; i<rr_n; i++){
        int dx = peaks[i+1]-peaks[i];
        if (dx<1) dx=1;
        int32_t rr = (dx*1000)/fs;
        rr_ms[i]=rr;
        rr_sum+=rr;
        if (rr < rr_min) rr_min = rr;
        if (rr > rr_max) rr_max = rr;
    }

    int32_t rr_mean = rr_sum / rr_n;
    int32_t hr_bpm = (rr_mean>0)? 60000/rr_mean : 0;

    /* SDNN */
    int64_t acc=0;
    for(int i=0;i<rr_n;i++){
        int64_t d = rr_ms[i] - rr_mean;
        acc += d*d;
    }
    uint32_t sdnn_ms = isqrt_u64(acc/(rr_n-1));

    /* RMSSD */
    uint64_t diff_acc = 0;
    for(int i=0;i<rr_n-1;i++){
        int64_t d = rr_ms[i+1]-rr_ms[i];
        diff_acc += d*d;
    }
    uint32_t rmssd_ms = isqrt_u64(diff_acc/(rr_n-1));

    /* CVRR (Q10 scaled) */
    int32_t cvrr_q10 = (rr_mean>0)? (sdnn_ms*CV_SCALE_Q10)/rr_mean : 0;

    /* Median RR */
    int32_t *tmp = malloc(sizeof(int32_t)*rr_n);
    memcpy(tmp, rr_ms, sizeof(int32_t)*rr_n);
    qsort(tmp, rr_n, sizeof(int32_t), cmp_int32);

    int32_t rr_median = (rr_n%2)? tmp[rr_n/2]
                               : (tmp[rr_n/2-1]+tmp[rr_n/2])/2;
    free(tmp);

    /* QRS width (Method B) */
    int64_t qrs_sum=0;
    int qrs_valid=0;

    for(int k=0;k<npeaks;k++){
        int p = peaks[k];
        uint16_t t = thI_arr[p];
        uint16_t tl = t>>1;

        int L = p;
        while(L>0 && mva_arr[L]>=tl) L--;

        int R = p;
        while(R<ns-1 && mva_arr[R]>=tl) R++;

        int width_samples = R-L;
        if (width_samples<1) continue;

        int32_t width_ms = (width_samples*1000)/fs;
        qrs_sum += width_ms;
        qrs_valid++;
    }

    int32_t qrs_mean_ms = (qrs_valid>0)? qrs_sum/qrs_valid : 0;

    /* SQI */
    int32_t expected = (ns * hr_bpm) / (fs * 60);
    if (expected<1) expected=1;
    int32_t sqi_q10 = (npeaks * SQI_SCALE_Q10) / expected;

    /* Save row */
    mkdir("features_ecg", 0777);
    char outcsv[256];
    snprintf(outcsv,sizeof(outcsv),"features_ecg/all_ecg_features.csv");

    write_features_csv(
        outcsv, record_name,
        npeaks,
        rr_mean,
        rr_median,
        rr_min,
        rr_max,
        sdnn_ms,
        rmssd_ms,
        cvrr_q10,
        hr_bpm,
        qrs_mean_ms,
        sqi_q10
    );

    printf("Appended features for record %s → %s\n", record_name, outcsv);

    free(sig);
    free(peaks);
    free(mva_arr);
    free(thI_arr);
    free(rr_ms);

    return 0;
}
