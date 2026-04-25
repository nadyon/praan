/*
 * ecg_pipeline.c
 *
 * Host-side ECG feature extraction pipeline.
 * Reads a UART log file (one millivolt float per line),
 * resamples 250 Hz → 360 Hz, runs Pan-Tompkins,
 * extracts 6 features, standardises them, and prints the
 * feature vector for the ML model.
 *
 * Compile (inside health_pipeline/):
 *   make ecg
 *
 * Usage:
 *   ./ecg_pipeline  <uart_log_file>
 *   ./ecg_pipeline  ../../scripts/uart_logs/ecg_20250101_120000.txt
 *
 * Output (stdout):
 *   Feature vector line consumed by ml_pipeline.ipynb:
 *   ECG_FEATURES: RR_mean_ms=805.17 HR_bpm=77.31 ... (raw)
 *   ECG_SCALED:   0.123456 -0.456789 ...             (z-scored, 6 values)
 *   ECG_CLASS:    Normal                              (from embedded model)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "PanTompkins.h"
#include "ecg_model.h"

/* ------------------------------------------------------------------ */
/* Resampling constants                                                 */
/* ecg_monitor streams at 250 SPS, PanTompkins expects 200 Hz.        */
/* We resample 250→200 by simple linear interpolation.                 */
/* ------------------------------------------------------------------ */
#define INPUT_FS       250
#define PT_FS          200
#define MAX_SAMPLES    8000    /* 250 SPS x 32s — generous ceiling */

/* ------------------------------------------------------------------ */
/* StandardScaler constants (fitted on MIT-BIH training set)           */
/* Order: RR_mean_ms, HR_bpm, SDNN_ms, RMSSD_ms, mean_QRS_ms, SQI_q10 */
/* ------------------------------------------------------------------ */
static const double ECG_MEAN[6] = {
    805.1666666666666,
    77.3125,
    146.33333333333334,
    171.33333333333334,
    26089.041666666668,
    1025.6041666666667
};
static const double ECG_STD[6] = {
    166.17113835507723,
    15.831345818238784,
    162.21761789508423,
    172.84932423613606,
    95291.18882338106,
    5.626350146606788
};

/* ------------------------------------------------------------------ */
/* RR interval accumulator (filled by PT callback)                     */
/* ------------------------------------------------------------------ */
#define MAX_BEATS 512

static int32_t  rr_intervals[MAX_BEATS];   /* in samples at PT_FS */
static int      n_beats       = 0;
static int32_t  last_beat_pos = -1;
static int32_t  qrs_durations[MAX_BEATS];
static int      n_qrs         = 0;

/* ------------------------------------------------------------------ */
/* SQI accumulator — count samples where MVA > threshold               */
/* ------------------------------------------------------------------ */
static uint32_t sqi_sum   = 0;
static uint32_t sqi_count = 0;

/* ------------------------------------------------------------------ */
/* Linear resample: src[n_src] at INPUT_FS → dst[*n_dst] at PT_FS     */
/* ------------------------------------------------------------------ */
static int resample_250_to_200(const double *src, int n_src,
                                int16_t *dst,  int *n_dst)
{
    /* ratio = INPUT_FS / PT_FS = 250/200 = 1.25
       for each output sample i, input position = i * 1.25            */
    double ratio = (double)INPUT_FS / (double)PT_FS;
    int out_len  = (int)((n_src - 1) / ratio);
    if (out_len <= 0) return -1;

    for (int i = 0; i < out_len; i++) {
        double pos  = i * ratio;
        int    idx  = (int)pos;
        double frac = pos - idx;
        double val  = src[idx] * (1.0 - frac) + src[idx + 1] * frac;
        /* millivolts → ADC-like integer: scale to ±2048 range */
        int32_t ival = (int32_t)(val * 400.0);
        if (ival >  32767) ival =  32767;
        if (ival < -32768) ival = -32768;
        dst[i] = (int16_t)ival;
    }
    *n_dst = out_len;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Statistics helpers                                                   */
/* ------------------------------------------------------------------ */
static double mean_i32(const int32_t *a, int n)
{
    if (n == 0) return 0.0;
    double s = 0;
    for (int i = 0; i < n; i++) s += a[i];
    return s / n;
}

static double rmssd_i32(const int32_t *a, int n)
{
    if (n < 2) return 0.0;
    double s = 0;
    for (int i = 1; i < n; i++) {
        double d = a[i] - a[i-1];
        s += d * d;
    }
    return sqrt(s / (n - 1));
}

static double sdnn_i32(const int32_t *a, int n)
{
    if (n < 2) return 0.0;
    double m = mean_i32(a, n);
    double s = 0;
    for (int i = 0; i < n; i++) {
        double d = a[i] - m;
        s += d * d;
    }
    return sqrt(s / n);
}

/* ------------------------------------------------------------------ */
/* Main                                                                 */
/* ------------------------------------------------------------------ */
int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <uart_log_file>\n", argv[0]);
        fprintf(stderr, "  e.g. %s ../../scripts/uart_logs/ecg_20250101_120000.txt\n", argv[0]);
        return 1;
    }

    /* ---- 1. Load UART log ---- */
    FILE *fp = fopen(argv[1], "r");
    if (!fp) {
        fprintf(stderr, "[ecg_pipeline] Cannot open: %s\n", argv[1]);
        return 1;
    }

    static double raw_mv[MAX_SAMPLES];
    int n_raw = 0;
    char line[64];

    while (fgets(line, sizeof(line), fp) && n_raw < MAX_SAMPLES) {
        double v;
        if (sscanf(line, "%lf", &v) == 1) {
            raw_mv[n_raw++] = v;
        }
    }
    fclose(fp);

    if (n_raw < 50) {
        fprintf(stderr, "[ecg_pipeline] Too few samples (%d) in %s\n", n_raw, argv[1]);
        return 1;
    }
    printf("[ecg_pipeline] Loaded %d samples from %s\n", n_raw, argv[1]);

    /* ---- 2. Resample 250 → 200 Hz ---- */
    static int16_t resampled[MAX_SAMPLES];
    int n_pt = 0;

    if (resample_250_to_200(raw_mv, n_raw, resampled, &n_pt) != 0) {
        fprintf(stderr, "[ecg_pipeline] Resampling failed.\n");
        return 1;
    }
    printf("[ecg_pipeline] Resampled to %d samples at %d Hz\n", n_pt, PT_FS);

    /* ---- 3. Run Pan-Tompkins ---- */
    PT_init();

    for (int i = 0; i < n_pt; i++) {
        int16_t beat_delay = PT_StateMachine(resampled[i]);

        /* SQI: accumulate MVA value */
        sqi_sum += PT_get_MVFilter_output();
        sqi_count++;

        if (beat_delay > 0 && n_beats < MAX_BEATS) {
            int32_t beat_pos = (int32_t)i - beat_delay;

            if (last_beat_pos >= 0) {
                int32_t rr = beat_pos - last_beat_pos;
                if (rr > 10 && rr < 500) {         /* sanity: 24–3000 bpm */
                    rr_intervals[n_beats] = rr;
                    n_beats++;
                }
            }
            last_beat_pos = beat_pos;

            /* QRS duration proxy: use beat_delay as QRS width estimate */
            if (n_qrs < MAX_BEATS) {
                qrs_durations[n_qrs++] = (int32_t)beat_delay;
            }
        }
    }
    printf("[ecg_pipeline] Pan-Tompkins detected %d beats\n", n_beats);

    /* ---- 4. Extract features ---- */
    /* Convert RR from samples@200Hz to milliseconds */
    static int32_t rr_ms[MAX_BEATS];
    for (int i = 0; i < n_beats; i++) {
        rr_ms[i] = (int32_t)((double)rr_intervals[i] * 1000.0 / PT_FS);
    }

    double rr_mean_ms  = (n_beats > 0) ? mean_i32(rr_ms, n_beats)  : 800.0;
    double sdnn_ms     = (n_beats > 1) ? sdnn_i32(rr_ms, n_beats)  : 0.0;
    double rmssd_ms    = (n_beats > 1) ? rmssd_i32(rr_ms, n_beats) : 0.0;
    double hr_bpm      = (rr_mean_ms > 0) ? (60000.0 / rr_mean_ms) : 75.0;

    /* mean_QRS_ms: convert QRS duration samples→ms, then mean */
    static int32_t qrs_ms[MAX_BEATS];
    for (int i = 0; i < n_qrs; i++) {
        qrs_ms[i] = (int32_t)((double)qrs_durations[i] * 1000.0 / PT_FS);
    }
    double mean_qrs_ms = (n_qrs > 0) ? mean_i32(qrs_ms, n_qrs) : 0.0;

    /* SQI: use mean MVA value scaled to match training range (~1000–1036) */
    double sqi_q10 = (sqi_count > 0)
                     ? (1000.0 + (double)sqi_sum / sqi_count / 32.0)
                     : 1025.0;
    if (sqi_q10 > 1036.0) sqi_q10 = 1036.0;
    if (sqi_q10 < 1000.0) sqi_q10 = 1000.0;

    /* ---- 5. Print raw features ---- */
    printf("\n");
    printf("ECG_FEATURES: RR_mean_ms=%.2f HR_bpm=%.2f SDNN_ms=%.2f "
           "RMSSD_ms=%.2f mean_QRS_ms=%.2f SQI_q10=%.2f\n",
           rr_mean_ms, hr_bpm, sdnn_ms, rmssd_ms, mean_qrs_ms, sqi_q10);

    /* ---- 6. Standardise (z-score) ---- */
    double raw[6]    = { rr_mean_ms, hr_bpm, sdnn_ms,
                         rmssd_ms,   mean_qrs_ms, sqi_q10 };
    double scaled[6];
    for (int i = 0; i < 6; i++) {
        scaled[i] = (raw[i] - ECG_MEAN[i]) / ECG_STD[i];
    }

    printf("ECG_SCALED:   %.6f %.6f %.6f %.6f %.6f %.6f\n",
           scaled[0], scaled[1], scaled[2],
           scaled[3], scaled[4], scaled[5]);

    /* ---- 7. Classify using embedded decision tree ---- */
    const char *ecg_class = ecg_classify(
        (float)scaled[0], (float)scaled[1], (float)scaled[2],
        (float)scaled[3], (float)scaled[4], (float)scaled[5]);

    printf("ECG_CLASS:    %s\n\n", ecg_class);

    return 0;
}
